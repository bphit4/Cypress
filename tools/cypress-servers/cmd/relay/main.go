package main

import (
	"flag"
	"fmt"
	"os"

	"cypress-servers/internal/relay"
)

func main() {
	cfg := relay.Config{}
	flag.StringVar(&cfg.Bind, "bind", "0.0.0.0", "UDP bind address")
	flag.IntVar(&cfg.Port, "port", 25201, "UDP relay port")
	flag.StringVar(&cfg.APIBind, "api-bind", "0.0.0.0", "HTTP API bind address")
	flag.IntVar(&cfg.APIPort, "api-port", 8080, "HTTP API port")
	flag.StringVar(&cfg.RelayHost, "relay-host", "relay.local", "Public relay hostname")
	flag.StringVar(&cfg.PublicDomain, "public-domain", "", "Vanity domain suffix (e.g. v0e.dev)")
	flag.StringVar(&cfg.PublicPrefix, "public-prefix", "cypress", "Prefix for vanity hosts")
	flag.IntVar(&cfg.ClientTimeout, "client-timeout", 180, "Idle client timeout (seconds)")
	flag.IntVar(&cfg.ServerTimeout, "server-timeout", 90, "Server registration timeout (seconds)")
	flag.StringVar(&cfg.LogFile, "log-file", "", "Path to append log lines to disk")
	flag.StringVar(&cfg.LeaseFile, "lease-file", "relay_leases.json", "Path to persist leases")
	flag.BoolVar(&cfg.NoDashboard, "no-dashboard", false, "Disable live terminal dashboard")
	flag.StringVar(&cfg.MasterURL, "master-url", "", "Master server URL for ticket validation (e.g. http://127.0.0.1:27900)")
	flag.Parse()

	if err := relay.Run(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "fatal: %v\n", err)
		os.Exit(1)
	}
}
