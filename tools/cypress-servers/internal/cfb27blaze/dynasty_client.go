package cfb27blaze

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

type DynastySession struct {
	ID          int64  `json:"id"`
	Name        string `json:"name"`
	DynastyMode string `json:"dynastyMode,omitempty"`
	CurrentWeek int    `json:"currentWeek,omitempty"`
	Stage       string `json:"stage,omitempty"`
	MaxUsers    int    `json:"maxUsers,omitempty"`
}

type DynastyClient struct {
	baseURL string
	client  *http.Client
}

func NewDynastyClient(baseURL string) *DynastyClient {
	return &DynastyClient{
		baseURL: strings.TrimRight(baseURL, "/"),
		client:  &http.Client{Timeout: 5 * time.Second},
	}
}

func (c *DynastyClient) EnsureSeeded(ctx context.Context, name string) (DynastySession, error) {
	sessions, err := c.ListSessions(ctx)
	if err != nil {
		return DynastySession{}, err
	}
	if len(sessions) > 0 {
		return sessions[0], nil
	}
	if strings.TrimSpace(name) == "" {
		name = "Local Dynasty"
	}
	return c.CreateSession(ctx, DynastySession{
		Name:        name,
		DynastyMode: "Online Dynasty",
		CurrentWeek: 1,
		Stage:       "preseason",
		MaxUsers:    32,
	})
}

func (c *DynastyClient) ListSessions(ctx context.Context) ([]DynastySession, error) {
	var response struct {
		Sessions []DynastySession `json:"sessions"`
	}
	if err := c.doJSON(ctx, http.MethodGet, "/sessions", nil, &response); err != nil {
		return nil, err
	}
	return response.Sessions, nil
}

func (c *DynastyClient) CreateSession(ctx context.Context, session DynastySession) (DynastySession, error) {
	var created DynastySession
	if err := c.doJSON(ctx, http.MethodPost, "/sessions", session, &created); err != nil {
		return DynastySession{}, err
	}
	return created, nil
}

func (c *DynastyClient) doJSON(ctx context.Context, method, path string, requestBody any, responseBody any) error {
	var body io.Reader
	if requestBody != nil {
		encoded, err := json.Marshal(requestBody)
		if err != nil {
			return err
		}
		body = bytes.NewReader(encoded)
	}
	req, err := http.NewRequestWithContext(ctx, method, c.baseURL+path, body)
	if err != nil {
		return err
	}
	if requestBody != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := c.client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		message, _ := io.ReadAll(io.LimitReader(resp.Body, 4096))
		return fmt.Errorf("dynasty service %s %s returned %d: %s", method, path, resp.StatusCode, strings.TrimSpace(string(message)))
	}
	return json.NewDecoder(resp.Body).Decode(responseBody)
}
