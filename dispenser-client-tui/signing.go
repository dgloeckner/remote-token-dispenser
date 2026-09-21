package main

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"
)

// The client half of request signing (issue #8).
//
// Until #8 every request carried `X-API-Key`, the shared secret itself, in
// clear over the WLAN.  It is gone from this client — there is no fallback,
// no flag and no header left over, because no device is deployed and a shim
// would only keep the old habit alive.
//
// The canonical string is the one thing both ends must build identically:
//
//	METHOD \n PATH \n BODY \n NONCE
//
// single LF separators, none at the end, path without any query string, body
// exactly as sent (the empty string for a GET).  The firmware's copy is
// firmware/dispenser/request_signer.cpp and the mock's is
// dispenser-mock/signing.go.

const (
	// NonceHexLen and SignatureHexLen are the wire widths of the two headers.
	NonceHexLen     = 32
	SignatureHexLen = 64
	// nonceSafetyMargin is how long before a nonce's stated TTL this client
	// stops using it.  A nonce that expires between the signature and the
	// device's clock costs a round trip, and 5 s is more than a WLAN ever
	// takes.
	nonceSafetyMargin = 5 * time.Second
)

// CanonicalString is the string the signature is computed over.
func CanonicalString(method, path, body, nonce string) string {
	return fmt.Sprintf("%s\n%s\n%s\n%s", method, path, body, nonce)
}

// SignRequest returns the value of X-Signature: lowercase hex, 64 characters.
func SignRequest(key, method, path, body, nonce string) string {
	mac := hmac.New(sha256.New, []byte(key))
	mac.Write([]byte(CanonicalString(method, path, body, nonce)))
	return hex.EncodeToString(mac.Sum(nil))
}

// nonceCache holds the one nonce this client is currently working with.
//
// It is a cache and not a fetch-per-request because a status poll does not
// spend its nonce: one `GET /nonce` covers a POST and every poll behind it
// until the nonce expires.  A POST does spend it, so the cache is dropped
// after one.
type nonceCache struct {
	mu      sync.Mutex
	nonce   string
	expires time.Time
}

func (c *nonceCache) get() (string, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.nonce == "" || time.Now().After(c.expires) {
		return "", false
	}
	return c.nonce, true
}

func (c *nonceCache) put(nonce string, ttl time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.nonce = nonce
	if ttl <= nonceSafetyMargin {
		c.expires = time.Now().Add(ttl)
	} else {
		c.expires = time.Now().Add(ttl - nonceSafetyMargin)
	}
}

// drop forgets the cached nonce: after a POST has spent it, and after any 401
// that named the nonce as the reason.
func (c *nonceCache) drop() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.nonce = ""
	c.expires = time.Time{}
}

// NonceResponse is GET /nonce.
type NonceResponse struct {
	Nonce string `json:"nonce"`
	TTL   int    `json:"ttl"`
}

// fetchNonce asks the device for a fresh nonce.  Unauthenticated, and it has
// to be: the client holds nothing it could sign with until it has one.
func (c *DispenserClient) fetchNonce() (string, error) {
	resp, err := c.HTTPClient.Get(c.BaseURL + "/nonce")
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}
	if resp.StatusCode != 200 {
		return "", fmt.Errorf("GET /nonce returned %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}
	var n NonceResponse
	if err := json.Unmarshal(body, &n); err != nil {
		return "", fmt.Errorf("GET /nonce is not JSON: %w", err)
	}
	if len(n.Nonce) != NonceHexLen {
		return "", fmt.Errorf("GET /nonce handed out %q, want %d hex characters", n.Nonce, NonceHexLen)
	}
	ttl := time.Duration(n.TTL) * time.Second
	if ttl <= 0 {
		ttl = 30 * time.Second
	}
	c.nonces.put(n.Nonce, ttl)
	return n.Nonce, nil
}

// nonce returns a usable nonce, fetching one when the cache has none.
func (c *DispenserClient) nonce(force bool) (string, error) {
	if !force {
		if n, ok := c.nonces.get(); ok {
			return n, nil
		}
	}
	return c.fetchNonce()
}

// signedRequest builds a signed request.  `mutating` is what SPENDS the nonce
// on the device, so the client drops its cached copy after one.
func (c *DispenserClient) signedRequest(method, path, body string, mutating, forceFreshNonce bool) (*http.Request, error) {
	n, err := c.nonce(forceFreshNonce)
	if err != nil {
		return nil, err
	}
	var rdr io.Reader
	if body != "" {
		rdr = strings.NewReader(body)
	}
	req, err := http.NewRequest(method, c.BaseURL+path, rdr)
	if err != nil {
		return nil, err
	}
	req.Header.Set("X-Nonce", n)
	req.Header.Set("X-Signature", SignRequest(c.SigningKey, method, path, body, n))
	if mutating {
		// The device marks it spent the moment the signature verifies; keeping
		// it would buy one guaranteed 401 on the next call.
		c.nonces.drop()
	}
	return req, nil
}

// isNonceRejection reports whether a 401 body says the NONCE was the problem.
//
// This is the whole of the retry contract: reason "nonce" means fetch a fresh
// one and retry ONCE — safe, because every mutating request is idempotent by
// tx_id — and reason "signature" means the request is wrong and a retry
// changes nothing.  Without the distinction a client either loops forever on
// a misconfigured key or gives up on a nonce that merely expired.
func isNonceRejection(body []byte) bool {
	var resp ErrorResponse
	if err := json.Unmarshal(body, &resp); err != nil {
		return false
	}
	return resp.Reason == "nonce"
}

// doSigned sends a signed request and, on a 401 that names the nonce, fetches
// a fresh nonce and sends it exactly once more.
func (c *DispenserClient) doSigned(method, path, body string, mutating bool, extra map[string]string) (*http.Response, []byte, error) {
	for attempt := 0; attempt < 2; attempt++ {
		req, err := c.signedRequest(method, path, body, mutating, attempt > 0)
		if err != nil {
			return nil, nil, err
		}
		for k, v := range extra {
			req.Header.Set(k, v)
		}
		resp, err := c.HTTPClient.Do(req)
		if err != nil {
			return nil, nil, err
		}
		blob, readErr := io.ReadAll(resp.Body)
		resp.Body.Close()
		if readErr != nil {
			return resp, nil, readErr
		}
		if resp.StatusCode == http.StatusUnauthorized && attempt == 0 && isNonceRejection(blob) {
			c.nonces.drop()
			continue
		}
		return resp, blob, nil
	}
	return nil, nil, fmt.Errorf("unreachable")
}
