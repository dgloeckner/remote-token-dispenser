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

public:
    ErrorDecoder errorDecoder;
    ErrorHistory errorHistory;

    HopperControlMock() : pulse_count(0), jam_detected(false), motor_running(false) {}

    void begin() {}

    void startMotor() {
        motor_running = true;
    }

    void stopMotor() {
        motor_running = false;
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
