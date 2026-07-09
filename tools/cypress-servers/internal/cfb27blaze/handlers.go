package cfb27blaze

import (
	"context"

	"cypress-servers/internal/blaze"
)

const (
	ComponentAuthentication    uint16 = 0x0001
	CommandAuthenticationLogin uint16 = 0x000a
	ErrorCommandNotFound       uint16 = 0x0001
	ErrorSystem                uint16 = 0x0002
	LocalBlazeID               int64  = 1001
	localNucleusAccountID      int64  = 1001
	localPersonaID             int64  = 1001
)

type handler func(context.Context, blaze.Frame) ([]blaze.Field, uint16)

func (s *Service) registerDefaultHandlers() {
	s.handlers[route{ComponentAuthentication, CommandAuthenticationLogin}] = s.handleLocalLogin
}

func (s *Service) handleLocalLogin(_ context.Context, _ blaze.Frame) ([]blaze.Field, uint16) {
	return []blaze.Field{
		{Tag: "BUID", Type: blaze.TypeInteger, Value: LocalBlazeID},
		{Tag: "NAME", Type: blaze.TypeString, Value: s.config.Profile},
		{Tag: "NUID", Type: blaze.TypeInteger, Value: localNucleusAccountID},
		{Tag: "PID", Type: blaze.TypeInteger, Value: localPersonaID},
		{Tag: "SESS", Type: blaze.TypeString, Value: "cypress-local-session"},
	}, 0
}
