# CFB27 Blaze Framing and Capture Analyzer Design

Date: 2026-07-16
Status: Approved for implementation

## Goal

Make Cypress decode and encode the Blaze frame format observed in the CFB27
ProtoSSL plaintext capture, then provide a read-only command that inventories
complete Blaze frames from ACP/PCAP captures. This milestone does not add or
change Blaze service handlers.

## Evidence

`protossl_dump_1783976660.acp` is a standard little-endian PCAP file produced
by the supplied EA-MITM ACP writer. Its TCP payloads are captured after
ProtoSSL decryption. Complete CFB27 messages use a 16-byte header with this
layout:

| Offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 4 | Big-endian payload length |
| `0x04` | 2 | Big-endian reserved value |
| `0x06` | 2 | Big-endian component ID |
| `0x08` | 2 | Big-endian command ID |
| `0x0A` | 2 | Big-endian error code |
| `0x0C` | 1 | Message type and user-index metadata |
| `0x0D` | 3 | Big-endian 24-bit message ID |
| `0x10` | N | TDF payload |

The captured authentication login begins with:

```text
00 00 0F 43 00 00 00 01 00 0A 00 00 07 00 00 00
```

The current Cypress codec reads component and command two bytes too early and
models the final four bytes as a 32-bit message ID. A real login consequently
cannot route to the existing authentication handler.

## Scope

### Included

- Replace the internal Blaze frame codec with the captured CFB27 layout.
- Preserve the public `MessageType`, `UserIndex`, and `MessageID` concepts used
  by the CFB27 service while packing them into the observed four metadata bytes.
- Reject message IDs greater than `0xFFFFFF` when encoding.
- Preserve the reserved field on decode and encode so observed nonzero values
  can be analyzed without losing data.
- Add capture-derived codec fixtures and boundary/error tests.
- Add a read-only `cfb27capture` command for ACP/PCAP files.
- Emit machine-readable JSON and a deterministic route-frequency summary.
- Validate the analyzer against the supplied capture.

### Excluded

- New Blaze component or command handlers.
- TCP stream reassembly across multiple capture records.
- Guessing whether unrelated synthetic connections belong to one logical
  stream.
- TLS interception, certificate bypass changes, packet injection, endpoint
  redirection, or game launch changes.
- Online-menu feature-gate changes.

## Codec Design

`blaze.Header` gains `Reserved uint16`. The existing fields remain available
to callers. The metadata byte is packed as two four-bit values: message type
in the high nibble and user index in the low nibble. The message ID is encoded
as an unsigned, big-endian 24-bit integer in bytes `0x0D` through `0x0F`.

The decoder accepts every reserved value and returns it to the caller. The
encoder writes the caller-provided reserved value. This is lossless and avoids
claiming that zero is the only valid value before more captures are available.

The decoder retains the existing 16 MiB payload limit. The encoder returns a
specific error for a message type or user index greater than `0x0F`, or a
message ID greater than `0xFFFFFF`.

## Capture Analyzer Design

The analyzer is a new Go command under `cmd/cfb27capture`. Its parsing logic
lives in `internal/cfb27capture` so it can be tested independently of command
line output.

The parser supports classic PCAP files with Ethernet link type 1, both PCAP
byte orders, IPv4, and TCP. For each record it:

1. Validates the PCAP and packet boundaries.
2. Locates the TCP payload using IPv4 and TCP header lengths.
3. Attempts to decode one or more complete Blaze frames contained wholly in
   that record.
4. Records incomplete or invalid payloads as skipped records with a reason.
5. Never combines payload bytes from different synthetic connections or PCAP
   records.

The JSON report includes input metadata, packet counts, decoded frames,
direction, endpoints, timestamp, component, command, error, message type, user
index, message ID, payload length, and skip-reason totals. It does not emit raw
payloads by default because captured login payloads can contain authentication
or account data.

Direction is inferred from the EA-MITM convention only when one endpoint is
`127.0.0.1`; otherwise it is reported as `unknown`. Endpoint information is
evidence metadata, not authoritative service identity.

The text summary groups decoded frames by direction, component, command,
message type, and error code. Sorting is deterministic: direction, component,
command, message type, then error code.

## Error Handling

- Invalid PCAP headers or unsupported link types terminate with a clear error.
- Truncated packet records terminate with their record number and expected
  length.
- Unsupported network protocols and empty TCP payloads are counted, not
  treated as fatal.
- A payload that is not a complete Blaze frame is counted with a bounded,
  non-sensitive reason. Raw payload bytes are never included in errors.
- Output files are written only after parsing completes successfully.

## Testing

Codec tests use exact CFB27 header bytes, including the captured login header,
and cover:

- Correct component and command offsets.
- Metadata nibble packing.
- 24-bit message-ID boundaries.
- Reserved-field preservation.
- Oversized payload rejection.
- Partial-header and partial-payload errors.

Analyzer tests construct minimal PCAP byte streams in memory and cover both
byte orders, valid Ethernet/IPv4/TCP extraction, multiple complete frames in a
record, skipped partial frames, unsupported link types, and deterministic
route aggregation.

Acceptance requires:

1. `go test ./...` passes from `tools/cypress-servers`.
2. The analyzer decodes the supplied ACP file without exposing raw payloads.
3. The report identifies at least the captured authentication route
   `component=0x0001`, `command=0x000A`.
4. The existing CFB27 Blaze service tests pass using the corrected codec.

## Security and Privacy

Captures and process dumps may contain EA tokens, account identifiers, IP
addresses, and other personal data. The analyzer does not print TDF values or
raw payloads. Generated reports must remain free of bearer tokens and login
payload contents unless a future explicit opt-in redaction design is approved.
