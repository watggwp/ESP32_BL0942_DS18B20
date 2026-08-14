// Example 2 -- same BL0942 + 9x DS18B20 reads as example 1, plus a live
// Wi-Fi dashboard: ESPAsyncWebServer serves a self-contained dark-themed UI
// (dashboard_html.h) and pushes JSON updates once a second over
// Server-Sent Events. Calibration factors and the accumulated energy total
// persist across reboots in NVS (Preferences).
//
// DS18B20s are addressed by slot, not by bus order: /settings assigns each
// sensor's ROM address to a fixed dashboard position and a name, stored in NVS,
// so "slot 3" stays the same physical sensor across reboots and rewiring.
//
// Wi-Fi credentials also live in NVS and are entered from a phone at /wifi --
// see wifi_portal.h. Nothing about the network is compiled in.

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "config.h"
#include "BL0942.h"
#include "StatusLED.h"
#include "dashboard_html.h"
#include "settings_html.h"
#include "thermal_js.h"
#include "wifi_portal.h"

BL0942 meter;
StatusLED led;
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature sensors(&oneWire);

Preferences prefs;        // "meter"   -- calibration + energy total
Preferences sensorPrefs;  // "sensors" -- the slot map below
AsyncWebServer server(80);
AsyncEventSource events("/events");

// A slot is a fixed position on the dashboard. It owns a ROM address rather
// than a bus index, which is the whole point: OneWire enumerates by ROM code,
// so pulling one sensor renumbers every sensor after it. Anchored to the
// address, a missing sensor just leaves its own slot showing "offline".
struct SensorSlot {
    DeviceAddress addr;
    char name[48];   // UTF-8, so ~15 Thai characters
    bool online;     // seen on the bus during the last scan
};
SensorSlot slots[DS18B20_COUNT];
uint8_t slotCount = 0;
uint32_t configVersion = 1;   // bumped on every change so open pages refetch names
volatile bool rescanRequested = false;

float energyKWh = 0;
uint32_t lastRead = 0;
uint32_t lastEnergyReadMs = 0;
uint32_t lastPersist = 0;
BL0942Data lastSample;

// ---------------------------------------------------------------------------
// Sensor slot map
// ---------------------------------------------------------------------------
static void addrToHex(const DeviceAddress addr, char *out /* >= 17 bytes */) {
    static const char *digits = "0123456789ABCDEF";   // not HEX: Print.h defines that
    for (uint8_t i = 0; i < 8; i++) {
        out[i * 2]     = digits[addr[i] >> 4];
        out[i * 2 + 1] = digits[addr[i] & 0x0F];
    }
    out[16] = '\0';
}

static bool hexToAddr(const char *hex, DeviceAddress out) {
    if (!hex || strlen(hex) != 16) return false;
    for (uint8_t i = 0; i < 16; i++) {
        char c = hex[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9')      nibble = c - '0';
        else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
        else return false;
        if (i % 2 == 0) out[i / 2] = nibble << 4;
        else            out[i / 2] |= nibble;
    }
    return true;
}

// Names are UTF-8; a blind strncpy would happily cut a Thai character in half
// and leave an invalid sequence that breaks the JSON we serve it back in.
static void copyName(char *dest, size_t size, const char *src) {
    size_t n = strlen(src);
    if (n > size - 1) {
        n = size - 1;
        while (n > 0 && (src[n] & 0xC0) == 0x80) n--;   // back off to a char boundary
    }
    memcpy(dest, src, n);
    dest[n] = '\0';
}

static uint8_t scanBus(DeviceAddress *out, uint8_t max) {
    sensors.begin();
    uint8_t onBus = min((int)sensors.getDeviceCount(), (int)max);
    uint8_t got = 0;
    for (uint8_t i = 0; i < onBus; i++) {
        if (sensors.getAddress(out[got], i)) {
            sensors.setResolution(out[got], DS18B20_RESOLUTION);
            got++;
        }
    }
    return got;
}

// Rebuild the slot table from the saved map plus whatever is actually on the
// bus right now. Saved entries keep their slot even when the sensor is absent;
// a sensor nobody has configured yet lands in the first free slot.
static void applySensorMap() {
    DeviceAddress found[DS18B20_COUNT];
    uint8_t foundCount = scanBus(found, DS18B20_COUNT);
    bool claimed[DS18B20_COUNT] = {false};

    memset(slots, 0, sizeof(slots));
    slotCount = 0;

    String raw = sensorPrefs.getString("map", "");
    if (raw.length()) {
        JsonDocument doc;
        if (deserializeJson(doc, raw) == DeserializationError::Ok) {
            for (JsonObject entry : doc.as<JsonArray>()) {
                if (slotCount >= DS18B20_COUNT) break;
                DeviceAddress addr;
                if (!hexToAddr(entry["a"] | "", addr)) continue;
                SensorSlot &s = slots[slotCount];
                memcpy(s.addr, addr, 8);
                copyName(s.name, sizeof(s.name), entry["n"] | "");
                for (uint8_t k = 0; k < foundCount; k++) {
                    if (memcmp(found[k], addr, 8) == 0) {
                        s.online = true;
                        claimed[k] = true;
                        break;
                    }
                }
                slotCount++;
            }
        } else {
            Serial.println("Sensors: stored map is unreadable -- falling back to bus order");
        }
    }

    for (uint8_t k = 0; k < foundCount && slotCount < DS18B20_COUNT; k++) {
        if (claimed[k]) continue;
        SensorSlot &s = slots[slotCount];
        memcpy(s.addr, found[k], 8);
        s.online = true;
        slotCount++;
    }

    Serial.printf("DS18B20: %u on the bus (GPIO%d), %u slot(s)\n", foundCount, ONEWIRE_PIN, slotCount);
    for (uint8_t i = 0; i < slotCount; i++) {
        char hex[17];
        addrToHex(slots[i].addr, hex);
        Serial.printf("  slot %u  %s  %s%s\n", i, hex,
                      slots[i].name[0] ? slots[i].name : "(unnamed)",
                      slots[i].online ? "" : "   [offline]");
    }
}

static void saveSensorMap() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < slotCount; i++) {
        char hex[17];
        addrToHex(slots[i].addr, hex);
        JsonObject entry = arr.add<JsonObject>();
        entry["a"] = hex;             // char[] -- ArduinoJson copies it
        entry["n"] = slots[i].name;
    }
    String out;
    serializeJson(doc, out);
    sensorPrefs.putString("map", out);
    configVersion++;
    Serial.printf("Sensors: saved %u slot(s), %u bytes\n", slotCount, out.length());
}

static void loadCalibration() {
    prefs.begin("meter", false);
    float kI = prefs.getFloat("kI", 1.0f);
    float kV = prefs.getFloat("kV", 1.0f);
    float kP = prefs.getFloat("kP", 1.0f);
    meter.setCalibration(kI, kV, kP);
    energyKWh = prefs.getFloat("kwh", 0.0f);
    Serial.printf("Loaded calibration kI=%.4f kV=%.4f kP=%.4f, energy=%.3f kWh\n", kI, kV, kP, energyKWh);
}

// A fault on the meter outranks everything: a board that is on the network but
// not reading is the more urgent of the two problems.
static void updateLed(bool meterOk) {
    if (!meterOk) {
        led.setMode(LedMode::BLINK_ERROR);
    } else switch (WiFiPortal::state()) {
        case WiFiPortalState::CONNECTED:  led.setMode(LedMode::SOLID);      break;
        case WiFiPortalState::PORTAL:     led.setMode(LedMode::BLINK_SLOW); break;   // waiting for setup
        case WiFiPortalState::CONNECTING: led.setMode(LedMode::BLINK_FAST); break;
    }
}

static void sendSensorList(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["version"] = configVersion;
    doc["fw"] = FIRMWARE_VERSION;      // both pages that fetch this show it in
    doc["build"] = FIRMWARE_BUILD;     // their footer -- see config.h
    doc["max"] = DS18B20_COUNT;
    doc["tmin"] = TEMP_COLOR_MIN_C;   // ends of the thermal ramp, from config.h
    doc["tmax"] = TEMP_COLOR_MAX_C;
    JsonArray arr = doc["sensors"].to<JsonArray>();
    for (uint8_t i = 0; i < slotCount; i++) {
        char hex[17];
        addrToHex(slots[i].addr, hex);
        JsonObject o = arr.add<JsonObject>();
        o["slot"] = i;
        o["addr"] = hex;
        o["name"] = slots[i].name;
        o["online"] = slots[i].online;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void setupRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        // In setup mode the only useful thing anyone can do here is hand over
        // credentials, so send them straight to the portal page.
        if (WiFiPortal::state() == WiFiPortalState::PORTAL) {
            request->redirect("/wifi");
            return;
        }
        request->send(200, "text/html", DASHBOARD_HTML);
    });

    // One settings document, two entry points: /settings opens on the Sensors
    // tab, /wifi on the Wi-Fi one. /wifi has to keep working as its own URL --
    // it is where the captive portal and the setup-mode redirect above send
    // people, and it is what gets written on a label.
    auto sendSettings = [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", SETTINGS_HTML);
    };
    server.on("/settings", HTTP_GET, sendSettings);
    server.on("/wifi", HTTP_GET, sendSettings);

    // Shared by both pages, so the thermal palette has one definition.
    server.on("/thermal.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/javascript", THERMAL_JS);
    });

    server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
        float kI, kV, kP;
        meter.getCalibration(kI, kV, kP);
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"kI\":%.4f,\"kV\":%.4f,\"kP\":%.4f}", kI, kV, kP);
        request->send(200, "application/json", buf);
    });

    auto *calibHandler = new AsyncCallbackJsonWebHandler("/api/calibration",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject obj = json.as<JsonObject>();
            float kI = obj["kI"] | 1.0f;
            float kV = obj["kV"] | 1.0f;
            float kP = obj["kP"] | 1.0f;
            meter.setCalibration(kI, kV, kP);
            prefs.putFloat("kI", kI);
            prefs.putFloat("kV", kV);
            prefs.putFloat("kP", kP);
            request->send(200, "application/json", "{\"ok\":true}");
        });
    server.addHandler(calibHandler);

    server.on("/api/energy/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        energyKWh = 0;
        prefs.putFloat("kwh", 0);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendSensorList(request);
    });

    // Registered before the JSON handler below: a OneWire search blocks for
    // tens of milliseconds, which has no business running on the AsyncTCP task,
    // so this only raises a flag for loop() to act on.
    server.on("/api/sensors/rescan", HTTP_POST, [](AsyncWebServerRequest *request) {
        rescanRequested = true;
        request->send(200, "application/json", "{\"ok\":true}");
    });

    auto *sensorHandler = new AsyncCallbackJsonWebHandler("/api/sensors",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonArray in = json["sensors"].as<JsonArray>();
            if (in.isNull()) {
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"expected {sensors:[...]}\"}");
                return;
            }

            SensorSlot rebuilt[DS18B20_COUNT] = {};
            uint8_t n = 0;
            for (JsonObject o : in) {
                if (n >= DS18B20_COUNT) break;
                DeviceAddress addr;
                if (!hexToAddr(o["addr"] | "", addr)) continue;

                bool dup = false;
                for (uint8_t k = 0; k < n; k++) {
                    if (memcmp(rebuilt[k].addr, addr, 8) == 0) { dup = true; break; }
                }
                if (dup) continue;   // one sensor cannot hold two slots

                memcpy(rebuilt[n].addr, addr, 8);
                copyName(rebuilt[n].name, sizeof(rebuilt[n].name), o["name"] | "");
                // carry over what the last bus scan knew about this address
                for (uint8_t k = 0; k < slotCount; k++) {
                    if (memcmp(slots[k].addr, addr, 8) == 0) {
                        rebuilt[n].online = slots[k].online;
                        break;
                    }
                }
                n++;
            }

            memcpy(slots, rebuilt, sizeof(slots));
            slotCount = n;
            saveSensorMap();
            request->send(200, "application/json", "{\"ok\":true}");
        });
    server.addHandler(sensorHandler);

    WiFiPortal::registerRoutes(server);   // /wifi, /api/wifi*, captive-portal catch-all

    server.addHandler(&events);
    server.begin();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ESP32 + BL0942 + DS18B20 -- Web Dashboard Example ===");
    Serial.printf("Firmware v%s (built %s)\n", FIRMWARE_VERSION, FIRMWARE_BUILD);

    led.begin(STATUS_LED_PIN, STATUS_LED_ACTIVE_HIGH);
    led.setMode(LedMode::BLINK_FAST);

    loadCalibration();

    meter.begin(Serial2, BL0942_RX_PIN, BL0942_TX_PIN, BL0942_BAUD, BL0942_ADDRESS);
    meter.setDebug(BL0942_DEBUG);
    if (!meter.configure(BL0942_AC_FREQ_60HZ)) {
        Serial.println("BL0942: configure() FAILED -- check UART wiring/pins");
    }

    sensorPrefs.begin("sensors", false);
    applySensorMap();

    // Conversions run in the background from here on: request now, collect on
    // the next cycle. A 12-bit DS18B20 needs 750ms and loop() reads once a
    // second, so the value is always ready -- and loop() never sits blocked
    // waiting for it, which is what keeps the portal's DNS answering.
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();

    WiFiPortal::begin(DEVICE_HOSTNAME, [] { led.update(); });
    setupRoutes();

    lastRead = lastEnergyReadMs = lastPersist = millis();
}

void loop() {
    led.update();
    WiFiPortal::loop();   // above the early return below -- the portal DNS is
                          // polled from here and starves if it is skipped

    if (rescanRequested) {
        rescanRequested = false;
        applySensorMap();
        configVersion++;
        sensors.requestTemperatures();   // the bus search aborted the pending one
    }

    uint32_t now = millis();
    if (now - lastRead < SENSOR_READ_INTERVAL_MS) {
        return;
    }
    float dtHours = (now - lastEnergyReadMs) / 3600000.0f;
    lastEnergyReadMs = now;
    lastRead = now;

    bool ok = meter.readAll(lastSample);
    if (ok) {
        energyKWh += (lastSample.activePowerW * dtHours) / 1000.0f;
    }
    updateLed(ok);

    if (now - lastPersist > 60000) {
        prefs.putFloat("kwh", energyKWh);
        lastPersist = now;
    }

    JsonDocument doc;
    doc["v"] = ok ? lastSample.voltageV : 0;
    doc["i"] = ok ? lastSample.currentA : 0;
    doc["p"] = ok ? lastSample.activePowerW : 0;
    doc["f"] = ok ? lastSample.frequencyHz : 0;
    doc["e"] = energyKWh;
    doc["noLoad"] = ok && lastSample.noLoad;
    doc["uptime"] = now;
    doc["heap"] = ESP.getFreeHeap();
    doc["cfg"] = configVersion;   // pages refetch their labels when this moves

    // Reads the conversion started on the previous cycle, so this returns
    // immediately instead of blocking for the 750ms it takes to run.
    JsonArray temps = doc["temps"].to<JsonArray>();
    for (uint8_t i = 0; i < slotCount; i++) {
        if (!slots[i].online) {
            temps.add(nullptr);
            continue;
        }
        float c = sensors.getTempC(slots[i].addr);
        if (c == DEVICE_DISCONNECTED_C) {
            temps.add(nullptr);
        } else {
            temps.add(c);
        }
    }
    sensors.requestTemperatures();   // start the next one; ready a second from now

    String payload;
    serializeJson(doc, payload);
    events.send(payload.c_str(), "data", now);
}
