package main

import (
	"flag"
	"fmt"
	"os"

	"cypress-servers/internal/cfb27gateway"
)

func main() {
	cfg := cfb27gateway.Config{}
	flag.StringVar(&cfg.Bind, "bind", "127.0.0.1", "HTTP bind address")
	flag.IntVar(&cfg.Port, "port", 27920, "HTTP gateway/logger port")
	flag.StringVar(&cfg.LogFile, "log-file", "cfb27_gateway.log", "JSONL request log path")
	flag.StringVar(&cfg.CandidatesFile, "candidates-file", "cfb27-endpoints.json", "observed endpoint candidate JSON path")
	flag.Parse()

	if err := cfb27gateway.Run(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "fatal: %v\n", err)
		os.Exit(1)
	}
}
