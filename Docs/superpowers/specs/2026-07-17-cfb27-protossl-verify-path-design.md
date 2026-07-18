# CFB27 ProtoSSL Verify Path: Design

Date: 2026-07-17
Status: Approved for implementation

## Problem

The CFB27 online menus (Online Dynasty, Mascot Mashup, Play a Friend) stay
disabled because the game never establishes an authenticated Blaze session. The
whole Cypress CFB27 stack is built to provide that session locally, but it is
blocked at the first secure hop: the game's DirtySDK **ProtoSSL** client rejects
the local Blaze redirector's TLS certificate and tears the connection down
before `ClientKeyExchange`.

Two facts from prior evidence define the blocker:

1. On a TLS 1.2 run the game reads the full server flight (ServerHello,
   Certificate, ServerKeyExchange, ServerHelloDone) and then sends no
   `ClientKeyExchange` and closes on timeout, with no certificate-rejection
   alert.
2. The installed BearSSL `br_x509_minimal` `end_chain` bypass hook is **never
   invoked** on this connection.

Conclusion: the live certificate decision does not run through the BearSSL
minimal-X509 vtable Cypress detected and hooked. ProtoSSL is validating the
chain against its pinned EA "gosca" CA through a different code path, so the
existing bypass targets inactive code.

## Two defects fixed as prerequisites

- The launcher never set `CYPRESS_CFB27_ENABLE_BEARSSL_BYPASS`, and
  `BridgeConfig` defaults it to `false`, so on a normal launch the DLL only
  redirected DNS/TCP and never attempted certificate acceptance at all. The
  launcher now enables the bypass and the runtime-code dump for CFB27.
- The BearSSL bypass falls back to a Trial-only `end_chain` RVA when the
  structural vtable search does not find exactly one candidate. The retail
  `CollegeFB27.exe` almost certainly does not match that RVA. This is recorded
  as a known hazard and is superseded by pinning the real verify path.

## Objective

Pin, in a single game run, which ProtoSSL runtime-code region actually executes
during the certificate decision, so a correct bypass (or trust-anchor patch)
can be attached to the live path instead of to inactive BearSSL code.

## Approach: guard-page execution coverage

Cypress already carries a static list of candidate ProtoSSL runtime-code regions
(`LogProtoSslRuntimeCodeBytes`). Rather than hook them blind — impossible with
unknown calling conventions — the probe observes execution without modifying
code:

1. Arm `PAGE_GUARD` over the memory pages spanning each candidate region.
2. A vectored exception handler catches `STATUS_GUARD_PAGE_VIOLATION`. When the
   faulting instruction pointer lands inside a watched region, it logs the RVA
   plus a stack backtrace once per region.
3. Because the guard is auto-cleared per access, the handler single-steps one
   instruction and re-arms the page so later regions on the same page are still
   caught. Once a page's regions are all logged, it is left open so hot code
   runs at full speed. A global fault budget hard-stops runaway slowdown.

The result is a live coverage map: the ordered set of candidate functions that
executed on a run that reached ServerHelloDone. Combined with the existing
`tls-server-hello-done` recv-stack trace and the bridge's `tls-client-hello`
event (negotiated version, ciphers, curves, signature schemes), this identifies
the verify caller.

The probe is diagnostic only. It never alters game code or data, is gated by
`BridgeConfig::enableProtoSslVerifyProbe` (env
`CYPRESS_CFB27_ENABLE_PROTOSSL_PROBE`), and defaults off.

## Considered alternatives

- **Hook each candidate with MinHook.** Rejected: forwarding through a
  trampoline requires each target's calling convention, which is unknown.
- **Hardware breakpoints via debug registers.** Rejected for now: only four
  slots, per-thread arming, and more fragile than guard pages for a first pass.
- **Signature-scan BearSSL RSA verify and hook it.** Deferred: needs byte
  patterns not yet in hand; can follow once the region is narrowed.

## Downstream fix options (after the path is pinned)

1. **Force success on the live verify routine** once its entry is identified
   from the coverage map, analogous to the BearSSL `end_chain` force-OK but on
   the correct function.
2. **Replace the pinned trust anchor.** Switch the bridge from a random runtime
   CA to a fixed embedded CA, then overwrite ProtoSSL's pinned gosca modulus in
   memory with that CA's modulus so the local chain validates natively. This
   avoids fighting control flow and is the preferred long-term fix.

Selection between them depends on what the coverage map and the negotiated TLS
version show.

## Failure handling

- Unresolved or non-executable regions are skipped with a log line; if none
  resolve, the probe does not install.
- If the vectored handler cannot be registered, the probe reports failure and
  the rest of bootstrap is unaffected.
- Guard faults outside watched pages are passed through unchanged.
- A fault-count budget disables re-arming to bound worst-case slowdown.

## Observability

New `cfb27-bridge.log` lines:

- `ProtoSSL verify probe armed regions=<n> pages=<n>`
- `protossl-exec name=<region> rva=0x<rva> regionRVA=0x<rva> stack=<frames>`

Correlate these with the bridge `tls-client-hello`, `tls-server-first-write`,
and `tls-handshake-failed` events for the same connection.

## Scope boundaries

This milestone does not change Blaze handlers, does not implement the downstream
bypass, does not modify TLS negotiation, and does not touch Frosty. It only adds
the opt-in coverage probe and the launcher/config wiring needed to run it.

## Completion criteria

A single launch with the probe enabled produces at least one `protossl-exec`
line on a run that reaches `tls-server-hello-done` / `tls-server-first-write`,
identifying the ProtoSSL region executing at the certificate decision, and the
protocol-discovery log records the negotiated TLS version and the observed
region set.
