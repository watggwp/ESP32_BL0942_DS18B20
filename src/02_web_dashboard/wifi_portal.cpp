#include "wifi_portal.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <esp_mac.h>

#include "config.h"
#include "wifi_html.h"

namespace {

Preferences prefs;          // "wifi" -- ssid / pass
DNSServer dns;

WiFiPortalState portalState = WiFiPortalState::CONNECTING;
char apName[40] = "";
char apUrl[48]  = "";
char hostName[40] = "esp32";
bool mdnsUp = false;
uint32_t lastLinkCheck = 0;

// Anything that touches NVS, the Wi-Fi driver, or the reset line is raised as a
// flag here and carried out by loop(), never inline in a request handler -- the
// same reason /api/sensors/rescan defers its OneWire search. Fixed buffers
// rather than String: these are written from the AsyncTCP task and read from
// loop(), and a String reallocating under the reader is a crash, not a stale
// value. The payload lands before the flag is raised, and only one request is
// ever in flight, so loop() always sees a complete pair.
volatile bool scanRequested  = false;
volatile bool scanFailed     = false;   // the driver refused to start one
volatile bool applyRequested = false;
volatile bool forgetRequested = false;
char pendingSsid[33] = "";
char pendingPass[64] = "";

bool rebootPending = false;
uint32_t rebootAt = 0;

// The network the station keeps retrying underneath an open portal, and whether
// it is currently stood down for a scan. A station that is mid-connect makes
// esp_wifi_scan_start() fail outright, which is exactly the state a board is in
// after someone types the wrong password -- so a rescan has to pause it.
char retrySsid[33] = "";
char retryPass[64] = "";
bool staPaused = false;

// Cached for /api/wifi so the request handler never has to open NVS.
char savedSsid[33] = "";

// esp_wifi_scan_start() refuses outright while the station is mid-connect --
// "sta is connecting, return error" -- which is exactly where a board sits when
// its saved password is wrong, because it retries that network forever.
// WiFi.disconnect() does NOT settle by the time it returns: the driver finishes
// whatever handshake step it is on first, so waiting a few tens of milliseconds
// and hoping is not enough. Dropping the station interface and putting it back
// is the one thing that reliably ends a connect attempt. The AP, if one is up,
// is a separate interface and stays up across this.
void standDownStation() {
    WiFi.enableSTA(false);
    delay(120);
    WiFi.enableSTA(true);
    delay(120);
}

void startMDNS() {
    if (mdnsUp) {
        MDNS.end();   // it does not survive the interface going down
        mdnsUp = false;
    }
    if (MDNS.begin(hostName)) {
        MDNS.addService("http", "tcp", 80);
        mdnsUp = true;
        Serial.printf("mDNS: http://%s.local/\n", hostName);
    }
}

// `retrySsid` is the network we failed to join, or nullptr if there was none.
// The station is put back on it after the scan so it keeps trying underneath
// the portal; loop() folds the portal away if that retry ever lands.
void startPortal(const char *ssid, const char *pass) {
    if (ssid) {
        strlcpy(retrySsid, ssid, sizeof(retrySsid));
        strlcpy(retryPass, pass ? pass : "", sizeof(retryPass));
        standDownStation();
    }

    // Scan BEFORE the AP exists. A scan sweeps every channel, taking the radio
    // away from the AP for seconds at a time -- which drops whatever phone is
    // sitting on the setup page, and is exactly when that page needs the
    // network list. Doing it now means the portal opens with the list already
    // in hand and never has to disrupt itself to get one.
    int found = WiFi.scanNetworks();     // blocking is fine: nothing is up yet
    if (found == WIFI_SCAN_FAILED) {     // give the driver one more moment
        delay(400);
        found = WiFi.scanNetworks();
    }
    Serial.printf("Wi-Fi: pre-scan found %d network(s)\n", found);

    WiFi.mode(WIFI_AP_STA);   // AP for the phone, STA so a rescan still works

    // Straight from eFuse. Asking the driver (WiFi.softAPmacAddress) returns
    // all zeros until the AP interface actually exists, which is after this
    // point -- and an AP called "P1-Setup-0000" is the same on every board.
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(apName, sizeof(apName), "%s-%02X%02X", WIFI_PORTAL_AP_PREFIX, mac[4], mac[5]);

    const char *pw = strlen(WIFI_PORTAL_AP_PASSWORD) >= 8 ? WIFI_PORTAL_AP_PASSWORD : nullptr;
    WiFi.softAP(apName, pw);
    delay(100);   // the AP needs a moment before softAPIP() is meaningful

    IPAddress ip = WiFi.softAPIP();
    snprintf(apUrl, sizeof(apUrl), "http://%s/wifi", ip.toString().c_str());

    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", ip);   // every lookup resolves to us -> captive portal

    portalState = WiFiPortalState::PORTAL;
    Serial.printf("Wi-Fi: setup portal up -- join \"%s\"%s, then open http://%s/\n",
                  apName, pw ? "" : " (open network)", ip.toString().c_str());

    if (retrySsid[0]) {
        WiFi.begin(retrySsid, retryPass);   // resume retrying in the background
    }
}

bool tryConnect(const char *ssid, const char *pass, void (*tick)()) {
    WiFi.persistent(false);   // NVS here is ours, not the driver's shadow copy
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostName);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);

    Serial.printf("Wi-Fi: connecting to \"%s\"", ssid);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
        if (tick) tick();
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("Wi-Fi: could not join \"%s\" within %ds\n",
                      ssid, WIFI_CONNECT_TIMEOUT_MS / 1000);
        return false;
    }

    portalState = WiFiPortalState::CONNECTED;
    Serial.printf("Wi-Fi: connected, IP=%s\n", WiFi.localIP().toString().c_str());
    startMDNS();
    return true;
}

void sendStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;
    bool portal = portalState == WiFiPortalState::PORTAL;
    doc["portal"] = portal;
    doc["connected"] = portalState == WiFiPortalState::CONNECTED;
    doc["host"] = hostName;
    doc["ap"] = apName;
    doc["saved"] = savedSsid;
    doc["fw"] = FIRMWARE_VERSION;    // the only page reachable in portal mode,
    doc["build"] = FIRMWARE_BUILD;   // so the version has to be readable here too
    if (portalState == WiFiPortalState::CONNECTED) {
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
    } else if (portal) {
        doc["ip"] = WiFi.softAPIP().toString();
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Results of the last completed scan, strongest first, one row per SSID.
void sendScan(AsyncWebServerRequest *request) {
    int n = WiFi.scanComplete();
    bool force = request->hasParam("force");

    // Bail out before touching the result array whenever loop() is about to
    // free it, so a second tab cannot read a set that is being deleted.
    if (scanRequested || n == WIFI_SCAN_RUNNING) {
        request->send(200, "application/json", "{\"scanning\":true}");
        return;
    }
    if (force || (n < 0 && !scanFailed)) {
        scanRequested = true;   // loop() starts it; the driver dislikes being
        request->send(200, "application/json", "{\"scanning\":true}");
        return;                 // poked from the AsyncTCP task
    }
    if (n < 0) {   // refused, and re-asking on our own would just spin
        request->send(200, "application/json",
                      "{\"scanning\":false,\"networks\":[],"
                      "\"error\":\"the radio would not scan just now\"}");
        return;
    }

    struct Row { int idx; int32_t rssi; };
    Row rows[24];
    uint8_t count = 0;

    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;   // hidden network, nothing to click on

        int dup = -1;
        for (uint8_t k = 0; k < count; k++) {
            if (WiFi.SSID(rows[k].idx) == ssid) { dup = k; break; }
        }
        int32_t rssi = WiFi.RSSI(i);
        if (dup >= 0) {                 // same SSID on two bands/APs
            if (rssi > rows[dup].rssi) rows[dup] = {i, rssi};
            continue;
        }
        if (count >= sizeof(rows) / sizeof(rows[0])) continue;
        rows[count++] = {i, rssi};
    }

    for (uint8_t i = 1; i < count; i++) {   // few enough rows that insertion sort wins
        Row key = rows[i];
        int8_t j = i - 1;
        while (j >= 0 && rows[j].rssi < key.rssi) { rows[j + 1] = rows[j]; j--; }
        rows[j + 1] = key;
    }

    JsonDocument doc;
    doc["scanning"] = false;
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = WiFi.SSID(rows[i].idx);
        o["rssi"] = rows[i].rssi;
        o["lock"] = WiFi.encryptionType(rows[i].idx) != WIFI_AUTH_OPEN;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

}  // namespace

// ---------------------------------------------------------------------------

void WiFiPortal::begin(const char *hostname, void (*tick)()) {
    strlcpy(hostName, hostname, sizeof(hostName));
    prefs.begin("wifi", false);

    strlcpy(savedSsid, prefs.getString("ssid", "").c_str(), sizeof(savedSsid));
    String pass = prefs.getString("pass", "");

    // NVS is the only source of credentials -- nothing is compiled into the
    // image, so every board off the flasher behaves the same and no password
    // ever sits in the source tree.
    if (!savedSsid[0]) {
        Serial.println("Wi-Fi: no network configured -- starting the setup portal");
        startPortal(nullptr, nullptr);
        return;
    }

    if (!tryConnect(savedSsid, pass.c_str(), tick)) {
        // The station keeps retrying underneath the portal on purpose: a router
        // that was simply slower to boot than the meter should not leave a
        // cabinet-mounted board sitting in setup mode until someone notices.
        // loop() closes the portal if that retry ever lands.
        startPortal(savedSsid, pass.c_str());
    }
}

void WiFiPortal::loop() {
    if (portalState == WiFiPortalState::PORTAL) {
        dns.processNextRequest();
    }

    if (scanRequested && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        scanRequested = false;
        // Stand the station down first. Retrying a network it cannot join --
        // the state a board is in the moment after someone mistypes a password
        // -- makes esp_wifi_scan_start() fail outright.
        if (portalState == WiFiPortalState::PORTAL && retrySsid[0] && !staPaused) {
            staPaused = true;
            standDownStation();
        }
        // async: results collected by /api/wifi/scan. A refusal leaves
        // scanComplete() at -2 forever, indistinguishable from "never ran", so
        // without this flag the page would poll "scanning..." for eternity.
        scanFailed = WiFi.scanNetworks(true) == WIFI_SCAN_FAILED;
        if (scanFailed) Serial.println("Wi-Fi: scan refused by the driver");
    }

    // Put the station back the moment the sweep is over -- including when the
    // scan never started, or it would stay parked for good.
    if (staPaused && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        staPaused = false;
        WiFi.begin(retrySsid, retryPass);
    }

    if (applyRequested) {
        applyRequested = false;
        prefs.putString("ssid", pendingSsid);
        prefs.putString("pass", pendingPass);
        strlcpy(savedSsid, pendingSsid, sizeof(savedSsid));
        memset(pendingPass, 0, sizeof(pendingPass));   // don't keep it in RAM
        Serial.printf("Wi-Fi: saved credentials for \"%s\", restarting\n", savedSsid);
        rebootPending = true;
        rebootAt = millis() + 1200;   // long enough for the response to flush
    }

    if (forgetRequested) {
        forgetRequested = false;
        prefs.remove("ssid");
        prefs.remove("pass");
        savedSsid[0] = '\0';
        Serial.println("Wi-Fi: credentials cleared, restarting into the setup portal");
        rebootPending = true;
        rebootAt = millis() + 1200;
    }

    uint32_t now = millis();

    if (rebootPending && (int32_t)(now - rebootAt) >= 0) {
        ESP.restart();
    }

    if (now - lastLinkCheck < 1000) return;
    lastLinkCheck = now;
    bool up = WiFi.status() == WL_CONNECTED;

    if (portalState == WiFiPortalState::PORTAL) {
        // The station never stopped retrying the saved network. If it finally
        // gets in -- a router that came up after the meter did -- fold the
        // portal away rather than stranding the board in setup mode.
        if (up) {
            dns.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            portalState = WiFiPortalState::CONNECTED;
            Serial.printf("Wi-Fi: saved network came back, portal closed, IP=%s\n",
                          WiFi.localIP().toString().c_str());
            startMDNS();
        }
        return;
    }

    // The core reconnects on its own; this only keeps the reported state (and
    // therefore the LED) honest, and puts mDNS back after the interface returns.
    if (up && portalState != WiFiPortalState::CONNECTED) {
        portalState = WiFiPortalState::CONNECTED;
        Serial.printf("Wi-Fi: reconnected, IP=%s\n", WiFi.localIP().toString().c_str());
        startMDNS();
    } else if (!up && portalState == WiFiPortalState::CONNECTED) {
        portalState = WiFiPortalState::CONNECTING;
        mdnsUp = false;
        Serial.println("Wi-Fi: link lost -- retrying in the background");
    }
}

void WiFiPortal::registerRoutes(AsyncWebServer &server) {
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", WIFI_HTML);
    });

    // Sub-paths first: a plain-string route matches its own children too
    // ("/api/wifi" would otherwise swallow "/api/wifi/scan").
    server.on("/api/wifi/scan", HTTP_GET, sendScan);

    server.on("/api/wifi/forget", HTTP_POST, [](AsyncWebServerRequest *request) {
        forgetRequested = true;
        request->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
    });

    server.on("/api/wifi", HTTP_GET, sendStatus);

    auto *handler = new AsyncCallbackJsonWebHandler("/api/wifi",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject obj = json.as<JsonObject>();
            String ssid = obj["ssid"] | "";
            String pass = obj["pass"] | "";

            if (!ssid.length() || ssid.length() > 32) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"ssid must be 1-32 characters\"}");
                return;
            }
            if (pass.length() > 63 || (pass.length() > 0 && pass.length() < 8)) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"password must be empty or 8-63 characters\"}");
                return;
            }

            strlcpy(pendingSsid, ssid.c_str(), sizeof(pendingSsid));
            strlcpy(pendingPass, pass.c_str(), sizeof(pendingPass));
            applyRequested = true;   // payload first, then the flag
            request->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
        });
    server.addHandler(handler);

    server.onNotFound([](AsyncWebServerRequest *request) {
        // A phone probing for internet access (generate_204, hotspot-detect.html,
        // ...) lands here because DNS sent it to us. Redirecting instead of
        // answering is what makes it raise the sign-in sheet on its own.
        if (portalState == WiFiPortalState::PORTAL) {
            request->redirect(apUrl);
            return;
        }
        request->send(404, "text/plain", "Not found");
    });
}

WiFiPortalState WiFiPortal::state() {
    return portalState;
}

const char *WiFiPortal::apSsid() {
    return apName;
}
