package cfb27blaze

import (
	"context"
	"testing"
	"time"

	"cypress-servers/internal/blaze"
)

func inviteTestPlayer(key uint32, persona int64, name string, group int64) *gamePlayer {
	return &gamePlayer{
		identity: clientIdentity{
			blazeID:     persona,
			nucleusID:   persona + 10000,
			personaID:   persona,
			personaName: name,
		},
		playerKey:       key,
		connectionID:    "connection-" + name,
		connectionGroup: group,
		userSessionID:   persona + 20000,
		externalIP:      0x0a000000 + uint32(key),
		externalPort:    defaultGamePort,
	}
}

func putOpenInviteGame(r *gameRegistry, id int64, host *gamePlayer) *gameSession {
	r.mu.Lock()
	defer r.mu.Unlock()
	game := &gameSession{id: id, players: []*gamePlayer{host}}
	r.games[id] = game
	return game
}

func TestTargetedJoinUsesRequestedGame(t *testing.T) {
	registry := newGameRegistry()
	gameA := putOpenInviteGame(registry, 101, inviteTestPlayer(1, 1001, "HostA", 501))
	gameB := putOpenInviteGame(registry, 202, inviteTestPlayer(2, 1002, "HostB", 502))
	guest := inviteTestPlayer(3, 1003, "Guest", 503)

	joined, ok := registry.targetedJoin(gameB.id, guest)
	if !ok {
		t.Fatal("targeted join was rejected")
	}
	if joined != gameB {
		t.Fatalf("joined game %d, want requested game %d", joined.id, gameB.id)
	}
	if got := len(registry.playersOf(gameA.id)); got != 1 {
		t.Fatalf("game A has %d players, want 1", got)
	}
	if got := len(registry.playersOf(gameB.id)); got != 2 {
		t.Fatalf("game B has %d players, want 2", got)
	}
}

func TestGameDataRequestReturnsRequestedGame(t *testing.T) {
	svc := NewService(Config{Profile: "LocalPlayer"})
	gameA := putOpenInviteGame(svc.games, 301, inviteTestPlayer(1, 1101, "HostA", 601))
	gameB := putOpenInviteGame(svc.games, 402, inviteTestPlayer(2, 1102, "HostB", 602))

	payload, err := blaze.Encode([]blaze.Field{
		{Tag: "DNAM", Type: blaze.TypeString, Value: "default"},
		{Tag: "GLST", Type: blaze.TypeList, Value: blaze.List{
			ElementType: blaze.TypeInteger,
			Values:      []any{gameB.id},
		}},
	})
	if err != nil {
		t.Fatal(err)
	}
	fields, code := svc.handleGameManagerGetGameData(context.Background(), blaze.Frame{Payload: payload})
	if code != 0 {
		t.Fatalf("4/105 returned error 0x%X", code)
	}
	data, ok := findList(fields, "GDAT")
	if !ok || len(data.Values) != 1 {
		t.Fatalf("GDAT = %#v, want one game record", data)
	}
	record, ok := data.Values[0].([]blaze.Field)
	if !ok {
		t.Fatalf("GDAT[0] = %T, want struct", data.Values[0])
	}
	if got := findIntPath(t, record, "GID"); got != gameB.id {
		t.Fatalf("GDAT[0]/GID = %d, want %d", got, gameB.id)
	}
	if got := findIntPath(t, record, "GSTA"); got != gameStatePreGame {
		t.Fatalf("GDAT[0]/GSTA = %d, want %d", got, gameStatePreGame)
	}
	if _, ok := findList(record, "ROST"); !ok {
		t.Fatal("GDAT[0] has no ROST")
	}
	roster, _ := findList(record, "ROST")
	entry, ok := roster.Values[0].([]blaze.Field)
	if !ok {
		t.Fatalf("GDAT[0]/ROST[0] = %T, want struct", roster.Values[0])
	}
	if got := findIntPath(t, entry, "CONG"); got != gameB.players[0].connectionGroup {
		t.Fatalf("GDAT[0]/ROST[0]/CONG = %d, want %d", got, gameB.players[0].connectionGroup)
	}
	if got := findIntPath(t, entry, "GID"); got != gameB.id {
		t.Fatalf("GDAT[0]/ROST[0]/GID = %d, want %d", got, gameB.id)
	}
	if got := len(svc.games.playersOf(gameA.id)); got != 1 {
		t.Fatalf("4/105 mutated game A: %d players", got)
	}
}

func TestTargetedGameManagerJoinSeatsRequestedGame(t *testing.T) {
	svc := NewService(Config{Profile: "LocalPlayer"})
	gameA := putOpenInviteGame(svc.games, 501, inviteTestPlayer(1, 1201, "HostA", 701))
	gameB := putOpenInviteGame(svc.games, 602, inviteTestPlayer(2, 1202, "HostB", 702))
	guest := inviteTestPlayer(3, 1203, "Guest", 703)
	cs := &clientSession{
		identity:     guest.identity,
		playerKey:    guest.playerKey,
		externalIP:   guest.externalIP,
		externalPort: guest.externalPort,
	}
	requestPayload, err := blaze.Encode([]blaze.Field{
		{Tag: "GID", Type: blaze.TypeInteger, Value: gameB.id},
		{Tag: "CMGD", Type: blaze.TypeStruct, Value: []blaze.Field{
			{Tag: "PNET", Type: blaze.TypeUnion, Value: blaze.Union{
				ActiveMember: 2,
				Value: &blaze.Field{Tag: "VALU", Type: blaze.TypeStruct, Value: []blaze.Field{
					{Tag: "INIP", Type: blaze.TypeStruct, Value: []blaze.Field{
						{Tag: "PORT", Type: blaze.TypeInteger, Value: defaultGamePort},
					}},
				}},
			}},
		}},
		{Tag: "PLJD", Type: blaze.TypeStruct, Value: []blaze.Field{
			{Tag: "BTPL", Type: blaze.TypeObjectID, Value: blaze.ObjectID{
				Type: blaze.ObjectType{Component: ComponentUserSessions, Type: 2},
				ID:   guest.connectionGroup,
			}},
		}},
	})
	if err != nil {
		t.Fatal(err)
	}
	reply, code := svc.handleGameManagerJoinGame(
		withConnectionID(withClientSession(context.Background(), cs), guest.connectionID),
		blaze.Frame{Payload: requestPayload},
	)
	if code != 0 {
		t.Fatalf("4/9 returned error 0x%X", code)
	}
	if got := findIntPath(t, reply, "GID"); got != gameB.id {
		t.Fatalf("4/9 reply GID = %d, want %d", got, gameB.id)
	}
	if got := len(svc.games.playersOf(gameA.id)); got != 1 {
		t.Fatalf("4/9 mutated game A: %d players", got)
	}
	if got := len(svc.games.playersOf(gameB.id)); got != 2 {
		t.Fatalf("4/9 left game B with %d players, want 2", got)
	}
}

func TestInviteRegistryConsumesAndExpires(t *testing.T) {
	registry := newGameInviteRegistry()
	now := time.Date(2026, time.August, 20, 12, 0, 0, 0, time.UTC)

	registry.offer(1001, 1002, 9001, now)
	if !registry.consume(1002, 9001, now.Add(time.Second)) {
		t.Fatal("fresh invite was not consumable")
	}
	if registry.consume(1002, 9001, now.Add(2*time.Second)) {
		t.Fatal("invite was consumed more than once")
	}

	registry.offer(1001, 1002, 9002, now)
	registry.offer(1001, 1003, 9003, now)
	registry.prune(now.Add(gameInviteTTL + time.Second))
	if registry.consume(1002, 9002, now.Add(gameInviteTTL+time.Second)) {
		t.Fatal("expired invite remained for target 1002")
	}
	if registry.consume(1003, 9003, now.Add(gameInviteTTL+time.Second)) {
		t.Fatal("expired invite remained for target 1003")
	}
}

func TestGameManagerLeaveRemovesInviteState(t *testing.T) {
	svc := NewService(Config{Profile: "LocalPlayer"})
	host := inviteTestPlayer(1, 1401, "Host", 901)
	game := putOpenInviteGame(svc.games, 11001, host)
	svc.invites.offer(host.identity.personaID, 1402, game.id, time.Now().UTC())

	payload, err := blaze.Encode([]blaze.Field{
		{Tag: "GID", Type: blaze.TypeInteger, Value: game.id},
		{Tag: "PID", Type: blaze.TypeInteger, Value: host.identity.personaID},
	})
	if err != nil {
		t.Fatal(err)
	}
	if _, code := svc.handleRemovePlayer(context.Background(), blaze.Frame{Payload: payload}); code != 0 {
		t.Fatalf("4/22 returned error 0x%X", code)
	}
	if svc.invites.consume(1402, game.id, time.Now().UTC()) {
		t.Fatal("4/22 left an invite for the removed game")
	}

	game = putOpenInviteGame(svc.games, 11002, host)
	svc.invites.offer(host.identity.personaID, 1402, game.id, time.Now().UTC())
	if _, code := svc.handleLeaveGroup(context.Background(), blaze.Frame{Payload: mustEncodeInviteFields(t, game.id, host.identity.personaID)}); code != 0 {
		t.Fatalf("4/67 returned error 0x%X", code)
	}
	if svc.invites.consume(1402, game.id, time.Now().UTC()) {
		t.Fatal("4/67 left an invite for the removed game")
	}
}

func mustEncodeInviteFields(t *testing.T, gameID, personaID int64) []byte {
	t.Helper()
	payload, err := blaze.Encode([]blaze.Field{
		{Tag: "GID", Type: blaze.TypeInteger, Value: gameID},
		{Tag: "PID", Type: blaze.TypeInteger, Value: personaID},
	})
	if err != nil {
		t.Fatal(err)
	}
	return payload
}

func TestCompatibilityJoinDoesNotCrossPairMultipleOpenRooms(t *testing.T) {
	registry := newGameRegistry()
	putOpenInviteGame(registry, 10001, inviteTestPlayer(1, 1301, "HostA", 801))
	putOpenInviteGame(registry, 10002, inviteTestPlayer(2, 1302, "HostB", 802))

	joiner := inviteTestPlayer(3, 1303, "Joiner", 803)
	game, created := registry.createOrJoin(joiner)
	if !created {
		t.Fatal("untargeted joiner was paired with an ambiguous open room")
	}
	if game.id == 10001 || game.id == 10002 {
		t.Fatalf("untargeted joiner entered existing room %d", game.id)
	}
	if got := len(registry.playersOf(10001)); got != 1 {
		t.Fatalf("room 10001 has %d players, want 1", got)
	}
	if got := len(registry.playersOf(10002)); got != 1 {
		t.Fatalf("room 10002 has %d players, want 1", got)
	}
}

func TestOSDKInviteCommandIsRegistered(t *testing.T) {
	svc := NewService(Config{Profile: "LocalPlayer"})
	handler := svc.handlers[route{ComponentOSDK, 17}]
	if handler == nil {
		t.Fatal("OSDK 2056/17 invite handler is not registered")
	}
	if _, code := handler(context.Background(), blaze.Frame{}); code != 0 {
		t.Fatalf("OSDK 2056/17 returned error 0x%X", code)
	}
}
