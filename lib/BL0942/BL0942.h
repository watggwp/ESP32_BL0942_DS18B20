#pragma once
// Driver for the Belling BL0942 calibration-free single-phase energy metering IC.
//
// Implements the UART slave protocol exactly as specified in:
//   BL0942 datasheet V1.06, section 3.2 (UART) and section 1.5/1.6 (registers).
//   https://www.belling.com.cn/media/file_object/bel_product/BL0942/datasheet/BL0942_V1.06_en.pdf
//
// Frame formats (IC address 0, i.e. A2_NCS=A1=0 as wired on this board):
//   Write : TX [0xA8, ADDR, DATA_L, DATA_M, DATA_H, CHECKSUM]
//   Read  : TX [0x58, ADDR]           RX [DATA_L, DATA_M, DATA_H, CHECKSUM]
//   CHECKSUM = (~(sum of all preceding bytes)) & 0xFF
//
// Packet read-all (datasheet 3.2.6): TX [0x58, 0xAA] RX 23 bytes starting
// with 0x55, containing I_RMS, V_RMS, I_FAST_RMS, WATT, CF_CNT, FREQ, STATUS
// and a trailing checksum -- one exchange instead of six separate reads.

#include <Arduino.h>

// Register addresses (BL0942 datasheet, section 1.5)
enum BL0942Register : uint8_t {
    BL0942_REG_I_WAVE          = 0x01,
    BL0942_REG_V_WAVE          = 0x02,
    BL0942_REG_I_RMS           = 0x03,
    BL0942_REG_V_RMS           = 0x04,
    BL0942_REG_I_FAST_RMS      = 0x05,
    BL0942_REG_WATT            = 0x06,
    BL0942_REG_CF_CNT          = 0x07,
    BL0942_REG_FREQ            = 0x08,
    BL0942_REG_STATUS          = 0x09,
    BL0942_REG_I_RMSOS         = 0x12,
    BL0942_REG_WA_CREEP        = 0x14,
    BL0942_REG_I_FAST_RMS_TH   = 0x15,
    BL0942_REG_I_FAST_RMS_CYC  = 0x16,
    BL0942_REG_FREQ_CYC        = 0x17,
    BL0942_REG_OT_FUNX         = 0x18,
    BL0942_REG_MODE            = 0x19,
    BL0942_REG_GAIN_CR         = 0x1A,
    BL0942_REG_SOFT_RESET      = 0x1C,
    BL0942_REG_USR_WRPROT      = 0x1D,
};

// One fully decoded, calibrated sample.
struct BL0942Data {
    bool     valid        = false;
    float    voltageV     = 0;
    float    currentA     = 0;
    float    activePowerW = 0;   // signed: negative = reverse energy flow
    float    frequencyHz  = 0;
    uint32_t rawIRms      = 0;
    uint32_t rawVRms      = 0;
    int32_t  rawWatt      = 0;
    uint32_t rawCfCnt     = 0;
    uint16_t rawFreq      = 0;
    uint16_t status       = 0;
    bool     noLoad       = false; // CREEP_F: anti-creep, no real load present
};

class BL0942 {
public:
    // serial: a HardwareSerial already free to dedicate to this IC (e.g. Serial2).
    // rxPin/txPin: ESP32 GPIOs wired to the BL0942 TX/SDO and RX/SDI pins.
    // baud: must match SCLK_BPS pin strap / MODE[9:8] register (this board = 9600bps).
    void begin(HardwareSerial &serial, int rxPin, int txPin, uint32_t baud = 9600, uint8_t address = 0);

    // Push the calibration-relevant registers (MODE, GAIN_CR) to known values.
    // Safe to call every boot; requires the USR_WRPROT unlock sequence first.
    bool configure(bool acFreq60Hz = false);

    // Single register access.
    bool writeRegister(uint8_t reg, uint32_t data);
    bool readRegister(uint8_t reg, uint32_t &outData);

    // One-shot read of every metering register via the 0xAA packet-read command.
    bool readAll(BL0942Data &out);

    // raw register -> physical unit, using the datasheet formulas plus the
    // runtime calibration multipliers set by setCalibration() (see README for
    // the calibration procedure -- CT/VT/burden tolerances make an on-datasheet
    // constant only a starting point, not a guarantee of accuracy).
    float currentFromRaw(uint32_t raw) const;
    float voltageFromRaw(uint32_t raw) const;
    float powerFromRaw(int32_t raw) const;
    static float frequencyFromRaw(uint16_t raw);

    // kI/kV/kP: multiply the nominal datasheet-derived reading to correct for
    // real-world component tolerance. 1.0 = trust the nominal formula as-is.
    void setCalibration(float kI, float kV, float kP);
    void getCalibration(float &kI, float &kV, float &kP) const;

    // When enabled, readAll()/readExact() print raw bytes and the
    // expected-vs-received checksum to Serial on every failure -- useful
    // while bringing up a new board (wrong address/baud show up immediately).
    void setDebug(bool enabled) { _debug = enabled; }

private:
    HardwareSerial *_serial = nullptr;
    uint8_t _readHead  = 0x58; // {0,1,0,1,1,0,A2,A1}
    uint8_t _writeHead = 0xA8; // {1,0,1,0,1,0,A2,A1}
    bool _debug = false;

    float _kI = 1.0f;
    float _kV = 1.0f;
    float _kP = 1.0f;

    bool unlockWriteProtect();
    void flushInput();
    bool readExact(uint8_t *buf, size_t len, uint32_t timeoutMs = 100);
};
