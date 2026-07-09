package cfb27blaze

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
)

func TestDynastyClientEnsuresOneSeededSession(t *testing.T) {
	var mu sync.Mutex
	sessions := make([]DynastySession, 0)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		defer mu.Unlock()
		w.Header().Set("Content-Type", "application/json")
		switch {
		case r.Method == http.MethodGet && r.URL.Path == "/sessions":
			_ = json.NewEncoder(w).Encode(map[string]any{"sessions": sessions})
		case r.Method == http.MethodPost && r.URL.Path == "/sessions":
			var request DynastySession
			if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
				http.Error(w, err.Error(), http.StatusBadRequest)
				return
			}
			request.ID = 1
			sessions = append(sessions, request)
			w.WriteHeader(http.StatusCreated)
			_ = json.NewEncoder(w).Encode(request)
		default:
			http.NotFound(w, r)
		}
	}))
	defer server.Close()

	client := NewDynastyClient(server.URL)
	session, err := client.EnsureSeeded(t.Context(), "Local Dynasty")
	if err != nil {
		t.Fatal(err)
	}
	if session.ID != 1 || session.Name != "Local Dynasty" {
		t.Fatalf("unexpected seeded session: %#v", session)
	}

	again, err := client.EnsureSeeded(t.Context(), "Ignored Name")
	if err != nil {
		t.Fatal(err)
	}
	if again.ID != session.ID {
		t.Fatalf("expected existing session, got %#v", again)
	}
	if len(sessions) != 1 {
		t.Fatalf("expected exactly one session, got %d", len(sessions))
	}
}
