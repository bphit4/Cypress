package master

import (
	"crypto/ed25519"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"regexp"
	"strings"
	"sync"
	"time"

	"cypress-servers/internal/ea"

	_ "github.com/mattn/go-sqlite3"
)

// config

type Config struct {
	Bind        string
	Port        int
	BehindProxy bool
	DBFile      string
	SecretFile  string
}

// constants

const (
	staleTimeout    = 90 * time.Second
	cleanupInterval = 30 * time.Second
	maxServers      = 1000
	maxBodySize     = 64 * 1024
	iconMaxB64      = 15000
	listCacheTTL    = 5 * time.Second
	modTokenExpiry  = 24 * time.Hour
	blacklistDur    = 1 * time.Hour
	rateLoginMax    = 25
	rateLoginWindow = 15 * time.Minute
)

var privateIPRe = regexp.MustCompile(`^(0\.0\.0\.0|127\.|10\.|192\.168\.|172\.(1[6-9]|2[0-9]|3[01])\.)`)

// rate limiter

type rateBucket struct {
	hits []time.Time
}

type rateLimiter struct {
	mu      sync.Mutex
	buckets map[string]*rateBucket // "ip:scope"
}

func newRateLimiter() *rateLimiter {
	return &rateLimiter{buckets: make(map[string]*rateBucket)}
}

func (rl *rateLimiter) check(ip, scope string, maxReq int, window time.Duration) bool {
	rl.mu.Lock()
	defer rl.mu.Unlock()

	key := ip + ":" + scope
	b, ok := rl.buckets[key]
	if !ok {
		b = &rateBucket{}
		rl.buckets[key] = b
	}

	now := time.Now()
	cutoff := now.Add(-window)

	// trim old entries
	i := 0
	for i < len(b.hits) && b.hits[i].Before(cutoff) {
		i++
	}
	b.hits = b.hits[i:]

	if len(b.hits) >= maxReq {
		return false
	}
	b.hits = append(b.hits, now)
	return true
}

func (rl *rateLimiter) cleanup() {
	rl.mu.Lock()
	defer rl.mu.Unlock()
	now := time.Now()
	for k, b := range rl.buckets {
		if len(b.hits) == 0 || b.hits[len(b.hits)-1].Before(now.Add(-2*time.Minute)) {
			delete(rl.buckets, k)
		}
	}
}

// server entry

type serverEntry struct {
	Address       string    `json:"address"`
	Port          int       `json:"port"`
	Game          string    `json:"game"`
	Token         string    `json:"-"`
	Players       int       `json:"players"`
	MaxPlayers    int       `json:"maxPlayers"`
	Motd          string    `json:"motd,omitempty"`
	Icon          string    `json:"-"`
	Modded        bool      `json:"modded,omitempty"`
	ModpackURL    string    `json:"modpackUrl,omitempty"`
	Level         string    `json:"level,omitempty"`
	Mode          string    `json:"mode,omitempty"`
	RelayAddress  string    `json:"relayAddress,omitempty"`
	RelayKey      string    `json:"relayKey,omitempty"`
	RelayCode     string    `json:"relayCode,omitempty"`
	HasPassword   bool      `json:"hasPassword,omitempty"`
	GamePort      int       `json:"gamePort,omitempty"`
	VpnType       string    `json:"vpnType,omitempty"`
	VpnNetwork    string    `json:"vpnNetwork,omitempty"`
	VpnPassword   string    `json:"vpnPassword,omitempty"`
	HeartbeatIP   string    `json:"-"`
	LastHeartbeat time.Time `json:"-"`
}

func (s *serverEntry) key() string {
	return fmt.Sprintf("%s:%d", s.Address, s.Port)
}

func (s *serverEntry) toLite() map[string]any {
	d := map[string]any{
		"address":    s.Address,
		"port":       s.Port,
		"game":       s.Game,
		"players":    s.Players,
		"maxPlayers": s.MaxPlayers,
	}
	if s.Motd != "" {
		d["motd"] = s.Motd
	}
	if s.Modded {
		d["modded"] = true
	}
	if s.ModpackURL != "" {
		d["modpackUrl"] = s.ModpackURL
	}
	if s.Level != "" {
		d["level"] = s.Level
	}
	if s.Mode != "" {
		d["mode"] = s.Mode
	}
	if s.Icon != "" {
		d["hasIcon"] = true
	}
	if s.RelayAddress != "" {
		d["relayAddress"] = s.RelayAddress
	}
	if s.RelayKey != "" {
		d["relayKey"] = s.RelayKey
	}
	if s.RelayCode != "" {
		d["relayCode"] = s.RelayCode
	}
	if s.HasPassword {
		d["hasPassword"] = true
	}
	if s.GamePort > 0 {
		d["gamePort"] = s.GamePort
	}
	if s.VpnType != "" {
		d["vpnType"] = s.VpnType
		d["vpnNetwork"] = s.VpnNetwork
		d["vpnPassword"] = s.VpnPassword
	}
	return d
}

// master state

type masterState struct {
	mu              sync.RWMutex
	servers         map[string]*serverEntry
	rl              *rateLimiter
	listCache       []byte
	listCacheTime   time.Time
	db              *sql.DB
	bannedServerIPs map[string]bool
	pinnedServers   map[string]bool
	behindProxy     bool
	modSecret       string
	eaJWKS          *ea.JWKSCache
	ticketSecret    []byte
	signingKey      ed25519.PrivateKey
	hwidSalt        []byte
}

func newState(db *sql.DB, behindProxy bool, modSecret string, jwks *ea.JWKSCache, ticketSecret []byte) *masterState {
	return &masterState{
		servers:         make(map[string]*serverEntry),
		rl:              newRateLimiter(),
		db:              db,
		bannedServerIPs: make(map[string]bool),
		pinnedServers:   make(map[string]bool),
		behindProxy:     behindProxy,
		modSecret:       modSecret,
		eaJWKS:          jwks,
		ticketSecret:    ticketSecret,
	}
}

func (s *masterState) getCachedList() []byte {
	s.mu.RLock()
	if s.listCache != nil && time.Since(s.listCacheTime) < listCacheTTL {
		c := s.listCache
		s.mu.RUnlock()
		return c
	}
	s.mu.RUnlock()

	s.mu.Lock()
	defer s.mu.Unlock()

	// double-check after acquiring write lock
	if s.listCache != nil && time.Since(s.listCacheTime) < listCacheTTL {
		return s.listCache
	}

	var list []map[string]any
	for _, v := range s.servers {
		if s.bannedServerIPs[v.Address] || s.bannedServerIPs[v.HeartbeatIP] {
			continue
		}
		entry := v.toLite()
		key := v.Address + ":" + fmt.Sprintf("%d", v.Port)
		if s.pinnedServers[key] {
			entry["pinned"] = true
		}
		list = append(list, entry)
	}
	if list == nil {
		list = []map[string]any{}
	}
	data, _ := json.Marshal(map[string]any{"servers": list})
	s.listCache = data
	s.listCacheTime = time.Now()
	return s.listCache
}

func (s *masterState) invalidateCache() {
	s.listCache = nil
}

// helpers

func getRealIP(r *http.Request, behindProxy bool) string {
	if behindProxy {
		if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
			return strings.TrimSpace(strings.SplitN(xff, ",", 2)[0])
		}
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func isPrivateIP(ip string) bool {
	return privateIPRe.MatchString(ip)
}

func jsonResp(w http.ResponseWriter, status int, data any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}

func errResp(w http.ResponseWriter, status int, msg string) {
	jsonResp(w, status, map[string]string{"error": msg})
}

func generateToken(n int) string {
	b := make([]byte, n)
	rand.Read(b)
	return hex.EncodeToString(b)
}

func clampInt(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func truncStr(s string, max int) string {
	if len(s) > max {
		return s[:max]
	}
	return s
}

// accept only http/https URLs; returns "" for anything else.
func sanitizeURL(s string, max int) string {
	s = strings.TrimSpace(s)
	if len(s) > max {
		s = s[:max]
	}
	low := strings.ToLower(s)
	if strings.HasPrefix(low, "http://") || strings.HasPrefix(low, "https://") {
		return s
	}
	return ""
}

func getOrCreateSecret(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err == nil {
		return strings.TrimSpace(string(data)), nil
	}
	secret := generateToken(48)
	if err := os.WriteFile(path, []byte(secret), 0600); err != nil {
		return "", fmt.Errorf("failed to write secret file: %w", err)
	}
	fmt.Printf("[!] generated moderator secret. keep this safe: %s\n", secret)
	fmt.Printf("[!] saved to %s\n", path)
	return secret, nil
}

// database

func initDB(path string) (*sql.DB, error) {
	db, err := sql.Open("sqlite3", path+"?_journal_mode=WAL&_busy_timeout=5000")
	if err != nil {
		return nil, err
	}

	// run table creation first (without indexes that depend on migrated columns)
	tableSchema := `
	CREATE TABLE IF NOT EXISTS moderators (
		ea_pid TEXT PRIMARY KEY,
		display_name TEXT NOT NULL DEFAULT '',
		added_by TEXT NOT NULL DEFAULT '',
		created_at REAL NOT NULL
	);
	CREATE TABLE IF NOT EXISTS mod_sessions (
		token TEXT PRIMARY KEY,
		ea_pid TEXT NOT NULL,
		created_at REAL NOT NULL,
		expires_at REAL NOT NULL,
		FOREIGN KEY (ea_pid) REFERENCES moderators(ea_pid)
	);
	CREATE TABLE IF NOT EXISTS global_bans (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		ea_pid TEXT NOT NULL DEFAULT '',
		account_id TEXT NOT NULL DEFAULT '',
		hwid TEXT NOT NULL DEFAULT '',
		components TEXT NOT NULL DEFAULT '[]',
		reason TEXT NOT NULL DEFAULT '',
		banned_by TEXT NOT NULL,
		created_at REAL NOT NULL
	);
	CREATE TABLE IF NOT EXISTS banned_servers (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		ip TEXT UNIQUE NOT NULL,
		reason TEXT NOT NULL DEFAULT '',
		banned_by TEXT NOT NULL,
		created_at REAL NOT NULL
	);
	CREATE TABLE IF NOT EXISTS pinned_servers (
		address TEXT PRIMARY KEY,
		pinned_by TEXT NOT NULL,
		created_at REAL NOT NULL
	);
	CREATE TABLE IF NOT EXISTS ip_blacklist (
		ip TEXT PRIMARY KEY,
		fail_count INTEGER NOT NULL DEFAULT 0,
		first_fail_at REAL NOT NULL,
		blacklisted_at REAL
	);
	CREATE TABLE IF NOT EXISTS audit_log (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		action TEXT NOT NULL,
		mod_username TEXT,
		details TEXT,
		ip TEXT,
		created_at REAL NOT NULL
	);`

	_, err = db.Exec(tableSchema)
	if err != nil {
		return nil, err
	}

	// migrations for existing dbs
	db.Exec("ALTER TABLE global_bans ADD COLUMN ea_pid TEXT NOT NULL DEFAULT ''")
	db.Exec("ALTER TABLE global_bans ADD COLUMN account_id TEXT NOT NULL DEFAULT ''")

	// indexes (safe now that columns exist)
	db.Exec("CREATE INDEX IF NOT EXISTS idx_global_bans_ea_pid ON global_bans(ea_pid)")
	db.Exec("CREATE INDEX IF NOT EXISTS idx_global_bans_account_id ON global_bans(account_id)")

	return db, nil
}

func loadBannedServerIPs(db *sql.DB) (map[string]bool, error) {
	rows, err := db.Query("SELECT ip FROM banned_servers")
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	m := make(map[string]bool)
	for rows.Next() {
		var ip string
		rows.Scan(&ip)
		m[ip] = true
	}
	return m, nil
}

func loadPinnedServers(db *sql.DB) (map[string]bool, error) {
	rows, err := db.Query("SELECT address FROM pinned_servers")
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	m := make(map[string]bool)
	for rows.Next() {
		var addr string
		rows.Scan(&addr)
		m[addr] = true
	}
	return m, nil
}

func auditLog(db *sql.DB, action, modUser, details, ip string) {
	db.Exec("INSERT INTO audit_log (action, mod_username, details, ip, created_at) VALUES (?, ?, ?, ?, ?)",
		action, modUser, details, ip, float64(time.Now().Unix()))
}

func checkIPBlacklisted(db *sql.DB, ip string) bool {
	var blacklistedAt sql.NullFloat64
	err := db.QueryRow("SELECT blacklisted_at FROM ip_blacklist WHERE ip = ?", ip).Scan(&blacklistedAt)
	if err != nil || !blacklistedAt.Valid {
		return false
	}
	if time.Since(time.Unix(int64(blacklistedAt.Float64), 0)) > blacklistDur {
		db.Exec("DELETE FROM ip_blacklist WHERE ip = ?", ip)
		return false
	}
	return true
}

func recordLoginFailure(db *sql.DB, ip string) {
	now := float64(time.Now().Unix())
	var failCount int
	var firstFail float64
	err := db.QueryRow("SELECT fail_count, first_fail_at FROM ip_blacklist WHERE ip = ?", ip).Scan(&failCount, &firstFail)
	if err != nil {
		db.Exec("INSERT INTO ip_blacklist (ip, fail_count, first_fail_at) VALUES (?, 1, ?)", ip, now)
		return
	}
	if now-firstFail > 900 {
		db.Exec("UPDATE ip_blacklist SET fail_count = 1, first_fail_at = ?, blacklisted_at = NULL WHERE ip = ?", now, ip)
		return
	}
	failCount++
	if failCount >= 25 {
		db.Exec("UPDATE ip_blacklist SET fail_count = ?, blacklisted_at = ? WHERE ip = ?", failCount, now, ip)
		auditLog(db, "ip_blacklisted", "", "too many login failures", ip)
	} else {
		db.Exec("UPDATE ip_blacklist SET fail_count = ? WHERE ip = ?", failCount, ip)
	}
}

func validateModToken(db *sql.DB, r *http.Request) (eaPid string, displayName string, token string, ok bool) {
	auth := r.Header.Get("Authorization")
	if !strings.HasPrefix(auth, "Bearer ") {
		return
	}
	token = auth[7:]
	now := float64(time.Now().Unix())
	err := db.QueryRow(
		"SELECT s.ea_pid, m.display_name FROM mod_sessions s JOIN moderators m ON s.ea_pid = m.ea_pid WHERE s.token = ? AND s.expires_at > ?",
		token, now,
	).Scan(&eaPid, &displayName)
	if err != nil {
		return "", "", "", false
	}
	ok = true
	return
}

// json body helper
func readJSON(r *http.Request, maxSize int64) (map[string]any, error) {
	if r.ContentLength > maxSize {
		return nil, fmt.Errorf("request body too large")
	}
	r.Body = http.MaxBytesReader(nil, r.Body, maxSize)
	var data map[string]any
	if err := json.NewDecoder(r.Body).Decode(&data); err != nil {
		return nil, err
	}
	return data, nil
}

func getString(m map[string]any, key string) string {
	v, _ := m[key].(string)
	return v
}

func getInt(m map[string]any, key string, def int) int {
	switch v := m[key].(type) {
	case float64:
		return int(v)
	case int:
		return v
	}
	return def
}

func getBool(m map[string]any, key string) bool {
	v, _ := m[key].(bool)
	return v
}

// handlers

func (s *masterState) handleServers(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "servers", 10, 10*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}
	body := s.getCachedList()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Write(body)
}

func (s *masterState) handleIcon(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "icon", 60, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}
	key := r.URL.Query().Get("key")
	s.mu.RLock()
	entry := s.servers[key]
	s.mu.RUnlock()
	if entry == nil || entry.Icon == "" {
		errResp(w, 404, "No icon")
		return
	}
	jsonResp(w, 200, map[string]string{"icon": entry.Icon})
}

func (s *masterState) handleHealth(w http.ResponseWriter, r *http.Request) {
	s.mu.RLock()
	n := len(s.servers)
	s.mu.RUnlock()
	jsonResp(w, 200, map[string]any{"status": "ok", "servers": n})
}

func (s *masterState) handleHeartbeat(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "heartbeat", 6, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	game := getString(data, "game")
	if game != "GW1" && game != "GW2" && game != "BFN" {
		errResp(w, 400, "Invalid game")
		return
	}

	port := getInt(data, "port", 0)
	if port <= 0 {
		errResp(w, 400, "Invalid port")
		return
	}

	addr := strings.SplitN(strings.TrimSpace(getString(data, "address")), "/", 2)[0]
	if addr == "" || isPrivateIP(addr) {
		addr = ip
	}

	key := fmt.Sprintf("%s:%d", addr, port)

	s.mu.Lock()
	defer s.mu.Unlock()

	existing := s.servers[key]
	if existing != nil {
		token := getString(data, "token")
		if token != existing.Token {
			errResp(w, 403, "Invalid token")
			return
		}

		existing.MaxPlayers = clampInt(getInt(data, "maxPlayers", existing.MaxPlayers), 1, 100)
		existing.Players = clampInt(getInt(data, "players", existing.Players), 0, existing.MaxPlayers)
		existing.Motd = truncStr(getString(data, "motd"), 256)
		existing.Modded = getBool(data, "modded")
		existing.ModpackURL = sanitizeURL(getString(data, "modpackUrl"), 512)
		existing.Level = truncStr(getString(data, "level"), 128)
		existing.Mode = truncStr(getString(data, "mode"), 128)
		existing.RelayAddress = truncStr(getString(data, "relayAddress"), 256)
		existing.RelayKey = truncStr(getString(data, "relayKey"), 256)
		existing.RelayCode = truncStr(getString(data, "relayCode"), 16)
		existing.HasPassword = getBool(data, "hasPassword")
		existing.GamePort = getInt(data, "gamePort", existing.GamePort)
		existing.VpnType = truncStr(getString(data, "vpnType"), 64)
		existing.VpnNetwork = truncStr(getString(data, "vpnNetwork"), 128)
		existing.VpnPassword = truncStr(getString(data, "vpnPassword"), 128)
		existing.HeartbeatIP = ip
		existing.LastHeartbeat = time.Now()

		if icon := getString(data, "icon"); icon != "" {
			existing.Icon = truncStr(icon, iconMaxB64)
		}

		s.invalidateCache()
		jsonResp(w, 200, map[string]any{"ok": true, "key": key, "token": existing.Token})
		return
	}

	if len(s.servers) >= maxServers {
		errResp(w, 503, "Server list full")
		return
	}

	token := generateToken(32)
	maxP := clampInt(getInt(data, "maxPlayers", 24), 1, 100)
	entry := &serverEntry{
		Address:       addr,
		Port:          port,
		Game:          game,
		Token:         token,
		Players:       clampInt(getInt(data, "players", 0), 0, maxP),
		MaxPlayers:    maxP,
		Motd:          truncStr(getString(data, "motd"), 256),
		Icon:          truncStr(getString(data, "icon"), iconMaxB64),
		Modded:        getBool(data, "modded"),
		ModpackURL:    sanitizeURL(getString(data, "modpackUrl"), 512),
		Level:         truncStr(getString(data, "level"), 128),
		Mode:          truncStr(getString(data, "mode"), 128),
		RelayAddress:  truncStr(getString(data, "relayAddress"), 256),
		RelayKey:      truncStr(getString(data, "relayKey"), 256),
		RelayCode:     truncStr(getString(data, "relayCode"), 16),
		HasPassword:   getBool(data, "hasPassword"),
		GamePort:      getInt(data, "gamePort", 0),
		VpnType:       truncStr(getString(data, "vpnType"), 64),
		VpnNetwork:    truncStr(getString(data, "vpnNetwork"), 128),
		VpnPassword:   truncStr(getString(data, "vpnPassword"), 128),
		HeartbeatIP:   ip,
		LastHeartbeat: time.Now(),
	}
	s.servers[key] = entry
	s.invalidateCache()
	log.Printf("[+] new server: %s (%s)", key, game)
	jsonResp(w, 200, map[string]any{"ok": true, "key": key, "token": token})
}

func (s *masterState) handleDeregister(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "deregister", 10, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}
	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	addr := strings.TrimSpace(getString(data, "address"))
	if addr == "" || isPrivateIP(addr) {
		addr = ip
	}
	port := getInt(data, "port", 0)
	token := getString(data, "token")
	key := fmt.Sprintf("%s:%d", addr, port)

	s.mu.Lock()
	defer s.mu.Unlock()

	existing := s.servers[key]
	if existing == nil {
		jsonResp(w, 200, map[string]any{"ok": true})
		return
	}
	if token != existing.Token {
		errResp(w, 403, "Invalid token")
		return
	}
	delete(s.servers, key)
	s.invalidateCache()
	log.Printf("[-] deregistered: %s", key)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// add a mod by ea pid (secret-protected)
func (s *masterState) handleModAdd(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if checkIPBlacklisted(s.db, ip) {
		errResp(w, 403, "IP blacklisted")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	eaPid := strings.TrimSpace(getString(data, "ea_pid"))
	displayName := strings.TrimSpace(getString(data, "display_name"))
	secret := getString(data, "secret")

	if eaPid == "" {
		errResp(w, 400, "ea_pid required")
		return
	}

	expectedHash := sha256.Sum256([]byte(s.modSecret))
	givenHash := sha256.Sum256([]byte(secret))
	if subtle.ConstantTimeCompare(expectedHash[:], givenHash[:]) != 1 {
		recordLoginFailure(s.db, ip)
		errResp(w, 403, "Invalid secret")
		return
	}

	_, err = s.db.Exec(
		"INSERT OR IGNORE INTO moderators (ea_pid, display_name, added_by, created_at) VALUES (?, ?, ?, ?)",
		eaPid, displayName, "operator", float64(time.Now().Unix()))
	if err != nil {
		errResp(w, 500, "Internal error")
		return
	}
	auditLog(s.db, "mod_add", eaPid, displayName, ip)
	log.Printf("[mod] added moderator: %s (%s)", eaPid, displayName)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// remove a mod by ea pid (secret-protected)
func (s *masterState) handleModRemove(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if checkIPBlacklisted(s.db, ip) {
		errResp(w, 403, "IP blacklisted")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	eaPid := strings.TrimSpace(getString(data, "ea_pid"))
	secret := getString(data, "secret")

	if eaPid == "" {
		errResp(w, 400, "ea_pid required")
		return
	}

	expectedHash := sha256.Sum256([]byte(s.modSecret))
	givenHash := sha256.Sum256([]byte(secret))
	if subtle.ConstantTimeCompare(expectedHash[:], givenHash[:]) != 1 {
		recordLoginFailure(s.db, ip)
		errResp(w, 403, "Invalid secret")
		return
	}

	// revoke all sessions for this mod
	s.db.Exec("DELETE FROM mod_sessions WHERE ea_pid = ?", eaPid)
	s.db.Exec("DELETE FROM moderators WHERE ea_pid = ?", eaPid)
	auditLog(s.db, "mod_remove", eaPid, "", ip)
	log.Printf("[mod] removed moderator: %s", eaPid)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// mod login via ea session, validates player session, checks if pid is whitelisted
func (s *masterState) handleModLogin(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if checkIPBlacklisted(s.db, ip) {
		errResp(w, 403, "IP blacklisted")
		return
	}

	if !s.rl.check(ip, "mod_login", rateLoginMax, rateLoginWindow) {
		errResp(w, 429, "Rate limited")
		return
	}

	// validate ea session from authorization header
	eaPid, eaName, sessionOk := s.validatePlayerSession(r)
	if !sessionOk {
		errResp(w, 401, "Valid EA session required")
		return
	}

	// check if this pid is a whitelisted moderator
	var displayName string
	err := s.db.QueryRow("SELECT display_name FROM moderators WHERE ea_pid = ?", eaPid).Scan(&displayName)
	if err != nil {
		errResp(w, 403, "Not a moderator")
		return
	}

	// update display name from ea if it was empty
	if displayName == "" && eaName != "" {
		displayName = eaName
		s.db.Exec("UPDATE moderators SET display_name = ? WHERE ea_pid = ?", displayName, eaPid)
	}

	token := generateToken(48)
	now := float64(time.Now().Unix())
	expiresAt := now + modTokenExpiry.Seconds()
	s.db.Exec("INSERT INTO mod_sessions (token, ea_pid, created_at, expires_at) VALUES (?, ?, ?, ?)",
		token, eaPid, now, expiresAt)
	auditLog(s.db, "mod_login", displayName, eaPid, ip)
	jsonResp(w, 200, map[string]any{"ok": true, "token": token, "username": displayName, "expires_at": expiresAt})
}

func (s *masterState) handleModMe(w http.ResponseWriter, r *http.Request) {
	eaPid, displayName, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}
	jsonResp(w, 200, map[string]any{"ok": true, "username": displayName, "ea_pid": eaPid})
}

func (s *masterState) handleModVerify(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "mod_verify", 30, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}
	nonce := r.URL.Query().Get("nonce")
	sig := r.URL.Query().Get("sig")
	if nonce == "" || sig == "" {
		jsonResp(w, 200, map[string]any{"ok": false})
		return
	}
	now := float64(time.Now().Unix())
	rows, err := s.db.Query(
		"SELECT s.token, m.display_name FROM mod_sessions s JOIN moderators m ON s.ea_pid = m.ea_pid WHERE s.expires_at > ?", now)
	if err != nil {
		jsonResp(w, 200, map[string]any{"ok": false})
		return
	}
	defer rows.Close()
	for rows.Next() {
		var token, displayName string
		rows.Scan(&token, &displayName)
		mac := hmac.New(sha256.New, []byte(token))
		mac.Write([]byte(nonce))
		expected := hex.EncodeToString(mac.Sum(nil))
		if hmac.Equal([]byte(expected), []byte(sig)) {
			jsonResp(w, 200, map[string]any{"ok": true, "username": displayName})
			return
		}
	}
	jsonResp(w, 200, map[string]any{"ok": false})
}

func (s *masterState) handleModLogout(w http.ResponseWriter, r *http.Request) {
	_, _, token, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}
	s.db.Exec("DELETE FROM mod_sessions WHERE token = ?", token)
	jsonResp(w, 200, map[string]any{"ok": true})
}

func (s *masterState) handleGlobalBan(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	eaPid := strings.TrimSpace(getString(data, "ea_pid"))
	accountId := strings.TrimSpace(getString(data, "account_id"))
	hwid := strings.TrimSpace(getString(data, "hwid"))
	reason := truncStr(getString(data, "reason"), 512)

	var components []string
	if raw, ok := data["components"].([]any); ok {
		for _, c := range raw {
			if cs, ok := c.(string); ok {
				components = append(components, truncStr(cs, 128))
			}
			if len(components) >= 64 {
				break
			}
		}
	}

	if hwid == "" && len(components) > 0 {
		hwid = components[0]
	}
	if hwid == "" && eaPid == "" && accountId == "" {
		errResp(w, 400, "Need at least one identifier (ea_pid, account_id, hwid, or component)")
		return
	}

	compsJSON, _ := json.Marshal(components)
	now := float64(time.Now().Unix())
	s.db.Exec("INSERT INTO global_bans (ea_pid, account_id, hwid, components, reason, banned_by, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
		eaPid, accountId, hwid, string(compsJSON), reason, username, now)

	// also flag in players table so EA auth login rejects them
	if eaPid != "" {
		s.db.Exec("UPDATE players SET banned = 1, ban_reason = ?, banned_by = ? WHERE pid = ?", reason, username, eaPid)
		s.db.Exec("DELETE FROM player_sessions WHERE pid = ?", eaPid)
	}

	auditLog(s.db, "global_ban", username, fmt.Sprintf("pid=%s account=%s hwid=%s reason=%s", eaPid, accountId, truncStr(hwid, 16), reason), ip)
	log.Printf("[mod] %s globally banned pid=%s account=%s hwid=%s...", username, eaPid, accountId, truncStr(hwid, 16))
	jsonResp(w, 200, map[string]any{"ok": true})
}

func (s *masterState) handleGlobalUnban(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	banID := getInt(data, "id", 0)

	// look up the ea_pid before deleting so we can clear players.banned
	var eaPid string
	s.db.QueryRow("SELECT ea_pid FROM global_bans WHERE id = ?", banID).Scan(&eaPid)

	s.db.Exec("DELETE FROM global_bans WHERE id = ?", banID)

	if eaPid != "" {
		// only clear if no other bans remain for this pid
		var remaining int
		s.db.QueryRow("SELECT COUNT(*) FROM global_bans WHERE ea_pid = ?", eaPid).Scan(&remaining)
		if remaining == 0 {
			s.db.Exec("UPDATE players SET banned = 0, ban_reason = '', banned_by = '' WHERE pid = ?", eaPid)
		}
	}

	auditLog(s.db, "global_unban", username, fmt.Sprintf("ban_id=%d", banID), ip)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// POST /mod/ban-by-pid, ban by EA PID, auto-looks up all identifiers
func (s *masterState) handleBanByPid(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	pid := strings.TrimSpace(getString(data, "pid"))
	reason := truncStr(getString(data, "reason"), 512)
	if pid == "" {
		errResp(w, 400, "PID required")
		return
	}

	// look up account_id and hwid_hash from accounts table via ea_pid
	var accountId, hwidHash string
	s.db.QueryRow("SELECT account_id, hwid_hash FROM accounts WHERE ea_pid = ?", pid).Scan(&accountId, &hwidHash)

	now := float64(time.Now().Unix())

	s.db.Exec("INSERT INTO global_bans (ea_pid, account_id, hwid, components, reason, banned_by, created_at) VALUES (?, ?, ?, '[]', ?, ?, ?)",
		pid, accountId, hwidHash, reason, username, now)

	// also flag in players table so EA auth rejects them
	s.db.Exec("UPDATE players SET banned = 1, ban_reason = ?, banned_by = ? WHERE pid = ?", reason, username, pid)
	s.db.Exec("DELETE FROM player_sessions WHERE pid = ?", pid)

	auditLog(s.db, "ban_by_pid", username, fmt.Sprintf("pid=%s account=%s reason=%s", pid, accountId, reason), ip)
	log.Printf("[mod] %s banned by PID %s (account=%s)", username, pid, accountId)
	jsonResp(w, 200, map[string]any{"ok": true, "account_id": accountId})
}

func (s *masterState) handleGlobalBansList(w http.ResponseWriter, r *http.Request) {
	_, _, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	rows, err := s.db.Query("SELECT id, ea_pid, account_id, hwid, components, reason, banned_by, created_at FROM global_bans ORDER BY created_at DESC")
	if err != nil {
		errResp(w, 500, "Database error")
		return
	}
	defer rows.Close()

	var bans []map[string]any
	for rows.Next() {
		var id int
		var eaPid, accountId, hwid, comps, reason, bannedBy string
		var createdAt float64
		rows.Scan(&id, &eaPid, &accountId, &hwid, &comps, &reason, &bannedBy, &createdAt)
		var components []string
		json.Unmarshal([]byte(comps), &components)
		bans = append(bans, map[string]any{
			"id": id, "ea_pid": eaPid, "account_id": accountId, "hwid": hwid, "components": components,
			"reason": reason, "banned_by": bannedBy, "created_at": createdAt,
		})
	}
	if bans == nil {
		bans = []map[string]any{}
	}
	jsonResp(w, 200, map[string]any{"ok": true, "bans": bans})
}

func (s *masterState) handleBanCheck(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "ban_check", 120, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	hwid := strings.TrimSpace(getString(data, "hwid"))
	eaPid := strings.TrimSpace(getString(data, "ea_pid"))
	accountId := strings.TrimSpace(getString(data, "account_id"))
	var components []string
	if raw, ok := data["components"].([]any); ok {
		for _, c := range raw {
			if cs, ok := c.(string); ok {
				components = append(components, cs)
			}
		}
	}

	if hwid == "" && len(components) == 0 && eaPid == "" && accountId == "" {
		errResp(w, 400, "HWID, components, ea_pid, or account_id required")
		return
	}

	// check ea_pid match
	if eaPid != "" {
		var reason string
		err = s.db.QueryRow("SELECT reason FROM global_bans WHERE ea_pid = ? AND ea_pid != ''", eaPid).Scan(&reason)
		if err == nil {
			jsonResp(w, 200, map[string]any{"banned": true, "reason": reason})
			return
		}
	}

	// check account_id match
	if accountId != "" {
		var reason string
		err = s.db.QueryRow("SELECT reason FROM global_bans WHERE account_id = ? AND account_id != ''", accountId).Scan(&reason)
		if err == nil {
			jsonResp(w, 200, map[string]any{"banned": true, "reason": reason})
			return
		}
	}

	// check direct hwid match
	var reason string
	err = s.db.QueryRow("SELECT reason FROM global_bans WHERE hwid = ?", hwid).Scan(&reason)
	if err == nil {
		jsonResp(w, 200, map[string]any{"banned": true, "reason": reason})
		return
	}

	// check component overlap
	if len(components) > 0 {
		rows, err := s.db.Query("SELECT components, reason FROM global_bans WHERE components != '[]' LIMIT 10000")
		if err == nil {
			defer rows.Close()
			componentSet := make(map[string]bool)
			for _, c := range components {
				componentSet[c] = true
			}
			for rows.Next() {
				var compsJSON, banReason string
				rows.Scan(&compsJSON, &banReason)
				var banComps []string
				json.Unmarshal([]byte(compsJSON), &banComps)
				for _, bc := range banComps {
					if componentSet[bc] {
						jsonResp(w, 200, map[string]any{"banned": true, "reason": banReason})
						return
					}
				}
			}
		}
	}

	jsonResp(w, 200, map[string]any{"banned": false})
}

func (s *masterState) handleBanServer(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	serverIP := strings.TrimSpace(getString(data, "ip"))
	reason := truncStr(getString(data, "reason"), 512)
	if serverIP == "" {
		errResp(w, 400, "Server IP required")
		return
	}

	_, err = s.db.Exec("INSERT INTO banned_servers (ip, reason, banned_by, created_at) VALUES (?, ?, ?, ?)",
		serverIP, reason, username, float64(time.Now().Unix()))
	if err != nil {
		errResp(w, 409, "Server already banned")
		return
	}

	s.mu.Lock()
	s.bannedServerIPs[serverIP] = true
	s.invalidateCache()
	s.mu.Unlock()
	auditLog(s.db, "server_ban", username, fmt.Sprintf("ip=%s reason=%s", serverIP, reason), ip)
	log.Printf("[mod] %s banned server %s", username, serverIP)
	jsonResp(w, 200, map[string]any{"ok": true})
}

func (s *masterState) handleBanServerByKey(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	serverKey := strings.TrimSpace(getString(data, "key"))
	reason := truncStr(getString(data, "reason"), 512)
	if serverKey == "" {
		errResp(w, 400, "Server key required")
		return
	}

	s.mu.RLock()
	entry := s.servers[serverKey]
	s.mu.RUnlock()
	if entry == nil {
		errResp(w, 404, "Server not found in browser")
		return
	}

	banIP := entry.HeartbeatIP
	if banIP == "" {
		banIP = entry.Address
	}
	if banIP == "" {
		errResp(w, 400, "Could not resolve server IP")
		return
	}

	_, err = s.db.Exec("INSERT INTO banned_servers (ip, reason, banned_by, created_at) VALUES (?, ?, ?, ?)",
		banIP, reason, username, float64(time.Now().Unix()))
	if err != nil {
		errResp(w, 409, "Server already banned")
		return
	}

	s.mu.Lock()
	s.bannedServerIPs[banIP] = true
	s.invalidateCache()
	s.mu.Unlock()
	auditLog(s.db, "server_ban", username, fmt.Sprintf("key=%s ip=%s reason=%s", serverKey, banIP, reason), ip)
	log.Printf("[mod] %s banned server %s (ip=%s)", username, serverKey, banIP)
	jsonResp(w, 200, map[string]any{"ok": true, "ip": banIP})
}

func (s *masterState) handleUnbanServer(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	serverIP := strings.TrimSpace(getString(data, "ip"))
	if serverIP == "" {
		errResp(w, 400, "Server IP required")
		return
	}

	s.db.Exec("DELETE FROM banned_servers WHERE ip = ?", serverIP)
	s.mu.Lock()
	delete(s.bannedServerIPs, serverIP)
	s.invalidateCache()
	s.mu.Unlock()
	auditLog(s.db, "server_unban", username, fmt.Sprintf("ip=%s", serverIP), ip)
	jsonResp(w, 200, map[string]any{"ok": true})
}

func (s *masterState) handleBannedServersList(w http.ResponseWriter, r *http.Request) {
	_, _, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	rows, err := s.db.Query("SELECT id, ip, reason, banned_by, created_at FROM banned_servers ORDER BY created_at DESC")
	if err != nil {
		errResp(w, 500, "Database error")
		return
	}
	defer rows.Close()

	var servers []map[string]any
	for rows.Next() {
		var id int
		var ip, reason, bannedBy string
		var createdAt float64
		rows.Scan(&id, &ip, &reason, &bannedBy, &createdAt)
		servers = append(servers, map[string]any{
			"id": id, "ip": ip, "reason": reason, "banned_by": bannedBy, "created_at": createdAt,
		})
	}
	if servers == nil {
		servers = []map[string]any{}
	}
	jsonResp(w, 200, map[string]any{"ok": true, "servers": servers})
}

func (s *masterState) handlePinServer(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	address := strings.TrimSpace(getString(data, "address"))
	if address == "" {
		errResp(w, 400, "Server address required")
		return
	}

	_, err = s.db.Exec("INSERT OR IGNORE INTO pinned_servers (address, pinned_by, created_at) VALUES (?, ?, ?)",
		address, username, float64(time.Now().Unix()))
	if err != nil {
		errResp(w, 500, "Database error")
		return
	}

	s.mu.Lock()
	s.pinnedServers[address] = true
	s.invalidateCache()
	s.mu.Unlock()
	auditLog(s.db, "pin_server", username, fmt.Sprintf("address=%s", address), ip)
	log.Printf("[mod] %s pinned server %s", username, address)
	jsonResp(w, 200, map[string]any{"ok": true})
}

func (s *masterState) handleUnpinServer(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, username, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	address := strings.TrimSpace(getString(data, "address"))
	if address == "" {
		errResp(w, 400, "Server address required")
		return
	}

	s.db.Exec("DELETE FROM pinned_servers WHERE address = ?", address)

	s.mu.Lock()
	delete(s.pinnedServers, address)
	s.invalidateCache()
	s.mu.Unlock()
	auditLog(s.db, "unpin_server", username, fmt.Sprintf("address=%s", address), ip)
	log.Printf("[mod] %s unpinned server %s", username, address)
	jsonResp(w, 200, map[string]any{"ok": true})
}

func (s *masterState) handleOptions(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
	w.WriteHeader(204)
}

// cleanup loop

func (s *masterState) cleanupLoop() {
	ticker := time.NewTicker(cleanupInterval)
	for range ticker.C {
		now := time.Now()
		s.mu.Lock()
		var stale []string
		for k, v := range s.servers {
			if now.Sub(v.LastHeartbeat) > staleTimeout {
				stale = append(stale, k)
			}
		}
		for _, k := range stale {
			delete(s.servers, k)
		}
		if len(stale) > 0 {
			s.invalidateCache()
			log.Printf("[cleanup] removed %d stale server(s), %d active", len(stale), len(s.servers))
		}
		s.mu.Unlock()

		s.rl.cleanup()

		nowF := float64(now.Unix())
		s.db.Exec("DELETE FROM mod_sessions WHERE expires_at < ?", nowF)
		s.db.Exec("DELETE FROM player_sessions WHERE expires_at < ?", nowF)
		s.db.Exec("DELETE FROM ip_blacklist WHERE blacklisted_at IS NOT NULL AND blacklisted_at < ?", nowF-blacklistDur.Seconds())
	}
}

// cors middleware wrapping method router
func methodRouter(get, post http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			if get != nil {
				get(w, r)
			} else {
				http.NotFound(w, r)
			}
		case http.MethodPost:
			if post != nil {
				post(w, r)
			} else {
				http.NotFound(w, r)
			}
		case http.MethodOptions:
			w.Header().Set("Access-Control-Allow-Origin", "*")
			w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
			w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
			w.WriteHeader(204)
		default:
			http.NotFound(w, r)
		}
	}
}

// Run starts the master server
func Run(cfg Config) error {
	modSecret, err := getOrCreateSecret(cfg.SecretFile)
	if err != nil {
		return err
	}

	db, err := initDB(cfg.DBFile)
	if err != nil {
		return fmt.Errorf("failed to init db: %w", err)
	}
	defer db.Close()

	if err := initAuthDB(db); err != nil {
		return fmt.Errorf("failed to init auth db: %w", err)
	}

	if err := initIdentityDB(db); err != nil {
		return fmt.Errorf("failed to init identity db: %w", err)
	}

	signingKey, err := loadOrCreateSigningKey("signing_key.bin")
	if err != nil {
		return fmt.Errorf("failed to init signing key: %w", err)
	}

	hwidSalt, err := loadOrCreateHWIDSalt("hwid_salt.bin")
	if err != nil {
		return fmt.Errorf("failed to init hwid salt: %w", err)
	}

	banned, err := loadBannedServerIPs(db)
	if err != nil {
		return fmt.Errorf("failed to load banned servers: %w", err)
	}

	jwks := ea.NewJWKSCache()

	ticketSecret, err := getOrCreateTicketSecret()
	if err != nil {
		return fmt.Errorf("failed to init ticket secret: %w", err)
	}

	state := newState(db, cfg.BehindProxy, modSecret, jwks, ticketSecret)
	state.bannedServerIPs = banned

	pinned, err := loadPinnedServers(db)
	if err != nil {
		return fmt.Errorf("failed to load pinned servers: %w", err)
	}
	state.pinnedServers = pinned
	state.signingKey = signingKey
	state.hwidSalt = hwidSalt

	go state.cleanupLoop()

	mux := http.NewServeMux()
	mux.HandleFunc("/servers", methodRouter(state.handleServers, nil))
	mux.HandleFunc("/icon", methodRouter(state.handleIcon, nil))
	mux.HandleFunc("/health", methodRouter(state.handleHealth, nil))
	mux.HandleFunc("/heartbeat", methodRouter(nil, state.handleHeartbeat))
	mux.HandleFunc("/deregister", methodRouter(nil, state.handleDeregister))

	mux.HandleFunc("/mod/add", methodRouter(nil, state.handleModAdd))
	mux.HandleFunc("/mod/remove", methodRouter(nil, state.handleModRemove))
	mux.HandleFunc("/mod/login", methodRouter(nil, state.handleModLogin))
	mux.HandleFunc("/mod/me", methodRouter(state.handleModMe, nil))
	mux.HandleFunc("/mod/verify", methodRouter(state.handleModVerify, nil))
	mux.HandleFunc("/mod/logout", methodRouter(nil, state.handleModLogout))

	mux.HandleFunc("/mod/global-ban", methodRouter(nil, state.handleGlobalBan))
	mux.HandleFunc("/mod/global-unban", methodRouter(nil, state.handleGlobalUnban))
	mux.HandleFunc("/mod/global-bans", methodRouter(state.handleGlobalBansList, nil))
	mux.HandleFunc("/mod/ban-by-pid", methodRouter(nil, state.handleBanByPid))

	mux.HandleFunc("/bans/check", methodRouter(nil, state.handleBanCheck))

	mux.HandleFunc("/mod/ban-server", methodRouter(nil, state.handleBanServer))
	mux.HandleFunc("/mod/ban-server-by-key", methodRouter(nil, state.handleBanServerByKey))
	mux.HandleFunc("/mod/unban-server", methodRouter(nil, state.handleUnbanServer))
	mux.HandleFunc("/mod/banned-servers", methodRouter(state.handleBannedServersList, nil))

	mux.HandleFunc("/mod/pin-server", methodRouter(nil, state.handlePinServer))
	mux.HandleFunc("/mod/unpin-server", methodRouter(nil, state.handleUnpinServer))

	// player auth
	mux.HandleFunc("/auth/login", methodRouter(nil, state.handleAuthLogin))
	mux.HandleFunc("/auth/refresh", methodRouter(nil, state.handleAuthRefresh))
	mux.HandleFunc("/auth/me", methodRouter(state.handleAuthMe, nil))
	mux.HandleFunc("/auth/ticket", methodRouter(nil, state.handleAuthTicket))
	mux.HandleFunc("/auth/logout", methodRouter(nil, state.handleAuthLogout))

	// player moderation
	mux.HandleFunc("/mod/ban-player", methodRouter(nil, state.handleBanPlayer))
	mux.HandleFunc("/mod/unban-player", methodRouter(nil, state.handleUnbanPlayer))
	mux.HandleFunc("/mod/banned-players", methodRouter(state.handleBannedPlayersList, nil))
	mux.HandleFunc("/mod/players", methodRouter(state.handlePlayersList, nil))

	// ticket validation (for game servers)
	mux.HandleFunc("/auth/validate-ticket", methodRouter(nil, state.handleValidateTicket))

	// identity (ed25519 accounts)
	mux.HandleFunc("/auth/check-identity", methodRouter(state.handleCheckIdentity, state.handleCheckIdentity))
	mux.HandleFunc("/auth/register", methodRouter(nil, state.handleAuthRegister))
	mux.HandleFunc("/auth/refresh-identity", methodRouter(nil, state.handleAuthRefreshIdentity))
	mux.HandleFunc("/auth/rebind", methodRouter(nil, state.handleAuthRebind))
	mux.HandleFunc("/auth/set-nickname", methodRouter(nil, state.handleSetNickname))
	mux.HandleFunc("/auth/pubkey", methodRouter(state.handleAuthPubkey, nil))
	mux.HandleFunc("/auth/banlist", methodRouter(state.handleAuthBanlist, nil))
	mux.HandleFunc("/mod/ban-account", methodRouter(nil, state.handleBanAccount))
	mux.HandleFunc("/mod/unban-account", methodRouter(nil, state.handleUnbanAccount))

	addr := fmt.Sprintf("%s:%d", cfg.Bind, cfg.Port)
	log.Printf("cypress master server listening on %s", addr)
	return http.ListenAndServe(addr, mux)
}
