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
	Length      uint32
	Component   uint16
	Command     uint16
	ErrorCode   uint16
	MessageType MessageType
	UserIndex   uint8
	MessageID   uint32
}

type Frame struct {
	Header  Header
	Payload []byte
}

func ReadFrame(r io.Reader) (Frame, error) {
	var raw [HeaderSize]byte
	if _, err := io.ReadFull(r, raw[:]); err != nil {
		return Frame{}, err
	}

	header := Header{
		Length:      binary.BigEndian.Uint32(raw[0:4]),
		Component:   binary.BigEndian.Uint16(raw[4:6]),
		Command:     binary.BigEndian.Uint16(raw[6:8]),
		ErrorCode:   binary.BigEndian.Uint16(raw[8:10]),
		MessageType: MessageType(raw[10]),
		UserIndex:   raw[11],
		MessageID:   binary.BigEndian.Uint32(raw[12:16]),
	}
	if header.Length > MaxPayloadSize {
		return Frame{}, fmt.Errorf("blaze payload length %d exceeds limit %d", header.Length, MaxPayloadSize)
	}

	payload := make([]byte, int(header.Length))
	if _, err := io.ReadFull(r, payload); err != nil {
		return Frame{}, err
	}
	return Frame{Header: header, Payload: payload}, nil
}

func WriteFrame(w io.Writer, frame Frame) error {
	if len(frame.Payload) > MaxPayloadSize {
		return fmt.Errorf("blaze payload length %d exceeds limit %d", len(frame.Payload), MaxPayloadSize)
	}

	frame.Header.Length = uint32(len(frame.Payload))
	var raw [HeaderSize]byte
	binary.BigEndian.PutUint32(raw[0:4], frame.Header.Length)
	binary.BigEndian.PutUint16(raw[4:6], frame.Header.Component)
	binary.BigEndian.PutUint16(raw[6:8], frame.Header.Command)
	binary.BigEndian.PutUint16(raw[8:10], frame.Header.ErrorCode)
	raw[10] = byte(frame.Header.MessageType)
	raw[11] = frame.Header.UserIndex
	binary.BigEndian.PutUint32(raw[12:16], frame.Header.MessageID)

	if err := writeAll(w, raw[:]); err != nil {
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
