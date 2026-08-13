#include "StatusLED.h"

void StatusLED::begin(uint8_t pin, bool activeHigh) {
    _pin = pin;
    _activeHigh = activeHigh;
    pinMode(_pin, OUTPUT);
    write(false);
}

void StatusLED::write(bool on) {
    _state = on;
    digitalWrite(_pin, on == _activeHigh ? HIGH : LOW);
}

void StatusLED::setMode(LedMode mode) {
    if (_mode == mode) return;
    _mode = mode;
    _lastToggle = millis();
    _errorPhase = 0;
    if (mode == LedMode::OFF) write(false);
    if (mode == LedMode::SOLID) write(true);
}

void StatusLED::update() {
    uint32_t now = millis();

    switch (_mode) {
        case LedMode::OFF:
        case LedMode::SOLID:
            return; // static, nothing to animate

        case LedMode::BLINK_SLOW:
            if (now - _lastToggle >= 500) {
                write(!_state);
                _lastToggle = now;
            }
            break;

        case LedMode::BLINK_FAST:
            if (now - _lastToggle >= 100) {
                write(!_state);
                _lastToggle = now;
            }
            break;

        case LedMode::BLINK_ERROR: {
            // Two quick pulses then a pause: on-off-on-off-pause, repeat.
            static const uint16_t pattern[] = {80, 80, 80, 700};
            uint16_t interval = pattern[_errorPhase % 4];
            if (now - _lastToggle >= interval) {
                write(_errorPhase % 2 == 0);
                _lastToggle = now;
                _errorPhase = (_errorPhase + 1) % 4;
            }
            break;
        }
    }
}
