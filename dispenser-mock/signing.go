package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"fmt"
	"sync"
	"time"
)

// Request signing (issue #8), the mock's half.
//
// The firmware's implementation is firmware/dispenser/request_signer.cpp; this
// one exists so the conformance suite can exercise a client against something
// that is not an ESP8266.  Both are written from the same paragraph of
// dispenser-protocol.md, and `the_canonical_string_is_method_path_body_nonce`
// in the native suite pins the firmware's side of it.
//
// `X-API-Key` is gone from this file and from every other: there is no device
// in the field, so there is no transition and no shim.

const (
	// NonceTTL is how long an issued nonce stays usable — the protocol's
	// reservation TTL, long enough for a POST and the polls behind it.
	NonceTTL = 30 * time.Second
	// NoncePoolSize bounds the outstanding nonces; a ninth evicts the oldest.
	NoncePoolSize = 8
	// NonceHexLen and SignatureHexLen are the wire widths.
	NonceHexLen     = 32
	SignatureHexLen = 64
)

// CanonicalString is THE interoperability detail: METHOD \n PATH \n BODY \n
// NONCE, single LF separators, none at the end.  Path carries no query string.
func CanonicalString(method, path, body, nonce string) string {
	return fmt.Sprintf("%s\n%s\n%s\n%s", method, path, body, nonce)
}

// SignRequest returns the value of X-Signature: lowercase hex, 64 characters.
func SignRequest(key, method, path, body, nonce string) string {
	mac := hmac.New(sha256.New, []byte(key))
	mac.Write([]byte(CanonicalString(method, path, body, nonce)))
	return hex.EncodeToString(mac.Sum(nil))
}

// AuthResult mirrors the firmware's enum.  The distinction is the whole of the
// client contract on a 401: ResultBadNonce means "fetch one and retry once",
// ResultBadSignature means "stop, a retry changes nothing".
type AuthResult int

const (
	ResultOK AuthResult = iota
	ResultBadSignature
	ResultBadNonce
)

func (r AuthResult) reason() string {
	if r == ResultBadNonce {
		return "nonce"
	}
	return "signature"
}

type nonceSlot struct {
	issued time.Time
	spent  bool
}

// NoncePool is the device's memory of what it has handed out.
type NoncePool struct {
	mu     sync.Mutex
	key    string
	live   map[string]*nonceSlot
	order  []string
	nowFn  func() time.Time
	randFn func() string
}

func NewNoncePool(key string) *NoncePool {
	return &NoncePool{
		key:   key,
		live:  map[string]*nonceSlot{},
		nowFn: time.Now,
		randFn: func() string {
			buf := make([]byte, NonceHexLen/2)
			if _, err := rand.Read(buf); err != nil {
				// crypto/rand does not fail on Linux; if it ever does, a
				// predictable nonce is worse than no dispenser.
				panic(err)
			}
			return hex.EncodeToString(buf)
		},
	}
}

// Issue hands out a nonce and remembers it.
func (p *NoncePool) Issue() string {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.sweepLocked()

	n := p.randFn()
	p.live[n] = &nonceSlot{issued: p.nowFn()}
	p.order = append(p.order, n)
	for len(p.order) > NoncePoolSize {
		delete(p.live, p.order[0])
		p.order = p.order[1:]
	}
	return n
}

func (p *NoncePool) sweepLocked() {
	now := p.nowFn()
	kept := p.order[:0]
	for _, n := range p.order {
		slot, ok := p.live[n]
		if !ok {
			continue
		}
		if now.Sub(slot.issued) > NonceTTL {
			delete(p.live, n)
			continue
		}
		kept = append(kept, n)
	}
	p.order = kept
}

// Verify checks one request.  `mutating` spends the nonce, so the identical
// bytes replayed afterwards are ResultBadNonce; a read-only request leaves it
// live until it expires, which is what lets one nonce serve a transaction's
// status polls.
func (p *NoncePool) Verify(method, path, body, nonce, signature string, mutating bool) AuthResult {
	// No headers at all is a signature problem, not a nonce problem: a client
	// that was never told to sign must not be sent round the retry loop.
	if nonce == "" || signature == "" {
		return ResultBadSignature
	}
	if len(signature) != SignatureHexLen {
		return ResultBadSignature
	}
	if len(nonce) != NonceHexLen {
		return ResultBadNonce
	}

	p.mu.Lock()
	defer p.mu.Unlock()
	p.sweepLocked()

	slot, ok := p.live[nonce]
	if !ok {
		return ResultBadNonce
	}

	// The signature is checked BEFORE the nonce is spent, and "already spent"
	// comes after it: otherwise anyone without the key could take a terminal
	// out of service by replaying its bytes with one character changed.
	want := SignRequest(p.key, method, path, body, nonce)
	if subtle.ConstantTimeCompare([]byte(want), []byte(signature)) != 1 {
		return ResultBadSignature
	}

	if mutating {
		if slot.spent {
			return ResultBadNonce
		}
		slot.spent = true
	}
	return ResultOK
}
