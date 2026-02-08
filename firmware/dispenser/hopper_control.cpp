// firmware/dispenser/hopper_control.cpp

#include "hopper_control.h"

// Static variables for ISR
static volatile uint8_t pulse_count = 0;
static volatile unsigned long last_pulse_time = 0;

void IRAM_ATTR HopperControl::handleCoinPulse() {
  pulse_count++;
  last_pulse_time = millis();
}

void HopperControl::begin() {
  // Configure GPIO pins
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);  // Motor off

  pinMode(COIN_PULSE_PIN, INPUT_PULLUP);
  pinMode(HOPPER_LOW_PIN, INPUT_PULLUP);

  // Attach interrupt for coin pulse (FALLING edge)
  attachInterrupt(digitalPinToInterrupt(COIN_PULSE_PIN),
                  handleCoinPulse, FALLING);

  // Initialize pulse tracking
  pulse_count = 0;
  last_pulse_time = millis();
}

void HopperControl::startMotor() {
  digitalWrite(MOTOR_PIN, HIGH);
  last_pulse_time = millis();  // Reset watchdog
}

void HopperControl::stopMotor() {
  digitalWrite(MOTOR_PIN, LOW);
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
