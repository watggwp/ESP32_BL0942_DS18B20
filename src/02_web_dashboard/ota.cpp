#include "ota.h"

#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>

#include "config.h"

namespace {

uint32_t rebootAt = 0;      // 0 = no reboot pending

const esp_partition_t *targetSlot() { return esp_ota_get_next_update_partition(nullptr); }

OTA::Verify verifyStateVal = OTA::Verify::NOT_APPLICABLE;
uint32_t verifyDeadline = 0;

// ---------------------------------------------------------------------------
// Update from a URL
// ---------------------------------------------------------------------------
OTA::UrlState urlStateVal = OTA::UrlState::IDLE;
char urlMsg[144] = "";
char pendingUrl[256] = "";

void urlUpdateTask(void *) {
    Serial.printf("OTA: fetching %s\n", pendingUrl);

    // rebootOnUpdate(false): the reboot is deferred to loop() so the result can
    // be published back to ThingsBoard before the board disappears. A silent
    // restart looks identical to a crash from the outside.
    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    t_httpUpdate_return ret;
    if (strncmp(pendingUrl, "https://", 8) == 0) {
        WiFiClientSecure secure;
        // TLS either way, but the certificate is not checked: a firmware URL
        // usually points at the same broker the board already trusts, and
        // pinning a CA here would need updating whenever it rotates.
        secure.setInsecure();
        secure.setTimeout(20);
        ret = httpUpdate.update(secure, pendingUrl);
    } else {
        WiFiClient plain;
        ret = httpUpdate.update(plain, pendingUrl);
    }

    switch (ret) {
        case HTTP_UPDATE_OK:
            snprintf(urlMsg, sizeof(urlMsg), "flashed, rebooting");
            urlStateVal = OTA::UrlState::DONE_OK;
            rebootAt = millis() + 2500;   // room for a result message to go out
            break;
        case HTTP_UPDATE_NO_UPDATES:
            snprintf(urlMsg, sizeof(urlMsg), "server had nothing to send");
            urlStateVal = OTA::UrlState::DONE_FAIL;
            break;
        default:
            snprintf(urlMsg, sizeof(urlMsg), "%d %s", httpUpdate.getLastError(),
                     httpUpdate.getLastErrorString().c_str());
            urlStateVal = OTA::UrlState::DONE_FAIL;
            break;
    }
    Serial.printf("OTA: url update %s -- %s\n",
                  urlStateVal == OTA::UrlState::DONE_OK ? "OK" : "FAILED", urlMsg);
    vTaskDelete(nullptr);
}

}  // namespace

// Overrides the weak default in the Arduino core (esp32-hal-misc.c). Returning
// true stops initArduino() from calling esp_ota_mark_app_valid_cancel_rollback()
// before setup() has even run -- which is what normally throws the rollback
// window away in the first millisecond of the new image's life.
extern "C" bool verifyRollbackLater() {
    return true;
}

bool OTA::startFromUrl(const char *url) {
    if (urlStateVal == UrlState::RUNNING) {
        Serial.println("OTA: a url update is already running");
        return false;
    }
    if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
        Serial.println("OTA: url must start with http:// or https://");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("OTA: no network for a url update");
        return false;
    }
    strlcpy(pendingUrl, url, sizeof(pendingUrl));
    urlMsg[0] = '\0';
    urlStateVal = UrlState::RUNNING;
    if (xTaskCreate(urlUpdateTask, "otaurl", OTA_URL_TASK_STACK, nullptr, 1, nullptr) != pdPASS) {
        urlStateVal = UrlState::DONE_FAIL;
        strlcpy(urlMsg, "could not start the download task", sizeof(urlMsg));
        return false;
    }
    return true;
}

OTA::UrlState OTA::urlState() { return urlStateVal; }
const char *OTA::urlMessage() { return urlMsg; }
void OTA::clearUrlState() {
    if (urlStateVal == UrlState::DONE_OK || urlStateVal == UrlState::DONE_FAIL) {
        urlStateVal = UrlState::IDLE;
    }
}

void OTA::begin() {
    // A USB flash writes boot_app0 alongside the image and comes up already
    // accepted, so this only ever fires after an over-the-air update.
    const esp_partition_t *self = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (self && esp_ota_get_state_partition(self, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        verifyStateVal = OTA::Verify::PENDING;
        verifyDeadline = millis() + (uint32_t)OTA_VERIFY_UPTIME_S * 1000UL;
        Serial.printf("OTA: this image is on probation -- %us of healthy running "
                      "plus Wi-Fi to keep it, otherwise the bootloader reverts\n",
                      (unsigned)OTA_VERIFY_UPTIME_S);
    } else {
        verifyStateVal = OTA::Verify::CONFIRMED;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = targetSlot();
    Serial.printf("OTA: running from %s, updates go to %s (%u bytes)\n",
                  running ? running->label : "?",
                  next ? next->label : "none",
                  next ? next->size : 0);
}

OTA::Verify OTA::verifyState() { return verifyStateVal; }

uint32_t OTA::verifySecondsLeft() {
    if (verifyStateVal != Verify::PENDING) return 0;
    int32_t left = (int32_t)(verifyDeadline - millis());
    return left > 0 ? (uint32_t)left / 1000 : 0;
}

void OTA::loop() {
    // Accept the image only once it has actually worked for a while. Wi-Fi is
    // part of the test on purpose: an image that boots but cannot get on the
    // network is unreachable, and unreachable is the failure that OTA exists to
    // avoid. Nothing here has to run for a rollback to happen -- a reset before
    // this point is the signal, and a board too broken to reach this line is
    // exactly the board that should be reverted.
    if (verifyStateVal == Verify::PENDING && millis() >= verifyDeadline &&
        WiFi.status() == WL_CONNECTED) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            verifyStateVal = Verify::CONFIRMED;
            Serial.println("OTA: image confirmed, rollback window closed");
        }
    }

    if (rebootAt && millis() >= rebootAt) {
        Serial.println("OTA: rebooting into the new firmware");
        Serial.flush();
        ESP.restart();
    }
}

// Status only. Firmware itself arrives from ThingsBoard over MQTT -- there is no
// upload endpoint, so nothing served here can flash the board.
void OTA::registerRoutes(AsyncWebServer &server) {
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
        doc["verify"] = verifyStateVal == Verify::PENDING     ? "pending"
                      : verifyStateVal == Verify::CONFIRMED   ? "confirmed" : "n/a";
        if (verifyStateVal == Verify::PENDING) doc["verifyLeft"] = OTA::verifySecondsLeft();
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });
}
