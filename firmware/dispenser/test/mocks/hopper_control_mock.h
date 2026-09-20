// Test double for HopperControl — implements the production IHopper interface.
// It models the two hardware behaviours the manager depends on: the pulse
// counter and the ISR-level motor stop armed via setMotorStopAt().
#ifndef HOPPER_CONTROL_MOCK_H
#define HOPPER_CONTROL_MOCK_H

#include <stdint.h>

#include "interfaces.h"

class HopperControlMock : public IHopper {
private:
    uint8_t pulse_count;
    bool jam_detected;
    bool motor_running;
    uint8_t isr_stop_at;    // Target count at which the ISR stops the motor
    uint8_t decoded_error;  // a hopper error waiting to be taken
    int start_motor_calls;
    int reset_pulse_calls;

public:
    HopperControlMock()
      : pulse_count(0), jam_detected(false), motor_running(false), isr_stop_at(0),
        decoded_error(0), start_motor_calls(0), reset_pulse_calls(0) {}

    void begin() {}

    void startMotor() override {
        motor_running = true;
        start_motor_calls++;
    }

    void stopMotor() override {
        motor_running = false;
        isr_stop_at = 0;
    }

    // Arm ISR-level stop: motor GPIO is written LOW the moment pulse_count
    // reaches this target, without waiting for the main loop()
    void setMotorStopAt(uint8_t count) override {
        isr_stop_at = count;
    }

    uint8_t getMotorStopAt() const {
        return isr_stop_at;
    }

    // Simulate a coin-pulse ISR firing (FALLING edge on COIN_PULSE_PIN).
    // Increments pulse_count and, if the count reaches isr_stop_at, stops
    // the motor immediately — exactly what the real ISR does.
    void simulatePulseISR() {
        pulse_count++;
        if (isr_stop_at > 0 && pulse_count >= isr_stop_at) {
            motor_running = false;
            isr_stop_at = 0;
        }
    }

    uint8_t getPulseCount() override {
        return pulse_count;
    }

    void resetPulseCount() override {
        pulse_count = 0;
        reset_pulse_calls++;
    }

    bool checkJam() override {
        return jam_detected;
    }

    // The decoded hopper error the manager has not seen yet (issue #6).
    // Consumed by the call, exactly like the production HopperControl.
    uint8_t takeDecodedError() override {
        uint8_t code = decoded_error;
        decoded_error = 0;
        return code;
    }

    // Test helper: the hopper reported error code `code` (1-7).  On the
    // device this is the pulse-encoded error line, decoded in loop().
    void reportError(uint8_t code) {
        decoded_error = code;
    }

    // Test helpers
    void setPulseCount(uint8_t count) {
        pulse_count = count;
    }

    void setJamDetected(bool jammed) {
        jam_detected = jammed;
    }

    bool isMotorRunning() const {
        return motor_running;
    }

    int getStartMotorCalls() const { return start_motor_calls; }
    int getResetPulseCalls() const { return reset_pulse_calls; }
};

#endif // HOPPER_CONTROL_MOCK_H
