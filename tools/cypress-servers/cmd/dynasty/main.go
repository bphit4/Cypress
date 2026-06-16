package main

import (
	"flag"
	"fmt"
	"os"

	"cypress-servers/internal/dynasty"
)

func main() {
	cfg := dynasty.Config{}
	flag.StringVar(&cfg.Bind, "bind", "0.0.0.0", "HTTP bind address")
	flag.IntVar(&cfg.Port, "port", 27910, "HTTP/WebSocket port")
	flag.StringVar(&cfg.SchemaRoot, "schema-root", `C:\Users\Shadow\Desktop\CFB27\Dynasty_Files`, "CFB27 Dynasty .FTX schema root")
	flag.StringVar(&cfg.DBFile, "db", "cfb27_dynasty.db", "SQLite database file")
	flag.Parse()

	if err := dynasty.Run(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "fatal: %v\n", err)
		os.Exit(1)
	}
}
