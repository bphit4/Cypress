package blaze

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
)

const (
	HeaderSize     = 16
	MaxPayloadSize = 16 << 20
)

type MessageType uint8

const (
	MessageTypeRequest      MessageType = 0
	MessageTypeReply        MessageType = 1
	MessageTypeNotification MessageType = 2
	MessageTypeErrorReply   MessageType = 3
	MessageTypePing         MessageType = 4
	MessageTypePingReply    MessageType = 5
)

type Header struct {
	Length       uint32
	MetadataSize uint16
	Component    uint16
	Command      uint16
	ErrorCode    uint16
	MessageType  MessageType
	UserIndex    uint8
	MessageID    uint32
	Options      uint8
	Reserved     uint8
}

type Frame struct {
	Header   Header
	Metadata []byte
	Payload  []byte
}

func ReadFrame(r io.Reader) (Frame, error) {
	var raw [HeaderSize]byte
	if _, err := io.ReadFull(r, raw[:]); err != nil {
		return Frame{}, err
	}

	header := Header{
		Length:       binary.BigEndian.Uint32(raw[0:4]),
		MetadataSize: binary.BigEndian.Uint16(raw[4:6]),
		Component:    binary.BigEndian.Uint16(raw[6:8]),
		Command:      binary.BigEndian.Uint16(raw[8:10]),
		MessageID:    uint32(raw[10])<<16 | uint32(raw[11])<<8 | uint32(raw[12]),
		MessageType:  MessageType(raw[13] >> 5),
		UserIndex:    raw[13] & 0x1f,
		Options:      raw[14],
		Reserved:     raw[15],
	}
	if header.Length > MaxPayloadSize {
		return Frame{}, fmt.Errorf("blaze payload length %d exceeds limit %d", header.Length, MaxPayloadSize)
	}

	metadata := make([]byte, int(header.MetadataSize))
	if _, err := io.ReadFull(r, metadata); err != nil {
		return Frame{}, err
	}
	if len(metadata) > 0 {
		if fields, err := Decode(metadata); err == nil {
			for _, field := range fields {
				if field.Tag == "ERRC" {
					if value, ok := field.Value.(int64); ok {
						header.ErrorCode = uint16(value)
					}
				}
			}
		}
	}

	payload := make([]byte, int(header.Length))
	if _, err := io.ReadFull(r, payload); err != nil {
		return Frame{}, err
	}
	return Frame{Header: header, Metadata: metadata, Payload: payload}, nil
}

func WriteFrame(w io.Writer, frame Frame) error {
	if len(frame.Payload) > MaxPayloadSize {
		return fmt.Errorf("blaze payload length %d exceeds limit %d", len(frame.Payload), MaxPayloadSize)
	}
	if frame.Header.MessageID > 0x00ffffff {
		return fmt.Errorf("Fire2 message ID 0x%x exceeds 24-bit limit", frame.Header.MessageID)
	}
	if frame.Header.MessageType > 7 {
		return fmt.Errorf("Fire2 message type %d exceeds 3-bit limit", frame.Header.MessageType)
	}
	if frame.Header.UserIndex > 31 {
		return fmt.Errorf("Fire2 user index %d exceeds 5-bit limit", frame.Header.UserIndex)
	}

	metadata := frame.Metadata
	if len(metadata) == 0 && frame.Header.ErrorCode != 0 {
		var err error
		metadata, err = Encode([]Field{{Tag: "ERRC", Type: TypeInteger, Value: int64(frame.Header.ErrorCode)}})
		if err != nil {
			return fmt.Errorf("encode Fire2 error metadata: %w", err)
		}
	}
	if len(metadata) > int(^uint16(0)) {
		return fmt.Errorf("Fire2 metadata length %d exceeds limit %d", len(metadata), ^uint16(0))
	}

	frame.Header.Length = uint32(len(frame.Payload))
	frame.Header.MetadataSize = uint16(len(metadata))
	var raw [HeaderSize]byte
	binary.BigEndian.PutUint32(raw[0:4], frame.Header.Length)
	binary.BigEndian.PutUint16(raw[4:6], frame.Header.MetadataSize)
	binary.BigEndian.PutUint16(raw[6:8], frame.Header.Component)
	binary.BigEndian.PutUint16(raw[8:10], frame.Header.Command)
	raw[10] = byte(frame.Header.MessageID >> 16)
	raw[11] = byte(frame.Header.MessageID >> 8)
	raw[12] = byte(frame.Header.MessageID)
	raw[13] = byte(frame.Header.MessageType)<<5 | frame.Header.UserIndex
	raw[14] = frame.Header.Options
	raw[15] = frame.Header.Reserved

	if err := writeAll(w, raw[:]); err != nil {
		return err
	}
	if err := writeAll(w, metadata); err != nil {
		return err
	}
	return writeAll(w, frame.Payload)
}

func writeAll(w io.Writer, data []byte) error {
	for len(data) > 0 {
		n, err := w.Write(data)
		if err != nil {
			return err
		}
		if n == 0 {
			return errors.New("short write")
		}
		data = data[n:]
	}
	return nil
}
