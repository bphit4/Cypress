package cfb27blaze

import (
	"crypto/tls"
	"net"
	"testing"
)

func TestGeneratedTLSConfigCompletesHandshake(t *testing.T) {
	serverConfig, err := newTLSConfig("tls13")
	if err != nil {
		t.Fatal(err)
	}

	serverSide, clientSide := net.Pipe()
	defer serverSide.Close()
	defer clientSide.Close()

	server := tls.Server(serverSide, serverConfig)
	client := tls.Client(clientSide, &tls.Config{
		InsecureSkipVerify: true,
		ServerName:         "spring25.client.blazeredirector.ea.com",
	})

	serverResult := make(chan error, 1)
	go func() {
		serverResult <- server.Handshake()
	}()
	if err := client.Handshake(); err != nil {
		t.Fatal(err)
	}
	if err := <-serverResult; err != nil {
		t.Fatal(err)
	}
}
