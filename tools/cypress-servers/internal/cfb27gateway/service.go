package cfb27gateway

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"
)

type Config struct {
	Bind          string
	Port          int
	LogFile       string
	CandidatesFile string
}

type Service struct {
	startedAt time.Time
	logFile   string
	mu        sync.Mutex
	events    []Event
}

type Event struct {
	Time       time.Time         `json:"time"`
	Method     string            `json:"method"`
	Path       string            `json:"path"`
	RemoteAddr string            `json:"remoteAddr"`
	Host       string            `json:"host"`
	UserAgent  string            `json:"userAgent,omitempty"`
	Headers    map[string]string `json:"headers,omitempty"`
	BodyBytes  int               `json:"bodyBytes"`
}

type Candidate struct {
	Remote      string `json:"remote"`
	Address     string `json:"address"`
	Port        int    `json:"port"`
	ReverseDNS  string `json:"reverseDns"`
	Status      string `json:"status"`
	FirstSeen   string `json:"firstSeen"`
	Description string `json:"description"`
}

func Run(cfg Config) error {
	if cfg.Bind == "" {
		cfg.Bind = "127.0.0.1"
	}
	if cfg.Port == 0 {
		cfg.Port = 27920
	}
	if cfg.LogFile == "" {
		cfg.LogFile = "cfb27_gateway.log"
	}
	if cfg.CandidatesFile != "" {
		if err := writeDefaultCandidates(cfg.CandidatesFile); err != nil {
			return err
		}
	}

	svc := &Service{startedAt: time.Now().UTC(), logFile: cfg.LogFile}
	if err := ensureParentDir(cfg.LogFile); err != nil {
		return err
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/health", svc.handleHealth)
	mux.HandleFunc("/events", svc.handleEvents)
	mux.HandleFunc("/", svc.handleCatchAll)

	addr := fmt.Sprintf("%s:%d", cfg.Bind, cfg.Port)
	fmt.Printf("CFB27 gateway/logger listening on http://%s\n", addr)
	return http.ListenAndServe(addr, withCORS(mux))
}

func (s *Service) handleHealth(w http.ResponseWriter, r *http.Request) {
	jsonResp(w, 200, map[string]any{
		"status": "ok",
		"service": "cfb27-gateway",
		"startedAt": s.startedAt,
		"events": len(s.snapshotEvents()),
		"note": "Logging gateway only. No EA production service emulation is enabled.",
	})
}

func (s *Service) handleEvents(w http.ResponseWriter, r *http.Request) {
	jsonResp(w, 200, map[string]any{"events": s.snapshotEvents()})
}

func (s *Service) handleCatchAll(w http.ResponseWriter, r *http.Request) {
	bodyBytes := 0
	if r.Body != nil {
		body, _ := io.ReadAll(io.LimitReader(r.Body, 1<<20))
		bodyBytes = len(body)
	}
	headers := map[string]string{}
	for k, v := range r.Header {
		if len(v) > 0 {
			headers[k] = v[0]
		}
	}
	event := Event{
		Time:       time.Now().UTC(),
		Method:     r.Method,
		Path:       r.URL.RequestURI(),
		RemoteAddr: r.RemoteAddr,
		Host:       r.Host,
		UserAgent:  r.UserAgent(),
		Headers:    headers,
		BodyBytes:  bodyBytes,
	}
	s.record(event)
	jsonResp(w, 404, map[string]any{
		"error": "cfb27 gateway logger has no handler for this path",
		"path": r.URL.RequestURI(),
	})
}

func (s *Service) record(event Event) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.events = append(s.events, event)
	if len(s.events) > 200 {
		s.events = s.events[len(s.events)-200:]
	}
	if s.logFile != "" {
		if f, err := os.OpenFile(s.logFile, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644); err == nil {
			_ = json.NewEncoder(f).Encode(event)
			_ = f.Close()
		}
	}
}

func (s *Service) snapshotEvents() []Event {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make([]Event, len(s.events))
	copy(out, s.events)
	return out
}

func writeDefaultCandidates(path string) error {
	if _, err := os.Stat(path); err == nil {
		return nil
	}
	if err := ensureParentDir(path); err != nil {
		return err
	}
	candidates := []Candidate{
		{
			Remote:      "13.216.20.181:443",
			Address:     "13.216.20.181",
			Port:        443,
			ReverseDNS:  "ec2-13-216-20-181.compute-1.amazonaws.com",
			Status:      "observed_unknown_https_candidate",
			FirstSeen:   "2026-06-16",
			Description: "Repeated CFB27-owned HTTPS connection during online-gated screens.",
		},
		{
			Remote:      "54.236.91.7:443",
			Address:     "54.236.91.7",
			Port:        443,
			ReverseDNS:  "ec2-54-236-91-7.compute-1.amazonaws.com",
			Status:      "observed_unknown_https_candidate",
			FirstSeen:   "2026-06-16",
			Description: "Earlier CFB27-owned HTTPS connection during fake anti-cheat/offline run.",
		},
		{
			Remote:      "54.144.133.160:443",
			Address:     "54.144.133.160",
			Port:        443,
			ReverseDNS:  "ec2-54-144-133-160.compute-1.amazonaws.com",
			Status:      "observed_unknown_https_candidate",
			FirstSeen:   "2026-06-16",
			Description: "CFB27-owned HTTPS connection during Dynasty and multi-mode trigger live traces.",
		},
	}
	b, err := json.MarshalIndent(map[string]any{
		"mode": "observe-first",
		"gateway": "http://127.0.0.1:27920",
		"candidates": candidates,
		"notes": []string{
			"These are proven process-owned CFB27 remotes, not confirmed protocol endpoints.",
			"Do not redirect blindly; TLS/SNI/certificate behavior must be identified first.",
		},
	}, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, b, 0644)
}

func ensureParentDir(path string) error {
	dir := filepath.Dir(path)
	if dir == "." || dir == "" {
		return nil
	}
	return os.MkdirAll(dir, 0755)
}

func jsonResp(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}
