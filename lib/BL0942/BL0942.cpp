#include "BL0942.h"

// ---------------------------------------------------------------------------
// Nominal (pre-calibration) conversion constants.
//
// Derived from the datasheet register formulas (section 2.2 / 2.5):
//   I_RMS = 305978 * Vi(mV) / Vref        Vref plugged in as the bare
//   V_RMS = 73989  * Vv(mV) / Vref        typical value 1.218 (not 1218mV --
//   WATT  = 3537 * Vi(mV)*Vv(mV)*cos(phi) / Vref^2   that's how the constants
//                                                     were fitted; verified by
//                                                     round-tripping full-scale
//                                                     inputs back to sane
//                                                     register magnitudes).
//
// Current channel (HIGH CONFIDENCE - fixed by this board's resistors):
//   CT ratio 2000:1, burden R30+R31 = 1ohm + 1ohm = 2ohm (per schematic note).
//   Vi(mV) = I_primary(A) / 2000 * 2ohm * 1000 = I_primary(A) * 1.0 mV/A
//
// Voltage channel (confirmed on hardware): line -> 100kohm series resistor
//   -> ZMPT107-1 primary (~1:1 turns ratio) -> R11 = 24.9ohm secondary burden.
//   I_primary = V_line / 100,000ohm  (e.g. 230V -> 2.3mA, consistent with the
//   ZMPT107-1's typical ~2mA-class rating -- a useful sanity check that the
//   1:1 ratio assumption holds).
//   Vv(mV) = I_primary(A) * R11(ohm) * 1000 / V_line(V) = 0.249 mV/V
//   (Was previously assumed at 0.1992 mV/V from a guessed 2mA@250V rating
//   before the real 100kohm series resistor was confirmed; that guess read
//   289.8V against a true 231.8V, a 289.8*(0.1992/0.249)=231.84V predicted
//   correction -- matching the measurement to within 0.02%.)
//
// Both LSBs below are still only a starting point -- CT/VT/burden component
// tolerance and the BL0942's actual (non-laser-trimmed) Vref mean you should
// still calibrate against a known load or reference meter; see README
// "Calibration" section. setCalibration() applies the correction on top of
// these nominal values.
static constexpr float BL0942_VREF          = 1.218f;     // on-chip reference (typical), per datasheet
static constexpr float BL0942_MV_PER_AMP    = 1.0f;       // CT 2000:1 with 2ohm burden
static constexpr float BL0942_MV_PER_VOLT   = 0.249f;     // 100kohm series R, ZMPT107-1 ~1:1, 24.9ohm burden (confirmed)

static constexpr float NOMINAL_CURRENT_LSB = BL0942_VREF / (305978.0f * BL0942_MV_PER_AMP);   // A / count
static constexpr float NOMINAL_VOLTAGE_LSB = BL0942_VREF / (73989.0f * BL0942_MV_PER_VOLT);   // V / count
static constexpr float NOMINAL_POWER_LSB   = (BL0942_VREF * BL0942_VREF) /
                                              (3537.0f * BL0942_MV_PER_AMP * BL0942_MV_PER_VOLT); // W / count

void BL0942::begin(HardwareSerial &serial, int rxPin, int txPin, uint32_t baud, uint8_t address) {
    _serial = &serial;

    // Datasheet pin description (RX/SDI, TX/SDO): "need external pull-up
    // resistor for UART interface" -- these lines aren't push-pull. If the
    // board is missing that pull-up, enable the ESP32's weak internal one as
    // a software-only fallback (same net, still helps bias TX/SDO to a valid
    // idle-high level even though it's the RX pin from our side).
    pinMode(rxPin, INPUT_PULLUP);
    pinMode(txPin, INPUT_PULLUP);

    _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
    _serial->setTimeout(100);

    // {1,0,1,0,1,0,A2,A1} / {0,1,0,1,1,0,A2,A1} -- address is 2 bits (A2,A1)
    uint8_t a = address & 0x03;
    _writeHead = 0xA8 | a;
    _readHead  = 0x58 | a;
}

void BL0942::flushInput() {
    while (_serial->available()) {
        _serial->read();
    }
}

bool BL0942::readExact(uint8_t *buf, size_t len, uint32_t timeoutMs) {
    uint32_t start = millis();
    size_t got = 0;
    while (got < len) {
        if (_serial->available()) {
            buf[got++] = _serial->read();
        } else if (millis() - start > timeoutMs) {
            if (_debug) {
                Serial.printf("BL0942: timeout, got %u/%u bytes -- check wiring, address (0x%02X), and baud rate\n",
                              (unsigned)got, (unsigned)len, _readHead);
            }
            return false;
        }
    }
    return true;
}

bool BL0942::unlockWriteProtect() {
    return writeRegister(BL0942_REG_USR_WRPROT, 0x55);
}

bool BL0942::writeRegister(uint8_t reg, uint32_t data) {
    uint8_t dataL = data & 0xFF;
    uint8_t dataM = (data >> 8) & 0xFF;
    uint8_t dataH = (data >> 16) & 0xFF;
    uint8_t sum = _writeHead + reg + dataL + dataM + dataH;
    uint8_t checksum = (~sum) & 0xFF;

    flushInput();
    uint8_t frame[6] = {_writeHead, reg, dataL, dataM, dataH, checksum};
    _serial->write(frame, sizeof(frame));
    _serial->flush();
    return true;
}

bool BL0942::readRegister(uint8_t reg, uint32_t &outData) {
    flushInput();
    uint8_t cmd[2] = {_readHead, reg};
    _serial->write(cmd, sizeof(cmd));
    _serial->flush();

    uint8_t resp[4];
    if (!readExact(resp, sizeof(resp))) {
        return false;
    }
    uint8_t dataL = resp[0], dataM = resp[1], dataH = resp[2], checksum = resp[3];
    uint8_t expected = (~(uint8_t)(_readHead + reg + dataL + dataM + dataH)) & 0xFF;
    if (checksum != expected) {
        return false;
    }
    outData = ((uint32_t)dataH << 16) | ((uint32_t)dataM << 8) | dataL;
    return true;
}

bool BL0942::configure(bool acFreq60Hz) {
    if (!unlockWriteProtect()) {
        return false;
    }
    delayMicroseconds(200); // t3 frame-to-frame delay (datasheet min 0.5us, generous margin)

    // MODE (0x19): CF_EN=1, RMS refresh=400ms, FAST_RMS from full wave,
    // AC freq per argument, no CF_CNT clear-on-read, absolute energy
    // accumulation, baud rate follows the SCLK_BPS pin strap (9600bps here).
    uint32_t mode = 0;
    mode |= (1 << 0);               // reserved, datasheet default 1
    mode |= (1 << 1);               // reserved, datasheet default 1
    mode |= (1 << 2);               // CF_EN
    mode |= (0 << 3);               // RMS_UPDATE_SEL: 400ms
    mode |= (0 << 4);               // FAST_RMS_SEL: full wave
    mode |= ((acFreq60Hz ? 1 : 0) << 5); // AC_FREQ_SEL
    mode |= (0 << 6);               // CF_CNT_CLR_SEL: don't clear on read
    mode |= (1 << 7);               // CF_CNT_ADD_SEL: absolute accumulation
    mode |= (0b00 << 8);            // UART_RATE_SEL: follow SCLK_BPS pin

    if (!writeRegister(BL0942_REG_MODE, mode)) {
        return false;
    }
    delayMicroseconds(200);

    // GAIN_CR (0x1A): current channel gain = 16 (matches the datasheet
    // formula constants used by currentFromRaw()/powerFromRaw()).
    if (!writeRegister(BL0942_REG_GAIN_CR, 0b10)) {
        return false;
    }
    return true;
}

bool BL0942::readAll(BL0942Data &out) {
    flushInput();
    uint8_t cmd[2] = {_readHead, 0xAA};
    _serial->write(cmd, sizeof(cmd));
    _serial->flush();

    uint8_t resp[23];
    if (!readExact(resp, sizeof(resp))) {
        out.valid = false;
        return false;
    }

    if (resp[0] != 0x55) {
        if (_debug) {
            Serial.printf("BL0942: bad packet header 0x%02X (expected 0x55)\n", resp[0]);
        }
        out.valid = false;
        return false;
    }

    // checksum = ~(readHead + HEAD(0x55) + data[1..21]) & 0xFF -- per datasheet
    // 3.2.6. The 0xAA sent as the second command byte is NOT part of the sum;
    // it only selects packet-read mode.
    uint32_t sum = _readHead;
    for (int i = 0; i < 22; i++) {
        sum += resp[i];
    }
    uint8_t expected = (~(uint8_t)sum) & 0xFF;
    if (resp[22] != expected) {
        if (_debug) {
            Serial.printf("BL0942: checksum mismatch, got 0x%02X expected 0x%02X. Raw:", resp[22], expected);
            for (int i = 0; i < 23; i++) Serial.printf(" %02X", resp[i]);
            Serial.println();
        }
        out.valid = false;
        return false;
    }

    out.rawIRms  = ((uint32_t)resp[3] << 16) | ((uint32_t)resp[2] << 8) | resp[1];
    out.rawVRms  = ((uint32_t)resp[6] << 16) | ((uint32_t)resp[5] << 8) | resp[4];
    // resp[7..9] = I_FAST_RMS, not used for the primary readings.

    uint32_t wattRaw = ((uint32_t)resp[12] << 16) | ((uint32_t)resp[11] << 8) | resp[10];
    if (wattRaw & 0x800000) { // sign-extend 24-bit two's complement
        wattRaw |= 0xFF000000;
    }
    out.rawWatt = (int32_t)wattRaw;

    out.rawCfCnt = ((uint32_t)resp[15] << 16) | ((uint32_t)resp[14] << 8) | resp[13];
    out.rawFreq  = ((uint16_t)resp[17] << 8) | resp[16];
    out.status   = resp[19];

    out.currentA     = currentFromRaw(out.rawIRms);
    out.voltageV      = voltageFromRaw(out.rawVRms);
    out.activePowerW = powerFromRaw(out.rawWatt);
    out.frequencyHz  = frequencyFromRaw(out.rawFreq);
    out.noLoad       = out.status & (1 << 1); // CREEP_F
    out.valid        = true;
    return true;
}

float BL0942::currentFromRaw(uint32_t raw) const {
    return raw * NOMINAL_CURRENT_LSB * _kI;
}

float BL0942::voltageFromRaw(uint32_t raw) const {
    return raw * NOMINAL_VOLTAGE_LSB * _kV;
}

float BL0942::powerFromRaw(int32_t raw) const {
    return raw * NOMINAL_POWER_LSB * _kP;
}

float BL0942::frequencyFromRaw(uint16_t raw) {
    if (raw == 0) return 0;
    return 1000000.0f / raw;
}

void BL0942::setCalibration(float kI, float kV, float kP) {
    _kI = kI;
    _kV = kV;
    _kP = kP;
}

void BL0942::getCalibration(float &kI, float &kV, float &kP) const {
    kI = _kI;
    kV = _kV;
    kP = _kP;
}
