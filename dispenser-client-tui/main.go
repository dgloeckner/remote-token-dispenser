package main

import (
	"flag"
	"fmt"
	"os"
	"time"

	tea "github.com/charmbracelet/bubbletea"
)

var (
	version = "0.1.0"
)

func main() {
	// Subcommands come before the flag package sees anything: `conformance`
	// is a headless protocol run, the bare command is the TUI.
	if len(os.Args) > 1 && os.Args[1] == "conformance" {
		os.Exit(runConformance(os.Args[2:]))
	}

	endpoint := flag.String("endpoint", "http://192.168.4.20", "Dispenser base URL")
	// The shared signing secret (issue #8).  It never leaves this process:
	// requests carry HMAC-SHA256 over METHOD \n PATH \n BODY \n NONCE.  There
	// is deliberately no --api-key alias — no device is deployed, so keeping
	// one would only keep the old habit alive.
	signingKey := flag.String("signing-key", "", "Shared signing secret (or TOKEN_DISPENSER_SIGNING_KEY env). Never transmitted.")
	timeout := flag.Duration("timeout", 3*time.Second, "HTTP request timeout")
	showVersion := flag.Bool("version", false, "Show version")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `
🪙 Token Dispenser TUI — k9s-style testing dashboard

Usage: token-tui [flags]
       token-tui conformance [flags]   protocol conformance run (headless)

Flags:
`)
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, `
Environment:
  TOKEN_DISPENSER_SIGNING_KEY  Signing secret (alternative to --signing-key)
  TOKEN_DISPENSER_ENDPOINT  Endpoint URL (alternative to --endpoint)

Examples:
  token-tui --endpoint http://192.168.4.20 --signing-key mysecret
  TOKEN_DISPENSER_SIGNING_KEY=mysecret token-tui
  token-tui conformance --endpoint http://127.0.0.1:8080 --signing-key dev --target mock
  token-tui conformance --endpoint http://192.168.4.20 --signing-key mysecret --target simulator

Keys:
  1/2/3/4    Switch tabs (Dashboard / Dispense / Log / Burst)
  r          Refresh health
  q/Ctrl+C   Quit
  ↑/↓        Adjust quantity / scroll log
  Enter      Start dispense / burst
`)
	}

	flag.Parse()

	if *showVersion {
		fmt.Printf("token-tui %s\n", version)
		os.Exit(0)
	}

	// Resolve the signing key
	key := *signingKey
	if key == "" {
		key = os.Getenv("TOKEN_DISPENSER_SIGNING_KEY")
	}
	if key == "" {
		fmt.Fprintf(os.Stderr, "⚠  No signing key provided. Use --signing-key or TOKEN_DISPENSER_SIGNING_KEY env.\n")
		fmt.Fprintf(os.Stderr, "   Everything but the minimal GET /health will fail (401): since issue #8\n")
		fmt.Fprintf(os.Stderr, "   every request is signed, and an unsigned one is refused.\n\n")
	}

	// Resolve endpoint
	ep := *endpoint
	if envEP := os.Getenv("TOKEN_DISPENSER_ENDPOINT"); envEP != "" && ep == "http://192.168.4.20" {
		ep = envEP
	}

	client := NewDispenserClient(ep, key, *timeout)
	model := NewModel(client)

	p := tea.NewProgram(
		model,
		tea.WithAltScreen(),
		tea.WithMouseCellMotion(),
	)

	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}
