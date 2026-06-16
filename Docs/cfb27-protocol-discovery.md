# CFB27 Protocol Discovery

This file tracks verified CFB27 online/runtime behavior. Do not port PvZ offsets into CFB27 until an item has a repeatable validation result here.

## Current Status

- Cypress launcher, master server, relay metadata, and a safe no-op `cypress_CFB27.dll` are in place.
- CFB27 executable static strings show useful anchors: `Blaze`, `CareerModeRPC`, `LeagueSavesManager`, `FMOnlineFrontEnd`, and `SportsGamerSystemFactory`.
- Gameplay/network hooks are intentionally disabled.

## Launch Args Under Test

| Arg | Purpose | Status | Notes |
| --- | --- | --- | --- |
| `-Online.Backend Backend_Local` | Force local backend selection | unverified | Must be checked during main menu and online front end. |
| `-Online.PeerBackend Backend_Local` | Force peer backend selection | unverified | Must be checked with host and join attempts. |
| `-Client.ServerIp <addr>` | Direct client target | unverified | Current launcher client builder emits this. |
| `-Network.ServerAddress <addr>` | Host bind/advertise address | unverified | Current launcher server builder emits this. |
| `-listen <addr>` | Host listen mode | unverified | Current launcher server builder emits this. |

## Observation Checklist

Run each step with diagnostics enabled and record TCP/UDP endpoints, DNS attempts, process output, and launcher logs.

1. Main menu idle.
2. Online front end open.
3. League/Dynasty list screen open.
4. Host/private session attempt.
5. Join direct session attempt.
6. Join relay session attempt.
7. Clean shutdown.

## Evidence Log

Add dated entries here as mappings are confirmed.

| Date | Build | Scenario | Finding | Status |
| --- | --- | --- | --- | --- |

## Endpoint Catalog Sources

- `gcs.ea.com/application_id/CFB_27_PC_CLIENT` response: EADP identity, social, MCR, stats, leaderboards, realtime messaging, commerce, experimentation, and instrumentation endpoints.
- Local live-config dump (`Untitled.txt`): confirms football-specific feature gates and likely control-plane services for MCR, TeamBuilder, snapshots, datapatches, net resources, and online mode enablement.

Notable live-config keys:

- `MCR_ENABLED=1`
- `MCR_API_HOST=https://api.mcr.ea.com`
- `MCR_QUERY_HOST=https://q.mcr.ea.com`
- `MCR_SID_LEAGUE_TEAMBUILDER=161`
- `MCR_SID_REPLAYS=171`
- `MCR_SID_SNAPSHOTS=158`
- `MCR_SNAPSHOTS_ENABLED=1`
- `USING_LOCAL_SERVER=0`
- `SERVER_SLB_CONTEXT_REQUIRED=madden-2026-...`

The launcher evidence capture resolves and tags these known hosts in `known-ea-endpoints.json` and `known-ea-endpoint-matches.json`. A host appearing in the catalog is not proof that CFB27 used it; only a process-owned connection match during a scenario should be treated as evidence.

## Evidence Runs

Launcher captures are stored under:

`%APPDATA%\Cypress\Diagnostics\CFB27\runs\<timestamp>\`

Each run contains:

- `summary.md`: quick human-readable overview.
- `evidence.json`: structured run data with scenario, instances, launch args, environment, services, TCP connections, UDP listeners, and notes.
- `endpoints.json`: active TCP/UDP snapshot from Windows networking APIs.
- `services.json`: local master, relay API, and Dynasty health responses.
- `launcher-events.log`: recent CFB27 launcher status and process output lines.

Interpretation:

- `master.ok=true` means the private Cypress master API is reachable.
- `relay.ok=true` means the local relay API is reachable; relay traffic still needs direct gameplay validation.
- `dynasty.ok=true` means the CFB27 Dynasty service loaded the FTX catalog.
- A launch arg remains `unverified` until a run shows the game created matching local/direct/relay network activity or a confirmed runtime log line.

Runtime captures are evidence artifacts, not authoritative documentation. Copy only confirmed findings into the Evidence Log above.
