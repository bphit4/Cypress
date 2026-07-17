# CFB27 Blaze Framing and Capture Analyzer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct Cypress's CFB27 Blaze wire codec and add a privacy-safe ACP/PCAP route analyzer.

**Architecture:** The Blaze package owns the exact 16-byte wire header. A separate `internal/cfb27capture` package parses classic PCAP records and feeds complete TCP payload frames into that codec; a small command renders JSON or deterministic text without payload contents.

**Tech Stack:** Go standard library and the existing `internal/blaze` package.

## Global Constraints

- Do not add or change Blaze service handlers.
- Do not reassemble TCP payloads across PCAP records.
- Do not emit raw payloads or decoded TDF values.
- Preserve decoded reserved header values.
- Reject message IDs above `0xFFFFFF`.

---

### Task 1: Correct the CFB27 Blaze wire header

**Files:**
- Modify: `tools/cypress-servers/internal/blaze/frame.go`
- Modify: `tools/cypress-servers/internal/blaze/frame_test.go`

**Interfaces:**
- Produces: `Header.Reserved uint16`; corrected `ReadFrame(io.Reader)` and `WriteFrame(io.Writer, Frame)`.

- [ ] Add a failing capture-derived login-header test asserting component `1`, command `10`, metadata nibbles, and a 24-bit message ID.
- [ ] Run `go test ./internal/blaze -run TestReadCapturedCFB27LoginHeader -v` and confirm the current offset mismatch fails.
- [ ] Implement the corrected offsets, metadata packing, reserved field, and 24-bit message ID.
- [ ] Add failing boundary tests for message type, user index, and message ID overflow, then implement explicit validation.
- [ ] Run `go test ./internal/blaze -v` and confirm all codec tests pass.

### Task 2: Parse privacy-safe CFB27 ACP/PCAP captures

**Files:**
- Create: `tools/cypress-servers/internal/cfb27capture/pcap.go`
- Create: `tools/cypress-servers/internal/cfb27capture/pcap_test.go`

**Interfaces:**
- Produces: `Parse(io.Reader) (Report, error)`, `Report.Frames []FrameRecord`, `Report.Skipped map[string]int`, and `Report.Routes() []RouteCount`.

- [ ] Add failing in-memory PCAP tests for little/big endian headers, Ethernet/IPv4/TCP extraction, multiple frames, partial-frame skips, unsupported link types, direction, and deterministic routes.
- [ ] Run `go test ./internal/cfb27capture -v` and confirm failure because `Parse` is absent.
- [ ] Implement bounded classic-PCAP parsing and complete-record Blaze decoding without payload retention.
- [ ] Run `go test ./internal/cfb27capture -v` and confirm all parser tests pass.

### Task 3: Add the capture analyzer command

**Files:**
- Create: `tools/cypress-servers/cmd/cfb27capture/main.go`
- Create: `tools/cypress-servers/cmd/cfb27capture/main_test.go`

**Interfaces:**
- Consumes: `cfb27capture.Parse` and `Report.Routes`.
- Produces: `run([]string, io.Writer, io.Writer) int` supporting `-format text|json` and one input path.

- [ ] Add failing command tests for deterministic text, valid JSON, missing input, and invalid format.
- [ ] Run `go test ./cmd/cfb27capture -v` and confirm failure because `run` is absent.
- [ ] Implement the minimal command without raw payload output.
- [ ] Run `go test ./cmd/cfb27capture -v` and confirm all command tests pass.

### Task 4: Validate against the supplied capture

**Files:**
- Modify: `Docs/cfb27-protocol-discovery.md`

**Interfaces:**
- Consumes: `go run ./cmd/cfb27capture -format text <capture>`.
- Produces: a confirmed evidence-log entry describing corrected framing and observed route counts without sensitive payload data.

- [ ] Run `go test ./...` from `tools/cypress-servers` and require a clean pass.
- [ ] Run the analyzer against `C:\Users\Shadow\Downloads\protossl_dump_1783976660.acp` in text and JSON modes.
- [ ] Verify both reports contain component `0x0001`, command `0x000A`, and contain no bearer-token-like strings.
- [ ] Add the confirmed framing result to the protocol discovery evidence log.
- [ ] Run `git diff --check` and `go test ./...` again.
