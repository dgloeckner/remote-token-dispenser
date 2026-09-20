package main

// Conformance suite: one table of protocol cases, run against either the Go
// mock or a real ESP8266 (with the hopper simulator, or with the hopper
// itself).  The same binary, the same cases, the same verdict format —
// otherwise "the mock does it differently" stays invisible, which is exactly
// how the divergences in issue #1 survived.
//
//   token-tui conformance --endpoint http://127.0.0.1:8080 --api-key dev --target mock
//   token-tui conformance --endpoint http://192.168.4.20  --api-key … --target simulator
//
// Exit code is the verdict: 0 = every case that ran passed.

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"sort"
	"strings"
	"time"
)

// Target is what the suite is pointed at.
type Target string

const (
	// TargetMock is the Go mock in dispenser-mock/ — no hardware, scenarios
	// are selected by quantity.
	TargetMock Target = "mock"
	// TargetSimulator is a real ESP8266 running the firmware, with a second
	// D1 mini answering the motor line (docs/hopper-simulator.md).
	TargetSimulator Target = "simulator"
	// TargetHopper is a real ESP8266 with the Azkoyen hopper attached.
	TargetHopper Target = "hopper"
)

func (t Target) valid() bool {
	switch t {
	case TargetMock, TargetSimulator, TargetHopper:
		return true
	}
	return false
}

// isDevice reports whether the target is real firmware (as opposed to the mock).
func (t Target) isDevice() bool { return t == TargetSimulator || t == TargetHopper }

// Ctx is what a case gets to work with.
type Ctx struct {
	Client *DispenserClient
	Target Target
	// Interactive is the --interactive flag: cases that need a physical act
	// (press RST, cut power) only run when it is set.
	Interactive bool
	// LatencySamples overrides how many POSTs the latency case times.
	// Zero means the default; the suite's own tests lower it so a deliberately
	// slow fake device does not cost ten seconds of unit-test time.
	LatencySamples int
	in             *bufio.Reader
	out            io.Writer
	txCounter      int
}

// latencySamples is the sample count of post_latency_p95_below_300ms.
// Twenty, not the fifty of issue #4: on a real hopper every sample is a token
// that has to drop, and twenty already puts the p95 on the 19th value.
func (c *Ctx) latencySamples() int {
	if c.LatencySamples > 0 {
		return c.LatencySamples
	}
	return 20
}

// NextTxID returns a fresh tx_id (<= 16 characters, per the protocol).
func (c *Ctx) NextTxID(prefix string) string {
	c.txCounter++
	id := fmt.Sprintf("%s%d%03d", prefix, time.Now().Unix()%100000, c.txCounter)
	if len(id) > 16 {
		id = id[:16]
	}
	return id
}

// SlowQuantity returns a quantity that keeps the device dispensing long enough
// to send a second request while the first is still running.
// Mock: 15 selects the 500 ms/token scenario. Device: the hopper needs roughly
// a second per token anyway.
func (c *Ctx) SlowQuantity() int {
	if c.Target == TargetMock {
		return 15
	}
	return 5
}

// Prompt asks the operator for a physical act and waits for Enter.
func (c *Ctx) Prompt(what string) {
	fmt.Fprintf(c.out, "\n    ACTION REQUIRED: %s — then press Enter ", what)
	_, _ = c.in.ReadString('\n')
}

// Case is one protocol assertion.
type Case struct {
	Name string
	// Targets the case applies to. Empty means all of them.
	Targets []Target
	// NeedsInteraction marks a case that asks the operator to do something
	// physical; it is skipped unless --interactive is given.
	NeedsInteraction bool
	// Destructive marks a case that leaves the device in a state it cannot
	// leave by itself (an active hardware error). Those run last.
	Destructive bool
	// Note is printed with the verdict: what a failure means today.
	Note string
	Run  func(c *Ctx) error
}

func (tc Case) appliesTo(t Target) bool {
	if len(tc.Targets) == 0 {
		return true
	}
	for _, x := range tc.Targets {
		if x == t {
			return true
		}
	}
	return false
}

// Result is one row of the report.
type Result struct {
	Name     string        `json:"name"`
	Status   string        `json:"status"` // pass | fail | skip
	Detail   string        `json:"detail,omitempty"`
	Note     string        `json:"note,omitempty"`
	Duration time.Duration `json:"duration_ms"`
}

// Report is what --json writes.
type Report struct {
	Target   Target   `json:"target"`
	Endpoint string   `json:"endpoint"`
	Protocol int      `json:"protocol"`
	Started  string   `json:"started"`
	Passed   int      `json:"passed"`
	Failed   int      `json:"failed"`
	Skipped  int      `json:"skipped"`
	Cases    []Result `json:"cases"`
}

// runConformance is the `token-tui conformance …` subcommand.
// It returns the process exit code.
func runConformance(args []string) int {
	fs := flag.NewFlagSet("conformance", flag.ExitOnError)
	endpoint := fs.String("endpoint", "http://127.0.0.1:8080", "Dispenser base URL")
	apiKey := fs.String("api-key", "", "API key (or TOKEN_DISPENSER_API_KEY env)")
	target := fs.String("target", string(TargetMock), "mock | simulator | hopper")
	timeout := fs.Duration("timeout", 10*time.Second, "HTTP request timeout")
	jsonOut := fs.String("json", "", "Write a JSON report to this path")
	interactive := fs.Bool("interactive", false, "Run cases that need a physical act (press RST, cut power)")
	only := fs.String("only", "", "Run only cases whose name contains this substring")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, `Usage: token-tui conformance [flags]

Runs the protocol conformance table against the mock or a real device.
Exit code 0 means every case that ran passed.

Flags:
`)
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 2
	}

	tgt := Target(*target)
	if !tgt.valid() {
		fmt.Fprintf(os.Stderr, "unknown --target %q (mock | simulator | hopper)\n", *target)
		return 2
	}

	key := *apiKey
	if key == "" {
		key = os.Getenv("TOKEN_DISPENSER_API_KEY")
	}

	ctx := &Ctx{
		Client:      NewDispenserClient(*endpoint, key, *timeout),
		Target:      tgt,
		Interactive: *interactive,
		in:          bufio.NewReader(os.Stdin),
		out:         os.Stdout,
	}

	report := RunCases(ctx, ConformanceCases(), *only)
	report.Endpoint = ctx.Client.BaseURL

	if *jsonOut != "" {
		blob, err := json.MarshalIndent(report, "", "  ")
		if err == nil {
			err = os.WriteFile(*jsonOut, append(blob, '\n'), 0o644)
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "could not write %s: %v\n", *jsonOut, err)
			return 2
		}
	}

	if report.Failed > 0 {
		return 1
	}
	return 0
}

// RunCases executes the table and prints a per-case verdict.
// Exported for the unit tests, which run the same table against a fake server.
func RunCases(ctx *Ctx, cases []Case, only string) Report {
	report := Report{
		Target:   ctx.Target,
		Protocol: ProtocolVersion,
		Started:  time.Now().Format(time.RFC3339),
	}

	// Cases that leave the device in a state only a power cycle clears run last.
	ordered := make([]Case, len(cases))
	copy(ordered, cases)
	sort.SliceStable(ordered, func(i, j int) bool {
		return !ordered[i].Destructive && ordered[j].Destructive
	})

	fmt.Fprintf(ctx.out, "conformance: target=%s endpoint=%s protocol=%d\n\n",
		ctx.Target, ctx.Client.BaseURL, ProtocolVersion)

	for _, tc := range ordered {
		if only != "" && !strings.Contains(tc.Name, only) {
			continue
		}
		res := Result{Name: tc.Name, Note: tc.Note}

		switch {
		case !tc.appliesTo(ctx.Target):
			res.Status = "skip"
			res.Detail = fmt.Sprintf("not applicable to target %s", ctx.Target)
		case tc.NeedsInteraction && !ctx.Interactive:
			res.Status = "skip"
			res.Detail = "needs a physical act; run with --interactive"
		default:
			start := time.Now()
			err := tc.Run(ctx)
			res.Duration = time.Since(start) / time.Millisecond
			if err != nil {
				res.Status = "fail"
				res.Detail = err.Error()
			} else {
				res.Status = "pass"
			}
		}

		switch res.Status {
		case "pass":
			report.Passed++
			fmt.Fprintf(ctx.out, "  PASS  %-46s %4dms\n", res.Name, res.Duration)
		case "skip":
			report.Skipped++
			fmt.Fprintf(ctx.out, "  SKIP  %-46s %s\n", res.Name, res.Detail)
		default:
			report.Failed++
			fmt.Fprintf(ctx.out, "  FAIL  %-46s %s\n", res.Name, res.Detail)
			if res.Note != "" {
				fmt.Fprintf(ctx.out, "        note: %s\n", res.Note)
			}
		}
		report.Cases = append(report.Cases, res)
	}

	fmt.Fprintf(ctx.out, "\n%d passed, %d failed, %d skipped\n",
		report.Passed, report.Failed, report.Skipped)
	return report
}

// ---------------------------------------------------------------------------
// Raw HTTP helpers — for the cases the typed client deliberately cannot send
// (no key, malformed JSON, wrong content type).
// ---------------------------------------------------------------------------

type rawResponse struct {
	Status int
	Body   string
}

func (c *Ctx) raw(method, path string, body string, headers map[string]string) (rawResponse, error) {
	var rdr io.Reader
	if body != "" {
		rdr = bytes.NewBufferString(body)
	}
	req, err := http.NewRequest(method, c.Client.BaseURL+path, rdr)
	if err != nil {
		return rawResponse{}, err
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := c.Client.HTTPClient.Do(req)
	if err != nil {
		return rawResponse{}, err
	}
	defer resp.Body.Close()
	blob, err := io.ReadAll(resp.Body)
	if err != nil {
		return rawResponse{}, err
	}
	return rawResponse{Status: resp.StatusCode, Body: string(blob)}, nil
}

func (c *Ctx) postJSON(body string, withKey bool) (rawResponse, error) {
	h := map[string]string{"Content-Type": "application/json"}
	if withKey {
		h["X-API-Key"] = c.Client.APIKey
	}
	return c.raw("POST", "/dispense", body, h)
}

// postInTwoSegments sends POST /dispense with the JSON body split across two
// TCP writes, with a pause in between, so the two halves land in two segments.
//
// This cannot go through net/http: the client buffers the body and the server
// hands the handler a stream, which is exactly the framing detail under test.
// A device whose body callback parses the chunk it was given (ignoring index
// and total, as the firmware did before #4) answers 400 here.
func (c *Ctx) postInTwoSegments(body string) (rawResponse, error) {
	u, err := url.Parse(c.Client.BaseURL)
	if err != nil {
		return rawResponse{}, err
	}
	host := u.Host
	if u.Port() == "" {
		host = net.JoinHostPort(u.Hostname(), "80")
	}
	conn, err := net.DialTimeout("tcp", host, 5*time.Second)
	if err != nil {
		return rawResponse{}, fmt.Errorf("dial %s: %w", host, err)
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(15 * time.Second))

	head := fmt.Sprintf("POST /dispense HTTP/1.1\r\n"+
		"Host: %s\r\n"+
		"X-API-Key: %s\r\n"+
		"Content-Type: application/json\r\n"+
		"Content-Length: %d\r\n"+
		"Connection: close\r\n\r\n", u.Host, c.Client.APIKey, len(body))

	cut := len(body) / 2
	if _, err := io.WriteString(conn, head+body[:cut]); err != nil {
		return rawResponse{}, fmt.Errorf("writing the first segment: %w", err)
	}
	// Long enough that the two halves cannot be coalesced into one segment.
	time.Sleep(120 * time.Millisecond)
	if _, err := io.WriteString(conn, body[cut:]); err != nil {
		return rawResponse{}, fmt.Errorf("writing the second segment: %w", err)
	}

	resp, err := http.ReadResponse(bufio.NewReader(conn), &http.Request{Method: "POST"})
	if err != nil {
		return rawResponse{}, fmt.Errorf("no answer to a split body: %w", err)
	}
	defer resp.Body.Close()
	blob, err := io.ReadAll(resp.Body)
	if err != nil {
		return rawResponse{}, err
	}
	return rawResponse{Status: resp.StatusCode, Body: string(blob)}, nil
}

func wantStatus(got rawResponse, want int) error {
	if got.Status != want {
		return fmt.Errorf("expected HTTP %d, got %d (body: %s)", want, got.Status, strings.TrimSpace(got.Body))
	}
	return nil
}

// waitForFinalState polls GET /dispense/{tx_id} until the transaction leaves
// "dispensing", or the deadline passes.
func (c *Ctx) waitForFinalState(txID string, deadline time.Duration) (*DispenseResponse, error) {
	until := time.Now().Add(deadline)
	var last *DispenseResponse
	for time.Now().Before(until) {
		tx, res := c.Client.Status(txID)
		if res.Error != nil {
			return last, res.Error
		}
		last = tx
		if tx.State != "dispensing" {
			return tx, nil
		}
		time.Sleep(150 * time.Millisecond)
	}
	return last, fmt.Errorf("transaction %s still dispensing after %s", txID, deadline)
}
