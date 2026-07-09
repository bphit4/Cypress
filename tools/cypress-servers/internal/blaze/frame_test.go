package blaze

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func TestFrameRoundTrip(t *testing.T) {
	frame := Frame{
		Header: Header{
			Component:   0x0005,
			Command:     0x0001,
			MessageType: MessageTypeRequest,
			UserIndex:   1,
			MessageID:   0x01020304,
		},
		Payload: []byte{0xaa, 0xbb, 0xcc},
	}

	var wire bytes.Buffer
	if err := WriteFrame(&wire, frame); err != nil {
		t.Fatal(err)
	}

	const expected = "00000003000500010000000101020304aabbcc"
	if got := hex.EncodeToString(wire.Bytes()); got != expected {
		t.Fatalf("unexpected wire frame\nwant: %s\n got: %s", expected, got)
	}

	decoded, err := ReadFrame(&wire)
	if err != nil {
		t.Fatal(err)
	}
	if decoded.Header.Component != frame.Header.Component ||
		decoded.Header.Command != frame.Header.Command ||
		decoded.Header.MessageType != frame.Header.MessageType ||
		decoded.Header.UserIndex != frame.Header.UserIndex ||
		decoded.Header.MessageID != frame.Header.MessageID {
		t.Fatalf("header mismatch: %#v", decoded.Header)
	}
	if !bytes.Equal(decoded.Payload, frame.Payload) {
		t.Fatalf("payload mismatch: %x", decoded.Payload)
	}
}

func TestReadFrameRejectsOversizedPayload(t *testing.T) {
	wire := []byte{
		0x01, 0x00, 0x00, 0x01,
		0x00, 0x05,
		0x00, 0x01,
		0x00, 0x00,
		0x00,
		0x00,
		0x00, 0x00, 0x00, 0x01,
	}

	if _, err := ReadFrame(bytes.NewReader(wire)); err == nil {
		t.Fatal("expected oversized payload to be rejected")
	}
}
