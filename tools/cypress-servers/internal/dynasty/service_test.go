package dynasty

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
)

func TestServiceCreatesSessionAndAction(t *testing.T) {
	root := filepath.Clean(`C:\Users\Shadow\Desktop\CFB27\Dynasty_Files`)
	svc, err := NewService(root, ":memory:")
	if err != nil {
		t.Fatalf("NewService: %v", err)
	}
	defer svc.Close()

	mux := http.NewServeMux()
	svc.RegisterRoutes(mux)

	session := postJSON(t, mux, "/sessions", map[string]any{
		"name":        "Saturday League",
		"stage":       "week_1",
		"currentWeek": 1,
		"maxUsers":    32,
	})
	if session.Code != http.StatusCreated {
		t.Fatalf("create session got %d: %s", session.Code, session.Body.String())
	}
	var created Session
	if err := json.Unmarshal(session.Body.Bytes(), &created); err != nil {
		t.Fatalf("decode session: %v", err)
	}
	if created.ID == 0 || created.Name != "Saturday League" {
		t.Fatalf("bad session: %+v", created)
	}

	user := postJSON(t, mux, "/sessions/1/users", map[string]any{
		"displayName": "Shadow",
		"teamId":      "MICH",
		"isAdmin":     true,
	})
	if user.Code != http.StatusCreated {
		t.Fatalf("create user got %d: %s", user.Code, user.Body.String())
	}

	action := postJSON(t, mux, "/sessions/1/actions", map[string]any{
		"userId": 1,
		"type":   "AdvanceStageRequest",
		"payload": map[string]any{
			"targetStage": "week_2",
		},
	})
	if action.Code != http.StatusCreated {
		t.Fatalf("create action got %d: %s", action.Code, action.Body.String())
	}

	resolved := postJSON(t, mux, "/sessions/1/actions/1/resolve", map[string]any{
		"status": "resolved",
		"response": map[string]any{
			"ok": true,
		},
	})
	if resolved.Code != http.StatusOK {
		t.Fatalf("resolve action got %d: %s", resolved.Code, resolved.Body.String())
	}
	var resolvedAction Action
	if err := json.Unmarshal(resolved.Body.Bytes(), &resolvedAction); err != nil {
		t.Fatalf("decode action: %v", err)
	}
	if resolvedAction.Status != "resolved" || len(resolvedAction.Response) == 0 {
		t.Fatalf("bad resolved action: %+v", resolvedAction)
	}
}

func TestServiceSchemaEndpoints(t *testing.T) {
	root := filepath.Clean(`C:\Users\Shadow\Desktop\CFB27\Dynasty_Files`)
	svc, err := NewService(root, ":memory:")
	if err != nil {
		t.Fatalf("NewService: %v", err)
	}
	defer svc.Close()

	mux := http.NewServeMux()
	svc.RegisterRoutes(mux)
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/schemas/uirequestforms", nil)
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("schema endpoint got %d: %s", rec.Code, rec.Body.String())
	}
	var body struct {
		Count int `json:"count"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode schema response: %v", err)
	}
	if body.Count != 96 {
		t.Fatalf("expected 96 UIRequestForm schemas, got %d", body.Count)
	}
}

func postJSON(t *testing.T, h http.Handler, path string, body any) *httptest.ResponseRecorder {
	t.Helper()
	data, err := json.Marshal(body)
	if err != nil {
		t.Fatal(err)
	}
	req := httptest.NewRequest(http.MethodPost, path, bytes.NewReader(data))
	req.Header.Set("Content-Type", "application/json")
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)
	return rec
}
