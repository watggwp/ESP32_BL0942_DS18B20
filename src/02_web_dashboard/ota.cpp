#include "ota.h"

#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <esp_ota_ops.h>

#include "config.h"

namespace {

Preferences prefs;   // "ota"

// Optional. Empty means anyone who can reach the page can flash the board, which
// is the same trust level as the rest of /settings -- except that this one hands
// over arbitrary code execution rather than a threshold. Set one on any board
// that shares a network with people who have no business reflashing it.
char otaKey[33] = "";

uint32_t rebootAt = 0;      // 0 = no reboot pending
bool     uploadOk = false;
size_t   written  = 0;
char     failure[96] = "";

const esp_partition_t *targetSlot() { return esp_ota_get_next_update_partition(nullptr); }

// Stop the transfer and remember why. Everything after this in the same upload
// is ignored, and the response handler turns it into an HTTP error the page can
// actually show the operator.
void fail(const char *why) {
    if (!failure[0]) strlcpy(failure, why, sizeof(failure));
    if (Update.isRunning()) Update.abort();
    Serial.printf("OTA: %s\n", failure);
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index,
                  uint8_t *data, size_t len, bool final) {
    if (index == 0) {
        uploadOk = false;
        written = 0;
        failure[0] = '\0';

        if (otaKey[0]) {
            const AsyncWebHeader *h = request->getHeader("X-OTA-Key");
            if (!h || h->value() != otaKey) { fail("wrong OTA password"); return; }
        }

        // Every ESP32 image starts 0xE9. Checking it here costs nothing and
        // catches the mistakes people actually make -- bootloader.bin,
        // partitions.bin, a zip, another project's build -- before a single byte
        // of the spare slot has been erased.
        if (len < 1 || data[0] != 0xE9) {
            fail("not an ESP32 firmware image (bad magic byte)");
            return;
        }

        const esp_partition_t *slot = targetSlot();
        if (!slot) { fail("no spare OTA slot -- check the partition table"); return; }
        Serial.printf("OTA: receiving '%s' into %s (%u bytes free)\n",
                      filename.c_str(), slot->label, slot->size);

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            fail(Update.errorString());
            return;
        }
    }

    if (failure[0]) return;   // already given up on this upload

    if (len && Update.write(data, len) != len) {
        fail(Update.errorString());
        return;
    }
    written += len;

    if (final) {
        // end(true) is what verifies the image and marks the slot bootable. If
        // it refuses, the running firmware is still the one in the other slot.
        if (!Update.end(true)) {
            fail(Update.errorString());
            return;
        }
        uploadOk = true;
        Serial.printf("OTA: %u bytes written and verified\n", written);
    }
}

}  // namespace

void OTA::begin() {
    prefs.begin("ota", false);
    prefs.getString("key", otaKey, sizeof(otaKey));

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = targetSlot();
    Serial.printf("OTA: running from %s, updates go to %s (%u bytes), password %s\n",
                  running ? running->label : "?",
                  next ? next->label : "none",
                  next ? next->size : 0,
                  otaKey[0] ? "set" : "not set");
}

void OTA::loop() {
    if (rebootAt && millis() >= rebootAt) {
        Serial.println("OTA: rebooting into the new firmware");
        Serial.flush();
        ESP.restart();
    }
}

void OTA::registerRoutes(AsyncWebServer &server) {
    // Sub-paths first: a plain-string route matches its own children too.
    auto *keyHandler = new AsyncCallbackJsonWebHandler("/api/ota/key",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            const char *key = json.as<JsonObject>()["key"] | "";
            strlcpy(otaKey, key, sizeof(otaKey));
            prefs.putString("key", otaKey);
            Serial.printf("OTA: password %s\n", otaKey[0] ? "set" : "cleared");
            request->send(200, "application/json", "{\"ok\":true}");
        });
    server.addHandler(keyHandler);

    server.on("/api/ota", HTTP_GET, [](AsyncWebServerRequest *request) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *next = targetSlot();
        JsonDocument doc;
        doc["fw"] = FIRMWARE_VERSION;
        doc["build"] = FIRMWARE_BUILD;
        doc["running"] = running ? running->label : "?";
        doc["target"] = next ? next->label : "";
        doc["targetSize"] = next ? next->size : 0;
        doc["sketch"] = ESP.getSketchSize();
        doc["keySet"] = otaKey[0] != '\0';
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    server.on("/api/ota", HTTP_POST,
        // Runs once the whole body has been received, including when the upload
        // handler gave up early -- which is the only place the browser can be
        // told what went wrong.
        [](AsyncWebServerRequest *request) {
            if (uploadOk) {
                AsyncWebServerResponse *res = request->beginResponse(
                    200, "application/json", "{\"ok\":true,\"reboot\":true}");
                res->addHeader("Connection", "close");
                request->send(res);
                // Long enough for the reply to leave the board. Restarting from
                // here would drop the socket and the page would report a failed
                // upload that actually succeeded.
                rebootAt = millis() + 1200;
                return;
            }
            JsonDocument doc;
            doc["ok"] = false;
            doc["error"] = failure[0] ? failure : "no firmware file in the request";
            String out;
            serializeJson(doc, out);
            AsyncWebServerResponse *res = request->beginResponse(400, "application/json", out);
            res->addHeader("Connection", "close");
            request->send(res);
        },
        handleUpload);
}
