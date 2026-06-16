package master

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestHeartbeatAcceptsCFB27(t *testing.T) {
	db, err := sql.Open("sqlite", ":memory:")
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()

	state := newState(db, false, "", nil, nil)
	payload := map[string]any{
		"address":      "203.0.113.10",
		"port":         27901,
		"game":         "CFB27",
		"players":      3,
		"maxPlayers":   32,
		"motd":         "Test Dynasty",
		"dynastyMode":  "Online Dynasty",
		"leagueName":   "Saturday League",
		"currentStage": "Week 1",
		"teamCount":    12,
		"rosterModded": true,
	}
	body, err := json.Marshal(payload)
	if err != nil {
		t.Fatal(err)
	}

	req := httptest.NewRequest(http.MethodPost, "/heartbeat", bytes.NewReader(body))
	req.RemoteAddr = "203.0.113.10:51000"
	rec := httptest.NewRecorder()

	state.handleHeartbeat(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected CFB27 heartbeat to be accepted, got %d: %s", rec.Code, rec.Body.String())
	}
	list := state.getCachedList()
	if !bytes.Contains(list, []byte(`"game":"CFB27"`)) {
		t.Fatalf("expected CFB27 server in browser list: %s", string(list))
	}
	if !bytes.Contains(list, []byte(`"leagueName":"Saturday League"`)) {
		t.Fatalf("expected CFB27 league metadata in browser list: %s", string(list))
	}
}
