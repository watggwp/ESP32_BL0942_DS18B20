// Example 1 -- read the BL0942 power meter + 9x DS18B20 temperature sensors
// and print everything to Serial. No Wi-Fi, no web server: the simplest
// possible smoke test that the wiring and calibration are sane.
//
// Wiring (see README.md / schematic for the full picture):
//   BL0942 TX/SDO (pin14) -> ESP32 GPIO16
//   BL0942 RX/SDI (pin13) -> ESP32 GPIO17
//   DS18B20 data (all 9, shared) -> ESP32 GPIO4, 4.7k pull-up to 3V3
//   Status LED -> ESP32 GPIO2

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "BL0942.h"
#include "StatusLED.h"

BL0942 meter;
StatusLED led;

OneWire oneWire(ONEWIRE_PIN);
DallasTemperature sensors(&oneWire);
DeviceAddress sensorAddr[DS18B20_COUNT];
uint8_t sensorCount = 0;

uint32_t lastRead = 0;

static void printAddress(const DeviceAddress addr) {
    for (uint8_t i = 0; i < 8; i++) {
        if (addr[i] < 0x10) Serial.print('0');
        Serial.print(addr[i], HEX);
    }
}

static void scanDS18B20() {
    sensors.begin();
    sensorCount = min((int)sensors.getDeviceCount(), DS18B20_COUNT);

    Serial.printf("DS18B20: found %u device(s) on GPIO%d\n", sensorCount, ONEWIRE_PIN);
    if (sensorCount < DS18B20_COUNT) {
        Serial.printf("  (expected %u -- check wiring / 4.7k pull-up if fewer showed up)\n", DS18B20_COUNT);
    }

    for (uint8_t i = 0; i < sensorCount; i++) {
        if (sensors.getAddress(sensorAddr[i], i)) {
            sensors.setResolution(sensorAddr[i], DS18B20_RESOLUTION);
            Serial.printf("  #%u  addr=", i);
            printAddress(sensorAddr[i]);
            Serial.println();
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ESP32 + BL0942 + DS18B20 -- Serial Monitor Example ===");

    led.begin(STATUS_LED_PIN, STATUS_LED_ACTIVE_HIGH);
    led.setMode(LedMode::BLINK_FAST);

    meter.begin(Serial2, BL0942_RX_PIN, BL0942_TX_PIN, BL0942_BAUD, BL0942_ADDRESS);
    meter.setDebug(BL0942_DEBUG);
    if (meter.configure(BL0942_AC_FREQ_60HZ)) {
        Serial.println("BL0942: configured OK");
    } else {
        Serial.println("BL0942: configure() FAILED -- check UART wiring/pins");
    }

    scanDS18B20();

    led.setMode(LedMode::BLINK_SLOW);
    lastRead = millis();
}

void loop() {
    led.update();

    if (millis() - lastRead < SENSOR_READ_INTERVAL_MS) {
        return;
    }
    lastRead = millis();

    BL0942Data e;
    bool ok = meter.readAll(e);

    sensors.requestTemperatures();

    Serial.println("--------------------------------------------------------------");
    if (ok) {
        Serial.printf("Voltage: %6.1f V   Current: %6.3f A   Power: %7.1f W   Freq: %5.2f Hz%s\n",
                      e.voltageV, e.currentA, e.activePowerW, e.frequencyHz,
                      e.noLoad ? "   [no-load]" : "");
    } else {
        Serial.println("BL0942: read failed (checksum/timeout) -- check wiring");
        led.setMode(LedMode::BLINK_ERROR);
    }

    for (uint8_t i = 0; i < sensorCount; i++) {
        float c = sensors.getTempC(sensorAddr[i]);
        if (c == DEVICE_DISCONNECTED_C) {
            Serial.printf("  T%u: disconnected\n", i);
        } else {
            Serial.printf("  T%u: %6.2f C\n", i, c);
        }
    }

    if (ok) {
        led.setMode(LedMode::BLINK_SLOW);
    }
}
