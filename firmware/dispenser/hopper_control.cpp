// firmware/dispenser/hopper_control.cpp

#include "hopper_control.h"

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

void IRAM_ATTR HopperControl::handleCoinPulse() {
  pulse_count++;
  last_pulse_time = millis();
  // Note: Serial.print in ISR can cause crashes, so we just count
}

void HopperControl::begin() {
  Serial.println("[HopperControl] Initializing...");

  // Configure GPIO pins
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);  // Motor off at startup
  // Note: With D1→IN+ wiring, LOW = LED off = OUT high (~6V) = motor OFF (NEGATIVE mode)
  Serial.print("[HopperControl] MOTOR_PIN (D1) configured as OUTPUT, set to LOW (motor OFF)");
  Serial.print(" - Current state: ");
  Serial.println(digitalRead(MOTOR_PIN));

  pinMode(COIN_PULSE_PIN, INPUT_PULLUP);
  pinMode(ERROR_SIGNAL_PIN, INPUT_PULLUP);
  pinMode(HOPPER_LOW_PIN, INPUT_PULLUP);

  Serial.println("[HopperControl] Input pins configured with INPUT_PULLUP");
  Serial.print("  COIN_PULSE_PIN (D7): ");
  Serial.println(digitalRead(COIN_PULSE_PIN));
  Serial.print("  ERROR_SIGNAL_PIN (D5): ");
  Serial.println(digitalRead(ERROR_SIGNAL_PIN));
  Serial.print("  HOPPER_LOW_PIN (D6): ");
  Serial.println(digitalRead(HOPPER_LOW_PIN));

  // Attach interrupt for coin pulse (FALLING edge)
  attachInterrupt(digitalPinToInterrupt(COIN_PULSE_PIN),
                  handleCoinPulse, FALLING);
  Serial.println("[HopperControl] Interrupt attached to COIN_PULSE_PIN (FALLING edge)");

  // Initialize pulse tracking
  pulse_count = 0;
  last_pulse_time = millis();
  Serial.println("[HopperControl] Initialization complete");
}

void HopperControl::startMotor() {
  Serial.println("[HopperControl] *** STARTING MOTOR ***");
  Serial.print("  Setting MOTOR_PIN (D1) to HIGH (motor ON)...");
  // GPIO HIGH → optocoupler LED ON → OUT LOW → motor ON (NEGATIVE mode)
  // Requires: R1 modified (330Ω parallel) for 13.3mA → saturation → OUT < 0.5V
  digitalWrite(MOTOR_PIN, HIGH);
  Serial.print(" - Current state: ");
  Serial.println(digitalRead(MOTOR_PIN));
  last_pulse_time = millis();  // Reset watchdog
  Serial.println("[HopperControl] Motor started, watchdog reset");
}

void HopperControl::stopMotor() {
  Serial.println("[HopperControl] *** STOPPING MOTOR ***");
  Serial.print("  Setting MOTOR_PIN (D1) to LOW (motor OFF)...");
  // GPIO LOW → optocoupler LED OFF → OUT HIGH (~6V) → motor OFF (NEGATIVE mode)
  digitalWrite(MOTOR_PIN, LOW);
  Serial.print(" - Current state: ");
  Serial.println(digitalRead(MOTOR_PIN));
  Serial.println("[HopperControl] Motor stopped");
}

uint8_t HopperControl::getPulseCount() {
  noInterrupts();
  uint8_t count = pulse_count;
  interrupts();
  return count;
}

void HopperControl::resetPulseCount() {
  pulse_count = 0;
  last_pulse_time = millis();
}

bool HopperControl::checkJam() {
  // Check if no pulse received within JAM_TIMEOUT_MS
  noInterrupts();
  unsigned long last_time = last_pulse_time;
  interrupts();

  return (millis() - last_time > JAM_TIMEOUT_MS);
}

bool HopperControl::isHopperLow() {
  // Hopper low sensor is active LOW
  return digitalRead(HOPPER_LOW_PIN) == LOW;
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

uint8_t HopperControl::getHopperLowRaw() {
  return digitalRead(HOPPER_LOW_PIN) == LOW ? 0 : 1;
}
