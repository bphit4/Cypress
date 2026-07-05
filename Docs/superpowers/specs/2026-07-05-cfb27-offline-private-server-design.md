# CFB27 Offline Private Server: First Milestone Design

Date: 2026-07-05
Status: Approved design, pending written-spec review

## Objective

Enable `CollegeFB27_Trial.exe` to run through Fake AC, remain completely
offline, pass Press Start, reach the main menu with local online presence,
select Cloud Dynasty, display one locally stored Dynasty, and load it into the
Dynasty hub.

The first milestone runs the game client, injected DLL, Blaze bridge, and
Dynasty service on the same Windows PC.

## Confirmed Constraints

- The game is launched through Fake AC.
- DLL injection through the `dinput8.dll` proxy is allowed.
- The client must not contact EA or any other external service.
- The supported initial executable is `CollegeFB27_Trial.exe` with SHA-256:
  `B16F49F6E53F81C084B0E1B2F1EAFB1DA78CE51BEE3660BD5A79ED92C626817D`.
- The existing CFB27 DLL is currently only a DirectInput forwarding stub.
- The existing CFB27 gateway is an HTTP logger, not a Blaze protocol server.
- The existing Go Dynasty service and SQLite model remain the authoritative
  local data store.

## Discovery Findings

The 57 observed remote candidates are not interchangeable servers. They cover
identity, social, telemetry, statistics, leaderboards, redirector, and game
backend traffic, with several load-balanced addresses.

`166.117.23.51:443`, one of the observed candidates, resolves from
`spring25.client.blazeredirector.ea.com`. The game uses this Blaze redirector
to obtain a backend `ServerInstance`. Replacing that response is a cleaner
control point than redirecting every observed address.

The executable also contains Blaze redirector, service-name, authentication,
Dynasty component, and TDF metadata. The `-servicename` argument identifies the
DirtySDK/Blaze service and certificate context; it is not by itself an endpoint
override.

## Considered Approaches

### 1. Redirector Hook With Local Blaze Bridge

Inject a DLL, intercept the Blaze redirector result, and return an insecure
loopback backend. A local service implements the minimum Blaze/TDF protocol and
translates Dynasty operations to the existing REST service.

This is the selected approach because it avoids EA TLS while preserving the
game's own request serialization and creates a path toward multiple clients.

### 2. Winsock And DNS Redirection

Hook `getaddrinfo` and `connect` to redirect EA addresses. This still requires
emulating EA TLS and several unrelated services, and it is sensitive to address
rotation. It is not selected.

### 3. Fully In-Process Response Spoofing

Intercept and fabricate every required RPC response inside the DLL. This may
produce a quick UI proof but couples game offsets, protocol behavior, and
Dynasty state into one fragile module. It is not selected as the primary
architecture.

## Architecture

### Launcher

The Cypress Launcher owns startup order:

1. Start `dynasty.exe` on `127.0.0.1:27910`.
2. Start `cfb27blaze.exe` with Blaze TCP on `127.0.0.1:27920` and its
   diagnostics HTTP endpoint on `127.0.0.1:27921`.
3. Verify both health checks.
4. Place or verify the CFB27 `dinput8.dll` proxy.
5. Launch `CollegeFB27_Trial.exe` through Fake AC.

The launcher passes the bridge contract through
`CYPRESS_CFB27_BLAZE_HOST`, `CYPRESS_CFB27_BLAZE_PORT`,
`CYPRESS_CFB27_PROFILE`, and `CYPRESS_CFB27_RUN_DIR`. It does not start the
master or relay services for this milestone.

### Injected CFB27 DLL

The DLL keeps the DirectInput forwarding export and starts bridge
initialization on a worker thread outside `DllMain`.

Its responsibilities are:

- Verify the executable SHA-256 against a supported-build manifest.
- Locate hook targets using versioned byte signatures.
- Intercept Blaze redirector completion and replace its `ServerInstance`
  endpoint with `127.0.0.1:27920`.
- Mark the returned endpoint as an insecure local connection so EA TLS is not
  involved.
- Supply a deterministic local identity at the Blaze identity/authentication
  boundary.
- Satisfy any pre-Blaze online-state callback that otherwise prevents Press
  Start from reaching the local backend.
- Emit a dedicated bridge log containing signature, hook, identity, and
  redirector state.

The DLL does not store Dynasty data or implement application RPC handlers.

### Local Blaze Bridge

`cfb27blaze.exe` replaces the logger-only role for port `27920`. It also
exposes `/health` and `/events` over loopback HTTP on port `27921`. It is a Go
service with four isolated layers:

1. TCP session handling.
2. Blaze frame and TDF codec.
3. Component/command dispatcher.
4. CFB27 handlers and a typed client for `dynasty.exe`.

The service records every request with connection ID, request ID, component,
command, decoded fields, raw payload, response, and duration.

Initial handlers cover only:

- Connection initialization and keepalive.
- Local authentication and user-session creation.
- Post-auth configuration required by the main menu.
- Presence state required to expose Cloud Dynasty.
- Cloud Dynasty availability.
- Dynasty list containing one seeded local session.
- Opening that session and returning enough state to enter the Dynasty hub.

Unknown commands return a valid Blaze unsupported-command error and are logged.

### Dynasty Service

The existing `dynasty.exe` REST API and SQLite database remain the source of
truth for local sessions, users, teams, stages, and actions. The Blaze bridge
is its only game-facing adapter.

The first-run launcher seeds one local Dynasty if the database contains none.

## Data Flow

1. The game requests online initialization.
2. The DLL supplies the local identity and allows the Blaze path to proceed.
3. The game performs redirector discovery.
4. The DLL replaces the redirector result with the insecure loopback endpoint.
5. The game opens a Blaze connection to `cfb27blaze.exe`.
6. The bridge decodes requests and returns local login, session, config, and
   presence responses.
7. When the game requests Cloud Dynasty data, the bridge queries
   `dynasty.exe`, converts its response to Blaze/TDF, and returns it.
8. Selecting the seeded Dynasty loads its local state through the same bridge.

No EA hostname, IP address, token endpoint, CDN, telemetry endpoint, or
production backend participates in this flow.

## Failure Handling

- An unknown executable hash fails closed before installing hooks.
- A missing or ambiguous signature fails closed and identifies the signature
  in the bridge log.
- The launcher does not start the game unless both local services pass health
  checks.
- A malformed Blaze frame closes only its local TCP session.
- An unknown command receives a protocol-valid error instead of a dropped
  connection.
- REST failures are converted to explicit Blaze errors and logged with the
  associated request ID.
- Hook initialization and shutdown are idempotent.
- No exception may cross a DLL hook boundary.

## Observability

The launcher creates one run directory containing:

- `launcher.log`
- `cfb27-bridge.log`
- `cfb27-blaze.jsonl`
- `dynasty.log`
- The active configuration
- The executable hash
- A final external-connection audit

Logs use shared run and connection IDs so an in-game action can be followed
across the DLL, Blaze bridge, and Dynasty service.

## Testing

### Automated Tests

- Golden tests for Blaze frame parsing and serialization.
- Golden tests for every supported TDF type.
- Request/response tests for redirector, authentication, user session,
  presence, and Dynasty handlers.
- Error tests for malformed frames, unknown components, unknown commands, and
  unavailable Dynasty storage.
- Integration tests using a simulated Blaze client against
  `cfb27blaze.exe` and a temporary Dynasty database.
- DLL tests for executable-hash checks and signature uniqueness against the
  supported Trial executable.

### In-Game Acceptance Test

1. Disconnect the PC from the internet or block all external traffic.
2. Start the CFB27 private stack from Cypress.
3. Launch the Trial executable through Fake AC.
4. Pass Press Start without an EA connection error.
5. Reach the main menu with local presence active.
6. Open Dynasty and select Cloud Dynasty.
7. See exactly one seeded local Dynasty.
8. Load it into the Dynasty hub.
9. Confirm the run audit contains zero external connections.

## Scope Boundaries

This milestone does not include:

- A second player or remote clients.
- Gameplay session hosting.
- Week advancement.
- Complete Dynasty synchronization.
- EA account compatibility.
- Real anti-cheat compatibility.
- Production EA TLS or service emulation.
- Master-server listing, relay traffic, or public hosting.

Those capabilities are separate milestones after the local Cloud Dynasty flow
works reliably and its required Blaze command set is known.

## Completion Criteria

The milestone is complete only when the exact in-game acceptance test succeeds
on the supported Trial build, all automated tests pass, and the external
connection audit reports zero non-loopback connections.
