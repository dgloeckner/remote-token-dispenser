// Mock HopperControl for testing
#ifndef HOPPER_CONTROL_MOCK_H
#define HOPPER_CONTROL_MOCK_H

#include <stdint.h>

// Minimal mocks for dependencies
class ErrorDecoder {
public:
    void begin() {}
    void update() {}
};

class ErrorHistory {
public:
    void begin() {}
    void clearActive() {}
    void* getActive() { return nullptr; }
};

class HopperControlMock {
private:
    uint8_t pulse_count;
    bool jam_detected;
    bool motor_running;
    uint8_t isr_stop_at;  // Target count at which ISR should stop motor

public:
    ErrorDecoder errorDecoder;
    ErrorHistory errorHistory;

    HopperControlMock() : pulse_count(0), jam_detected(false), motor_running(false), isr_stop_at(0) {}

    void begin() {}

    void startMotor() {
        motor_running = true;
    }

    void stopMotor() {
        motor_running = false;
        isr_stop_at = 0;
    }

    // Arm ISR-level stop: motor GPIO is written LOW the moment pulse_count
    // reaches this target, without waiting for the main loop()
    void setMotorStopAt(uint8_t count) {
        isr_stop_at = count;
    }

    uint8_t getMotorStopAt() const {
        return isr_stop_at;
    }

    // Simulate a coin-pulse ISR firing (FALLING edge on COIN_PULSE_PIN).
    // Increments pulse_count and, if the count reaches isr_stop_at, stops
    // the motor immediately — exactly what the real ISR will do after the fix.
    void simulatePulseISR() {
        pulse_count++;
        if (isr_stop_at > 0 && pulse_count >= isr_stop_at) {
            motor_running = false;
            isr_stop_at = 0;
        }
    }

    uint8_t getPulseCount() {
        return pulse_count;
    }

    void resetPulseCount() {
        pulse_count = 0;
    }

    bool checkJam() {
        return jam_detected;
    }

    bool isHopperLow() {
        return false;
    }

    uint8_t getCoinPulseRaw() { return 0; }
    bool isCoinPulseActive() { return false; }
    uint8_t getErrorSignalRaw() { return 0; }
    bool isErrorSignalActive() { return false; }
    uint8_t getHopperLowRaw() { return 0; }
    void updateErrorDecoder() {}

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
};

#endif // HOPPER_CONTROL_MOCK_H
