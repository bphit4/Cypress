# CFB27 H2H Invite System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make private-server H2H invites resolve a specific live game and let the invited player enter that game with the capture-compatible Blaze sequence.

**Architecture:** Keep GameManager state authoritative in the existing in-memory registry, add a focused invite registry for target/game/expiry bookkeeping, and route `4/105` and `4/9` through explicit targeted handlers. Reuse the existing dynamic peer-hosted setup builder for both targeted and compatibility joins; register OSDK `2056/17` as the capture-backed successful invite acknowledgement.

**Tech Stack:** Go, the repository's Blaze TDF encoder/decoder, `go test`, embedded capture fixtures.

**Spec:** `Docs/superpowers/specs/2026-08-20-cfb27-h2h-invite-system.md`

## Global Constraints

- Preserve the capture-backed `4/105` → `4/9` ordering and response shapes.
- Never select a different game than the requested `GID`.
- Preserve each player's request-derived connection group and peer address.
- Do not invent an undocumented GameInvite protobuf schema.
- Use the existing peer-hosted `4/20` builder and run the complete Go test suite before completion.

---

### Task 1: Add failing registry and targeted-join tests

**Files:**
- Create: `tools/cypress-servers/internal/cfb27blaze/game_invites_test.go`
- Modify: `tools/cypress-servers/internal/cfb27blaze/h2h_test.go` only if a shared H2H helper is needed

**Interfaces:**
- Consumes: existing `gameRegistry`, `gameSession`, `gamePlayer`, `NewService`, `blaze.Encode`, and `blaze.Decode`.
- Produces: regression tests defining `targetedJoin`, `gameDataForRequest`, invite cleanup, and compatibility-room behavior before implementation.

- [ ] **Step 1: Write the failing tests**

Add tests that:

```go
func TestTargetedJoinUsesRequestedGame(t *testing.T) { /* create two rooms; join game B; assert only B has guest */ }
func TestGameDataRequestReturnsRequestedGame(t *testing.T) { /* call 4/105 with GLST=[game B]; assert GDAT[0].GID == game B */ }
func TestInviteRegistryConsumesAndExpires(t *testing.T) { /* offer; consume once; add expired entry; prune; assert absent */ }
func TestCompatibilityJoinDoesNotCrossPairMultipleOpenRooms(t *testing.T) { /* two open rooms; untargeted caller starts a third */ }
func TestOSDKInviteCommandIsRegistered(t *testing.T) { /* assert route 2056/17 returns error code zero */ }
```

Use distinct `playerKey`, persona, connection group, and game ids in every
fixture. Build the `4/105` payload as `DNAM=default` and a `GLST` integer list;
build the targeted join payload with `GID`, `CMGD`, and `PLJD/BTPL` so it uses
the same parser as real requests.

- [ ] **Step 2: Run the focused tests and verify they fail for the current defects**

Run from `tools/cypress-servers`:

```powershell
go test ./internal/cfb27blaze -run 'Test(TargetedJoin|GameDataRequest|InviteRegistry|CompatibilityJoin|OSDKInvite)' -count=1
```

Expected: FAIL because `4/105` is an empty acknowledgement, `4/9` has no
targeted handler, multiple rooms are selected by the old first-room loop, and
OSDK `2056/17` is not registered.

### Task 2: Implement invite state and targeted GameManager state transitions

**Files:**
- Create: `tools/cypress-servers/internal/cfb27blaze/game_invites.go`
- Modify: `tools/cypress-servers/internal/cfb27blaze/service.go:43-86,119-151`
- Modify: `tools/cypress-servers/internal/cfb27blaze/gamemanager.go:90-145,333-385,473-598`

**Interfaces:**
- Consumes: `gamePlayer`, `gameSession`, `gameRegistry`, `clientSession`, and existing notification builders.
- Produces: `gameInviteRegistry`, `gameRegistry.game(id)`, `gameRegistry.targetedJoin`, `gameRegistry.openGames`, `handleGameManagerGetGameData`, and `handleGameManagerJoinGame`.

- [ ] **Step 1: Add the minimal invite registry implementation**

Create a mutex-protected registry with entries containing sender persona,
target persona, game id, and expiry. Provide exact methods:

```go
type gameInviteRegistry struct { /* mutex, entries, clock */ }
func (r *gameInviteRegistry) offer(senderPersona, targetPersona, gameID int64, now time.Time)
func (r *gameInviteRegistry) consume(targetPersona, gameID int64, now time.Time) bool
func (r *gameInviteRegistry) removeGame(gameID int64)
func (r *gameInviteRegistry) removePlayer(personaID int64)
func (r *gameInviteRegistry) prune(now time.Time)
```

Use a fixed short TTL constant and make cleanup deterministic in tests by
passing `now` to the internal operations.

- [ ] **Step 2: Add exact-game registry operations**

Implement lookup by id, targeted join with capacity and duplicate-player
checks, and a stable `openGames` snapshot. Change compatibility selection to
use a single open room only; otherwise create a new room. Keep all mutations
under the existing registry mutex and remove invite entries when a game is
deleted or a player leaves.

- [ ] **Step 3: Add service-owned invite state and capture-compatible handlers**

Initialize `invites` in `NewService`, register GameManager `4/105` and `4/9`,
and have the handlers decode only the fields needed for routing. Return the
existing `{ESID,GID,JGS,OCAL}` reply for a successful targeted join. Return a
non-mutating Blaze error for an unknown/full target instead of falling back to
another game.

- [ ] **Step 4: Run the focused tests**

```powershell
go test ./internal/cfb27blaze -run 'Test(TargetedJoin|GameDataRequest|InviteRegistry|CompatibilityJoin|OSDKInvite)' -count=1
```

Expected: registry, targeted routing, and cleanup tests pass; the `GDAT` shape
test may still fail until Task 3 builds the live game record.

### Task 3: Build live GDAT and finish invite transport compatibility

**Files:**
- Create: `tools/cypress-servers/internal/cfb27blaze/gamedata.go`
- Modify: `tools/cypress-servers/internal/cfb27blaze/handlers.go:78-87`
- Modify: `tools/cypress-servers/internal/cfb27blaze/gamemanager.go:333-385,445-470`

**Interfaces:**
- Consumes: `gameSession`, `gamePlayer`, `gameSetupNotification` identity helpers, and `gameInviteRegistry`.
- Produces: `buildGameDataResponse`, `handleGameManagerGetGameData`, and capture-compatible notification refresh after a targeted/compatibility join.

- [ ] **Step 1: Add the failing GDAT shape assertions**

Extend the targeted data test to assert `GDAT` is a list with one record and
that the record contains `GID`, `GSTA=130`, `GTYP`, `GNAM`, `NTOP=0`, and a
`ROST` list whose player entries carry the live persona name, `CONG`, `UGID`,
and game id values.

- [ ] **Step 2: Build the live game record**

Create the smallest capture-compatible record that includes the fields above,
the live admin list, player count/list, and the roster entries generated from
the current `gamePlayer` values. Encode it as a `GDAT` list, not as a top-level
game struct, and preserve the requested game id even when another room exists.

- [ ] **Step 3: Register OSDK `2056/17` and route its acknowledgement**

Register the captured OSDK invite command with an empty-success handler. If its
payload decodes into a target/game pair, record that offer in the invite
registry; if it is opaque, acknowledge it without guessing a protobuf schema.
This keeps the verified transport working while leaving undocumented payload
interpretation out of the server.

- [ ] **Step 4: Refresh both players after the second seat is filled**

Refactor the join notification path so a second player receives the current
setup/state and the existing player receives the refreshed setup/state too,
then send reciprocal `4/21`, `4/116`, and `4/30` notifications. Preserve the
request socket for the direct reply and do not send through an arbitrary idle
socket.

- [ ] **Step 5: Run focused H2H tests**

```powershell
go test ./internal/cfb27blaze -run 'Test(GameSetup|PlayerJoining|CreateOrJoin|TargetedJoin|GameDataRequest|InviteRegistry|CompatibilityJoin|OSDKInvite)' -count=1
```

Expected: PASS.

### Task 4: Verify the complete server and inspect the patch

**Files:**
- Modify: none unless verification exposes a regression

- [ ] **Step 1: Run formatting and diff checks**

From `tools/cypress-servers`:

```powershell
gofmt -w internal/cfb27blaze/game_invites.go internal/cfb27blaze/gamedata.go internal/cfb27blaze/gamemanager.go internal/cfb27blaze/handlers.go internal/cfb27blaze/service.go internal/cfb27blaze/game_invites_test.go
git diff --check
```

- [ ] **Step 2: Run the package suite**

```powershell
go test ./internal/cfb27blaze -count=1
```

- [ ] **Step 3: Run the full Go suite**

```powershell
go test ./... -count=1
```

- [ ] **Step 4: Review changed files and verify no unrelated edits**

From the repository root:

```powershell
git status --short
git diff --stat
git diff -- tools/cypress-servers/internal/cfb27blaze Docs/superpowers/specs Docs/superpowers/plans
```

Confirm the diff contains only the H2H invite implementation, its tests, and
the two design documents.

### Task 5: Commit the verified fix on main

**Files:**
- Modify: git history only

- [ ] **Step 1: Commit the implementation**

From the repository root:

```powershell
git add Docs/superpowers/specs/2026-08-20-cfb27-h2h-invite-system.md Docs/superpowers/plans/2026-08-20-cfb27-h2h-invite-system.md tools/cypress-servers/internal/cfb27blaze
git commit -m "fix: make CFB27 H2H invites join targeted games"
```

- [ ] **Step 2: Verify the commit and clean worktree**

```powershell
git status --short --branch
git log -1 --oneline
```

Expected: `main` has the new commit and no uncommitted files remain.
