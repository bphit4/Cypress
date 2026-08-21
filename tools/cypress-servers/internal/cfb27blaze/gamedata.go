package cfb27blaze

import (
	"context"
	"time"

	"cypress-servers/internal/blaze"
)

// gameDataAttributes are the stable H2H settings observed in the successful
// 4/105 response. They are enough for the invite screen to render a room while
// keeping the private server independent of EA's matchmaking service.
func gameDataAttributes() blaze.Map {
	entries := []blaze.MapEntry{
		{Key: "accClock", Value: "12"},
		{Key: "evenTeams", Value: "0"},
		{Key: "fairplay", Value: "1"},
		{Key: "fatigue", Value: "0"},
		{Key: "FIXED_RATE", Value: "35"},
		{Key: "gameFlow", Value: "1"},
		{Key: "gameMode", Value: "onurn"},
		{Key: "gameSkill", Value: "3"},
		{Key: "injuries", Value: "0"},
		{Key: "isUsingUGC", Value: "0"},
		{Key: "lcuVersion", Value: "1.0"},
		{Key: "OSDK_gameMode", Value: "1"},
		{Key: "platformPriority", Value: "1"},
		{Key: "quarterLen", Value: "6"},
		{Key: "weather", Value: "1"},
	}
	return blaze.Map{KeyType: blaze.TypeString, ValueType: blaze.TypeString, Entries: entries}
}

func gameDataRosterEntry(game *gameSession, player *gamePlayer) []blaze.Field {
	return []blaze.Field{
		{Tag: "AIDS", Type: blaze.TypeStruct, Value: localAccountIdentityFields(player.identity)},
		{Tag: "CNTY", Type: blaze.TypeInteger, Value: localCountry},
		{Tag: "ENID", Type: blaze.TypeString, Value: ""},
		{Tag: "EXID", Type: blaze.TypeInteger, Value: player.identity.nucleusID},
		{Tag: "JGTS", Type: blaze.TypeInteger, Value: time.Now().UTC().UnixMicro()},
		{Tag: "LOC", Type: blaze.TypeInteger, Value: localLocale},
		{Tag: "NAME", Type: blaze.TypeString, Value: player.identity.personaName},
		{Tag: "NASP", Type: blaze.TypeString, Value: "cem_ea_id"},
		{Tag: "PID", Type: blaze.TypeInteger, Value: player.identity.personaID},
		{Tag: "RCTS", Type: blaze.TypeInteger, Value: int64(0)},
		{Tag: "ROLE", Type: blaze.TypeString, Value: ""},
		{Tag: "STAT", Type: blaze.TypeInteger, Value: playerStateActive},
		{Tag: "STYP", Type: blaze.TypeInteger, Value: int64(1)},
		{Tag: "TIDX", Type: blaze.TypeInteger, Value: player.slot},
		{Tag: "CONG", Type: blaze.TypeInteger, Value: player.connectionGroup},
		{Tag: "UGID", Type: blaze.TypeObjectID, Value: blaze.ObjectID{
			Type: blaze.ObjectType{Component: ComponentUserSessions, Type: 2},
			ID:   player.connectionGroup,
		}},
		{Tag: "GID", Type: blaze.TypeInteger, Value: game.id},
	}
}

func buildGameDataRecord(game *gameSession, players []*gamePlayer) []blaze.Field {
	admins := make([]any, 0, 1)
	if len(players) > 0 {
		admins = append(admins, players[0].identity.personaID)
	}
	roster := make([]any, 0, len(players))
	for _, player := range players {
		roster = append(roster, gameDataRosterEntry(game, player))
	}
	return []blaze.Field{
		{Tag: "ADMN", Type: blaze.TypeList, Value: blaze.List{ElementType: blaze.TypeInteger, Values: admins}},
		{Tag: "ATTR", Type: blaze.TypeMap, Value: gameDataAttributes()},
		{Tag: "CAP", Type: blaze.TypeList, Value: blaze.List{ElementType: blaze.TypeInteger, Values: []any{int64(0), int64(2), int64(0), int64(0)}}},
		{Tag: "CPLY", Type: blaze.TypeInteger, Value: int64(1)},
		{Tag: "CPOP", Type: blaze.TypeInteger, Value: int64(1)},
		{Tag: "CPOV", Type: blaze.TypeList, Value: blaze.List{ElementType: blaze.TypeInteger, Values: []any{int64(4), int64(22), int64(23)}}},
		{Tag: "GID", Type: blaze.TypeInteger, Value: game.id},
		{Tag: "GMRG", Type: blaze.TypeInteger, Value: int64(0)},
		{Tag: "GNAM", Type: blaze.TypeString, Value: "College Football"},
		{Tag: "GSET", Type: blaze.TypeInteger, Value: int64(3124)},
		{Tag: "GSTA", Type: blaze.TypeInteger, Value: gameStatePreGame},
		{Tag: "GTYP", Type: blaze.TypeInteger, Value: int64(0)},
		{Tag: "GURL", Type: blaze.TypeString, Value: ""},
		{Tag: "HOST", Type: blaze.TypeInteger, Value: func() int64 {
			if len(players) == 0 {
				return 0
			}
			return players[0].identity.personaID
		}()},
		{Tag: "HSES", Type: blaze.TypeInteger, Value: func() int64 {
			if len(players) == 0 {
				return 0
			}
			return players[0].userSessionID
		}()},
		{Tag: "MODE", Type: blaze.TypeString, Value: "onurn"},
		{Tag: "NTOP", Type: blaze.TypeInteger, Value: gameNetworkTopology()},
		{Tag: "PCNT", Type: blaze.TypeList, Value: blaze.List{ElementType: blaze.TypeInteger, Values: []any{int64(0), int64(len(players)), int64(0), int64(0)}}},
		{Tag: "PRES", Type: blaze.TypeInteger, Value: int64(2)},
		{Tag: "PSAS", Type: blaze.TypeString, Value: "cypress-local"},
		{Tag: "PSID", Type: blaze.TypeString, Value: ""},
		{Tag: "QCAP", Type: blaze.TypeInteger, Value: int64(0)},
		{Tag: "QCNT", Type: blaze.TypeInteger, Value: int64(0)},
		{Tag: "ROST", Type: blaze.TypeList, Value: blaze.List{ElementType: blaze.TypeStruct, Values: roster}},
		{Tag: "SID", Type: blaze.TypeInteger, Value: int64(0)},
		{Tag: "TCAP", Type: blaze.TypeInteger, Value: int64(1)},
		{Tag: "VOIP", Type: blaze.TypeInteger, Value: int64(1)},
		{Tag: "VSTR", Type: blaze.TypeString, Value: "cypress-local"},
	}
}

func buildGameDataResponse(game *gameSession, players []*gamePlayer) []blaze.Field {
	return []blaze.Field{{Tag: "GDAT", Type: blaze.TypeList, Value: blaze.List{
		ElementType: blaze.TypeStruct,
		Values:      []any{buildGameDataRecord(game, players)},
	}}}
}

func requestedGameIDs(fields []blaze.Field) []int64 {
	list, ok := findList(fields, "GLST")
	if !ok {
		return nil
	}
	ids := make([]int64, 0, len(list.Values))
	for _, value := range list.Values {
		if id, ok := value.(int64); ok {
			ids = append(ids, id)
		}
	}
	return ids
}

// handleGameManagerGetGameData answers 4/105 with the requested game records.
// It never substitutes the first room for a requested id.
func (s *Service) handleGameManagerGetGameData(_ context.Context, request blaze.Frame) ([]blaze.Field, uint16) {
	fields, err := blaze.Decode(request.Payload)
	if err != nil {
		return []blaze.Field{{Tag: "GDAT", Type: blaze.TypeList, Value: blaze.List{ElementType: blaze.TypeStruct}}}, 0
	}
	ids := requestedGameIDs(fields)
	if len(ids) == 0 {
		for _, game := range s.games.openGames() {
			ids = append(ids, game.id)
		}
	}
	values := make([]any, 0, len(ids))
	for _, gameID := range ids {
		game, ok := s.games.game(gameID)
		if !ok {
			continue
		}
		values = append(values, buildGameDataRecord(game, s.games.playersOf(gameID)))
	}
	return []blaze.Field{{Tag: "GDAT", Type: blaze.TypeList, Value: blaze.List{
		ElementType: blaze.TypeStruct,
		Values:      values,
	}}}, 0
}
