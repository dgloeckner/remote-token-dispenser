package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
)

var (
	bind          = flag.String("bind", ":8080", "Network address to bind to")
	apiKey        = flag.String("api-key", "dev", "Required API key for authentication")
	listScenarios = flag.Bool("list-scenarios", false, "Print scenario mapping table and exit")
)

func main() {
	flag.Parse()

	if *listScenarios {
		printScenarios()
		return
	}

	// Create mock dispenser
	mock := NewMockDispenser(*apiKey)

	// Setup routes
	mux := http.NewServeMux()
	mock.RegisterHandlers(mux)

	// Start server
	log.Printf("Mock dispenser listening on %s", *bind)
	log.Printf("API Key: %s", *apiKey)
	log.Printf("Ready for requests. Use --list-scenarios to see available test cases.")

	if err := http.ListenAndServe(*bind, mux); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}

func printScenarios() {
	fmt.Println("Token Dispenser Mock - Scenario Mapping")
	fmt.Println("========================================")
	fmt.Println()
	fmt.Println("Request quantity determines test scenario:")
	fmt.Println()
	fmt.Println("SUCCESS:")
	fmt.Println("  1-3     Normal dispense (100ms/token)")
	fmt.Println("  16-20   Normal dispense (higher quantities)")
	fmt.Println()
	fmt.Println("FAILURES:")
	fmt.Println("  4       Timeout after 2 tokens (5s jam detection)")
	fmt.Println("  5       Crash after 1 token (socket close)")
	fmt.Println("  6       Partial dispense (4 of 6 tokens)")
	fmt.Println("  7       Load delay (2.5s first token, then 100ms)")
	fmt.Println()
	fmt.Println("HARDWARE ERRORS:")
	fmt.Println("  8       COIN_STUCK (error code 1)")
	fmt.Println("  9       SENSOR_OFF (error code 2)")
	fmt.Println("  10      JAM_PERMANENT (error code 3)")
	fmt.Println("  11      MAX_SPAN (error code 4)")
	fmt.Println("  12      MOTOR_FAULT (error code 5)")
	fmt.Println("  13      SENSOR_FAULT (error code 6)")
	fmt.Println("  14      POWER_FAULT (error code 7)")
	fmt.Println()
	fmt.Println("SPECIAL:")
	fmt.Println("  15      Slow dispense (500ms/token)")
	fmt.Println()
	fmt.Println("Example: curl -X POST http://localhost:8080/dispense \\")
	fmt.Println("  -H \"X-API-Key: dev\" \\")
	fmt.Println("  -H \"Content-Type: application/json\" \\")
	fmt.Println("  -d '{\"tx_id\":\"test1\",\"quantity\":4}'")
	fmt.Println()
}
