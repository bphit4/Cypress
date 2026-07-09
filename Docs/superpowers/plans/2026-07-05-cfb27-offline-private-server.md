# CFB27 Offline Private Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Launch the supported CFB27 Trial build through Fake AC, keep it offline, connect it to loopback Cypress services, and load one local Cloud Dynasty.

**Architecture:** The injected `dinput8.dll` validates the game build, installs CFB27-specific hooks, supplies a local identity, and replaces the Blaze redirector endpoint with loopback. A new Go Blaze bridge parses and logs Blaze frames, dispatches the minimum local session and Dynasty commands, and uses the existing Dynasty REST service as persistent storage.

**Tech Stack:** C++20, MinHook, Win32/Winsock, CMake, Go 1.22+, `net/http`, SQLite, .NET launcher, xUnit, PowerShell.

---

## File Structure

- `tools/cypress-servers/internal/blaze/frame.go`: Blaze packet header codec.
- `tools/cypress-servers/internal/blaze/tdf.go`: minimum typed-data codec.
- `tools/cypress-servers/internal/blaze/frame_test.go`: packet golden tests.
- `tools/cypress-servers/internal/blaze/tdf_test.go`: TDF golden tests.
- `tools/cypress-servers/internal/cfb27blaze/service.go`: TCP sessions, dispatch, and JSONL logging.
- `tools/cypress-servers/internal/cfb27blaze/handlers.go`: local auth, presence, and Dynasty handlers.
- `tools/cypress-servers/internal/cfb27blaze/service_test.go`: server and handler integration tests.
- `tools/cypress-servers/cmd/cfb27blaze/main.go`: service CLI.
- `Server/Source/CFB27/BridgeConfig.*`: environment contract and supported-build validation.
- `Server/Source/CFB27/BridgeLog.*`: file logging outside hook paths.
- `Server/Source/CFB27/PatternScanner.*`: executable-section signature scanning.
- `Server/Source/CFB27/RedirectorHook.*`: redirector result override.
- `Server/Source/CFB27/OfflineIdentityHook.*`: local identity and online-state bridge.
- `Server/dllmain.cpp`: safe worker-thread bootstrap.
- `Server/CMakeLists.txt`: include CFB27 bridge sources.
- `Launcher/CypressLauncher/MessageHandler.Diagnostics.cs`: local service orchestration.
- `Launcher/CypressLauncher/MessageHandler.Launch.cs`: bridge environment and offline launch arguments.
- `tools/cypress-servers/deploy/cfb27-private.example.json`: loopback bridge configuration.
- `tools/cypress-servers/deploy/run-cfb27-private.ps1`: start Dynasty and Blaze only.

### Task 1: Blaze Frame Codec

- [ ] Create failing golden tests for a 16-byte Blaze request header and payload round trip.
- [ ] Run `go test ./internal/blaze -run TestFrame -v` and verify the package is missing.
- [ ] Implement `Header`, `Frame`, `ReadFrame`, and `WriteFrame` with big-endian bounds checking.
- [ ] Run `go test ./internal/blaze -run TestFrame -v` and verify it passes.

The public shape is:

```go
type Header struct {
	Length      uint32
	Component   uint16
	Command     uint16
	ErrorCode   uint16
	MessageType uint8
	UserIndex   uint8
	MessageID   uint32
}

type Frame struct {
	Header  Header
	Payload []byte
}
```

### Task 2: Minimum TDF Codec

- [ ] Add failing tests for tags, integers, strings, blobs, structs, lists, and terminators.
- [ ] Run `go test ./internal/blaze -run TestTDF -v` and verify failures.
- [ ] Implement bounded decode and deterministic encode APIs:

```go
type Field struct {
	Tag   string
	Type  Type
	Value any
}

func Decode(data []byte) ([]Field, error)
func Encode(fields []Field) ([]byte, error)
```

- [ ] Run all Blaze codec tests and verify they pass.

### Task 3: Loopback Blaze Service

- [ ] Add failing tests for `/health`, frame logging, keepalive, unsupported commands, and deterministic local login.
- [ ] Implement TCP port `27920` and diagnostics HTTP port `27921`.
- [ ] Dispatch by `(component, command)` and preserve request message IDs.
- [ ] Write JSONL events containing run ID, connection ID, component, command, decoded payload, raw payload, and result.
- [ ] Add a typed `dynasty.exe` client and seed one session when `/sessions` is empty.
- [ ] Run `go test ./internal/cfb27blaze ./internal/blaze -v`.
- [ ] Add `cmd/cfb27blaze` and update `build.ps1`.

### Task 4: CFB27 DLL Bootstrap

- [ ] Add C++ tests for environment parsing, SHA-256 comparison, and unique signature matching.
- [ ] Implement the environment contract:

```text
CYPRESS_CFB27_BLAZE_HOST=127.0.0.1
CYPRESS_CFB27_BLAZE_PORT=27920
CYPRESS_CFB27_PROFILE=default
CYPRESS_CFB27_RUN_DIR=<absolute run directory>
```

- [ ] Validate SHA-256 `B16F49F6E53F81C084B0E1B2F1EAFB1DA78CE51BEE3660BD5A79ED92C626817D`.
- [ ] Start initialization on a worker thread from `DllMain`.
- [ ] Keep the real `DirectInput8Create` forwarding export.
- [ ] Fail closed and write `cfb27-bridge.log` when build validation or hook discovery fails.

### Task 5: Redirector And Identity Hooks

- [ ] Use Rizin against the supported executable to locate xrefs for `spring25.client.blazeredirector.ea.com`, `redirector-getServerInstance`, `trialServiceName`, and the redirector response endpoint fields.
- [ ] Record exact function RVAs and stable byte signatures in the supported-build manifest.
- [ ] Add a failing scanner test using copied executable bytes around each signature.
- [ ] Install MinHook detours only when every signature resolves uniquely.
- [ ] Replace secure redirector output with `127.0.0.1:27920` and preserve request completion.
- [ ] Supply the configured local identity through the auth/session callback used before Press Start.
- [ ] Build `cypress_CFB27.dll` and confirm it loads under Fake AC without the previous `0x186AA` launcher handoff false-positive.

### Task 6: Launcher Orchestration

- [ ] Add launcher tests for the CFB27 private-stack arguments and environment.
- [ ] Start only `dynasty.exe` and `cfb27blaze.exe` for the first milestone.
- [ ] Wait for `http://127.0.0.1:27910/health` and `http://127.0.0.1:27921/health`.
- [ ] Seed the run directory and pass the four bridge variables to the Trial process.
- [ ] Remove `-Online.Backend Backend_Local` and `-Online.PeerBackend Backend_Local` from CFB27 launch arguments because they force the game's built-in offline backend instead of the injected bridge.
- [ ] Copy the DLL only for Fake AC CFB27 launches.
- [ ] Build and run launcher tests.

### Task 7: Verification And Package

- [ ] Run `go test ./...`.
- [ ] Build all Go service executables.
- [ ] Build the CFB27 DLL in Release mode.
- [ ] Publish the launcher self-contained.
- [ ] Verify service health and a simulated Blaze login/Dynasty list flow.
- [ ] Run the game through Fake AC with external traffic blocked.
- [ ] Verify Press Start, main menu, Cloud Dynasty list, and local Dynasty hub.
- [ ] Verify the run audit reports zero non-loopback connections.
- [ ] Produce a self-contained tester ZIP with launcher, DLL, Go services, schemas, configuration, and exact test steps.
