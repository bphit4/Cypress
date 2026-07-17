package cfb27capture

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestParseExtractsFire2FrameAndRoutes(t *testing.T) {
	frame := []byte{0, 0, 0, 0, 0, 0, 0, 1, 0, 10, 0, 0, 7, 0, 0, 0}
	report, err := Parse(bytes.NewReader(testPCAP(binary.LittleEndian, testTCPPacket(frame))))
	if err != nil {
		t.Fatal(err)
	}
	if len(report.Frames) != 1 {
		t.Fatalf("expected one frame, got %d", len(report.Frames))
	}
	got := report.Frames[0]
	if got.Direction != "client_to_server" || got.Component != 1 || got.Command != 10 || got.MessageID != 7 {
		t.Fatalf("unexpected frame: %#v", got)
	}
	routes := report.Routes()
	if len(routes) != 1 || routes[0].Count != 1 || routes[0].Component != 1 || routes[0].Command != 10 {
		t.Fatalf("unexpected routes: %#v", routes)
	}
}

func TestParseSupportsBigEndianPCAP(t *testing.T) {
	report, err := Parse(bytes.NewReader(testPCAP(binary.BigEndian, testTCPPacket(make([]byte, 16)))))
	if err != nil || len(report.Frames) != 1 {
		t.Fatalf("frames=%d err=%v", len(report.Frames), err)
	}
}

func TestParseCountsPartialFrameWithoutPayloadDisclosure(t *testing.T) {
	report, err := Parse(bytes.NewReader(testPCAP(binary.LittleEndian, testTCPPacket([]byte{0, 0, 0, 20}))))
	if err != nil {
		t.Fatal(err)
	}
	if report.Skipped["partial_blaze_frame"] != 1 || len(report.Frames) != 0 {
		t.Fatalf("unexpected report: %#v", report)
	}
}

func TestParseRejectsUnsupportedLinkType(t *testing.T) {
	data := testPCAP(binary.LittleEndian)
	binary.LittleEndian.PutUint32(data[20:24], 101)
	if _, err := Parse(bytes.NewReader(data)); err == nil {
		t.Fatal("expected unsupported link type error")
	}
}

func testPCAP(order binary.ByteOrder, packets ...[]byte) []byte {
	var out bytes.Buffer
	if order == binary.LittleEndian {
		out.Write([]byte{0xd4, 0xc3, 0xb2, 0xa1})
	} else {
		out.Write([]byte{0xa1, 0xb2, 0xc3, 0xd4})
	}
	write := func(v any) { _ = binary.Write(&out, order, v) }
	write(uint16(2))
	write(uint16(4))
	write(uint32(0))
	write(uint32(0))
	write(uint32(65535))
	write(uint32(1))
	for i, packet := range packets {
		write(uint32(100 + i))
		write(uint32(200))
		write(uint32(len(packet)))
		write(uint32(len(packet)))
		out.Write(packet)
	}
	return out.Bytes()
}

func testTCPPacket(payload []byte) []byte {
	p := make([]byte, 14+20+20+len(payload))
	p[12], p[13] = 0x08, 0x00
	p[14], p[23] = 0x45, 6
	p[26], p[27], p[28], p[29] = 127, 0, 0, 1
	p[30], p[31], p[32], p[33] = 192, 168, 1, 1
	binary.BigEndian.PutUint16(p[34:36], 40000)
	binary.BigEndian.PutUint16(p[36:38], 443)
	p[46] = 5 << 4
	copy(p[54:], payload)
	return p
}
