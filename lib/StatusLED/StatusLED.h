#pragma once
// Non-blocking status LED on GPIO2 (ESP32 dev board's onboard blue LED).
// Call update() every loop() iteration; it never delay()s.

#include <Arduino.h>

enum class LedMode {
    OFF,
    SOLID,        // steady on -- fully up and healthy (e.g. Wi-Fi connected)
    BLINK_SLOW,   // 1x/sec -- normal operation heartbeat
    BLINK_FAST,   // 5x/sec -- initializing / connecting
    BLINK_ERROR,  // short double-blink pulse train -- sensor/comm fault
};

class StatusLED {
public:
    void begin(uint8_t pin = 2, bool activeHigh = true);
    void setMode(LedMode mode);
    void update();

private:
    uint8_t _pin = 2;
    bool _activeHigh = true;
    LedMode _mode = LedMode::OFF;
    uint32_t _lastToggle = 0;
    bool _state = false;
    uint8_t _errorPhase = 0;

    void write(bool on);
};
