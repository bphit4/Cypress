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
			UserIndex:   17,
			MessageID:   0x010203,
			Options:     1,
			Reserved:    0x7f,
		},
		Payload: []byte{0xaa, 0xbb, 0xcc},
	}

	var wire bytes.Buffer
	if err := WriteFrame(&wire, frame); err != nil {
		t.Fatal(err)
	}

	const expected = "0000000300000005000101020311017faabbcc"
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
		decoded.Header.MessageID != frame.Header.MessageID ||
		decoded.Header.Options != frame.Header.Options ||
		decoded.Header.Reserved != frame.Header.Reserved {
		t.Fatalf("header mismatch: %#v", decoded.Header)
	}
	if !bytes.Equal(decoded.Payload, frame.Payload) {
		t.Fatalf("payload mismatch: %x", decoded.Payload)
	}
}

func TestReadCapturedCFB27LoginHeader(t *testing.T) {
	wire, err := hex.DecodeString("00000f4300000001000a000007000000")
	if err != nil {
		t.Fatal(err)
	}
	wire = append(wire, make([]byte, 0x0f43)...)

	frame, err := ReadFrame(bytes.NewReader(wire))
	if err != nil {
		t.Fatal(err)
	}
	if frame.Header.Component != 1 || frame.Header.Command != 10 {
		t.Fatalf("unexpected route: component=%d command=%d", frame.Header.Component, frame.Header.Command)
	}
	if frame.Header.MessageID != 7 || frame.Header.MessageType != MessageTypeRequest || frame.Header.UserIndex != 0 {
		t.Fatalf("unexpected Fire2 metadata: %#v", frame.Header)
	}
}

func TestWriteFrameRejectsOutOfRangeFire2Fields(t *testing.T) {
	tests := []struct {
		name   string
		header Header
	}{
		{name: "message id", header: Header{MessageID: 0x1000000}},
		{name: "message type", header: Header{MessageType: 8}},
		{name: "user index", header: Header{UserIndex: 32}},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if err := WriteFrame(&bytes.Buffer{}, Frame{Header: test.header}); err == nil {
				t.Fatal("expected Fire2 field validation error")
			}
		})
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
