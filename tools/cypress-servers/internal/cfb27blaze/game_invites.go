package cfb27blaze

import (
	"sort"
	"sync"
	"time"
)

// gameInviteTTL is deliberately short. An invite is a rendezvous hint for a
// lobby that is currently open, not a durable friend relationship.
const gameInviteTTL = 2 * time.Minute

type gameInviteKey struct {
	targetPersona int64
	gameID        int64
}

type gameInvite struct {
	senderPersona int64
	targetPersona int64
	gameID        int64
	expiresAt     time.Time
}

// gameInviteRegistry keeps the state that the social invite transport does not
// expose in the captured Blaze stream. It is also the deterministic seam for a
// future decoded invite delivery without making GameManager infer a target
// from map iteration order.
type gameInviteRegistry struct {
	mu      sync.Mutex
	entries map[gameInviteKey]gameInvite
}

func newGameInviteRegistry() *gameInviteRegistry {
	return &gameInviteRegistry{entries: make(map[gameInviteKey]gameInvite)}
}

func (r *gameInviteRegistry) offer(senderPersona, targetPersona, gameID int64, now time.Time) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.pruneLocked(now)
	key := gameInviteKey{targetPersona: targetPersona, gameID: gameID}
	r.entries[key] = gameInvite{
		senderPersona: senderPersona,
		targetPersona: targetPersona,
		gameID:        gameID,
		expiresAt:     now.Add(gameInviteTTL),
	}
}

func (r *gameInviteRegistry) consume(targetPersona, gameID int64, now time.Time) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.pruneLocked(now)
	key := gameInviteKey{targetPersona: targetPersona, gameID: gameID}
	if _, ok := r.entries[key]; !ok {
		return false
	}
	delete(r.entries, key)
	return true
}

func (r *gameInviteRegistry) removeGame(gameID int64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for key := range r.entries {
		if key.gameID == gameID {
			delete(r.entries, key)
		}
	}
}

func (r *gameInviteRegistry) removePlayer(personaID int64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for key, invite := range r.entries {
		if key.targetPersona == personaID || invite.senderPersona == personaID {
			delete(r.entries, key)
		}
	}
}

func (r *gameInviteRegistry) prune(now time.Time) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.pruneLocked(now)
}

func (r *gameInviteRegistry) pruneLocked(now time.Time) {
	for key, invite := range r.entries {
		if !now.Before(invite.expiresAt) {
			delete(r.entries, key)
		}
	}
}

// offerGameToConnectedPlayers records a lobby offer for each currently online
// player. The actual GameInvite protobuf send/accept path is outside the
// captured private-server transport, but the following Blaze 4/105 -> 4/9
// sequence is fully addressable once the player selects a friend.
func (s *Service) offerGameToConnectedPlayers(gameID, senderPersona int64) {
	if s.invites == nil {
		return
	}
	now := time.Now().UTC()
	for _, player := range s.connectedPlayers(senderPersona) {
		s.invites.offer(senderPersona, player.personaID, gameID, now)
	}
}

// sortedGameIDs makes diagnostics and tests stable without exposing the
// registry's internal map.
func sortedGameIDs(games map[int64]*gameSession) []int64 {
	ids := make([]int64, 0, len(games))
	for id := range games {
		ids = append(ids, id)
	}
	sort.Slice(ids, func(i, j int) bool { return ids[i] < ids[j] })
	return ids
}
