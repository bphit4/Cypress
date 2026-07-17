package cfb27capture

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"sort"
	"time"

	"cypress-servers/internal/blaze"
)

type FrameRecord struct {
	Timestamp    time.Time         `json:"timestamp"`
	Direction    string            `json:"direction"`
	Source       string            `json:"source"`
	Destination  string            `json:"destination"`
	Component    uint16            `json:"component"`
	Command      uint16            `json:"command"`
	ErrorCode    uint16            `json:"errorCode"`
	MessageType  blaze.MessageType `json:"messageType"`
	UserIndex    uint8             `json:"userIndex"`
	MessageID    uint32            `json:"messageId"`
	Options      uint8             `json:"options"`
	MetadataSize uint16            `json:"metadataSize"`
	PayloadSize  uint32            `json:"payloadSize"`
}

type Report struct {
	Packets int            `json:"packets"`
	Frames  []FrameRecord  `json:"frames"`
	Skipped map[string]int `json:"skipped"`
}

type RouteCount struct {
	Direction   string            `json:"direction"`
	Component   uint16            `json:"component"`
	Command     uint16            `json:"command"`
	MessageType blaze.MessageType `json:"messageType"`
	ErrorCode   uint16            `json:"errorCode"`
	Count       int               `json:"count"`
}

func Parse(r io.Reader) (Report, error) {
	report := Report{Skipped: make(map[string]int)}
	header := make([]byte, 24)
	if _, err := io.ReadFull(r, header); err != nil {
		return report, fmt.Errorf("read PCAP header: %w", err)
	}
	var order binary.ByteOrder
	switch string(header[:4]) {
	case "\xd4\xc3\xb2\xa1":
		order = binary.LittleEndian
	case "\xa1\xb2\xc3\xd4":
		order = binary.BigEndian
	default:
		return report, fmt.Errorf("unsupported PCAP magic")
	}
	if linkType := order.Uint32(header[20:24]); linkType != 1 {
		return report, fmt.Errorf("unsupported PCAP link type %d", linkType)
	}

	for recordNumber := 1; ; recordNumber++ {
		recordHeader := make([]byte, 16)
		if _, err := io.ReadFull(r, recordHeader); err != nil {
			if err == io.EOF {
				break
			}
			return report, fmt.Errorf("read PCAP record %d header: %w", recordNumber, err)
		}
		seconds := order.Uint32(recordHeader[0:4])
		micros := order.Uint32(recordHeader[4:8])
		captured := order.Uint32(recordHeader[8:12])
		if captured > 64<<20 {
			return report, fmt.Errorf("PCAP record %d length %d exceeds limit", recordNumber, captured)
		}
		packet := make([]byte, int(captured))
		if _, err := io.ReadFull(r, packet); err != nil {
			return report, fmt.Errorf("read PCAP record %d payload: %w", recordNumber, err)
		}
		report.Packets++
		parsePacket(&report, packet, time.Unix(int64(seconds), int64(micros)*1000).UTC())
	}
	return report, nil
}

func parsePacket(report *Report, packet []byte, timestamp time.Time) {
	if len(packet) < 14 || !bytes.Equal(packet[12:14], []byte{0x08, 0x00}) {
		report.Skipped["non_ipv4"]++
		return
	}
	ip := packet[14:]
	if len(ip) < 20 || ip[0]>>4 != 4 {
		report.Skipped["invalid_ipv4"]++
		return
	}
	ipHeaderLength := int(ip[0]&0x0f) * 4
	if ipHeaderLength < 20 || len(ip) < ipHeaderLength || ip[9] != 6 {
		report.Skipped["non_tcp"]++
		return
	}
	tcp := ip[ipHeaderLength:]
	if len(tcp) < 20 {
		report.Skipped["invalid_tcp"]++
		return
	}
	tcpHeaderLength := int(tcp[12]>>4) * 4
	if tcpHeaderLength < 20 || len(tcp) < tcpHeaderLength {
		report.Skipped["invalid_tcp"]++
		return
	}
	payload := tcp[tcpHeaderLength:]
	if len(payload) == 0 {
		report.Skipped["empty_tcp_payload"]++
		return
	}
	sourceIP, destinationIP := net.IP(ip[12:16]).String(), net.IP(ip[16:20]).String()
	sourcePort, destinationPort := binary.BigEndian.Uint16(tcp[0:2]), binary.BigEndian.Uint16(tcp[2:4])
	direction := "unknown"
	if sourceIP == "127.0.0.1" {
		direction = "client_to_server"
	} else if destinationIP == "127.0.0.1" {
		direction = "server_to_client"
	}

	for len(payload) > 0 {
		if len(payload) < blaze.HeaderSize {
			report.Skipped["partial_blaze_frame"]++
			return
		}
		payloadSize := binary.BigEndian.Uint32(payload[0:4])
		metadataSize := binary.BigEndian.Uint16(payload[4:6])
		total := uint64(blaze.HeaderSize) + uint64(metadataSize) + uint64(payloadSize)
		if total > uint64(len(payload)) || total > blaze.HeaderSize+blaze.MaxPayloadSize+65535 {
			report.Skipped["partial_blaze_frame"]++
			return
		}
		frame, err := blaze.ReadFrame(bytes.NewReader(payload[:int(total)]))
		if err != nil {
			report.Skipped["invalid_blaze_frame"]++
			return
		}
		report.Frames = append(report.Frames, FrameRecord{
			Timestamp: timestamp, Direction: direction,
			Source: fmt.Sprintf("%s:%d", sourceIP, sourcePort), Destination: fmt.Sprintf("%s:%d", destinationIP, destinationPort),
			Component: frame.Header.Component, Command: frame.Header.Command, ErrorCode: frame.Header.ErrorCode,
			MessageType: frame.Header.MessageType, UserIndex: frame.Header.UserIndex, MessageID: frame.Header.MessageID,
			Options: frame.Header.Options, MetadataSize: frame.Header.MetadataSize, PayloadSize: frame.Header.Length,
		})
		payload = payload[int(total):]
	}
}

func (r Report) Routes() []RouteCount {
	type key struct {
		direction          string
		component, command uint16
		messageType        blaze.MessageType
		errorCode          uint16
	}
	counts := make(map[key]int)
	for _, frame := range r.Frames {
		counts[key{frame.Direction, frame.Component, frame.Command, frame.MessageType, frame.ErrorCode}]++
	}
	routes := make([]RouteCount, 0, len(counts))
	for k, count := range counts {
		routes = append(routes, RouteCount{k.direction, k.component, k.command, k.messageType, k.errorCode, count})
	}
	sort.Slice(routes, func(i, j int) bool {
		a, b := routes[i], routes[j]
		if a.Direction != b.Direction {
			return a.Direction < b.Direction
		}
		if a.Component != b.Component {
			return a.Component < b.Component
		}
		if a.Command != b.Command {
			return a.Command < b.Command
		}
		if a.MessageType != b.MessageType {
			return a.MessageType < b.MessageType
		}
		return a.ErrorCode < b.ErrorCode
	})
	return routes
}
