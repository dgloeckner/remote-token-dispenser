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
	endpoint := flag.String("endpoint", "http://192.168.4.20", "Dispenser base URL")
	apiKey := flag.String("api-key", "", "API key for dispenser (or TOKEN_DISPENSER_API_KEY env)")
	timeout := flag.Duration("timeout", 3*time.Second, "HTTP request timeout")
	showVersion := flag.Bool("version", false, "Show version")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `
🪙 Token Dispenser TUI — k9s-style testing dashboard

Usage: token-tui [flags]

Flags:
`)
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, `
Environment:
  TOKEN_DISPENSER_API_KEY   API key (alternative to --api-key)
  TOKEN_DISPENSER_ENDPOINT  Endpoint URL (alternative to --endpoint)

Examples:
  token-tui --endpoint http://192.168.4.20 --api-key mysecret
  TOKEN_DISPENSER_API_KEY=mysecret token-tui

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

	// Resolve API key
	key := *apiKey
	if key == "" {
		key = os.Getenv("TOKEN_DISPENSER_API_KEY")
	}
	if key == "" {
		fmt.Fprintf(os.Stderr, "⚠  No API key provided. Use --api-key or TOKEN_DISPENSER_API_KEY env.\n")
		fmt.Fprintf(os.Stderr, "   Health checks will work, but dispense operations will fail (401).\n\n")
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
