package cfb27blaze

import (
	"bufio"
	"bytes"
	"context"
	"crypto/tls"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"cypress-servers/internal/blaze"
)

type Config struct {
	Bind            string
	Port            int
	DiagnosticsBind string
	DiagnosticsPort int
	LogFile         string
	RunID           string
	Profile         string
	DynastyURL      string
	TLSMode         string
}

type Service struct {
	config     Config
	startedAt  time.Time
	handlers   map[route]handler
	dynasty    *DynastyClient
	tlsConfig  *tls.Config
	tlsError   error
	eventsMu   sync.RWMutex
	events     []Event
	connection atomic.Uint64
}

type route struct {
	component uint16
	command   uint16
}

type Event struct {
	Time          time.Time     `json:"time"`
	RunID         string        `json:"runId,omitempty"`
	ConnectionID  string        `json:"connectionId"`
	Status        string        `json:"status,omitempty"`
	RemoteAddr    string        `json:"remoteAddr,omitempty"`
	HTTPMethod    string        `json:"httpMethod,omitempty"`
	HTTPPath      string        `json:"httpPath,omitempty"`
	HTTPHost      string        `json:"httpHost,omitempty"`
	HTTPStatus    int           `json:"httpStatus,omitempty"`
	BodyBytes     int           `json:"bodyBytes,omitempty"`
	TLSServerName string        `json:"tlsServerName,omitempty"`
	TLSVersions   []string      `json:"tlsVersions,omitempty"`
	TLSCiphers    []string      `json:"tlsCiphers,omitempty"`
	TLSCurves     []string      `json:"tlsCurves,omitempty"`
	TLSSignatures []string      `json:"tlsSignatures,omitempty"`
	TLSNextProtos []string      `json:"tlsNextProtos,omitempty"`
	MessageID     uint32        `json:"messageId"`
	Component     uint16        `json:"component"`
	Command       uint16        `json:"command"`
	MessageType   uint8         `json:"messageType"`
	ErrorCode     uint16        `json:"errorCode"`
	PayloadHex    string        `json:"payloadHex,omitempty"`
	Decoded       []blaze.Field `json:"decoded,omitempty"`
	DecodeError   string        `json:"decodeError,omitempty"`
	Transport     string        `json:"transport,omitempty"`
	DurationMS    float64       `json:"durationMs"`
}

func NewService(cfg Config) *Service {
	if cfg.Bind == "" {
		cfg.Bind = "127.0.0.1"
	}
	if cfg.Port == 0 {
		cfg.Port = 27920
	}
	if cfg.DiagnosticsBind == "" {
		cfg.DiagnosticsBind = "127.0.0.1"
	}
	if cfg.DiagnosticsPort == 0 {
		cfg.DiagnosticsPort = 27921
	}
	if cfg.Profile == "" {
		cfg.Profile = "LocalPlayer"
	}
	if cfg.DynastyURL == "" {
		cfg.DynastyURL = "http://127.0.0.1:27910"
	}
	if cfg.TLSMode == "" {
		cfg.TLSMode = "tls13"
	}
	svc := &Service{
		config:    cfg,
		startedAt: time.Now().UTC(),
		handlers:  make(map[route]handler),
		dynasty:   NewDynastyClient(cfg.DynastyURL),
		events:    make([]Event, 0, 128),
	}
	svc.tlsConfig, svc.tlsError = newTLSConfig(cfg.TLSMode)
	svc.registerDefaultHandlers()
	return svc
}

func (s *Service) Run(ctx context.Context) error {
	listeners, err := s.listenBlaze()
	if err != nil {
		return err
	}
	defer closeListeners(listeners)

	diagnostics := &http.Server{
		Addr:              net.JoinHostPort(s.config.DiagnosticsBind, strconv.Itoa(s.config.DiagnosticsPort)),
		Handler:           s.DiagnosticsHandler(),
		ReadHeaderTimeout: 5 * time.Second,
	}
	diagnosticsErrors := make(chan error, 1)
	go func() {
		err := diagnostics.ListenAndServe()
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			diagnosticsErrors <- err
		}
	}()
	defer diagnostics.Shutdown(context.Background())

	go func() {
		<-ctx.Done()
		closeListeners(listeners)
		_ = diagnostics.Shutdown(context.Background())
	}()

	if _, err := s.dynasty.EnsureSeeded(ctx, "Local Dynasty"); err != nil {
		return fmt.Errorf("seed local dynasty: %w", err)
	}

	acceptErrors := make(chan error, len(listeners))
	for _, listener := range listeners {
		go func(listener net.Listener) {
			for {
				connection, err := listener.Accept()
				if err != nil {
					if ctx.Err() != nil {
						return
					}
					acceptErrors <- err
					return
				}
				id := fmt.Sprintf("c-%06d", s.connection.Add(1))
				go s.serveConnection(ctx, id, connection)
			}
		}(listener)
	}

	for {
		select {
		case <-ctx.Done():
			return nil
		case diagnosticsErr := <-diagnosticsErrors:
			return diagnosticsErr
		case err := <-acceptErrors:
			if ctx.Err() != nil {
				return nil
			}
			return err
		}
	}
}

func (s *Service) listenBlaze() ([]net.Listener, error) {
	address := net.JoinHostPort(s.config.Bind, strconv.Itoa(s.config.Port))
	primary, err := net.Listen("tcp", address)
	if err != nil {
		return nil, err
	}
	listeners := []net.Listener{primary}
	if s.config.Bind == "127.0.0.1" || s.config.Bind == "localhost" {
		loopback6, err := net.Listen("tcp6", net.JoinHostPort("::1", strconv.Itoa(s.config.Port)))
		if err == nil {
			listeners = append(listeners, loopback6)
		}
	}
	return listeners, nil
}

func closeListeners(listeners []net.Listener) {
	for _, listener := range listeners {
		_ = listener.Close()
	}
}

func (s *Service) serveConnection(ctx context.Context, connectionID string, connection net.Conn) {
	defer connection.Close()
	remote := ""
	if connection.RemoteAddr() != nil {
		remote = connection.RemoteAddr().String()
	}
	s.record(Event{
		Time:         time.Now().UTC(),
		RunID:        s.config.RunID,
		ConnectionID: connectionID,
		Status:       "accepted",
		RemoteAddr:   remote,
		Transport:    "tcp",
	})
	reader := bufio.NewReader(connection)
	_ = connection.SetReadDeadline(time.Now().Add(15 * time.Second))
	first, err := reader.Peek(1)
	_ = connection.SetReadDeadline(time.Time{})
	if err != nil {
		s.record(Event{
			Time:         time.Now().UTC(),
			RunID:        s.config.RunID,
			ConnectionID: connectionID,
			Status:       "peek-failed",
			RemoteAddr:   remote,
			Transport:    "tcp",
			DecodeError:  err.Error(),
		})
		return
	}
	s.recordFirstBytes(connectionID, remote, "tcp", reader, connection)

	transport := "plain"
	var frameReader io.Reader = reader
	var frameWriter io.Writer = connection
	if s.serveHTTPIfPresent(ctx, connectionID, remote, transport, reader, connection) {
		return
	}
	if first[0] == 0x16 {
		transport = "tls"
		if s.tlsError != nil {
			s.record(Event{
				Time:         time.Now().UTC(),
				RunID:        s.config.RunID,
				ConnectionID: connectionID,
				Transport:    transport,
				DecodeError:  "TLS configuration: " + s.tlsError.Error(),
			})
			return
		}
		tlsConfig := s.tlsConfig.Clone()
		tlsConfig.GetConfigForClient = func(hello *tls.ClientHelloInfo) (*tls.Config, error) {
			s.record(Event{
				Time:          time.Now().UTC(),
				RunID:         s.config.RunID,
				ConnectionID:  connectionID,
				Status:        "tls-client-hello",
				RemoteAddr:    remote,
				Transport:     transport,
				TLSServerName: hello.ServerName,
				TLSVersions:   formatTLSUint16s(hello.SupportedVersions),
				TLSCiphers:    formatTLSUint16s(hello.CipherSuites),
				TLSCurves:     formatTLSCurveIDs(hello.SupportedCurves),
				TLSSignatures: formatTLSSignatureSchemes(hello.SignatureSchemes),
				TLSNextProtos: hello.SupportedProtos,
			})
			return nil, nil
		}
		tap := &tlsRecordTap{}
		serverWriteLogged := atomic.Bool{}
		secure := tls.Server(&bufferedConnection{
			Conn:   connection,
			reader: reader,
			tap:    tap,
			onWrite: func(payload []byte) {
				if !serverWriteLogged.CompareAndSwap(false, true) {
					return
				}
				preview := payload
				if len(preview) > 32 {
					preview = preview[:32]
				}
				s.record(Event{
					Time:         time.Now().UTC(),
					RunID:        s.config.RunID,
					ConnectionID: connectionID,
					Status:       "tls-server-first-write",
					RemoteAddr:   remote,
					PayloadHex:   hex.EncodeToString(preview),
					Transport:    transport,
					BodyBytes:    len(payload),
				})
			},
		}, tlsConfig)
		_ = secure.SetDeadline(time.Now().Add(60 * time.Second))
		if err := secure.HandshakeContext(ctx); err != nil {
			decodeError := "TLS handshake: " + err.Error()
			// The game's ProtoSSL client sends a plaintext TLS alert before it drops the
			// connection. Capturing its description tells us why it aborts: 70/71 =
			// protocol_version (it refuses our TLS 1.2 downgrade), 40 = handshake_failure
			// (no acceptable cipher/curve), 42/46/48 = bad/unsupported/unknown-CA
			// certificate (cert pinning — the BearSSL end_chain path). Without this the
			// error is only a generic "forcibly closed by the remote host".
			if tap.haveAlert {
				decodeError += fmt.Sprintf(" (client TLS alert level=%d description=%d %s)",
					tap.alertLevel, tap.alertDesc, tlsAlertName(tap.alertDesc))
			}
			s.record(Event{
				Time:         time.Now().UTC(),
				RunID:        s.config.RunID,
				ConnectionID: connectionID,
				Status:       "tls-handshake-failed",
				RemoteAddr:   remote,
				Transport:    transport,
				DecodeError:  decodeError,
			})
			return
		}
		_ = secure.SetDeadline(time.Time{})
		s.record(Event{
			Time:         time.Now().UTC(),
			RunID:        s.config.RunID,
			ConnectionID: connectionID,
			Status:       "tls-handshake-ok",
			RemoteAddr:   remote,
			Transport:    transport,
		})
		secureReader := bufio.NewReader(secure)
		// Log the first decrypted bytes so the exact Blaze redirector request framing
		// can be captured from the real game (diagnostic for wire-format matching).
		_ = secure.SetReadDeadline(time.Now().Add(3 * time.Second))
		if decrypted, _ := secureReader.Peek(64); len(decrypted) > 0 {
			s.record(Event{
				Time:         time.Now().UTC(),
				RunID:        s.config.RunID,
				ConnectionID: connectionID,
				Status:       "tls-first-bytes",
				RemoteAddr:   remote,
				PayloadHex:   hex.EncodeToString(decrypted),
				Transport:    transport,
			})
		}
		_ = secure.SetReadDeadline(time.Time{})
		if s.serveHTTPIfPresent(ctx, connectionID, remote, transport, secureReader, secure) {
			return
		}
		frameReader = secureReader
		frameWriter = secure
	}

	for {
		if deadline, ok := ctx.Deadline(); ok {
			_ = connection.SetDeadline(deadline)
		}
		request, err := blaze.ReadFrame(frameReader)
		if err != nil {
			if !errors.Is(err, io.EOF) && !errors.Is(err, net.ErrClosed) {
				s.record(Event{
					Time:         time.Now().UTC(),
					RunID:        s.config.RunID,
					ConnectionID: connectionID,
					Transport:    transport,
					DecodeError:  err.Error(),
				})
			}
			return
		}
		response := s.HandleFrame(ctx, connectionID, request)
		if err := blaze.WriteFrame(frameWriter, response); err != nil {
			s.record(Event{
				Time:         time.Now().UTC(),
				RunID:        s.config.RunID,
				ConnectionID: connectionID,
				Transport:    transport,
				MessageID:    request.Header.MessageID,
				Component:    request.Header.Component,
				Command:      request.Header.Command,
				DecodeError:  err.Error(),
			})
			return
		}
	}
}

func (s *Service) recordFirstBytes(connectionID string, remote string, transport string, reader *bufio.Reader, connection net.Conn) {
	_ = connection.SetReadDeadline(time.Now().Add(250 * time.Millisecond))
	preview, err := reader.Peek(16)
	_ = connection.SetReadDeadline(time.Time{})
	if len(preview) == 0 {
		return
	}
	event := Event{
		Time:         time.Now().UTC(),
		RunID:        s.config.RunID,
		ConnectionID: connectionID,
		Status:       "first-bytes",
		RemoteAddr:   remote,
		PayloadHex:   hex.EncodeToString(preview),
		Transport:    transport,
	}
	if err != nil && !errors.Is(err, bufio.ErrBufferFull) {
		event.DecodeError = err.Error()
	}
	s.record(event)
}

type bufferedConnection struct {
	net.Conn
	reader  *bufio.Reader
	tap     *tlsRecordTap
	onWrite func([]byte)
}

func (c *bufferedConnection) Read(buffer []byte) (int, error) {
	n, err := c.reader.Read(buffer)
	if n > 0 && c.tap != nil {
		c.tap.consume(buffer[:n])
	}
	return n, err
}

func (c *bufferedConnection) Write(buffer []byte) (int, error) {
	if len(buffer) > 0 && c.onWrite != nil {
		c.onWrite(buffer)
	}
	return c.Conn.Write(buffer)
}

// tlsRecordTap walks the client->server byte stream, parsing TLS record headers to
// capture the first plaintext alert record. Handshake-phase alerts (before the client's
// ChangeCipherSpec) are unencrypted, so this reveals exactly why the game's ProtoSSL
// client aborts our local handshake. It is diagnostic only and never alters the bytes.
type tlsRecordTap struct {
	buf        []byte
	haveAlert  bool
	alertLevel byte
	alertDesc  byte
}

func (t *tlsRecordTap) consume(p []byte) {
	if t.haveAlert {
		return
	}
	t.buf = append(t.buf, p...)
	for len(t.buf) >= 5 {
		recordType := t.buf[0]
		recordLen := int(t.buf[3])<<8 | int(t.buf[4])
		if recordLen < 0 || recordLen > 1<<14 {
			// Not a sane TLS record header (likely encrypted data we can't frame); stop.
			t.buf = nil
			return
		}
		if len(t.buf) < 5+recordLen {
			break
		}
		payload := t.buf[5 : 5+recordLen]
		if recordType == 0x15 && len(payload) >= 2 {
			t.alertLevel = payload[0]
			t.alertDesc = payload[1]
			t.haveAlert = true
			return
		}
		t.buf = t.buf[5+recordLen:]
	}
	if len(t.buf) > 1<<16 {
		t.buf = t.buf[len(t.buf)-(1<<16):]
	}
}

func tlsAlertName(description byte) string {
	switch description {
	case 40:
		return "handshake_failure"
	case 42:
		return "bad_certificate"
	case 43:
		return "unsupported_certificate"
	case 44:
		return "certificate_revoked"
	case 45:
		return "certificate_expired"
	case 46:
		return "certificate_unknown"
	case 47:
		return "illegal_parameter"
	case 48:
		return "unknown_ca"
	case 50:
		return "decode_error"
	case 51:
		return "decrypt_error"
	case 70:
		return "protocol_version"
	case 71:
		return "insufficient_security"
	case 80:
		return "internal_error"
	case 90:
		return "user_canceled"
	case 109:
		return "missing_extension"
	case 112:
		return "unrecognized_name"
	default:
		return "unknown"
	}
}

func formatTLSUint16s(values []uint16) []string {
	if len(values) == 0 {
		return nil
	}
	formatted := make([]string, 0, len(values))
	for _, value := range values {
		formatted = append(formatted, fmt.Sprintf("0x%04x", value))
	}
	return formatted
}

func formatTLSCurveIDs(values []tls.CurveID) []string {
	if len(values) == 0 {
		return nil
	}
	formatted := make([]string, 0, len(values))
	for _, value := range values {
		formatted = append(formatted, fmt.Sprintf("0x%04x", uint16(value)))
	}
	return formatted
}

func formatTLSSignatureSchemes(values []tls.SignatureScheme) []string {
	if len(values) == 0 {
		return nil
	}
	formatted := make([]string, 0, len(values))
	for _, value := range values {
		formatted = append(formatted, fmt.Sprintf("0x%04x", uint16(value)))
	}
	return formatted
}

func (s *Service) serveHTTPIfPresent(
	ctx context.Context,
	connectionID string,
	remote string,
	transport string,
	reader *bufio.Reader,
	connection net.Conn,
) bool {
	_ = connection.SetReadDeadline(time.Now().Add(2 * time.Second))
	prefix, err := reader.Peek(4)
	_ = connection.SetReadDeadline(time.Time{})
	if err != nil || !looksLikeHTTPPrefix(prefix) {
		return false
	}
	if bytes.Equal(prefix, []byte("PRI ")) {
		s.record(Event{
			Time:         time.Now().UTC(),
			RunID:        s.config.RunID,
			ConnectionID: connectionID,
			Status:       "http2-preface",
			RemoteAddr:   remote,
			Transport:    transport,
		})
		return true
	}
	s.serveHTTPConnection(ctx, connectionID, remote, transport, reader, connection)
	return true
}

func looksLikeHTTPPrefix(prefix []byte) bool {
	if len(prefix) < 4 {
		return false
	}
	switch string(prefix) {
	case "GET ", "POST", "HEAD", "PUT ", "DELE", "OPTI", "PATC", "CONN", "PRI ":
		return true
	default:
		return false
	}
}

func (s *Service) serveHTTPConnection(
	_ context.Context,
	connectionID string,
	remote string,
	transport string,
	reader *bufio.Reader,
	connection net.Conn,
) {
	request, err := http.ReadRequest(reader)
	if err != nil {
		s.record(Event{
			Time:         time.Now().UTC(),
			RunID:        s.config.RunID,
			ConnectionID: connectionID,
			Status:       "http-read-failed",
			RemoteAddr:   remote,
			Transport:    transport,
			DecodeError:  err.Error(),
		})
		return
	}
	defer request.Body.Close()

	body, readErr := io.ReadAll(io.LimitReader(request.Body, 1<<20+1))
	payload := body
	if len(payload) > 512 {
		payload = payload[:512]
	}

	status := http.StatusOK
	responseBody := []byte("{}")
	s.record(Event{
		Time:         time.Now().UTC(),
		RunID:        s.config.RunID,
		ConnectionID: connectionID,
		Status:       "http-request",
		RemoteAddr:   remote,
		HTTPMethod:   request.Method,
		HTTPPath:     request.URL.RequestURI(),
		HTTPHost:     request.Host,
		HTTPStatus:   status,
		BodyBytes:    len(body),
		PayloadHex:   hex.EncodeToString(payload),
		Transport:    transport,
	})
	if readErr != nil {
		s.record(Event{
			Time:         time.Now().UTC(),
			RunID:        s.config.RunID,
			ConnectionID: connectionID,
			Status:       "http-body-read-failed",
			RemoteAddr:   remote,
			Transport:    transport,
			DecodeError:  readErr.Error(),
		})
		return
	}

	_, err = fmt.Fprintf(
		connection,
		"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
		status,
		http.StatusText(status),
		len(responseBody),
		responseBody)
	if err != nil {
		s.record(Event{
			Time:         time.Now().UTC(),
			RunID:        s.config.RunID,
			ConnectionID: connectionID,
			Status:       "http-write-failed",
			RemoteAddr:   remote,
			Transport:    transport,
			DecodeError:  err.Error(),
		})
	}
}

func (s *Service) HandleFrame(ctx context.Context, connectionID string, request blaze.Frame) blaze.Frame {
	started := time.Now()
	response := blaze.Frame{Header: blaze.Header{
		Component: request.Header.Component,
		Command:   request.Header.Command,
		UserIndex: request.Header.UserIndex,
		MessageID: request.Header.MessageID,
	}}

	if request.Header.MessageType == blaze.MessageTypePing {
		response.Header.MessageType = blaze.MessageTypePingReply
		response.Payload = append([]byte(nil), request.Payload...)
	} else if h, ok := s.handlers[route{request.Header.Component, request.Header.Command}]; ok {
		fields, errorCode := h(ctx, request)
		response.Header.ErrorCode = errorCode
		if errorCode == 0 {
			response.Header.MessageType = blaze.MessageTypeReply
		} else {
			response.Header.MessageType = blaze.MessageTypeErrorReply
		}
		payload, err := blaze.Encode(fields)
		if err != nil {
			response.Header.MessageType = blaze.MessageTypeErrorReply
			response.Header.ErrorCode = ErrorSystem
		} else {
			response.Payload = payload
		}
	} else {
		response.Header.MessageType = blaze.MessageTypeErrorReply
		response.Header.ErrorCode = ErrorCommandNotFound
	}

	decoded, decodeErr := blaze.Decode(request.Payload)
	event := Event{
		Time:         time.Now().UTC(),
		RunID:        s.config.RunID,
		ConnectionID: connectionID,
		MessageID:    request.Header.MessageID,
		Component:    request.Header.Component,
		Command:      request.Header.Command,
		MessageType:  uint8(request.Header.MessageType),
		ErrorCode:    response.Header.ErrorCode,
		PayloadHex:   hex.EncodeToString(request.Payload),
		Decoded:      decoded,
		DurationMS:   float64(time.Since(started).Microseconds()) / 1000,
	}
	if decodeErr != nil {
		event.DecodeError = decodeErr.Error()
	}
	s.record(event)
	return response
}

func (s *Service) Events() []Event {
	s.eventsMu.RLock()
	defer s.eventsMu.RUnlock()
	result := make([]Event, len(s.events))
	copy(result, s.events)
	return result
}

func (s *Service) record(event Event) {
	s.eventsMu.Lock()
	s.events = append(s.events, event)
	if len(s.events) > 500 {
		s.events = append([]Event(nil), s.events[len(s.events)-500:]...)
	}
	s.eventsMu.Unlock()

	if s.config.LogFile == "" {
		return
	}
	if err := os.MkdirAll(filepath.Dir(s.config.LogFile), 0755); err != nil {
		return
	}
	file, err := os.OpenFile(s.config.LogFile, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		return
	}
	_ = json.NewEncoder(file).Encode(event)
	_ = file.Close()
}

func (s *Service) DiagnosticsHandler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{
			"status":    "ok",
			"service":   "cfb27-blaze",
			"startedAt": s.startedAt,
			"events":    len(s.Events()),
			"tlsMode":   s.config.TLSMode,
			"runId":     s.config.RunID,
			"logFile":   s.config.LogFile,
		})
	})
	mux.HandleFunc("/events", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{"events": s.Events()})
	})
	return mux
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
