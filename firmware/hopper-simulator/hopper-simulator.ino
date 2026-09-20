// firmware/hopper-simulator/hopper-simulator.ino
//
// Azkoyen Hopper U-II simulator for a second Wemos D1 mini.
//
// It watches the dispenser's motor control line and answers with coin pulses,
// exactly as the hopper would — plus the misbehaviours that are otherwise only
// reproducible with a real jam, a real bounce or a real fault:
//
//   normal          one clean 30 ms pulse per token
//   bounce burst    a 3-edge burst within 5 ms per token (tests #5's filter)
//   coast pulse     one extra pulse 120 ms after the motor stops
//   jam after N     stop answering after N tokens; the dispenser must time out
//   error code N    pulse-encode an Azkoyen error on the error line
//   fast mode       300 ms per token instead of 1000 ms (unattended runs)
//
// Wiring and operation: docs/hopper-simulator.md
//
// Configuration is by serial command (115200 baud), so a test script or the
// conformance suite's prompt can tell the operator exactly what to type:
//
//   n          normal
//   b          bounce burst
//   c          coast pulse after stop
//   jN         jam after N tokens (j3)
//   eN         send Azkoyen error code N once (e5), N = 1..7
//   f          toggle fast mode
//   s          status
//   r          reset counters and mode

// --- pins --------------------------------------------------------------
// All three lines are 3.3 V logic between the two boards; the optocouplers
// sit between the dispenser and the real hopper, not here.
#define MOTOR_WATCH_PIN   D2   // <- dispenser D1 (motor control, HIGH = motor on)
#define COIN_PULSE_PIN    D5   // -> dispenser D7 (coin pulse, active LOW)
#define ERROR_OUT_PIN     D6   // -> dispenser D5 (error signal, active LOW)

// --- hopper timing (docs/azkoyen-hopper-protocol.md) --------------------
#define PULSE_LOW_MS        30   // one coin = 30 ms LOW
#define TOKEN_INTERVAL_MS 1000   // Hopper U-II dispenses roughly one token/s
#define FAST_INTERVAL_MS   300   // unattended conformance runs
#define COAST_DELAY_MS     120   // how late a coasting token arrives
#define BOUNCE_GAP_MS        2   // spacing inside a bounce burst

enum Mode {
  MODE_NORMAL,
  MODE_BOUNCE,
  MODE_COAST,
  MODE_JAM
};

Mode mode = MODE_NORMAL;
bool fastMode = false;
uint8_t jamAfter = 0;         // MODE_JAM: stop after this many tokens
uint16_t tokensThisRun = 0;   // tokens answered since the motor started
uint32_t totalTokens = 0;     // tokens answered since boot
bool motorWasOn = false;
uint32_t lastTokenMs = 0;
uint32_t motorStoppedMs = 0;
bool coastPending = false;

uint16_t tokenIntervalMs() {
  return fastMode ? FAST_INTERVAL_MS : TOKEN_INTERVAL_MS;
}

// One coin pulse: LOW for PULSE_LOW_MS, then back to idle HIGH.
void sendPulse(uint16_t lowMs) {
  digitalWrite(COIN_PULSE_PIN, LOW);
  delay(lowMs);
  digitalWrite(COIN_PULSE_PIN, HIGH);
}

// A bouncing sensor: three short edges inside ~5 ms. A dispenser that counts
// raw falling edges counts three tokens here; one is correct.
void sendBounceBurst() {
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(COIN_PULSE_PIN, LOW);
    delay(BOUNCE_GAP_MS);
    digitalWrite(COIN_PULSE_PIN, HIGH);
    delay(BOUNCE_GAP_MS);
  }
  // …followed by the real pulse, so a correct filter still counts exactly one.
  sendPulse(PULSE_LOW_MS);
}

// Azkoyen error encoding: 100 ms start pulse, 20 ms gap, then `code` pulses
// of 10 ms (docs/azkoyen-hopper-protocol.md section 3.5).
void sendErrorCode(uint8_t code) {
  if (code < 1 || code > 7) {
    return;
  }
  Serial.print("[sim] error code ");
  Serial.println(code);

  digitalWrite(ERROR_OUT_PIN, LOW);
  delay(100);
  digitalWrite(ERROR_OUT_PIN, HIGH);
  delay(20);

  for (uint8_t i = 0; i < code; i++) {
    digitalWrite(ERROR_OUT_PIN, LOW);
    delay(10);
    digitalWrite(ERROR_OUT_PIN, HIGH);
    delay(10);
  }
}

void printStatus() {
  Serial.print("[sim] mode=");
  switch (mode) {
    case MODE_NORMAL: Serial.print("normal"); break;
    case MODE_BOUNCE: Serial.print("bounce"); break;
    case MODE_COAST:  Serial.print("coast"); break;
    case MODE_JAM:    Serial.print("jam after "); Serial.print(jamAfter); break;
  }
  Serial.print(fastMode ? " fast" : " realtime");
  Serial.print(" motor=");
  Serial.print(digitalRead(MOTOR_WATCH_PIN) == HIGH ? "on" : "off");
  Serial.print(" tokens_this_run=");
  Serial.print(tokensThisRun);
  Serial.print(" total=");
  Serial.println(totalTokens);
}

void handleSerial() {
  if (!Serial.available()) {
    return;
  }
  int c = Serial.read();
  switch (c) {
    case 'n': mode = MODE_NORMAL; Serial.println("[sim] mode: normal"); break;
    case 'b': mode = MODE_BOUNCE; Serial.println("[sim] mode: bounce burst"); break;
    case 'c': mode = MODE_COAST;  Serial.println("[sim] mode: coast pulse after stop"); break;
    case 'j': {
      jamAfter = (uint8_t)Serial.parseInt();
      if (jamAfter == 0) {
        jamAfter = 1;
      }
      mode = MODE_JAM;
      Serial.print("[sim] mode: jam after ");
      Serial.println(jamAfter);
      break;
    }
    case 'e': sendErrorCode((uint8_t)Serial.parseInt()); break;
    case 'f':
      fastMode = !fastMode;
      Serial.print("[sim] fast mode ");
      Serial.println(fastMode ? "on (300ms/token)" : "off (1000ms/token)");
      break;
    case 's': printStatus(); break;
    case 'r':
      mode = MODE_NORMAL;
      jamAfter = 0;
      tokensThisRun = 0;
      totalTokens = 0;
      coastPending = false;
      Serial.println("[sim] reset");
      break;
    default: break;  // ignore newlines and noise
  }
}

void setup() {
  pinMode(MOTOR_WATCH_PIN, INPUT);
  pinMode(COIN_PULSE_PIN, OUTPUT);
  pinMode(ERROR_OUT_PIN, OUTPUT);
  digitalWrite(COIN_PULSE_PIN, HIGH);  // idle HIGH: inputs are active LOW
  digitalWrite(ERROR_OUT_PIN, HIGH);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[sim] Azkoyen Hopper U-II simulator ready");
  Serial.println("[sim] commands: n b c jN eN f s r  (see docs/hopper-simulator.md)");
}

void loop() {
  handleSerial();

  bool motorOn = digitalRead(MOTOR_WATCH_PIN) == HIGH;

  if (motorOn && !motorWasOn) {
    tokensThisRun = 0;
    coastPending = false;
    lastTokenMs = millis();
    Serial.println("[sim] motor ON");
  }

  if (!motorOn && motorWasOn) {
    motorStoppedMs = millis();
    coastPending = (mode == MODE_COAST);
    Serial.print("[sim] motor OFF after ");
    Serial.print(tokensThisRun);
    Serial.println(" token(s)");
  }
  motorWasOn = motorOn;

  // One extra token that slipped through while the disc coasted to a stop.
  // The dispenser has already cut the motor, so nothing but a firmware that
  // keeps counting after the stop will ever see it.
  if (coastPending && !motorOn && millis() - motorStoppedMs >= COAST_DELAY_MS) {
    coastPending = false;
    totalTokens++;
    sendPulse(PULSE_LOW_MS);
    Serial.println("[sim] coast pulse sent");
  }

  if (!motorOn) {
    return;
  }

  if (mode == MODE_JAM && tokensThisRun >= jamAfter) {
    return;  // jammed: the motor runs, nothing comes out
  }

  if (millis() - lastTokenMs < tokenIntervalMs()) {
    return;
  }
  lastTokenMs = millis();
  tokensThisRun++;
  totalTokens++;

  if (mode == MODE_BOUNCE) {
    sendBounceBurst();
  } else {
    sendPulse(PULSE_LOW_MS);
  }
}
