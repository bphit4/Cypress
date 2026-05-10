package master

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"regexp"
	"strings"
	"time"

	"golang.org/x/crypto/bcrypt"
)

const (
	jwtExpiry        = 7 * 24 * time.Hour
	backupCodeCount  = 8
	backupCodeLength = 16 // bytes, hex-encoded = 32 chars
	maxUsernameLen   = 32
	minUsernameLen   = 3
	maxNicknameLen   = 32
	minNicknameLen   = 3
)

// usernames/nicknames allow letters, numbers, underscores, hyphens, spaces (no leading/trailing)
var usernameRegex = regexp.MustCompile(`^[a-zA-Z0-9][a-zA-Z0-9 _-]*[a-zA-Z0-9]$|^[a-zA-Z0-9]$`)

// db schema for identity tables
const identitySchema = `
	CREATE TABLE IF NOT EXISTS accounts (
		account_id TEXT PRIMARY KEY,
		username TEXT NOT NULL UNIQUE,
		nickname TEXT NOT NULL DEFAULT '',
		public_key TEXT NOT NULL,
		hwid_hash TEXT NOT NULL,
		ea_pid TEXT NOT NULL DEFAULT '',
		ea_name TEXT NOT NULL DEFAULT '',
		registered_ip TEXT NOT NULL DEFAULT '',
		created_at REAL NOT NULL
	);
	CREATE INDEX IF NOT EXISTS idx_accounts_username ON accounts(username);
	CREATE INDEX IF NOT EXISTS idx_accounts_hwid_hash ON accounts(hwid_hash);
	CREATE INDEX IF NOT EXISTS idx_accounts_ea_pid ON accounts(ea_pid);

	CREATE TABLE IF NOT EXISTS backup_codes (
		account_id TEXT NOT NULL,
		code_hash TEXT NOT NULL,
		used INTEGER NOT NULL DEFAULT 0,
		FOREIGN KEY (account_id) REFERENCES accounts(account_id)
	);
	CREATE INDEX IF NOT EXISTS idx_backup_codes_account ON backup_codes(account_id);

	CREATE TABLE IF NOT EXISTS nickname_history (
		account_id TEXT NOT NULL,
		nickname TEXT NOT NULL,
		set_at REAL NOT NULL,
		FOREIGN KEY (account_id) REFERENCES accounts(account_id)
	);
	CREATE INDEX IF NOT EXISTS idx_nickname_history_account ON nickname_history(account_id);
`

func initIdentityDB(db *sql.DB) error {
	_, err := db.Exec(identitySchema)
	if err != nil {
		return err
	}
	// migration: add nickname column if missing
	db.Exec("ALTER TABLE accounts ADD COLUMN nickname TEXT NOT NULL DEFAULT ''")
	return nil
}

// ed25519 keypair for signing JWTs

func loadOrCreateSigningKey(path string) (ed25519.PrivateKey, error) {
	data, err := os.ReadFile(path)
	if err == nil && len(data) == ed25519.PrivateKeySize {
		log.Printf("[identity] loaded signing key from %s", path)
		return ed25519.PrivateKey(data), nil
	}

	_, priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, fmt.Errorf("keygen failed: %w", err)
	}
	if err := os.WriteFile(path, priv, 0600); err != nil {
		return nil, fmt.Errorf("failed to write signing key: %w", err)
	}
	log.Printf("[identity] generated new signing key at %s", path)
	return priv, nil
}

// hwid salt for hashing

func loadOrCreateHWIDSalt(path string) ([]byte, error) {
	data, err := os.ReadFile(path)
	if err == nil && len(data) == 32 {
		return data, nil
	}

	salt := make([]byte, 32)
	rand.Read(salt)
	if err := os.WriteFile(path, salt, 0600); err != nil {
		return nil, fmt.Errorf("failed to write hwid salt: %w", err)
	}
	log.Printf("[identity] generated hwid salt at %s", path)
	return salt, nil
}

func hashHWID(hwid string, salt []byte) string {
	h := sha256.Sum256(append([]byte(hwid), salt...))
	return hex.EncodeToString(h[:])
}

// jwt, simple ed25519-signed tokens
// base64url(header).base64url(payload).base64url(signature)

type jwtHeader struct {
	Alg string `json:"alg"`
	Typ string `json:"typ"`
}

type JWTClaims struct {
	Sub           string `json:"sub"` // account_id
	Username      string `json:"username"`
	Nickname      string `json:"nickname,omitempty"`
	PKFingerprint string `json:"pk_fp"` // sha256 of public key
	EAPID         string `json:"ea_pid,omitempty"`
	EAName        string `json:"ea_name,omitempty"`
	Iat           int64  `json:"iat"`
	Exp           int64  `json:"exp"`
}

var jwtHeaderB64 string

func init() {
	h, _ := json.Marshal(jwtHeader{Alg: "EdDSA", Typ: "JWT"})
	jwtHeaderB64 = base64.RawURLEncoding.EncodeToString(h)
}

func issueJWT(key ed25519.PrivateKey, claims JWTClaims) string {
	payload, _ := json.Marshal(claims)
	payloadB64 := base64.RawURLEncoding.EncodeToString(payload)
	sigInput := jwtHeaderB64 + "." + payloadB64
	sig := ed25519.Sign(key, []byte(sigInput))
	return sigInput + "." + base64.RawURLEncoding.EncodeToString(sig)
}

func verifyJWT(pubKey ed25519.PublicKey, token string) (*JWTClaims, error) {
	parts := strings.SplitN(token, ".", 3)
	if len(parts) != 3 {
		return nil, fmt.Errorf("malformed jwt")
	}

	sigInput := parts[0] + "." + parts[1]
	sig, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return nil, fmt.Errorf("bad signature encoding")
	}

	if !ed25519.Verify(pubKey, []byte(sigInput), sig) {
		return nil, fmt.Errorf("invalid signature")
	}

	payloadJSON, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("bad payload encoding")
	}

	var claims JWTClaims
	if err := json.Unmarshal(payloadJSON, &claims); err != nil {
		return nil, fmt.Errorf("bad payload json")
	}

	if time.Now().Unix() > claims.Exp {
		return nil, fmt.Errorf("token expired")
	}

	return &claims, nil
}

// decode jwt payload without verifying signature, used as fallback
// when signing key rotated but user can prove identity via challenge_sig
func decodeJWTUnsafe(token string) (*JWTClaims, error) {
	parts := strings.SplitN(token, ".", 3)
	if len(parts) != 3 {
		return nil, fmt.Errorf("malformed jwt")
	}

	payloadJSON, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("bad payload encoding")
	}

	var claims JWTClaims
	if err := json.Unmarshal(payloadJSON, &claims); err != nil {
		return nil, fmt.Errorf("bad payload json")
	}

	return &claims, nil
}

func pkFingerprint(pub ed25519.PublicKey) string {
	h := sha256.Sum256(pub)
	return hex.EncodeToString(h[:16]) // first 16 bytes = 32 hex chars
}

// backup codes

func generateBackupCodes() (plaintexts []string, hashes []string, err error) {
	for i := 0; i < backupCodeCount; i++ {
		raw := make([]byte, backupCodeLength)
		rand.Read(raw)
		code := hex.EncodeToString(raw)
		hash, err := bcrypt.GenerateFromPassword([]byte(code), bcrypt.DefaultCost)
		if err != nil {
			return nil, nil, err
		}
		plaintexts = append(plaintexts, code)
		hashes = append(hashes, string(hash))
	}
	return
}

func generateAccountID() string {
	b := make([]byte, 16)
	rand.Read(b)
	// uuid v4 format
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	return fmt.Sprintf("%08x-%04x-%04x-%04x-%012x",
		b[0:4], b[4:6], b[6:8], b[8:10], b[10:16])
}

// POST /auth/check-identity, check if the current ea account has an identity
// optionally updates public key + hwid if provided
func (s *masterState) handleCheckIdentity(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "check_identity", 15, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}

	eaPID, eaName, ok := s.validatePlayerSession(r)
	if !ok {
		errResp(w, 401, "EA login required")
		return
	}

	// optionally read body for key update
	var newPubKeyHex, newHwid string
	if r.Body != nil {
		if data, err := readJSON(r, maxBodySize); err == nil {
			newPubKeyHex = getString(data, "public_key")
			newHwid = getString(data, "hwid")
		}
	}

	var accountID, username, storedPubKey, nickname string
	err := s.db.QueryRow("SELECT account_id, username, public_key, nickname FROM accounts WHERE ea_pid = ?", eaPID).Scan(&accountID, &username, &storedPubKey, &nickname)
	if err != nil {
		jsonResp(w, 200, map[string]any{"ok": true, "registered": false})
		return
	}

	// update pubkey + hwid if provided
	usePubKey := storedPubKey
	if newPubKeyHex != "" {
		if pubBytes, err := hex.DecodeString(newPubKeyHex); err == nil && len(pubBytes) == ed25519.PublicKeySize {
			usePubKey = newPubKeyHex
			s.db.Exec("UPDATE accounts SET public_key = ? WHERE account_id = ?", newPubKeyHex, accountID)
		}
	}
	if newHwid != "" {
		hwidHash := hashHWID(newHwid, s.hwidSalt)
		s.db.Exec("UPDATE accounts SET hwid_hash = ? WHERE account_id = ?", hwidHash, accountID)
	}

	// reissue jwt
	pubKeyBytes, _ := hex.DecodeString(usePubKey)
	pubKey := ed25519.PublicKey(pubKeyBytes)
	claims := JWTClaims{
		Sub:           accountID,
		Username:      username,
		Nickname:      nickname,
		PKFingerprint: pkFingerprint(pubKey),
		EAPID:         eaPID,
		EAName:        eaName,
		Iat:           time.Now().Unix(),
		Exp:           time.Now().Add(jwtExpiry).Unix(),
	}
	jwt := issueJWT(s.signingKey, claims)

	jsonResp(w, 200, map[string]any{
		"ok":         true,
		"registered": true,
		"account_id": accountID,
		"username":   username,
		"jwt":        jwt,
	})
}

// POST /auth/register, create a new account with ed25519 public key
// needs ea session via Authorization header
func (s *masterState) handleAuthRegister(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)

	// require ea session
	eaPID, eaName, sessionOk := s.validatePlayerSession(r)
	if !sessionOk {
		errResp(w, 401, "EA login required for registration")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	username := strings.TrimSpace(getString(data, "username"))
	pubKeyHex := getString(data, "public_key")
	hwid := getString(data, "hwid")

	if len(username) < minUsernameLen || len(username) > maxUsernameLen {
		errResp(w, 400, fmt.Sprintf("Username must be %d-%d characters", minUsernameLen, maxUsernameLen))
		return
	}
	if !usernameRegex.MatchString(username) {
		errResp(w, 400, "Username can only contain letters, numbers, underscores, and hyphens")
		return
	}

	// validate public key (ed25519 = 32 bytes)
	pubKeyBytes, err := hex.DecodeString(pubKeyHex)
	if err != nil || len(pubKeyBytes) != ed25519.PublicKeySize {
		errResp(w, 400, "Invalid public key — expected 64 hex chars (32 bytes Ed25519)")
		return
	}

	if hwid == "" {
		errResp(w, 400, "HWID required")
		return
	}

	hwidHash := hashHWID(hwid, s.hwidSalt)

	// if this ea account already has an identity, reissue a fresh jwt
	var existingID, existingUser, existingPubKey, existingNick string
	err = s.db.QueryRow("SELECT account_id, username, public_key, nickname FROM accounts WHERE ea_pid = ?", eaPID).Scan(&existingID, &existingUser, &existingPubKey, &existingNick)
	if err == nil {
		// update pubkey + hwid if changed
		s.db.Exec("UPDATE accounts SET public_key = ?, hwid_hash = ? WHERE account_id = ?", pubKeyHex, hwidHash, existingID)
		pubKey := ed25519.PublicKey(pubKeyBytes)
		claims := JWTClaims{
			Sub:           existingID,
			Username:      existingUser,
			Nickname:      existingNick,
			PKFingerprint: pkFingerprint(pubKey),
			EAPID:         eaPID,
			EAName:        eaName,
			Iat:           time.Now().Unix(),
			Exp:           time.Now().Add(jwtExpiry).Unix(),
		}
		jwt := issueJWT(s.signingKey, claims)
		log.Printf("[identity] re-login: %s (%s) ea=%s from %s", existingUser, existingID, eaPID, ip)
		jsonResp(w, 200, map[string]any{
			"ok":         true,
			"account_id": existingID,
			"username":   existingUser,
			"jwt":        jwt,
		})
		return
	}

	// check hwid hasn't already registered
	var existing string
	err = s.db.QueryRow("SELECT account_id FROM accounts WHERE hwid_hash = ?", hwidHash).Scan(&existing)
	if err == nil {
		errResp(w, 409, "This hardware has already registered an account")
		return
	}

	// check hwid isn't banned
	var banReason string
	err = s.db.QueryRow("SELECT reason FROM global_bans WHERE hwid = ? LIMIT 1", hwidHash).Scan(&banReason)
	if err == nil {
		errResp(w, 403, "Hardware is banned")
		return
	}

	// check username availability
	err = s.db.QueryRow("SELECT account_id FROM accounts WHERE username = ?", username).Scan(&existing)
	if err == nil {
		errResp(w, 409, "Username already taken")
		return
	}

	// only count requests that made it past the duplicate/banned checks
	if !s.rl.check(ip, "register", 2, 24*time.Hour) {
		errResp(w, 429, "Rate limited: max 2 registrations per IP per day")
		return
	}

	accountID := generateAccountID()
	now := float64(time.Now().Unix())

	// generate backup codes
	codes, codeHashes, err := generateBackupCodes()
	if err != nil {
		log.Printf("[identity] backup code generation failed: %v", err)
		errResp(w, 500, "Internal error")
		return
	}

	tx, err := s.db.Begin()
	if err != nil {
		errResp(w, 500, "Internal error")
		return
	}
	defer tx.Rollback()

	_, err = tx.Exec(
		"INSERT INTO accounts (account_id, username, public_key, hwid_hash, ea_pid, ea_name, registered_ip, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
		accountID, username, pubKeyHex, hwidHash, eaPID, eaName, ip, now,
	)
	if err != nil {
		if strings.Contains(err.Error(), "UNIQUE") {
			errResp(w, 409, "Username already taken")
			return
		}
		log.Printf("[identity] insert account failed: %v", err)
		errResp(w, 500, "Internal error")
		return
	}

	for _, h := range codeHashes {
		_, err = tx.Exec("INSERT INTO backup_codes (account_id, code_hash, used) VALUES (?, ?, 0)", accountID, h)
		if err != nil {
			log.Printf("[identity] insert backup code failed: %v", err)
			errResp(w, 500, "Internal error")
			return
		}
	}

	if err := tx.Commit(); err != nil {
		errResp(w, 500, "Internal error")
		return
	}

	// issue jwt
	pubKey := ed25519.PublicKey(pubKeyBytes)
	claims := JWTClaims{
		Sub:           accountID,
		Username:      username,
		PKFingerprint: pkFingerprint(pubKey),
		EAPID:         eaPID,
		EAName:        eaName,
		Iat:           time.Now().Unix(),
		Exp:           time.Now().Add(jwtExpiry).Unix(),
	}
	jwt := issueJWT(s.signingKey, claims)

	auditLog(s.db, "account_register", username, fmt.Sprintf("id=%s ea=%s", accountID, eaPID), ip)
	log.Printf("[identity] registered: %s (%s) ea=%s from %s", username, accountID, eaPID, ip)

	jsonResp(w, 200, map[string]any{
		"ok":           true,
		"account_id":   accountID,
		"username":     username,
		"jwt":          jwt,
		"backup_codes": codes,
	})
}

// POST /auth/refresh-identity, refresh jwt by proving private key ownership
func (s *masterState) handleAuthRefreshIdentity(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "refresh_identity", 10, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	token := getString(data, "jwt")
	challengeSig := getString(data, "challenge_sig")

	if token == "" || challengeSig == "" {
		errResp(w, 400, "jwt and challenge_sig required")
		return
	}

	// verify the existing jwt (allow expired for refresh, but only within grace period)
	// fall back to unsigned decode if signing key rotated, challenge_sig still proves identity
	pubKey := s.signingKey.Public().(ed25519.PublicKey)
	claims, err := verifyJWTForRefresh(pubKey, token)
	unsafeRefresh := false
	if err != nil {
		claims, err = decodeJWTUnsafe(token)
		if err != nil {
			errResp(w, 401, fmt.Sprintf("Invalid token: %v", err))
			return
		}
		unsafeRefresh = true
	}

	// look up account's public key
	var storedPubKeyHex string
	var eaPID, eaName, nickname string
	err = s.db.QueryRow("SELECT public_key, ea_pid, ea_name, nickname FROM accounts WHERE account_id = ?", claims.Sub).Scan(&storedPubKeyHex, &eaPID, &eaName, &nickname)
	if err != nil {
		errResp(w, 404, "Account not found")
		return
	}

	// check if banned
	var banReason string
	err = s.db.QueryRow("SELECT reason FROM global_bans WHERE account_id = ? LIMIT 1", claims.Sub).Scan(&banReason)
	if err == nil {
		errResp(w, 403, fmt.Sprintf("Account banned: %s", banReason))
		return
	}

	// verify challenge signature proves they hold the private key
	// challenge = sha256(jwt) — the client signs the hash of their current jwt
	userPubBytes, _ := hex.DecodeString(storedPubKeyHex)
	userPub := ed25519.PublicKey(userPubBytes)
	challenge := sha256.Sum256([]byte(token))
	sigBytes, err := hex.DecodeString(challengeSig)
	if err != nil || !ed25519.Verify(userPub, challenge[:], sigBytes) {
		errResp(w, 401, "Challenge signature invalid")
		return
	}

	// reissue jwt
	newClaims := JWTClaims{
		Sub:           claims.Sub,
		Username:      claims.Username,
		Nickname:      nickname,
		PKFingerprint: claims.PKFingerprint,
		EAPID:         eaPID,
		EAName:        eaName,
		Iat:           time.Now().Unix(),
		Exp:           time.Now().Add(jwtExpiry).Unix(),
	}
	jwt := issueJWT(s.signingKey, newClaims)

	refreshType := "jwt refresh"
	if unsafeRefresh {
		refreshType = "jwt refresh (key-rotated)"
	}
	log.Printf("[identity] %s: %s (%s)", refreshType, claims.Username, claims.Sub)
	jsonResp(w, 200, map[string]any{
		"ok":  true,
		"jwt": jwt,
	})
}

// allow expired tokens for refresh (grace period: 30 days)
func verifyJWTForRefresh(pubKey ed25519.PublicKey, token string) (*JWTClaims, error) {
	parts := strings.SplitN(token, ".", 3)
	if len(parts) != 3 {
		return nil, fmt.Errorf("malformed jwt")
	}

	sigInput := parts[0] + "." + parts[1]
	sig, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return nil, fmt.Errorf("bad signature encoding")
	}

	if !ed25519.Verify(pubKey, []byte(sigInput), sig) {
		return nil, fmt.Errorf("invalid signature")
	}

	payloadJSON, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("bad payload encoding")
	}

	var claims JWTClaims
	if err := json.Unmarshal(payloadJSON, &claims); err != nil {
		return nil, fmt.Errorf("bad payload json")
	}

	// allow up to 30 days past expiry for refresh
	gracePeriod := int64(30 * 24 * 60 * 60)
	if time.Now().Unix() > claims.Exp+gracePeriod {
		return nil, fmt.Errorf("token expired beyond refresh window")
	}

	return &claims, nil
}

// POST /auth/rebind, transfer account to new hardware using backup code
func (s *masterState) handleAuthRebind(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "rebind", 5, time.Hour) {
		errResp(w, 429, "Rate limited")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	username := getString(data, "username")
	backupCode := getString(data, "backup_code")
	newPubKeyHex := getString(data, "new_public_key")
	newHWID := getString(data, "new_hwid")

	if username == "" || backupCode == "" || newPubKeyHex == "" || newHWID == "" {
		errResp(w, 400, "username, backup_code, new_public_key, and new_hwid required")
		return
	}

	newPubBytes, err := hex.DecodeString(newPubKeyHex)
	if err != nil || len(newPubBytes) != ed25519.PublicKeySize {
		errResp(w, 400, "Invalid public key")
		return
	}

	// find account
	var accountID, eaPID, eaName, nickname string
	err = s.db.QueryRow("SELECT account_id, ea_pid, ea_name, nickname FROM accounts WHERE username = ?", username).Scan(&accountID, &eaPID, &eaName, &nickname)
	if err != nil {
		errResp(w, 404, "Account not found")
		return
	}

	// check if banned
	var banReason string
	err = s.db.QueryRow("SELECT reason FROM global_bans WHERE account_id = ? LIMIT 1", accountID).Scan(&banReason)
	if err == nil {
		errResp(w, 403, fmt.Sprintf("Account banned: %s", banReason))
		return
	}

	// verify backup code
	rows, err := s.db.Query("SELECT rowid, code_hash FROM backup_codes WHERE account_id = ? AND used = 0", accountID)
	if err != nil {
		errResp(w, 500, "Internal error")
		return
	}
	defer rows.Close()

	var matchedRowID int64 = -1
	for rows.Next() {
		var rowID int64
		var codeHash string
		rows.Scan(&rowID, &codeHash)
		if bcrypt.CompareHashAndPassword([]byte(codeHash), []byte(backupCode)) == nil {
			matchedRowID = rowID
			break
		}
	}
	rows.Close()

	if matchedRowID < 0 {
		errResp(w, 401, "Invalid backup code")
		return
	}

	newHWIDHash := hashHWID(newHWID, s.hwidSalt)

	// check new hwid isn't banned
	err = s.db.QueryRow("SELECT reason FROM global_bans WHERE hwid = ? LIMIT 1", newHWIDHash).Scan(&banReason)
	if err == nil {
		errResp(w, 403, "New hardware is banned")
		return
	}

	tx, err := s.db.Begin()
	if err != nil {
		errResp(w, 500, "Internal error")
		return
	}
	defer tx.Rollback()

	// mark code as used
	tx.Exec("UPDATE backup_codes SET used = 1 WHERE rowid = ?", matchedRowID)

	// rebind account to new key + hwid
	tx.Exec("UPDATE accounts SET public_key = ?, hwid_hash = ? WHERE account_id = ?",
		newPubKeyHex, newHWIDHash, accountID)

	if err := tx.Commit(); err != nil {
		errResp(w, 500, "Internal error")
		return
	}

	// issue new jwt
	newPub := ed25519.PublicKey(newPubBytes)
	claims := JWTClaims{
		Sub:           accountID,
		Username:      username,
		Nickname:      nickname,
		PKFingerprint: pkFingerprint(newPub),
		EAPID:         eaPID,
		EAName:        eaName,
		Iat:           time.Now().Unix(),
		Exp:           time.Now().Add(jwtExpiry).Unix(),
	}
	jwt := issueJWT(s.signingKey, claims)

	auditLog(s.db, "account_rebind", username, fmt.Sprintf("id=%s", accountID), ip)
	log.Printf("[identity] rebind: %s (%s) from %s", username, accountID, ip)

	jsonResp(w, 200, map[string]any{
		"ok":  true,
		"jwt": jwt,
	})
}

// GET /auth/pubkey, serve master server's ed25519 public key
func (s *masterState) handleAuthPubkey(w http.ResponseWriter, r *http.Request) {
	pub := s.signingKey.Public().(ed25519.PublicKey)
	jsonResp(w, 200, map[string]any{
		"ok":          true,
		"public_key":  hex.EncodeToString(pub),
		"fingerprint": pkFingerprint(pub),
	})
}

// GET /auth/banlist, account + hwid + ea_pid ban lists for game servers
func (s *masterState) handleAuthBanlist(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "banlist", 6, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}
	bannedAccountSet := make(map[string]bool)
	bannedHWIDSet := make(map[string]bool)
	bannedEaPidSet := make(map[string]bool)

	rows, err := s.db.Query("SELECT ea_pid, account_id, hwid FROM global_bans")
	if err == nil {
		defer rows.Close()
		for rows.Next() {
			var pid, accId, hwid string
			rows.Scan(&pid, &accId, &hwid)
			if pid != "" {
				bannedEaPidSet[pid] = true
			}
			if accId != "" {
				bannedAccountSet[accId] = true
			}
			if hwid != "" {
				bannedHWIDSet[hwid] = true
			}
		}
	}

	toSlice := func(m map[string]bool) []string {
		if len(m) == 0 {
			return []string{}
		}
		s := make([]string, 0, len(m))
		for k := range m {
			s = append(s, k)
		}
		return s
	}

	jsonResp(w, 200, map[string]any{
		"banned_accounts": toSlice(bannedAccountSet),
		"banned_hwids":    toSlice(bannedHWIDSet),
		"banned_ea_pids":  toSlice(bannedEaPidSet),
	})
}

// POST /mod/ban-account, ban a cypress account (mod only)
func (s *masterState) handleBanAccount(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, modUsername, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	accountID := getString(data, "account_id")
	reason := truncStr(getString(data, "reason"), 512)
	banHWID := false
	if v, ok := data["ban_hwid"]; ok {
		if b, ok := v.(bool); ok {
			banHWID = b
		}
	}

	if accountID == "" {
		errResp(w, 400, "account_id required")
		return
	}

	// verify account exists
	var hwidHash string
	err = s.db.QueryRow("SELECT hwid_hash FROM accounts WHERE account_id = ?", accountID).Scan(&hwidHash)
	if err != nil {
		errResp(w, 404, "Account not found")
		return
	}

	now := float64(time.Now().Unix())

	hwid := ""
	if banHWID {
		hwid = hwidHash
	}

	s.db.Exec(
		"INSERT INTO global_bans (ea_pid, account_id, hwid, components, reason, banned_by, created_at) VALUES ('', ?, ?, '[]', ?, ?, ?)",
		accountID, hwid, reason, modUsername, now,
	)

	auditLog(s.db, "account_ban", modUsername, fmt.Sprintf("id=%s reason=%s hwid_ban=%v", accountID, reason, banHWID), ip)
	log.Printf("[mod] %s banned account %s (hwid_ban=%v)", modUsername, accountID, banHWID)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// POST /mod/unban-account, unban a cypress account (mod only)
func (s *masterState) handleUnbanAccount(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, modUsername, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	accountID := getString(data, "account_id")
	unbanHWID := false
	if v, ok := data["unban_hwid"]; ok {
		if b, ok := v.(bool); ok {
			unbanHWID = b
		}
	}

	if accountID == "" {
		errResp(w, 400, "account_id required")
		return
	}

	s.db.Exec("DELETE FROM global_bans WHERE account_id = ?", accountID)

	if unbanHWID {
		var hwidHash string
		if s.db.QueryRow("SELECT hwid_hash FROM accounts WHERE account_id = ?", accountID).Scan(&hwidHash) == nil {
			s.db.Exec("DELETE FROM global_bans WHERE hwid = ? AND account_id = ''", hwidHash)
		}
	}

	auditLog(s.db, "account_unban", modUsername, fmt.Sprintf("id=%s hwid_unban=%v", accountID, unbanHWID), ip)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// POST /auth/set-nickname, set or clear nickname (requires valid identity jwt + challenge sig)
func (s *masterState) handleSetNickname(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "set_nickname", 10, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}
	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	token := getString(data, "jwt")
	challengeSig := getString(data, "challenge_sig")
	nickname := strings.TrimSpace(getString(data, "nickname"))

	if token == "" || challengeSig == "" {
		errResp(w, 400, "jwt and challenge_sig required")
		return
	}

	// try normal verify first, fall back to unsigned decode if signing key rotated
	pubKey := s.signingKey.Public().(ed25519.PublicKey)
	claims, err := verifyJWT(pubKey, token)
	if err != nil {
		claims, err = decodeJWTUnsafe(token)
		if err != nil {
			errResp(w, 401, fmt.Sprintf("Invalid token: %v", err))
			return
		}
	}

	// verify challenge sig (same as refresh: sha256(jwt) signed with user key)
	var storedPubKeyHex string
	err = s.db.QueryRow("SELECT public_key FROM accounts WHERE account_id = ?", claims.Sub).Scan(&storedPubKeyHex)
	if err != nil {
		errResp(w, 404, "Account not found")
		return
	}

	userPubBytes, _ := hex.DecodeString(storedPubKeyHex)
	userPub := ed25519.PublicKey(userPubBytes)
	challenge := sha256.Sum256([]byte(token))
	sigBytes, err := hex.DecodeString(challengeSig)
	if err != nil || !ed25519.Verify(userPub, challenge[:], sigBytes) {
		errResp(w, 401, "Challenge signature invalid")
		return
	}

	// clearing nickname
	if nickname == "" {
		s.db.Exec("UPDATE accounts SET nickname = '' WHERE account_id = ?", claims.Sub)
		// reissue jwt without nickname
		newClaims := JWTClaims{
			Sub:           claims.Sub,
			Username:      claims.Username,
			Nickname:      "",
			PKFingerprint: claims.PKFingerprint,
			EAPID:         claims.EAPID,
			EAName:        claims.EAName,
			Iat:           time.Now().Unix(),
			Exp:           time.Now().Add(jwtExpiry).Unix(),
		}
		jwt := issueJWT(s.signingKey, newClaims)
		log.Printf("[identity] %s cleared nickname", claims.Username)
		auditLog(s.db, "nickname_clear", claims.Username, fmt.Sprintf("id=%s", claims.Sub), getRealIP(r, s.behindProxy))
		jsonResp(w, 200, map[string]any{"ok": true, "jwt": jwt})
		return
	}

	// validate nickname
	if len(nickname) < minNicknameLen || len(nickname) > maxNicknameLen {
		errResp(w, 400, fmt.Sprintf("Nickname must be %d-%d characters", minNicknameLen, maxNicknameLen))
		return
	}
	if !usernameRegex.MatchString(nickname) {
		errResp(w, 400, "Nickname can only contain letters, numbers, underscores, hyphens, and spaces")
		return
	}

	// nickname can't be someone else's registered username (case-insensitive)
	var conflictID string
	err = s.db.QueryRow("SELECT account_id FROM accounts WHERE LOWER(username) = LOWER(?) AND account_id != ?", nickname, claims.Sub).Scan(&conflictID)
	if err == nil {
		errResp(w, 409, "That name is a registered username belonging to another account")
		return
	}

	// nickname can't be someone else's nickname either
	err = s.db.QueryRow("SELECT account_id FROM accounts WHERE LOWER(nickname) = LOWER(?) AND account_id != ?", nickname, claims.Sub).Scan(&conflictID)
	if err == nil {
		errResp(w, 409, "That nickname is already taken")
		return
	}

	s.db.Exec("UPDATE accounts SET nickname = ? WHERE account_id = ?", nickname, claims.Sub)
	s.db.Exec("INSERT INTO nickname_history (account_id, nickname, set_at) VALUES (?, ?, ?)", claims.Sub, nickname, float64(time.Now().Unix()))

	// reissue jwt with nickname
	newClaims := JWTClaims{
		Sub:           claims.Sub,
		Username:      claims.Username,
		Nickname:      nickname,
		PKFingerprint: claims.PKFingerprint,
		EAPID:         claims.EAPID,
		EAName:        claims.EAName,
		Iat:           time.Now().Unix(),
		Exp:           time.Now().Add(jwtExpiry).Unix(),
	}
	jwt := issueJWT(s.signingKey, newClaims)

	log.Printf("[identity] %s set nickname to %q", claims.Username, nickname)
	auditLog(s.db, "nickname_set", claims.Username, fmt.Sprintf("id=%s nickname=%s", claims.Sub, nickname), getRealIP(r, s.behindProxy))
	jsonResp(w, 200, map[string]any{"ok": true, "jwt": jwt, "nickname": nickname})
}
