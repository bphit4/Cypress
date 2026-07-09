package cfb27blaze

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"cypress-servers/internal/blaze"
)

func TestHandleUnsupportedCommand(t *testing.T) {
	svc := NewService(Config{Profile: "LocalPlayer"})
	request := blaze.Frame{
		Header: blaze.Header{
			Component:   0x7777,
			Command:     0x1234,
			MessageType: blaze.MessageTypeRequest,
			MessageID:   99,
		},
	}

	response := svc.HandleFrame(context.Background(), "test-connection", request)

	if response.Header.MessageType != blaze.MessageTypeErrorReply {
		t.Fatalf("expected error reply, got %d", response.Header.MessageType)
	}
	if response.Header.ErrorCode != ErrorCommandNotFound {
		t.Fatalf("expected command-not-found error, got 0x%04x", response.Header.ErrorCode)
	}
	if response.Header.MessageID != request.Header.MessageID {
		t.Fatalf("expected message ID %d, got %d", request.Header.MessageID, response.Header.MessageID)
	}
	if len(svc.Events()) != 1 {
		t.Fatalf("expected one recorded event, got %d", len(svc.Events()))
	}
}

func TestHandleLocalLogin(t *testing.T) {
	svc := NewService(Config{Profile: "LocalPlayer"})
	request := blaze.Frame{
		Header: blaze.Header{
			Component:   ComponentAuthentication,
			Command:     CommandAuthenticationLogin,
			MessageType: blaze.MessageTypeRequest,
			MessageID:   7,
		},
	}

	response := svc.HandleFrame(context.Background(), "test-connection", request)
	if response.Header.MessageType != blaze.MessageTypeReply {
		t.Fatalf("expected reply, got %d with error 0x%04x", response.Header.MessageType, response.Header.ErrorCode)
	}

	fields, err := blaze.Decode(response.Payload)
	if err != nil {
		t.Fatal(err)
	}
	if value, ok := stringField(fields, "NAME"); !ok || value != "LocalPlayer" {
		t.Fatalf("expected LocalPlayer identity, got %q", value)
	}
	if value, ok := integerField(fields, "BUID"); !ok || value != LocalBlazeID {
		t.Fatalf("expected Blaze ID %d, got %d", LocalBlazeID, value)
	}
}

func TestDiagnosticsHealthAndEvents(t *testing.T) {
	svc := NewService(Config{Profile: "LocalPlayer"})
	svc.HandleFrame(context.Background(), "test-connection", blaze.Frame{
		Header: blaze.Header{Component: 0x7777, Command: 1, MessageID: 1},
	})

	req := httptest.NewRequest(http.MethodGet, "/health", nil)
	rec := httptest.NewRecorder()
	svc.DiagnosticsHandler().ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("expected health 200, got %d: %s", rec.Code, rec.Body.String())
	}

	var health map[string]any
	if err := json.Unmarshal(rec.Body.Bytes(), &health); err != nil {
		t.Fatal(err)
	}
	if health["service"] != "cfb27-blaze" {
		t.Fatalf("unexpected service: %#v", health["service"])
	}

	req = httptest.NewRequest(http.MethodGet, "/events", nil)
	rec = httptest.NewRecorder()
	svc.DiagnosticsHandler().ServeHTTP(rec, req)
	if rec.Code != http.StatusOK || !bytes.Contains(rec.Body.Bytes(), []byte(`"component":30583`)) {
		t.Fatalf("unexpected events response: %d %s", rec.Code, rec.Body.String())
	}
}

func stringField(fields []blaze.Field, tag string) (string, bool) {
	for _, field := range fields {
		if field.Tag == tag {
			value, ok := field.Value.(string)
			return value, ok
		}
	}
	return "", false
}

func integerField(fields []blaze.Field, tag string) (int64, bool) {
	for _, field := range fields {
		if field.Tag == tag {
			value, ok := field.Value.(int64)
			return value, ok
		}
	}
	return 0, false
}
