package relay

import (
	"bufio"
	"bytes"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"math/big"
	rand2 "math/rand/v2"
	"net"
	"net/http"
	"os"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

type Config struct {
	Bind          string
	Port          int
	APIBind       string
	APIPort       int
	RelayHost     string
	PublicDomain  string
	PublicPrefix  string
	ClientTimeout int
	ServerTimeout int
	LogFile       string
	LeaseFile     string
	NoDashboard   bool
	MasterURL     string
}

// strip port from host if already present, otherwise append cfg port
func buildRelayAddress(host string, port int) string {
	if strings.Contains(host, ":") {
		return host
	}
	return fmt.Sprintf("%s:%d", host, port)
}

// constants

const (
	registerPrefix    = "CYPRESS_PROXY_REGISTER|SERVER|"
	sideChannelPort   = 14638
	socketBufSize     = 2 * 1024 * 1024
	codeLength        = 6
	dashboardInterval = 1500 * time.Millisecond
	logLines          = 18
	leaseSaveInterval = 30 * time.Second
	expiryInterval    = 10 * time.Second
	tcpRelayBuf       = 8192
)

const codeAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

var randomWords = []string{
	"soldier", "allstar", "engineer", "scientist", "imp", "superbrainz", "deadbeard", "zomboss",
	"peashooter", "cactus", "sunflower", "chomper", "citron", "kernelcorn", "rose", "torchwood", "hovergoat",
}

var motdTagRe = regexp.MustCompile(`\[/?[a-zA-Z](?:[^\]]*?)?\]`)

func stripMotdTags(s string) string {
	return strings.TrimSpace(motdTagRe.ReplaceAllString(s, ""))
}

// tunnel frame protocol: [1B cmd][4B client_id BE][4B data_len BE][data]
const (
	cmdOpen         = 1
	cmdData         = 2
	cmdClose        = 3
	cmdUDP          = 4
	cmdPing         = 5
	cmdPong         = 6
	frameHdrSize    = 9       // 1 + 4 + 4
	maxFrameDataLen = 1 << 20 // 1mib
)

// stats

type relayStats struct {
	UDPBytesIn        atomic.Int64
	UDPBytesOut       atomic.Int64
	UDPPktsIn         atomic.Int64
	UDPPktsOut        atomic.Int64
	TCPBytesIn        atomic.Int64
	TCPBytesOut       atomic.Int64
	Rejected          atomic.Int64
	sendToClientViaGR atomic.Int64

	mu         sync.Mutex
	lastSnap   time.Time
	lastUDPIn  int64
	lastUDPOut int64
	lastTCPIn  int64
	lastTCPOut int64
	lastPkts   int64
	UDPInRate  float64
	UDPOutRate float64
	TCPInRate  float64
	TCPOutRate float64
	PktsRate   float64
}

func (s *relayStats) snap() {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	dt := now.Sub(s.lastSnap).Seconds()
	if dt < 0.1 {
		return
	}
	udpIn := s.UDPBytesIn.Load()
	udpOut := s.UDPBytesOut.Load()
	tcpIn := s.TCPBytesIn.Load()
	tcpOut := s.TCPBytesOut.Load()
	pkts := s.UDPPktsIn.Load() + s.UDPPktsOut.Load()

	s.UDPInRate = float64(udpIn-s.lastUDPIn) / dt
	s.UDPOutRate = float64(udpOut-s.lastUDPOut) / dt
	s.TCPInRate = float64(tcpIn-s.lastTCPIn) / dt
	s.TCPOutRate = float64(tcpOut-s.lastTCPOut) / dt
	s.PktsRate = float64(pkts-s.lastPkts) / dt

	s.lastSnap = now
	s.lastUDPIn = udpIn
	s.lastUDPOut = udpOut
	s.lastTCPIn = tcpIn
	s.lastTCPOut = tcpOut
	s.lastPkts = pkts
}

// tunnel state

type tunnelState struct {
	conn    net.Conn
	mu      sync.Mutex
	clients map[uint32]net.Conn
	nextID  uint32
}

// virtual client for TCP game relay (clients with UDP blocked)
type gameRelayConn struct {
	conn net.Conn
	mu   sync.Mutex
}

// lease

type relayLease struct {
	mu             sync.Mutex
	Key            string
	Code           string
	Slug           string
	ServerName     string
	RelayAddress   string
	DisplayHost    string
	JoinLink       string
	SigningSecret  []byte
	Game           string
	CreatedAt      float64
	ServerAddr     *net.UDPAddr
	ServerLastSeen time.Time
	ClientLastSeen map[string]time.Time // "ip:port" -> last seen
	Tunnel         *tunnelState
}

func (l *relayLease) toPayload() map[string]any {
	l.mu.Lock()
	defer l.mu.Unlock()
	return map[string]any{
		"relayAddress":     l.RelayAddress,
		"relayKey":         l.Key,
		"signingSecret":    hex.EncodeToString(l.SigningSecret),
		"code":             l.Code,
		"displayHost":      l.DisplayHost,
		"joinLink":         l.JoinLink,
		"serverName":       l.ServerName,
		"slug":             l.Slug,
		"game":             l.Game,
		"serverRegistered": l.ServerAddr != nil,
	}
}

// lease store

type leaseStore struct {
	mu               sync.RWMutex
	leasesByKey      map[string]*relayLease
	leasesByCode     map[string]string // code -> key
	leasesByServer   map[string]string // "ip:port" -> key
	usedSlugs        map[string]bool
	stats            relayStats
	startTime        time.Time
	logMu            sync.Mutex // separate from store mu to avoid deadlock
	logRing          []string
	logFile          *os.File
	leaseFile        string
	dashboard        bool
	relayAddress     string
	relayHost        string
	publicDomain     string
	publicPrefix     string
	serverTimeout    time.Duration
	clientTimeout    time.Duration
	masterURL        string
	udpConn          *net.UDPConn
	gameRelayMu      sync.RWMutex
	gameRelayClients map[string]*gameRelayConn // virtual addr -> TCP conn
	nextVirtualID    atomic.Uint32
}

func newLeaseStore(cfg Config) *leaseStore {
	var lf *os.File
	if cfg.LogFile != "" {
		f, err := os.OpenFile(cfg.LogFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err == nil {
			lf = f
		}
	}
	return &leaseStore{
		leasesByKey:      make(map[string]*relayLease),
		leasesByCode:     make(map[string]string),
		leasesByServer:   make(map[string]string),
		usedSlugs:        make(map[string]bool),
		gameRelayClients: make(map[string]*gameRelayConn),
		startTime:        time.Now(),
		logFile:          lf,
		leaseFile:        cfg.LeaseFile,
		dashboard:        !cfg.NoDashboard,
		relayAddress:     buildRelayAddress(cfg.RelayHost, cfg.Port),
		relayHost:        cfg.RelayHost,
		publicDomain:     cfg.PublicDomain,
		publicPrefix:     cfg.PublicPrefix,
		serverTimeout:    time.Duration(cfg.ServerTimeout) * time.Second,
		clientTimeout:    time.Duration(cfg.ClientTimeout) * time.Second,
		masterURL:        cfg.MasterURL,
	}
}

func (s *leaseStore) log(msg string) {
	ts := time.Now().Format("15:04:05")
	line := ts + "  " + msg
	s.logMu.Lock()
	s.logRing = append(s.logRing, line)
	if len(s.logRing) > logLines {
		s.logRing = s.logRing[len(s.logRing)-logLines:]
	}
	s.logMu.Unlock()
	if s.logFile != nil {
		s.logFile.WriteString(line + "\n")
	}
	if !s.dashboard {
		fmt.Println(line)
	}
}

func (s *leaseStore) buildDisplayHost(slug string) string {
	if s.publicDomain != "" {
		if s.publicPrefix != "" {
			return s.publicPrefix + "." + slug + "." + s.publicDomain
		}
		return slug + "." + s.publicDomain
	}
	return s.relayHost
}

func slugify(val string) string {
	slug := regexp.MustCompile(`[^a-z0-9]+`).ReplaceAllString(strings.ToLower(val), "-")
	slug = strings.Trim(slug, "-")
	if slug == "" {
		return "server"
	}
	return slug
}

func (s *leaseStore) makeUniqueSlug(name string) string {
	base := slugify(name)
	slug := base
	for s.usedSlugs[slug] {
		word := randomWords[randInt(len(randomWords))]
		slug = fmt.Sprintf("%s-%s-%s", base, word, randHex(2))
	}
	s.usedSlugs[slug] = true
	return slug
}

func (s *leaseStore) makeUniqueCode() string {
	for i := 0; i < 100; i++ {
		code := randCode(codeLength)
		if _, exists := s.leasesByCode[code]; !exists {
			return code
		}
	}
	return strings.ToUpper(randHex(4))
}

func (s *leaseStore) createLease(serverName, game string) *relayLease {
	s.mu.Lock()
	defer s.mu.Unlock()

	slug := s.makeUniqueSlug(serverName)
	key := randURLSafe(18)
	code := s.makeUniqueCode()
	displayHost := s.buildDisplayHost(slug)
	joinLink := fmt.Sprintf("cypress://%s?relay=%s&key=%s", displayHost, s.relayAddress, key)

	secret := make([]byte, 32)
	rand.Read(secret)

	lease := &relayLease{
		Key:            key,
		Code:           code,
		Slug:           slug,
		ServerName:     serverName,
		RelayAddress:   s.relayAddress,
		DisplayHost:    displayHost,
		JoinLink:       joinLink,
		SigningSecret:  secret,
		Game:           game,
		CreatedAt:      float64(time.Now().Unix()),
		ClientLastSeen: make(map[string]time.Time),
	}

	s.leasesByKey[key] = lease
	s.leasesByCode[code] = key
	s.log(fmt.Sprintf("created lease %s (%s) for %s -> %s", code, key, serverName, displayHost))
	go s.saveLeases()
	return lease
}

func (s *leaseStore) resolveCode(code string) *relayLease {
	s.mu.RLock()
	defer s.mu.RUnlock()
	key, ok := s.leasesByCode[strings.ToUpper(code)]
	if !ok {
		return nil
	}
	return s.leasesByKey[key]
}

func (s *leaseStore) registerServer(key string, addr *net.UDPAddr, timestamp, signature string) *relayLease {
	s.mu.Lock()
	defer s.mu.Unlock()

	lease, ok := s.leasesByKey[key]
	if !ok {
		return nil
	}

	// verify HMAC if provided
	if timestamp != "" && signature != "" {
		ts := parseFloat(timestamp)
		if math.Abs(float64(time.Now().Unix())-ts) > 30 {
			s.log(fmt.Sprintf("register rejected: timestamp too old for %s", lease.ServerName))
			return nil
		}
		mac := hmac.New(sha256.New, lease.SigningSecret)
		mac.Write([]byte(key + timestamp))
		expected := hex.EncodeToString(mac.Sum(nil))
		if !hmac.Equal([]byte(signature), []byte(expected)) {
			s.log(fmt.Sprintf("register rejected: bad signature for %s", lease.ServerName))
			return nil
		}
	}

	lease.mu.Lock()
	addrStr := addr.String()
	if lease.ServerAddr != nil {
		oldStr := lease.ServerAddr.String()
		if oldStr != addrStr {
			delete(s.leasesByServer, oldStr)
		}
	}
	lease.ServerAddr = addr
	lease.ServerLastSeen = time.Now()
	s.leasesByServer[addrStr] = key
	lease.mu.Unlock()

	return lease
}

func (s *leaseStore) expireOldEntries() {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	for _, lease := range s.leasesByKey {
		lease.mu.Lock()
		if lease.ServerAddr != nil && now.Sub(lease.ServerLastSeen) > s.serverTimeout {
			s.log(fmt.Sprintf("server expired: %s (%s)", lease.ServerName, lease.Key))
			delete(s.leasesByServer, lease.ServerAddr.String())
			lease.ServerAddr = nil
			lease.ServerLastSeen = time.Time{}
		}
		for addr, t := range lease.ClientLastSeen {
			if now.Sub(t) > s.clientTimeout {
				delete(lease.ClientLastSeen, addr)
			}
		}
		lease.mu.Unlock()
	}
}

func (s *leaseStore) sendToClient(udpConn *net.UDPConn, target *net.UDPAddr, data []byte) {
	targetStr := target.String()
	s.gameRelayMu.RLock()
	gr := s.gameRelayClients[targetStr]
	s.gameRelayMu.RUnlock()

	if gr != nil {
		var hdr [2]byte
		binary.BigEndian.PutUint16(hdr[:], uint16(len(data)))
		payload := make([]byte, 2+len(data))
		copy(payload[:2], hdr[:])
		copy(payload[2:], data)
		gr.mu.Lock()
		gr.conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
		_, err := gr.conn.Write(payload)
		gr.conn.SetWriteDeadline(time.Time{})
		gr.mu.Unlock()
		if err != nil {
			gr.conn.Close()
			return
		}
		s.stats.TCPBytesOut.Add(int64(len(data)) + 2)
		s.stats.sendToClientViaGR.Add(1)
		if c := s.stats.sendToClientViaGR.Load(); c <= 5 {
			s.log(fmt.Sprintf("   sendToClient via game relay: %s len=%d", targetStr, len(data)))
		}
	} else if udpConn != nil {
		udpConn.WriteToUDP(data, target)
		s.stats.UDPBytesOut.Add(int64(len(data)))
		s.stats.UDPPktsOut.Add(1)
	}
}

func (s *leaseStore) listLeases() []map[string]any {
	s.mu.RLock()
	defer s.mu.RUnlock()
	var out []map[string]any
	for _, l := range s.leasesByKey {
		out = append(out, l.toPayload())
	}
	if out == nil {
		out = []map[string]any{}
	}
	return out
}

type leaseJSON struct {
	Key           string  `json:"key"`
	Code          string  `json:"code"`
	Slug          string  `json:"slug"`
	ServerName    string  `json:"server_name"`
	RelayAddress  string  `json:"relay_address"`
	DisplayHost   string  `json:"display_host"`
	JoinLink      string  `json:"join_link"`
	SigningSecret string  `json:"signing_secret"`
	Game          string  `json:"game"`
	CreatedAt     float64 `json:"created_at"`
}

func (s *leaseStore) saveLeases() {
	if s.leaseFile == "" {
		return
	}
	s.mu.RLock()
	var data []leaseJSON
	for _, l := range s.leasesByKey {
		data = append(data, leaseJSON{
			Key:           l.Key,
			Code:          l.Code,
			Slug:          l.Slug,
			ServerName:    l.ServerName,
			RelayAddress:  l.RelayAddress,
			DisplayHost:   l.DisplayHost,
			JoinLink:      l.JoinLink,
			SigningSecret: hex.EncodeToString(l.SigningSecret),
			Game:          l.Game,
			CreatedAt:     l.CreatedAt,
		})
	}
	s.mu.RUnlock()

	raw, err := json.Marshal(data)
	if err != nil {
		s.log("x  failed to marshal leases: " + err.Error())
		return
	}
	tmp := s.leaseFile + ".tmp"
	if err := os.WriteFile(tmp, raw, 0644); err != nil {
		s.log("x  failed to save leases: " + err.Error())
		return
	}
	os.Rename(tmp, s.leaseFile)
}

func (s *leaseStore) loadLeases() int {
	if s.leaseFile == "" {
		return 0
	}
	raw, err := os.ReadFile(s.leaseFile)
	if err != nil {
		return 0
	}
	var entries []leaseJSON
	if err := json.Unmarshal(raw, &entries); err != nil {
		s.log("x  failed to load leases: " + err.Error())
		return 0
	}
	count := 0
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, e := range entries {
		if e.Key == "" || s.leasesByKey[e.Key] != nil {
			continue
		}
		secret, _ := hex.DecodeString(e.SigningSecret)
		lease := &relayLease{
			Key:            e.Key,
			Code:           e.Code,
			Slug:           e.Slug,
			ServerName:     e.ServerName,
			RelayAddress:   e.RelayAddress,
			DisplayHost:    e.DisplayHost,
			JoinLink:       e.JoinLink,
			SigningSecret:  secret,
			Game:           e.Game,
			CreatedAt:      e.CreatedAt,
			ClientLastSeen: make(map[string]time.Time),
		}
		s.leasesByKey[e.Key] = lease
		s.leasesByCode[e.Code] = e.Key
		s.usedSlugs[e.Slug] = true
		count++
	}
	return count
}

// udp relay

func runUDPRelay(store *leaseStore, bind string, port int) (*net.UDPConn, error) {
	addr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", bind, port))
	if err != nil {
		return nil, err
	}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		return nil, err
	}

	conn.SetReadBuffer(socketBufSize)
	conn.SetWriteBuffer(socketBufSize)

	go udpLoop(conn, store)
	return conn, nil
}

func udpLoop(conn *net.UDPConn, store *leaseStore) {
	buf := make([]byte, 65536)
	prefix := []byte(registerPrefix)
	prefixLen := len(prefix)

	for {
		n, addr, err := conn.ReadFromUDP(buf)
		if err != nil {
			continue
		}
		if n == 0 {
			continue
		}

		data := buf[:n]
		store.stats.UDPBytesIn.Add(int64(n))
		store.stats.UDPPktsIn.Add(1)

		// server registration
		if n > prefixLen && string(data[:prefixLen]) == registerPrefix {
			payload := strings.TrimSpace(string(data[prefixLen:]))
			store.log(fmt.Sprintf("   udp register attempt: payload=%q from %s", payload, addr))
			parts := strings.SplitN(payload, "|", 3)
			var key, ts, sig string
			switch len(parts) {
			case 3:
				key, ts, sig = parts[0], parts[1], parts[2]
			case 1:
				// unsigned registration (legacy DLL)
				key = parts[0]
			default:
				store.log(fmt.Sprintf("x  dropping malformed registration from %s", addr))
				store.stats.Rejected.Add(1)
				continue
			}
			lease := store.registerServer(key, addr, ts, sig)
			if lease == nil {
				store.log(fmt.Sprintf("x  udp register rejected: key=%q from %s", key, addr))
				store.stats.Rejected.Add(1)
				continue
			}
			store.log(fmt.Sprintf("*  udp server registered: %s @ %s", stripMotdTags(lease.ServerName), addr))
			ack := []byte("CYPRESS_PROXY_ACK")
			conn.WriteToUDP(ack, addr)
			store.stats.UDPBytesOut.Add(int64(len(ack)))
			store.stats.UDPPktsOut.Add(1)
			continue
		}

		addrStr := addr.String()

		// known server -> forward to client
		store.mu.RLock()
		key, isServer := store.leasesByServer[addrStr]
		store.mu.RUnlock()

		if isServer {
			store.mu.RLock()
			lease := store.leasesByKey[key]
			store.mu.RUnlock()
			if lease == nil || n < 6 {
				continue
			}
			lease.mu.Lock()
			lease.ServerLastSeen = time.Now()
			lease.mu.Unlock()

			// header: 4-byte ip + 2-byte port
			ip := net.IP(data[:4])
			port := binary.BigEndian.Uint16(data[4:6])
			target := &net.UDPAddr{IP: ip, Port: int(port)}

			lease.mu.Lock()
			lease.ClientLastSeen[target.String()] = time.Now()
			lease.mu.Unlock()

			store.sendToClient(conn, target, data[6:])
			continue
		}

		// client -> forward to server
		if n < 2 {
			continue
		}
		keyLen := int(data[0])
		if n < keyLen+1 {
			continue
		}
		relayKey := string(data[1 : keyLen+1])

		store.mu.RLock()
		lease := store.leasesByKey[relayKey]
		store.mu.RUnlock()

		if lease == nil {
			continue
		}
		lease.mu.Lock()
		serverAddr := lease.ServerAddr
		lease.ClientLastSeen[addrStr] = time.Now()
		lease.mu.Unlock()

		if serverAddr == nil {
			// no direct UDP path, try forwarding through TCP tunnel
			lease.mu.Lock()
			tunnel := lease.Tunnel
			lease.mu.Unlock()
			if tunnel != nil {
				// wrap client addr + game data as a tunnel UDP frame
				ip4 := addr.IP.To4()
				if ip4 != nil {
					var hdr [6]byte
					copy(hdr[:4], ip4)
					binary.BigEndian.PutUint16(hdr[4:], uint16(addr.Port))
					payload := make([]byte, 6+n-keyLen-1)
					copy(payload[:6], hdr[:])
					copy(payload[6:], data[keyLen+1:])
					writeFrame(tunnel.conn, &tunnel.mu, cmdUDP, 0, payload, &store.stats)
				}
			}
			continue
		}

		// pack client addr as header: 4-byte ip + 2-byte port
		ip4 := addr.IP.To4()
		if ip4 == nil {
			continue
		}
		var header [6]byte
		copy(header[:4], ip4)
		binary.BigEndian.PutUint16(header[4:], uint16(addr.Port))

		out := make([]byte, 6+n-keyLen-1)
		copy(out[:6], header[:])
		copy(out[6:], data[keyLen+1:])

		conn.WriteToUDP(out, serverAddr)
		store.stats.UDPBytesOut.Add(int64(len(out)))
		store.stats.UDPPktsOut.Add(1)
	}
}

// tcp side-channel tunnel

func writeFrame(conn net.Conn, mu *sync.Mutex, cmd byte, clientID uint32, data []byte, stats *relayStats) error {
	hdr := make([]byte, frameHdrSize)
	hdr[0] = cmd
	binary.BigEndian.PutUint32(hdr[1:5], clientID)
	binary.BigEndian.PutUint32(hdr[5:9], uint32(len(data)))
	payload := append(hdr, data...)

	mu.Lock()
	conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
	_, err := conn.Write(payload)
	conn.SetWriteDeadline(time.Time{})
	mu.Unlock()

	if stats != nil {
		stats.TCPBytesOut.Add(int64(len(payload)))
	}
	return err
}

func readFrame(conn net.Conn, stats *relayStats) (cmd byte, clientID uint32, data []byte, err error) {
	hdr := make([]byte, frameHdrSize)
	if _, err = io.ReadFull(conn, hdr); err != nil {
		return
	}
	cmd = hdr[0]
	clientID = binary.BigEndian.Uint32(hdr[1:5])
	dataLen := binary.BigEndian.Uint32(hdr[5:9])
	if dataLen > maxFrameDataLen {
		err = fmt.Errorf("frame too large: %d bytes", dataLen)
		return
	}
	if dataLen > 0 {
		data = make([]byte, dataLen)
		if _, err = io.ReadFull(conn, data); err != nil {
			return
		}
	}
	if stats != nil {
		stats.TCPBytesIn.Add(int64(frameHdrSize) + int64(dataLen))
	}
	return
}

func tunnelDispatcher(conn net.Conn, tunnel *tunnelState, store *leaseStore, stats *relayStats) {
	for {
		cmd, cid, data, err := readFrame(conn, stats)
		if err != nil {
			return
		}

		switch cmd {
		case cmdData:
			tunnel.mu.Lock()
			cw := tunnel.clients[cid]
			tunnel.mu.Unlock()
			if cw != nil {
				cw.Write(data)
			}
		case cmdClose:
			tunnel.mu.Lock()
			if cw, ok := tunnel.clients[cid]; ok {
				cw.Close()
				delete(tunnel.clients, cid)
			}
			tunnel.mu.Unlock()
		case cmdUDP:
			// registration packets from the bridge don't have the 6-byte header
			if len(data) >= len(registerPrefix) && string(data[:len(registerPrefix)]) == registerPrefix {
				break
			}
			// server sent game data through tunnel, forward to client
			if len(data) > 6 {
				ip := net.IP(data[:4])
				port := binary.BigEndian.Uint16(data[4:6])
				target := &net.UDPAddr{IP: ip, Port: int(port)}
				store.sendToClient(store.udpConn, target, data[6:])
			}
		case cmdPong:
			// keepalive response, connection is alive
		}
	}
}

func clientToTunnel(clientConn net.Conn, tunnel *tunnelState, clientID uint32, stats *relayStats) {
	buf := make([]byte, tcpRelayBuf)
	for {
		n, err := clientConn.Read(buf)
		if err != nil || n == 0 {
			return
		}
		if stats != nil {
			stats.TCPBytesIn.Add(int64(n))
		}
		if err := writeFrame(tunnel.conn, &tunnel.mu, cmdData, clientID, buf[:n], stats); err != nil {
			return
		}
	}
}

func runTCPRelay(store *leaseStore, bind string) (net.Listener, error) {
	ln, err := net.Listen("tcp", fmt.Sprintf("%s:%d", bind, sideChannelPort))
	if err != nil {
		return nil, err
	}
	go func() {
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			go handleTCPConn(conn, store)
		}
	}()
	return ln, nil
}

func handleTCPConn(conn net.Conn, store *leaseStore) {
	defer conn.Close()
	peer := conn.RemoteAddr().String()
	st := &store.stats

	conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	reader := bufio.NewReaderSize(conn, 4096)
	line, err := reader.ReadBytes('\n')
	if err != nil && len(line) == 0 {
		return
	}
	conn.SetReadDeadline(time.Time{})

	// trim trailing newline
	jsonData := bytes.TrimRight(line, "\n\r")

	var msg map[string]any
	if err := json.Unmarshal(jsonData, &msg); err != nil {
		store.log(fmt.Sprintf("x  bad handshake from %s", peer))
		return
	}

	// anything buffered after the newline is remainder
	remainder, _ := io.ReadAll(io.LimitReader(reader, int64(reader.Buffered())))

	msgType, _ := msg["type"].(string)
	key, _ := msg["key"].(string)

	store.mu.RLock()
	lease := store.leasesByKey[key]
	store.mu.RUnlock()

	switch msgType {
	case "register":
		if lease == nil {
			store.log(fmt.Sprintf("x  unknown key from server %s", peer))
			return
		}
		name := stripMotdTags(lease.ServerName)
		store.log(fmt.Sprintf("*  tcp tunnel registered: %s from %s", name, peer))

		tunnel := &tunnelState{
			conn:    conn,
			clients: make(map[uint32]net.Conn),
			nextID:  1,
		}

		// enable tcp keepalive
		if tc, ok := conn.(*net.TCPConn); ok {
			tc.SetKeepAlive(true)
			tc.SetKeepAlivePeriod(30 * time.Second)
		}

		lease.mu.Lock()
		lease.Tunnel = tunnel
		lease.mu.Unlock()

		// ping the DLL every 30s so the connection doesn't die from NAT/firewall timeout
		pingDone := make(chan struct{})
		go func() {
			ticker := time.NewTicker(30 * time.Second)
			defer ticker.Stop()
			for {
				select {
				case <-ticker.C:
					if err := writeFrame(conn, &tunnel.mu, cmdPing, 0, nil, st); err != nil {
						return
					}
				case <-pingDone:
					return
				}
			}
		}()

		tunnelDispatcher(conn, tunnel, store, st)
		close(pingDone)

		// cleanup
		lease.mu.Lock()
		lease.Tunnel = nil
		lease.mu.Unlock()
		tunnel.mu.Lock()
		for _, cw := range tunnel.clients {
			cw.Close()
		}
		tunnel.clients = nil
		tunnel.mu.Unlock()
		store.log(fmt.Sprintf("-  tcp tunnel lost: %s", name))

	case "relay":
		if lease == nil {
			store.log(fmt.Sprintf("x  client %s rejected (unknown key)", peer))
			st.Rejected.Add(1)
			return
		}

		// validate auth ticket
		if store.masterURL == "" {
			store.log(fmt.Sprintf("x  client %s rejected (no master url for ticket validation)", peer))
			st.Rejected.Add(1)
			return
		}
		ticketStr, _ := msg["ticket"].(string)
		if ticketStr == "" {
			store.log(fmt.Sprintf("x  client %s rejected (no ticket)", peer))
			st.Rejected.Add(1)
			return
		}
		valid, displayName := store.validateTicket(ticketStr)
		if !valid {
			store.log(fmt.Sprintf("x  client %s rejected (invalid ticket)", peer))
			st.Rejected.Add(1)
			return
		}
		_ = displayName

		lease.mu.Lock()
		tunnel := lease.Tunnel
		lease.mu.Unlock()
		if tunnel == nil {
			store.log(fmt.Sprintf("x  client %s rejected (no tunnel)", peer))
			st.Rejected.Add(1)
			return
		}

		// keep client tcp alive through NAT/firewalls
		if tc, ok := conn.(*net.TCPConn); ok {
			tc.SetKeepAlive(true)
			tc.SetKeepAlivePeriod(30 * time.Second)
		}

		tunnel.mu.Lock()
		if tunnel.clients == nil {
			tunnel.mu.Unlock()
			store.log(fmt.Sprintf("x  client %s rejected (tunnel torn down)", peer))
			st.Rejected.Add(1)
			return
		}
		clientID := tunnel.nextID
		tunnel.nextID++
		tunnel.clients[clientID] = conn
		tunnel.mu.Unlock()

		name := stripMotdTags(lease.ServerName)
		store.log(fmt.Sprintf("+  client %s -> cid=%d (%s)", peer, clientID, name))

		writeFrame(tunnel.conn, &tunnel.mu, cmdOpen, clientID, nil, st)

		// forward any remainder bytes that arrived with the handshake
		if len(remainder) > 0 {
			writeFrame(tunnel.conn, &tunnel.mu, cmdData, clientID, remainder, st)
		}

		clientToTunnel(conn, tunnel, clientID, st)

		tunnel.mu.Lock()
		delete(tunnel.clients, clientID)
		tunnel.mu.Unlock()
		writeFrame(tunnel.conn, &tunnel.mu, cmdClose, clientID, nil, st)
		store.log(fmt.Sprintf("-  client cid=%d disconnected", clientID))

	case "gameRelay":
		if lease == nil {
			store.log(fmt.Sprintf("x  game relay %s rejected (unknown key)", peer))
			st.Rejected.Add(1)
			return
		}

		// validate auth ticket
		if store.masterURL == "" {
			store.log(fmt.Sprintf("x  game relay %s rejected (no master url for ticket validation)", peer))
			st.Rejected.Add(1)
			return
		}
		ticketStr, _ := msg["ticket"].(string)
		if ticketStr == "" {
			store.log(fmt.Sprintf("x  game relay %s rejected (no ticket)", peer))
			st.Rejected.Add(1)
			return
		}
		valid, _ := store.validateTicket(ticketStr)
		if !valid {
			store.log(fmt.Sprintf("x  game relay %s rejected (invalid ticket)", peer))
			st.Rejected.Add(1)
			return
		}

		// find the client's real UDP address from the lease so we can impersonate it
		// this keeps the server SocketManager seeing the same peer as the initial direct UDP handshake
		// pick the most recently seen address that isn't already claimed by another game relay
		lease.mu.Lock()
		var clientAddr *net.UDPAddr
		var clientAddrStr string
		var bestTime time.Time
		for addr, lastSeen := range lease.ClientLastSeen {
			parsed, err := net.ResolveUDPAddr("udp", addr)
			if err == nil && !parsed.IP.IsPrivate() && !parsed.IP.IsLoopback() {
				store.gameRelayMu.RLock()
				_, claimed := store.gameRelayClients[addr]
				store.gameRelayMu.RUnlock()
				if !claimed && lastSeen.After(bestTime) {
					clientAddr = parsed
					clientAddrStr = addr
					bestTime = lastSeen
				}
			}
		}
		lease.mu.Unlock()

		if clientAddr == nil {
			// no prior UDP client found, fall back to virtual address
			vid := store.nextVirtualID.Add(1)
			clientAddr = &net.UDPAddr{
				IP:   net.IPv4(10, 255, byte(vid>>8), byte(vid)),
				Port: int(vid),
			}
			clientAddrStr = clientAddr.String()
			store.log(fmt.Sprintf("   game relay %s: no prior UDP client, using virtual %s", peer, clientAddrStr))
		}

		gr := &gameRelayConn{conn: conn}

		// keep game relay tcp alive through NAT/firewalls
		if tc, ok := conn.(*net.TCPConn); ok {
			tc.SetKeepAlive(true)
			tc.SetKeepAlivePeriod(30 * time.Second)
		}

		store.gameRelayMu.Lock()
		store.gameRelayClients[clientAddrStr] = gr
		store.gameRelayMu.Unlock()

		name := stripMotdTags(lease.ServerName)
		store.log(fmt.Sprintf("+  game relay %s -> %s (%s)", peer, clientAddrStr, name))

		defer func() {
			store.gameRelayMu.Lock()
			delete(store.gameRelayClients, clientAddrStr)
			store.gameRelayMu.Unlock()
			store.log(fmt.Sprintf("-  game relay %s disconnected", peer))
		}()

		// read loop: framed game data from client -> forward to server
		var reader io.Reader = conn
		if len(remainder) > 0 {
			reader = io.MultiReader(bytes.NewReader(remainder), conn)
		}
		pktCount := 0
		for {
			var lenBuf [2]byte
			if _, err := io.ReadFull(reader, lenBuf[:]); err != nil {
				store.log(fmt.Sprintf("   game relay %s read error: %v", peer, err))
				return
			}
			pktLen := binary.BigEndian.Uint16(lenBuf[:])
			if pktLen == 0 {
				store.log(fmt.Sprintf("   game relay %s got zero-length frame", peer))
				return
			}
			pkt := make([]byte, pktLen)
			if _, err := io.ReadFull(reader, pkt); err != nil {
				store.log(fmt.Sprintf("   game relay %s payload read error: %v (wanted %d)", peer, err, pktLen))
				return
			}
			st.TCPBytesIn.Add(int64(pktLen) + 2)
			pktCount++
			if pktCount <= 5 {
				store.log(fmt.Sprintf("   game relay pkt #%d: len=%d first=[%02x %02x %02x ...]", pktCount, pktLen, pkt[0], safeIdx(pkt, 1), safeIdx(pkt, 2)))
			}

			// strip key prefix (same format as UDP: [1B keyLen][key][payload])
			if len(pkt) < 2 {
				continue
			}
			keyLen := int(pkt[0])
			if len(pkt) < keyLen+1 {
				continue
			}
			gameData := pkt[keyLen+1:]

			// use client's real address in header so server sees same peer as direct UDP
			ip4 := clientAddr.IP.To4()
			var hdr [6]byte
			copy(hdr[:4], ip4)
			binary.BigEndian.PutUint16(hdr[4:], uint16(clientAddr.Port))
			out := make([]byte, 6+len(gameData))
			copy(out[:6], hdr[:])
			copy(out[6:], gameData)

			lease.mu.Lock()
			serverAddr := lease.ServerAddr
			tunnel := lease.Tunnel
			lease.ClientLastSeen[clientAddrStr] = time.Now()
			lease.mu.Unlock()

			if tunnel != nil {
				writeFrame(tunnel.conn, &tunnel.mu, cmdUDP, 0, out, st)
			} else if serverAddr != nil && store.udpConn != nil {
				store.udpConn.WriteToUDP(out, serverAddr)
				st.UDPBytesOut.Add(int64(len(out)))
				st.UDPPktsOut.Add(1)
			}
		}

	case "serverInfo":
		if key == "" || lease == nil {
			st.Rejected.Add(1)
			return
		}
		lease.mu.Lock()
		tunnel := lease.Tunnel
		lease.mu.Unlock()
		if tunnel == nil {
			st.Rejected.Add(1)
			return
		}

		tunnel.mu.Lock()
		clientID := tunnel.nextID
		tunnel.nextID++
		tunnel.clients[clientID] = conn
		tunnel.mu.Unlock()

		name := stripMotdTags(lease.ServerName)
		store.log(fmt.Sprintf("   serverInfo %s -> cid=%d (%s)", peer, clientID, name))

		writeFrame(tunnel.conn, &tunnel.mu, cmdOpen, clientID, nil, st)
		// forward the handshake line as first data
		writeFrame(tunnel.conn, &tunnel.mu, cmdData, clientID, append(line, remainder...), st)

		clientToTunnel(conn, tunnel, clientID, st)

		tunnel.mu.Lock()
		delete(tunnel.clients, clientID)
		tunnel.mu.Unlock()
		writeFrame(tunnel.conn, &tunnel.mu, cmdClose, clientID, nil, st)

	default:
		store.log(fmt.Sprintf("x  unknown type '%s' from %s", msgType, peer))
	}
}

// ticket validation via master server

func (s *leaseStore) validateTicket(ticketStr string) (valid bool, displayName string) {
	client := &http.Client{Timeout: 5 * time.Second}
	body := fmt.Sprintf(`{"ticket":%q}`, ticketStr)
	resp, err := client.Post(s.masterURL+"/auth/validate-ticket", "application/json", strings.NewReader(body))
	if err != nil {
		s.log(fmt.Sprintf("x  ticket validation failed: %v", err))
		return false, ""
	}
	defer resp.Body.Close()

	var result map[string]any
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return false, ""
	}

	if v, ok := result["valid"].(bool); ok && v {
		dn, _ := result["displayName"].(string)
		return true, dn
	}
	return false, ""
}

// http api

func runAPI(store *leaseStore, bind string, port int) error {
	mux := http.NewServeMux()

	mux.HandleFunc("/api/relays", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		switch r.Method {
		case http.MethodGet:
			json.NewEncoder(w).Encode(map[string]any{"leases": store.listLeases()})
		case http.MethodPost:
			var payload map[string]any
			if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
				w.WriteHeader(400)
				json.NewEncoder(w).Encode(map[string]string{"error": "invalid_json"})
				return
			}
			name, _ := payload["serverName"].(string)
			if name == "" {
				name = "Cypress Server"
			}
			game, _ := payload["game"].(string)
			if game == "" {
				game = "GW2"
			}
			lease := store.createLease(strings.TrimSpace(name), strings.TrimSpace(game))
			w.WriteHeader(201)
			json.NewEncoder(w).Encode(lease.toPayload())
		default:
			w.WriteHeader(405)
		}
	})

	mux.HandleFunc("/api/relays/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		code := strings.TrimPrefix(r.URL.Path, "/api/relays/")
		code = strings.TrimSpace(strings.ToUpper(code))
		if code == "" {
			w.WriteHeader(400)
			json.NewEncoder(w).Encode(map[string]string{"error": "missing code"})
			return
		}
		lease := store.resolveCode(code)
		if lease == nil {
			w.WriteHeader(404)
			json.NewEncoder(w).Encode(map[string]string{"error": "unknown code"})
			return
		}
		json.NewEncoder(w).Encode(lease.toPayload())
	})

	addr := fmt.Sprintf("%s:%d", bind, port)
	store.log(fmt.Sprintf("relay API listening on http://%s/api/relays", addr))
	go http.ListenAndServe(addr, mux)
	return nil
}

// dashboard

func fmtBytes(n int64) string {
	switch {
	case n < 1024:
		return fmt.Sprintf("%d B", n)
	case n < 1024*1024:
		return fmt.Sprintf("%.1f KB", float64(n)/1024)
	case n < 1024*1024*1024:
		return fmt.Sprintf("%.1f MB", float64(n)/(1024*1024))
	default:
		return fmt.Sprintf("%.2f GB", float64(n)/(1024*1024*1024))
	}
}

func fmtRate(bps float64) string {
	switch {
	case bps < 1024:
		return fmt.Sprintf("%.0f B/s", bps)
	case bps < 1024*1024:
		return fmt.Sprintf("%.1f KB/s", bps/1024)
	default:
		return fmt.Sprintf("%.1f MB/s", bps/(1024*1024))
	}
}

func fmtUptime(d time.Duration) string {
	days := int(d.Hours()) / 24
	hours := int(d.Hours()) % 24
	mins := int(d.Minutes()) % 60
	var parts []string
	if days > 0 {
		parts = append(parts, fmt.Sprintf("%dd", days))
	}
	if hours > 0 || days > 0 {
		parts = append(parts, fmt.Sprintf("%dh", hours))
	}
	parts = append(parts, fmt.Sprintf("%dm", mins))
	return strings.Join(parts, " ")
}

func fmtAgo(t time.Time) string {
	if t.IsZero() {
		return "never"
	}
	d := time.Since(t)
	switch {
	case d < time.Minute:
		return fmt.Sprintf("%ds ago", int(d.Seconds()))
	case d < time.Hour:
		return fmt.Sprintf("%dm ago", int(d.Minutes()))
	default:
		return fmt.Sprintf("%dh ago", int(d.Hours()))
	}
}

func safeIdx(b []byte, i int) byte {
	if i < len(b) {
		return b[i]
	}
	return 0
}

func renderDashboard(store *leaseStore, udpPort, tcpPort, apiPort int) string {
	st := &store.stats
	var sb strings.Builder

	uptime := fmtUptime(time.Since(store.startTime))
	hdr := fmt.Sprintf("cypress relay  |  up %s  |  udp :%d  tcp :%d  api :%d", uptime, udpPort, tcpPort, apiPort)
	sb.WriteString("\033[1;36m" + hdr + "\033[0m\n")
	sb.WriteString("\033[90m" + strings.Repeat("-", len(hdr)) + "\033[0m\n")

	store.mu.RLock()
	leases := make([]*relayLease, 0, len(store.leasesByKey))
	for _, l := range store.leasesByKey {
		leases = append(leases, l)
	}
	store.mu.RUnlock()

	if len(leases) > 0 {
		sb.WriteString(fmt.Sprintf("\033[1m%-6s %-30s %7s  %10s\033[0m\n", "game", "server", "clients", "last seen"))
		for _, l := range leases {
			l.mu.Lock()
			name := stripMotdTags(l.ServerName)
			if len(name) > 28 {
				name = name[:28]
			}
			clients := len(l.ClientLastSeen)
			seen := fmtAgo(l.ServerLastSeen)
			if l.ServerAddr == nil {
				seen = "\033[90moffline\033[0m"
			}
			tunnelMark := " "
			if l.Tunnel != nil {
				tunnelMark = "\033[32mT\033[0m"
			}
			l.mu.Unlock()
			sb.WriteString(fmt.Sprintf(" %-5s %-30s %5d  %s %10s\n", l.Game, name, clients, tunnelMark, seen))
		}
	} else {
		sb.WriteString("\033[90m  no servers\033[0m\n")
	}
	sb.WriteString("\n")

	st.mu.Lock()
	udpTotal := fmtBytes(st.lastUDPIn + st.lastUDPOut)
	tcpTotal := fmtBytes(st.lastTCPIn + st.lastTCPOut)
	udpRate := fmtRate(st.UDPInRate + st.UDPOutRate)
	tcpRate := fmtRate(st.TCPInRate + st.TCPOutRate)
	pps := fmt.Sprintf("%.0f pkt/s", st.PktsRate)
	st.mu.Unlock()
	rej := fmt.Sprintf("%d rejected", st.Rejected.Load())
	statsLine := fmt.Sprintf("udp %s (%s)  |  tcp %s (%s)  |  %s  |  %s", udpTotal, udpRate, tcpTotal, tcpRate, pps, rej)
	sb.WriteString("\033[33m" + statsLine + "\033[0m\n")
	sb.WriteString("\033[90m" + strings.Repeat("-", len(statsLine)) + "\033[0m\n")

	store.logMu.Lock()
	for _, line := range store.logRing {
		sb.WriteString(line + "\n")
	}
	store.logMu.Unlock()

	return "\033[H\033[J" + sb.String()
}

func dashboardLoop(store *leaseStore, udpPort, tcpPort, apiPort int) {
	ticker := time.NewTicker(dashboardInterval)
	for range ticker.C {
		store.stats.snap()
		frame := renderDashboard(store, udpPort, tcpPort, apiPort)
		os.Stdout.WriteString(frame)
	}
}

// helpers

func randInt(max int) int {
	n, _ := rand.Int(rand.Reader, big.NewInt(int64(max)))
	return int(n.Int64())
}

func randHex(n int) string {
	b := make([]byte, n)
	rand.Read(b)
	return hex.EncodeToString(b)
}

func randCode(n int) string {
	b := make([]byte, n)
	for i := range b {
		b[i] = codeAlphabet[randInt(len(codeAlphabet))]
	}
	return string(b)
}

func randURLSafe(n int) string {
	const charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
	b := make([]byte, n)
	for i := range b {
		b[i] = charset[rand2.IntN(len(charset))]
	}
	return string(b)
}

func parseFloat(s string) float64 {
	var f float64
	fmt.Sscanf(s, "%f", &f)
	return f
}

// Run starts the relay
func Run(cfg Config) error {
	store := newLeaseStore(cfg)

	restored := store.loadLeases()
	if restored > 0 {
		store.log(fmt.Sprintf("restored %d lease(s) from disk", restored))
	}

	udpConn, err := runUDPRelay(store, cfg.Bind, cfg.Port)
	if err != nil {
		return fmt.Errorf("udp relay: %w", err)
	}
	defer udpConn.Close()
	store.udpConn = udpConn
	store.log(fmt.Sprintf("relay listening on udp://%s:%d", cfg.Bind, cfg.Port))

	tcpLn, err := runTCPRelay(store, cfg.Bind)
	if err != nil {
		return fmt.Errorf("tcp relay: %w", err)
	}
	defer tcpLn.Close()
	store.log(fmt.Sprintf("tcp side-channel relay listening on tcp://%s:%d", cfg.Bind, sideChannelPort))

	if err := runAPI(store, cfg.APIBind, cfg.APIPort); err != nil {
		return fmt.Errorf("api: %w", err)
	}

	// expiry loop
	go func() {
		ticker := time.NewTicker(expiryInterval)
		saveCounter := 0
		for range ticker.C {
			store.expireOldEntries()
			saveCounter++
			if saveCounter >= int(leaseSaveInterval/expiryInterval) {
				saveCounter = 0
				store.saveLeases()
			}
		}
	}()

	if store.dashboard {
		go dashboardLoop(store, cfg.Port, sideChannelPort, cfg.APIPort)
	}

	// block forever
	select {}
}
