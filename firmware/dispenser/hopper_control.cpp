// firmware/dispenser/hopper_control.cpp

#include "hopper_control.h"
#include "log.h"
#include "pulse_filter.h"

// Global instance pointer for ISR
static HopperControl* hopperControlInstance = nullptr;

// ISR for error pin change detection
void IRAM_ATTR handleErrorPinChange() {
  if (hopperControlInstance) {
    bool pinState = digitalRead(ERROR_SIGNAL_PIN);
    unsigned long now = micros();
    hopperControlInstance->errorDecoder.handlePinChange(pinState, now);
  }
}

// Motor Control Signal Chain (with NEGATIVE mode hopper and modified optocoupler):
//
//   startMotor() → digitalWrite(MOTOR_PIN, HIGH)
//     → D1 = 3.3V
//     → Current through optocoupler LED: 3.3V / 248Ω (modified R1) = 13.3mA
//     → PC817 phototransistor saturates
//     → OUT pulled LOW (< 0.5V)
//     → Hopper control pin LOW
//     → NEGATIVE mode: motor ON ✓
//
//   stopMotor() → digitalWrite(MOTOR_PIN, LOW)
//     → D1 = 0V
//     → No current through optocoupler LED
//     → PC817 phototransistor OFF
//     → OUT pulled HIGH by R2 (10kΩ) to ~6V (voltage divider with hopper input)
//     → Hopper control pin HIGH (~6V)
//     → NEGATIVE mode: motor OFF ✓
//
// ⚠️ CRITICAL HARDWARE DEPENDENCIES:
//   - Hopper DIP switch in NEGATIVE mode (active LOW)
//   - Optocoupler R1 modified (330Ω parallel) for 13.3mA drive current
//   - Without these: motor behavior unreliable or inverted

// Static variables for ISR
static volatile uint8_t pulse_count = 0;
static volatile unsigned long last_pulse_time = 0;
// Target count at which the ISR stops the motor immediately.
// Armed by setMotorStopAt() before startMotor(); cleared by stopMotor() or
// the ISR itself once triggered.  0 = disabled (don't stop in ISR).
static volatile uint8_t isr_stop_at = 0;

// Which falling edges are coins (issue #5).  Everything closer than
// COIN_PULSE_MIN_GAP_MS to the last accepted edge is a bouncing sensor or an
// EMI spike from the motor, and counting it used to end the dispense one token
// early — with nothing anywhere saying so.  The filter is a plain class in
// IRAM; the decision stays inside the ISR, where the motor stop is.
static PulseFilter coinPulseFilter((uint32_t)COIN_PULSE_MIN_GAP_MS * 1000UL);

void IRAM_ATTR HopperControl::handleCoinPulse() {
  if (!coinPulseFilter.accept(micros())) {
    return;  // noise, not a coin: no count, and above all no motor stop
  }
  pulse_count++;
  last_pulse_time = millis();
  // Stop motor immediately if target count reached, eliminating the up-to-10ms
  // delay between the pulse firing and the main loop() reacting.  This is the
  // primary mitigation for the double-dispense bug: a 2nd coin cannot exit if
  // the motor is stopped at ISR time rather than on the next loop iteration.
  // digitalWrite() is ISR-safe on ESP8266/Arduino.
  if (isr_stop_at > 0 && pulse_count >= isr_stop_at) {
    digitalWrite(MOTOR_PIN, LOW);
    isr_stop_at = 0;  // Clear so subsequent coast-pulses don't re-trigger
  }
}

void HopperControl::begin() {
  LOG_DEBUG("hopper: initializing");

  // Configure GPIO pins
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);  // Motor off at startup
  // Note: With D1→IN+ wiring, LOW = LED off = OUT high (~6V) = motor OFF (NEGATIVE mode)
  LOG_DEBUG("hopper: MOTOR_PIN OUTPUT LOW (reads %d)", digitalRead(MOTOR_PIN));

  pinMode(COIN_PULSE_PIN, INPUT_PULLUP);
  pinMode(ERROR_SIGNAL_PIN, INPUT_PULLUP);
  // D6 is deliberately not configured: the hopper's empty sensor is a factory
  // option our unit does not have, so the pin sat on its pull-up and said
  // "not empty" forever.  /health published that as data until issue #6.

  LOG_DEBUG("hopper: inputs INPUT_PULLUP, coin=%d error=%d",
            digitalRead(COIN_PULSE_PIN), digitalRead(ERROR_SIGNAL_PIN));

  // Attach interrupt for coin pulse (FALLING edge)
  attachInterrupt(digitalPinToInterrupt(COIN_PULSE_PIN),
                  handleCoinPulse, FALLING);
  LOG_DEBUG("hopper: coin-pulse interrupt attached (FALLING)");

  // Initialize error decoder
  errorDecoder.begin();

  // Set global instance for ISR
  hopperControlInstance = this;

  // Attach interrupt for error signal (CHANGE edge - both FALLING and RISING)
  attachInterrupt(digitalPinToInterrupt(ERROR_SIGNAL_PIN),
                  handleErrorPinChange, CHANGE);
  LOG_DEBUG("hopper: error-signal interrupt attached (CHANGE)");

  // Initialize pulse tracking
  pulse_count = 0;
  coinPulseFilter.reset();
  isr_stop_at = 0;
  decoded_error = 0;
  last_pulse_time = millis();

  LOG_INFO("hopper ready");
}

void HopperControl::startMotor() {
  // The pin first, the log after.  A blocking serial write between the two
  // would be time the motor is not yet running — and on the stop path below it
  // is time the motor is still running (issue #4).
  // GPIO HIGH → optocoupler LED ON → OUT LOW → motor ON (NEGATIVE mode)
  // Requires: R1 modified (330Ω parallel) for 13.3mA → saturation → OUT < 0.5V
  digitalWrite(MOTOR_PIN, HIGH);
  last_pulse_time = millis();  // Reset watchdog
  LOG_INFO("motor ON (D1 reads %d)", digitalRead(MOTOR_PIN));
}

void HopperControl::setMotorStopAt(uint8_t count) {
  noInterrupts();
  isr_stop_at = count;
  interrupts();
}

void HopperControl::stopMotor() {
  // Clear ISR stop target first so a racing ISR doesn't re-fire after we stop.
  noInterrupts();
  isr_stop_at = 0;
  interrupts();
  // The pin write comes before every log call.  It used to come after two
  // Serial.print lines, so on the jam path the motor kept turning for as long
  // as the UART needed to drain them (issue #4).
  // GPIO LOW → optocoupler LED OFF → OUT HIGH (~6V) → motor OFF (NEGATIVE mode)
  digitalWrite(MOTOR_PIN, LOW);
  LOG_INFO("motor OFF (D1 reads %d)", digitalRead(MOTOR_PIN));
}

uint8_t HopperControl::getPulseCount() {
  noInterrupts();
  uint8_t count = pulse_count;
  interrupts();
  return count;
}

void HopperControl::resetPulseCount() {
  pulse_count = 0;
  // A new transaction starts with no history of edges, so its first pulse is
  // never measured against the last one of the previous dispense.
  coinPulseFilter.reset();
  last_pulse_time = millis();
}

// Edges the filter threw away since the last reset.  Diagnostics only: a
// hopper whose sensor bounces says so here instead of quietly dispensing short.
uint32_t HopperControl::getFilteredPulseCount() {
  return coinPulseFilter.rejected();
}

bool HopperControl::checkJam() {
  // Check if no pulse received within JAM_TIMEOUT_MS
  noInterrupts();
  unsigned long last_time = last_pulse_time;
  interrupts();

  return (millis() - last_time > JAM_TIMEOUT_MS);
}

uint8_t HopperControl::getCoinPulseRaw() {
  return digitalRead(COIN_PULSE_PIN) == LOW ? 0 : 1;
}

bool HopperControl::isCoinPulseActive() {
  return digitalRead(COIN_PULSE_PIN) == LOW;
}

uint8_t HopperControl::getErrorSignalRaw() {
  return digitalRead(ERROR_SIGNAL_PIN) == LOW ? 0 : 1;
}

bool HopperControl::isErrorSignalActive() {
  return digitalRead(ERROR_SIGNAL_PIN) == LOW;
}

void HopperControl::updateErrorDecoder() {
  errorDecoder.update();

  if (errorDecoder.hasNewError()) {
    ErrorCode code = errorDecoder.getErrorCode();
    errorHistory.addError(code);
    errorDecoder.reset();
    // Hand it to the manager, which is the only place that may stop a motor
    // (issue #6).  Before this the error went into the history list and
    // nowhere else, so the hopper reported "motor fault" while the firmware
    // kept driving it until the 5 s jam timeout.
    decoded_error = (uint8_t)code;

    LOG_ERROR("hopper error %s - %s", errorCodeToString(code),
              errorCodeToDescription(code));
  }
}

uint8_t HopperControl::takeDecodedError() {
  noInterrupts();
  uint8_t code = decoded_error;
  decoded_error = 0;
  interrupts();
  return code;
}
