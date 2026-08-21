# CFB27 H2H Invite System Design

## Problem

The private Blaze server currently acknowledges the H2H lobby setup but does not
implement the targeted game discovery/join contract used by the EA client. In
the verified join capture, the guest first sends GameManager `4/105` with a
game-id list, then sends `4/9` containing the selected game id. The server
currently treats both commands as empty acknowledgements. The invite transport
also sends OSDK `2056/17`, which is currently unregistered and therefore returns
`COMMAND_NOT_FOUND`.

The result is that the invite screen can list a peer, but the guest cannot
resolve the host's game or seat itself in that specific game. The existing
`4/10` implementation also pairs a caller with the first open room, which is
only safe as a compatibility fallback when there is one unambiguous pending
room.

## Capture-backed contract

- `4/105` request: `DNAM` plus `GLST` containing the requested game id.
- `4/105` response: a `GDAT` list containing the requested live game record.
- `4/9` request: `CMGD`/`PLJD` player data and a `GID` naming the target game.
- `4/9` response: the same compact `{ESID, GID, JGS, OCAL}` shape observed in
  the successful join capture.
- Successful joining still emits a rewritten peer-hosted `4/20` setup and the
  existing `4/21`, `4/116`, and `4/30` membership notifications.
- OSDK `2056/17` receives an empty successful Blaze response so the invite flow
  is not rejected at the transport registration/acknowledgement step.

## Design

1. Extend the in-memory game registry with lookup and targeted-seat operations.
   A targeted join must select the requested game id, reject missing/full games
   without mutating another room, and preserve the existing per-player mesh
   identity and socket routing.
2. Add a small invite registry keyed by `(target persona, game id)`. Entries
   have a short expiry, can be consumed once, and are removed when the game or
   player is removed. The registry is the stateful seam for future invite
   delivery parsing and gives the compatibility path a deterministic pending
   room instead of making room selection depend on map iteration order.
3. Build `GDAT` from the live game and current roster using the same identity,
   connection-group, and peer-hosted topology rules as `4/20`. Include the
   capture-required game identity/state fields and a roster list so the invite
   UI can display/select a real room; the peer address remains authoritative in
   the subsequent rewritten `4/20` setup.
4. Keep `4/10` for the current create-or-join flow. When there is exactly one
   open pending room it remains the compatibility path for clients that do not
   expose a target id. When multiple rooms are open, a caller without a target
   is placed in a new room rather than cross-paired with an unrelated invite.
5. After a second player joins, send the refreshed setup/state to both players
   and broadcast the reciprocal joining/membership notifications. This closes
   the current asymmetry where only the request socket receives the setup.

## Non-goals

- Reconstructing an undocumented GameInvite protobuf send/accept RPC that is
  absent from the captures. The gRPC subscription remains an idle private
  stream; the verified Blaze join contract is the authoritative compatibility
  path.
- Changing the peer-hosted topology or the existing dynasty/social services
  unrelated to H2H.

## Acceptance criteria

- A `4/105` request for game B never returns game A's data.
- A `4/9` request for game B seats the caller in game B only, and a missing or
  full target does not alter any room.
- Two simultaneous open rooms remain isolated when their guests use targeted
  joins; an untargeted compatibility caller joins only the single unambiguous
  open room.
- Invite entries are consumed once and cleaned on expiry, leave, and game
  deletion.
- OSDK `2056/17` no longer produces `COMMAND_NOT_FOUND`.
- Focused H2H tests and the complete Go test suite pass.
