package blaze

import (
	"bytes"
	"reflect"
	"testing"
)

func TestTDFTagEncoding(t *testing.T) {
	got, err := EncodeTag("VALU")
	if err != nil {
		t.Fatal(err)
	}
	if want := [3]byte{0xda, 0x1b, 0x35}; got != want {
		t.Fatalf("unexpected tag bytes: want %x, got %x", want, got)
	}
	if decoded := DecodeTag(got); decoded != "VALU" {
		t.Fatalf("unexpected decoded tag: %q", decoded)
	}
}

func TestTDFRoundTrip(t *testing.T) {
	fields := []Field{
		{Tag: "VALU", Type: TypeInteger, Value: int64(42)},
		{Tag: "NAME", Type: TypeString, Value: "Local Dynasty"},
		{Tag: "DATA", Type: TypeBlob, Value: []byte{0xde, 0xad, 0xbe, 0xef}},
		{
			Tag:  "USER",
			Type: TypeStruct,
			Value: []Field{
				{Tag: "ID", Type: TypeInteger, Value: int64(1001)},
				{Tag: "DISP", Type: TypeString, Value: "LocalPlayer"},
			},
		},
		{
			Tag:  "LIST",
			Type: TypeList,
			Value: List{
				ElementType: TypeString,
				Values:      []any{"one", "two"},
			},
		},
	}

	wire, err := Encode(fields)
	if err != nil {
		t.Fatal(err)
	}
	decoded, err := Decode(wire)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(decoded, fields) {
		t.Fatalf("round trip mismatch\nwant: %#v\n got: %#v", fields, decoded)
	}
}

func TestTDFDecodeRejectsTruncatedString(t *testing.T) {
	tag, err := EncodeTag("NAME")
	if err != nil {
		t.Fatal(err)
	}
	wire := append(tag[:], byte(TypeString), 0x05, 'a')

	if _, err := Decode(wire); err == nil {
		t.Fatal("expected truncated string error")
	}
}

func TestTDFDecodeRejectsExcessiveListCount(t *testing.T) {
	tag, err := EncodeTag("LIST")
	if err != nil {
		t.Fatal(err)
	}
	wire := bytes.NewBuffer(nil)
	wire.Write(tag[:])
	wire.WriteByte(byte(TypeList))
	wire.WriteByte(byte(TypeInteger))
	wire.Write([]byte{0xff, 0xff, 0xff, 0xff, 0x7f})

	if _, err := Decode(wire.Bytes()); err == nil {
		t.Fatal("expected excessive list count error")
	}
}
