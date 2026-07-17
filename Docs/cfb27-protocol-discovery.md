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
| 2026-07-16 | CFB27 release 50724 / ProtoSSL plaintext capture | Fire2 framing and route inventory | The 41.9 MB ACP contains 123,507 packets and 268 complete Fire2 frames. EA's supplied `Fire2Frame` source confirms the observed 16-byte layout: payload length, metadata length, component, command, 24-bit message number, packed type/user index, options, and reserved byte. The inventory includes authentication login `0x0001/0x000A`; Cypress's prior codec read component and command two bytes early. | confirmed |
| 2026-06-16 | CFB27 external beta / Cypress diagnostics | Real anti-cheat launch attempt | Snapshot `20260616_173452` had local Cypress services healthy but no live `CollegeFB27.exe`; the VM-rule boot prevented endpoint discovery. | inconclusive |
| 2026-06-16 | CFB27 external beta / Cypress diagnostics | Fake anti-cheat, Play Friend / Ultimate Team / Dynasty loading screens | Snapshots `20260616_174440`, `20260616_174459`, and `20260616_174526` all showed live `CollegeFB27.exe` PID `90256` with one process-owned established TCP connection from `10.0.4.10:31562` to `54.236.91.7:443`, plus a local listener on port `31562`. Reverse DNS for the remote is `ec2-54-236-91-7.compute-1.amazonaws.com`. No connection to local Cypress master, relay, or Dynasty service was owned by the game process. | observed |
| 2026-06-16 | CFB27 external beta / Cypress diagnostics | Known endpoint matching | `54.236.91.7:443` did not match the current GCS/live-config endpoint catalog at capture time. Treat it as an unknown AWS-hosted CFB27/EA service candidate until confirmed by repeated captures or TLS/application evidence. | needs identification |
| 2026-06-16 | CFB27 external beta / Cypress diagnostics build-next | Fake anti-cheat, repeated online-gated screens | Snapshots `20260616_181949`, `20260616_182014`, `20260616_182025`, `20260616_182032`, and `20260616_182035` all showed live `CollegeFB27.exe` PID `160676` with one process-owned established TCP connection from `10.0.4.10:31317` to `13.216.20.181:443`. Reverse DNS is `ec2-13-216-20-181.compute-1.amazonaws.com`. No process-owned connection to local Cypress master, relay, or Dynasty service appeared. | observed |
| 2026-06-16 | CFB27 external beta / Cypress diagnostics build-next | Known endpoint matching | The new CFB27 remote-candidate artifact worked. `13.216.20.181:443` did not match the current GCS/live-config catalog. Separate system-level or PID 0 matches appeared for `gateway.ea.com`/`tos.ea.com` and `freeform-river.data.ea.com`, but those were not owned by `CollegeFB27.exe` in these captures. | needs identification |
| 2026-06-16 | CFB27 external beta / Cypress diagnostics build-next | Dynasty network trigger attempt | Snapshot `20260616_183229`, taken after entering Dynasty to trigger network behavior, still showed only the same CFB27-owned remote `13.216.20.181:443`. No local Cypress master, relay, Dynasty, or new known EA endpoint was owned by the game process. | observed |
| 2026-06-16 | CFB27 external beta / netstat live trace | Passive 30-second process-owned socket sample | Live trace `live-traces/20260616_183132` sampled `CollegeFB27.exe` PID `160676` for 30 seconds. Every sample showed TCP `10.0.4.10:31317 -> 13.216.20.181:443` established and UDP `0.0.0.0:63298`. DNS cache only contained the PTR name `ec2-13-216-20-181.compute-1.amazonaws.com`, not the original service hostname. | observed |
| 2026-06-16 | CFB27 external beta / launcher Trace 30s | Dynasty only, then multi-mode trigger ending with Franchise/Dynasty | Live traces `20260616_191930` and `20260616_192500` each showed one stable CFB27-owned TCP connection to `54.144.133.160:443` plus UDP `*:*`. Reverse DNS is `ec2-54-144-133-160.compute-1.amazonaws.com`. This is a third observed AWS HTTPS candidate. | observed |
| 2026-06-16 | CFB27 local gateway/logger | Diagnostics gateway state | Gateway `http://127.0.0.1:27920` was healthy with zero inbound events after the traces, confirming no traffic is currently being redirected to Cypress. | observed |

## Endpoint Catalog Sources

- `gcs.ea.com/application_id/CFB_27_PC_CLIENT` response: EADP identity, social, MCR, stats, leaderboards, realtime messaging, commerce, experimentation, and instrumentation endpoints.
- Local live-config dump (`Untitled.txt`): confirms football-specific feature gates and likely control-plane services for MCR, TeamBuilder, snapshots, datapatches, net resources, and online mode enablement.
- InitFS config files under `C:\Users\Shadow\Desktop\CFB27\InitFS`: confirm branch service names, root override behavior, local client IP config, Caprica session host, and several online/Blaze/Juice toggles.

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

Notable InitFS keys:

- `Game.CFG` checks `/root/FootballServerSettings.cfg`, `/root/FootballOnlineSettings.cfg`, and `/root/FootballDBSettings.cfg` before falling back to `Scripts/...`. This may allow ModData/root config overrides without native hooks.
- `FootballServerSettings.CFG` sets `FootballServerSettings.ServiceEnvironment PROD`.
- `FootballServerSettings.CFG` sets PC service names including `cfb-2027-pc-ml`, `cfb-2027-pc-mtre`, `cfb-2027-pc-mtri`, and `cfb-2027-pc-fb-event`.
- `FootballOnlineSettings.CFG` sets `FootballOnlineSettings.OriginItemGroupId CFB_27PC`, roster download flags, and commented datapatch override keys.
- `Local.Client.CFG` sets `Client.ServerIp 10.8.168.238`.
- `LicenseeGame.CFG` sets `Juice.ServerIP 10.8.85.210`.
- `FootballGame.CFG` sets `Caprica.SessionServerHost caprica-sls.tib-k8s.qe-sports.ea.com` and `Caprica.SessionServerPort 5050`.
- `FootballGame.CFG` has `Blaze.ClientAutoAccountCreation true` and `Blaze.ServerAutoAccountCreation true`.

The launcher evidence capture resolves and tags these known hosts in `known-ea-endpoints.json` and `known-ea-endpoint-matches.json`. A host appearing in the catalog is not proof that CFB27 used it; only a process-owned connection match during a scenario should be treated as evidence.

## Endpoint Intercept Experiment

Cypress now tracks the strongest CFB27-owned endpoint candidates in:

`tools/cypress-servers/deploy/cfb27-endpoints.example.json`

The diagnostics stack also starts a local CFB27 gateway/logger:

`http://127.0.0.1:27920`

Gateway behavior:

- `/health`: confirms the logger is running.
- `/events`: returns logged inbound requests.
- any other path: records method, path, host, headers, body length, and remote address, then returns a JSON 404.

Important limitation:

- The observed CFB27 remotes are HTTPS `:443` AWS endpoints. Blindly redirecting them to the local gateway is expected to fail unless the game accepts the certificate/SNI path. The first supported experiment is therefore observe-first: identify stable remotes and hostname/TLS behavior before enabling any redirect.

Launcher support:

- `Trace 30s` runs a passive process-owned `netstat -ano` trace for live `CollegeFB27.exe` processes.
- `Network Trace` runs a 30-second Windows `netsh trace` capture and writes an `.etl` file for offline inspection.
- `Block Candidates` adds outbound Windows Firewall block rules for the observed CFB27 candidate IPs in `cfb27-endpoints.json`.
- `Unblock` removes those Cypress-created firewall rules.
- Trace output is stored under `%APPDATA%\Cypress\Diagnostics\CFB27\live-traces\<timestamp>\`.
- Trace files include `netstat-events.json`, `reverse-dns.json`, `event-groups.json`, and `summary.md`.

Controlled block test:

1. Run `Start Diagnostics`.
2. Run `Trace 30s` on the target screen and confirm the current CFB27-owned candidate.
3. Click `Block Candidates`.
4. Re-enter the same game screen or retry the same online action.
5. Capture a snapshot and note whether the in-game error changes.
6. Click `Unblock` immediately after the test.

If `Block Candidates` fails, run the launcher as administrator or add/remove equivalent firewall rules manually. Never leave candidate blocks enabled after a test unless you are intentionally isolating the game from those endpoints.

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
