# CFB27 ProtoSSL Verify Path: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Tasks 1-3 are DONE in this change; Tasks 4-6 require the game running and are the next session's work.

**Goal:** Pin, in a single game run, the ProtoSSL region that runs during the
live certificate decision, then attach the cert bypass to that path so the game
completes the local Blaze TLS handshake and the online menus enable.

**Architecture:** A guard-page execution-coverage probe in the injected DLL logs
which known ProtoSSL runtime-code regions execute during a handshake. The bridge
already logs the ClientHello and TLS alerts server-side. Together they identify
the verify caller and the negotiated version, which selects the downstream fix.

**Tech Stack:** C++ / MinHook / Win32 vectored exception handling (DLL); existing
Go bridge diagnostics; no new dependencies.

## Global Constraints

- The probe is diagnostic only: never modify game code or data.
- The probe stays off unless `CYPRESS_CFB27_ENABLE_PROTOSSL_PROBE` / config
  enables it.
- No Blaze handler, TLS-negotiation, or Frosty changes in this milestone.
- Keep the probe region list in sync with `LogProtoSslRuntimeCodeBytes`.

---

### Task 1: Enable the certificate bypass on the normal launch path (DONE)

**Files:**
- Modified: `Launcher/CypressLauncher/MessageHandler.Launch.cs`

- [x] Set `CYPRESS_CFB27_ENABLE_BEARSSL_BYPASS=1` and
  `CYPRESS_CFB27_DUMP_RUNTIME_CODE=1` in the CFB27 launch environment so a real
  launch exercises the bypass and emits runtime-code evidence.

### Task 2: Add the probe config flag (DONE)

**Files:**
- Modified: `Server/include/CFB27/BridgeConfig.h`
- Modified: `Server/Source/CFB27/BridgeConfig.cpp`

- [x] Add `enableProtoSslVerifyProbe` (default false), parsed from config key
  `enableProtoSslVerifyProbe` and env `CYPRESS_CFB27_ENABLE_PROTOSSL_PROBE`.

### Task 3: Implement the guard-page coverage probe (DONE)

**Files:**
- Modified: `Server/include/CFB27/MemoryDiscovery.h`
- Modified: `Server/Source/CFB27/MemoryDiscovery.cpp`
- Modified: `Server/Source/CFB27/BridgeBootstrap.cpp`

- [x] Add `InstallProtoSslVerifyProbe(BridgeLog&)`: resolve candidate regions,
  arm `PAGE_GUARD`, install a vectored handler that logs first execution per
  region with a backtrace, single-steps to re-arm, and honors a fault budget.
- [x] Call it from bootstrap when the flag is set.

### Task 4: Confirm the build gate accepts the running executable

**Files:**
- Verify: `Server/include/CFB27/BridgeConfig.h` (supported SHA-256 list)
- Reference: `cfb27-bridge.log`

- [ ] Build the DLL and launcher; launch CFB27 through the private stack.
- [ ] In `cfb27-bridge.log`, confirm `supported game build=...` (not
  `unsupported game build; no hooks installed`). If unsupported, capture the
  current `game SHA-256=` line and add it to `SupportedBuilds` /
  `BridgeConfig.h`, then relaunch.
- [ ] Confirm `installed redirector DNS/TCP hooks...` and the BearSSL bypass
  install/skip lines appear.

### Task 5: Capture the live verify path

**Files:**
- Reference: `cfb27-bridge.log`, bridge `cfb27-blaze.jsonl` events

- [ ] Enable the probe (`CYPRESS_CFB27_ENABLE_PROTOSSL_PROBE=1` or config), then
  drive the game to an online-gated screen so it dials the redirector.
- [ ] Confirm `ProtoSSL verify probe armed regions=<n> pages=<n>`.
- [ ] Collect all `protossl-exec name=... rva=... stack=...` lines for the
  connection that reaches `tls-server-hello-done` / `tls-server-first-write`.
- [ ] From the bridge, record the matching `tls-client-hello` (negotiated
  version, cipher suites, curves, signature schemes) and any
  `tls-handshake-failed` alert description.
- [ ] Note whether `discovery BearSSL X509 vtable candidates=` is 1 or not; if
  not 1, the current BearSSL fallback RVA is wrong for this build.

### Task 6: Attach the bypass to the live path and validate

**Files:**
- Modify: `Server/Source/CFB27/MemoryDiscovery.cpp` (verify hook or trust-anchor patch)
- Possibly modify: `tools/cypress-servers/internal/cfb27blaze/tls.go` (fixed CA)
- Modify: `Docs/cfb27-protocol-discovery.md`

- [ ] Choose the fix from Task 5 evidence: (a) force success on the identified
  ProtoSSL verify routine, or (b) switch the bridge to a fixed embedded CA and
  overwrite ProtoSSL's pinned gosca modulus in memory with that CA's modulus.
- [ ] Implement behind a config flag; fail closed on signature miss.
- [ ] Relaunch and confirm the bridge logs `tls-handshake-ok` followed by a
  decoded Blaze login (`component=0x0001 command=0x000A`).
- [ ] Verify the online-gated menu becomes selectable (Online Dynasty first).
- [ ] Add the confirmed verify-path RVA, negotiated TLS version, and bypass
  method to the protocol-discovery evidence log.
- [ ] Confirm the external-connection audit still reports zero non-loopback
  connections.
