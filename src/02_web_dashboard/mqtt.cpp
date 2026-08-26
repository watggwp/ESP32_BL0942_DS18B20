#include "mqtt.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

#include "config.h"
#include "ota.h"

namespace {

Preferences prefs;   // "mqtt"

struct Config {
    bool     enabled = false;
    char     host[64] = "";
    uint16_t port = MQTT_DEF_PORT;
    char     user[96] = "";      // ThingsBoard: the device access token goes here
    char     pass[64] = "";
    char     clientId[40] = "";
    char     pubTopic[80]  = MQTT_DEF_PUB_TOPIC;
    char     subTopic[80]  = MQTT_DEF_SUB_TOPIC;
    char     attrTopic[80] = MQTT_DEF_ATTR_TOPIC;   // empty = do not publish attributes
    uint16_t intervalS = MQTT_DEF_INTERVAL_S;
    bool     autoUpdate = false;   // act on the server's firmware attributes
    char     fwBase[80] = "";     // empty = https://<broker host>
};
Config cfg;

// The version this board was last told to become. Kept in NVS, and it is the
// only thing standing between a mismatched fw_target and an endless
// flash-reboot-compare-flash loop: after the attempt, this survives the reboot,
// so the same target is never chased twice. Cleared the moment the running
// version matches, which is what makes a genuine update self-resetting.
char fwTried[24] = "";
char fwNote[96]  = "";   // why the last auto-update did not happen, for the UI
uint8_t  fwTries = 0;    // attempts spent on fwTried, also in NVS
uint32_t fwRetryAt = 0;  // when to ask the server again after a failure

WiFiClient net;
PubSubClient client(net);
Mqtt::SlotNameFn slotName = nullptr;

// The last reading, handed over by loop() and read by the publisher task. Small
// enough to copy wholesale under the mutex, which keeps the locking trivial:
// nobody holds it across anything that can block.
struct Snapshot {
    bool  valid = false;
    bool  meterOk = false;
    float volts = 0, amps = 0, watts = 0, hertz = 0, energy = 0;
    float temps[DS18B20_COUNT];
    uint8_t tempCount = 0;
};
Snapshot shared;
SemaphoreHandle_t lock = nullptr;

uint32_t lastPublishMs = 0;
uint32_t lastAttemptMs = 0;
volatile bool     connectedFlag = false;
volatile uint32_t published = 0;
volatile uint32_t failures = 0;
char lastError[96] = "";
char lastRpcReply[96] = "";   // topic to answer the in-flight command on

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
void defaultClientId(char *out, size_t size) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(out, size, "%s-%02X%02X", DEVICE_HOSTNAME, mac[4], mac[5]);
}

void load() {
    prefs.begin("mqtt", false);
    cfg.enabled    = prefs.getBool("on", false);
    cfg.autoUpdate = prefs.getBool("auto", false);
    prefs.getString("tried", fwTried, sizeof(fwTried));
    fwTries = prefs.getUChar("trycnt", 0);
    cfg.port      = prefs.getUShort("port", MQTT_DEF_PORT);
    cfg.intervalS = prefs.getUShort("every", MQTT_DEF_INTERVAL_S);
    prefs.getString("host", cfg.host, sizeof(cfg.host));
    prefs.getString("user", cfg.user, sizeof(cfg.user));
    prefs.getString("pass", cfg.pass, sizeof(cfg.pass));
    prefs.getString("cid",  cfg.clientId, sizeof(cfg.clientId));
    prefs.getString("fwbase", cfg.fwBase, sizeof(cfg.fwBase));
    if (!prefs.getString("pub",  cfg.pubTopic,  sizeof(cfg.pubTopic)))  strlcpy(cfg.pubTopic,  MQTT_DEF_PUB_TOPIC,  sizeof(cfg.pubTopic));
    if (!prefs.getString("sub",  cfg.subTopic,  sizeof(cfg.subTopic)))  strlcpy(cfg.subTopic,  MQTT_DEF_SUB_TOPIC,  sizeof(cfg.subTopic));
    if (!prefs.isKey("attr")) strlcpy(cfg.attrTopic, MQTT_DEF_ATTR_TOPIC, sizeof(cfg.attrTopic));
    else prefs.getString("attr", cfg.attrTopic, sizeof(cfg.attrTopic));
    if (!cfg.clientId[0]) defaultClientId(cfg.clientId, sizeof(cfg.clientId));
    if (cfg.intervalS < 5) cfg.intervalS = 5;
}

void save() {
    prefs.putBool("on", cfg.enabled);
    prefs.putBool("auto", cfg.autoUpdate);
    prefs.putUShort("port", cfg.port);
    prefs.putUShort("every", cfg.intervalS);
    prefs.putString("host", cfg.host);
    prefs.putString("user", cfg.user);
    prefs.putString("pass", cfg.pass);
    prefs.putString("cid",  cfg.clientId);
    prefs.putString("fwbase", cfg.fwBase);
    prefs.putString("pub",  cfg.pubTopic);
    prefs.putString("sub",  cfg.subTopic);
    prefs.putString("attr", cfg.attrTopic);
}

// ---------------------------------------------------------------------------
// Incoming commands
// ---------------------------------------------------------------------------
// ThingsBoard sends RPC on v1/devices/me/rpc/request/<id> and expects the answer
// on .../rpc/response/<id>. Deriving the reply topic from the request keeps this
// working whatever the id is, and costs nothing on a plain broker where the
// substring is simply absent.
void deriveReplyTopic(const char *requestTopic, char *out, size_t size) {
    out[0] = '\0';
    const char *hit = strstr(requestTopic, "/request/");
    if (!hit) return;
    size_t head = hit - requestTopic;
    if (head + 10 >= size) return;
    memcpy(out, requestTopic, head);
    out[head] = '\0';
    strlcat(out, "/response/", size);
    strlcat(out, hit + 9, size);   // the id
}

void reply(const char *json) {
    if (!lastRpcReply[0] || !client.connected()) return;
    client.publish(lastRpcReply, json);
    lastRpcReply[0] = '\0';
}

// ---------------------------------------------------------------------------
// Firmware version, driven from the server
//
// TWO PROTOCOLS, ONE CODE PATH. ThingsBoard's OTA repository publishes its own
// shared attributes when a package is assigned -- fw_title, fw_version, fw_size,
// fw_checksum, and fw_url only when the package was created with "Use external
// URL". A plain broker has none of that, so fw_target/fw_url are accepted too.
// Whichever arrives, it comes down to a version to reach and a URL to fetch.
//
// WHERE THE IMAGE COMES FROM. With an external URL, that URL is used as given.
// Without one -- the normal case, where the .bin lives inside ThingsBoard -- the
// board builds the HTTP transport's download address from its own access token,
// so nothing has to be hosted anywhere:
//   {base}/api/v1/{token}/firmware?title=...&version=...
//
// THE LOOP IS THE DANGER, not the download. A version the .bin does not actually
// contain -- a typo, a stale file, a build that was never bumped -- makes a naive
// implementation flash, reboot, compare, and flash again, for ever, across the
// whole fleet at once. fwTried is written to NVS BEFORE the download starts, so
// it survives the reboot that follows and the same target is never chased twice.
// A real update clears it by simply arriving: the version then matches.
// ---------------------------------------------------------------------------

// ThingsBoard reads these off TELEMETRY, not attributes, and it is how its OTA
// page decides whether a device is up to date. Sent on every connect.
void reportCurrentFirmware() {
    if (!client.connected()) return;
    JsonDocument doc;
    doc["current_fw_title"] = FIRMWARE_TITLE;
    doc["current_fw_version"] = FIRMWARE_VERSION;
    String out;
    serializeJson(doc, out);
    client.publish(cfg.pubTopic, out.c_str());
}

// The state vocabulary is ThingsBoard's -- DOWNLOADING, DOWNLOADED, VERIFIED,
// UPDATING, UPDATED, FAILED. Anything else and its OTA page shows nothing.
void reportFwState(const char *state, const char *error) {
    if (!client.connected()) return;
    JsonDocument doc;
    doc["fw_state"] = state;
    doc["current_fw_title"] = FIRMWARE_TITLE;
    doc["current_fw_version"] = FIRMWARE_VERSION;
    if (error && error[0]) doc["fw_error"] = error;
    String out;
    serializeJson(doc, out);
    client.publish(cfg.pubTopic, out.c_str());
}

// Percent-encodes what a package title can legally contain and a query string
// cannot. Titles are typed by hand in the ThingsBoard UI, and "P1 Power Meter"
// with spaces is the obvious thing for someone to type.
void urlEncode(const char *in, char *out, size_t size) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const char *p = in; *p && o + 4 < size; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = 0;
}

// Where ThingsBoard serves a stored package from. The base defaults to https on
// the same host the MQTT client talks to, which is right for ThingsBoard Cloud;
// a self-hosted instance usually needs http://host:8080 set explicitly.
void buildTbUrl(const char *title, const char *version, char *out, size_t size) {
    char t[64], v[32];
    urlEncode(title, t, sizeof(t));
    urlEncode(version, v, sizeof(v));
    snprintf(out, size, "%s%s/api/v1/%s/firmware?title=%s&version=%s",
             cfg.fwBase[0] ? "" : "https://",
             cfg.fwBase[0] ? cfg.fwBase : cfg.host,
             cfg.user, t, v);
}

void checkVersion(const char *target, const char *url, const char *title) {
    if (!cfg.autoUpdate || !target || !target[0]) return;

    if (strcmp(target, FIRMWARE_VERSION) == 0) {
        // On target. Forget any previous attempt so a future version is free to
        // be tried, and say so once rather than on every reconnect.
        if (fwTried[0]) {
            fwTried[0] = '\0';
            fwTries = 0;
            prefs.remove("tried");
            prefs.remove("trycnt");
            fwNote[0] = '\0';
            Serial.printf("MQTT: now running the target version %s\n", target);
            reportFwState("UPDATED", nullptr);
        }
        return;
    }

    // Same target as last time and the attempts are spent. Two very different
    // failures land here -- a download that never finished, and an image that
    // flashed cleanly but did not contain the version it claimed -- and from the
    // outside they look identical, so both get the same bounded budget rather
    // than an unbounded retry that could flash in a loop for ever.
    bool sameTarget = (strcmp(target, fwTried) == 0);
    if (sameTarget && fwTries >= OTA_MAX_ATTEMPTS) {
        if (!fwNote[0]) {
            snprintf(fwNote, sizeof(fwNote), "gave up on %s after %u tries, still on %s",
                     target, fwTries, FIRMWARE_VERSION);
            Serial.printf("MQTT: %s\n", fwNote);
            reportFwState("FAILED", fwNote);
        }
        return;
    }

    // An external URL wins when the package carries one; otherwise pull the
    // image straight out of ThingsBoard using the device's own access token.
    char built[300];
    if (url && url[0]) {
        strlcpy(built, url, sizeof(built));
    } else if (title && title[0] && cfg.user[0]) {
        buildTbUrl(title, target, built, sizeof(built));
    } else {
        snprintf(fwNote, sizeof(fwNote), "version %s but no fw_url and no fw_title/token", target);
        Serial.printf("MQTT: %s\n", fwNote);
        reportFwState("FAILED", fwNote);
        return;
    }

    // Recorded before the fetch, not after: the board may never come back to
    // this line -- a successful update reboots straight out of it.
    strlcpy(fwTried, target, sizeof(fwTried));
    fwTries = sameTarget ? fwTries + 1 : 1;
    prefs.putString("tried", fwTried);
    prefs.putUChar("trycnt", fwTries);   // survives a power cut mid-download
    fwNote[0] = '\0';
    fwRetryAt = 0;

    Serial.printf("MQTT: version %s != target %s, fetching %s\n", FIRMWARE_VERSION, target, built);
    if (OTA::startFromUrl(built)) {
        reportFwState("DOWNLOADING", nullptr);
    } else {
        snprintf(fwNote, sizeof(fwNote), "could not start the download -- check the url");
        reportFwState("FAILED", fwNote);
    }
}

// Shared attributes arrive in two shapes: pushed as a flat object when they
// change, and wrapped in {"shared":{...}} when they come back from a request.
void handleAttributes(JsonDocument &doc) {
    JsonVariantConst shared = doc["shared"];

    // ThingsBoard's OTA repository keys first, then the plain-broker aliases.
    const char *version = shared["fw_version"] | "";
    const char *title   = shared["fw_title"] | "";
    const char *url     = shared["fw_url"] | "";
    if (!version[0]) version = doc["fw_version"] | "";
    if (!title[0])   title   = doc["fw_title"] | "";
    if (!url[0])     url     = doc["fw_url"] | "";
    if (!version[0]) version = shared["fw_target"] | "";
    if (!version[0]) version = doc["fw_target"] | "";

    if (version[0] || url[0]) checkVersion(version, url, title);
}


void onMessage(char *topic, uint8_t *payload, unsigned int len) {
    char body[384];
    size_t n = len < sizeof(body) - 1 ? len : sizeof(body) - 1;
    memcpy(body, payload, n);
    body[n] = '\0';
    Serial.printf("MQTT: %s -> %s\n", topic, body);

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("MQTT: command is not JSON, ignored");
        return;
    }

    // Attribute traffic is not a command and must not be answered as one. On a
    // plain broker this also catches our own attribute publish echoing back,
    // which carries no fw_target and so falls through harmlessly.
    if (cfg.attrTopic[0] && strncmp(topic, cfg.attrTopic, strlen(cfg.attrTopic)) == 0) {
        handleAttributes(doc);
        return;
    }

    // Two shapes accepted: ThingsBoard RPC ({"method":…,"params":{…}}) and a bare
    // {"url":…} for anyone driving this from a plain broker or mosquitto_pub.
    const char *method = doc["method"] | "";
    const char *url = doc["params"]["url"] | "";
    if (!url[0]) url = doc["url"] | "";

    deriveReplyTopic(topic, lastRpcReply, sizeof(lastRpcReply));

    bool wantsUpdate = !method[0] || strcmp(method, "fwUpdate") == 0 ||
                       strcmp(method, "ota") == 0 || strcmp(method, "update") == 0;
    if (!wantsUpdate) {
        reply("{\"ok\":false,\"error\":\"unknown method\"}");
        return;
    }
    if (!url || !url[0]) {
        reply("{\"ok\":false,\"error\":\"no url in the command\"}");
        return;
    }

    // Answered before the download starts, on purpose: fetching and flashing runs
    // for tens of seconds and a ThingsBoard RPC call times out long before that.
    // The outcome goes out afterwards as telemetry instead.
    if (OTA::startFromUrl(url)) {
        reply("{\"ok\":true,\"state\":\"DOWNLOADING\"}");
        if (client.connected()) client.publish(cfg.pubTopic, "{\"fw_state\":\"DOWNLOADING\"}");
    } else {
        reply("{\"ok\":false,\"error\":\"could not start -- check the url and that no update is running\"}");
    }
}

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------
// Building is split from sending so the settings page can show exactly what
// goes out, with or without a broker to send it to. Reading the real payload
// beats guessing at it from the source, and it is available before the first
// connection rather than only after one succeeds.
void buildAttributes(String &out) {
    JsonDocument doc;
    doc["fw"] = FIRMWARE_VERSION;
    doc["build"] = FIRMWARE_BUILD;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["host"] = DEVICE_HOSTNAME;
    // Sensor names as attributes, so a dashboard can label temp3 with whatever
    // the operator called that probe rather than repeating it in two places.
    if (slotName) {
        for (uint8_t i = 0; i < DS18B20_COUNT; i++) {
            const char *n = slotName(i);
            if (!n || !n[0]) continue;
            char key[12];
            snprintf(key, sizeof(key), "name%u", i + 1);
            doc[key] = n;
        }
    }
    serializeJson(doc, out);
}

void publishAttributes() {
    if (!cfg.attrTopic[0]) return;
    String out;
    buildAttributes(out);
    client.publish(cfg.attrTopic, out.c_str());
}

// false when there is no reading yet -- the first sample lands a second after
// boot, and an empty payload is worth showing as "nothing yet" rather than as a
// document full of nulls.
bool buildTelemetry(String &out) {
    Snapshot s;
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    s = shared;
    xSemaphoreGive(lock);
    if (!s.valid) return false;

    JsonDocument doc;
    // null rather than 0 when the meter did not answer: a zero here would land in
    // the history as a genuine power failure and there is no way to tell the two
    // apart after the fact.
    if (s.meterOk) {
        doc["voltage"] = serialized(String(s.volts, 1));
        doc["current"] = serialized(String(s.amps, 3));
        doc["power"]   = serialized(String(s.watts, 1));
        doc["frequency"] = serialized(String(s.hertz, 2));
    } else {
        doc["voltage"] = nullptr;
        doc["current"] = nullptr;
        doc["power"] = nullptr;
        doc["frequency"] = nullptr;
    }
    doc["energy"] = serialized(String(s.energy, 3));
    doc["heap"] = ESP.getFreeHeap();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    // EVERY slot is published every time, all DS18B20_COUNT of them, whether a
    // probe is fitted or not. Dropping the key for a missing sensor would leave
    // the receiving end with no series for it at all, and then the key appears
    // from nowhere the day someone plugs one in -- a dashboard built before that
    // has no panel for it, and history has a gap that looks like nothing rather
    // than like an absent sensor. A null says "asked, nothing there", which is a
    // fact worth recording.
    for (uint8_t i = 0; i < DS18B20_COUNT; i++) {
        char key[10];
        snprintf(key, sizeof(key), "temp%u", i + 1);
        if (i >= s.tempCount || isnan(s.temps[i])) doc[key] = nullptr;
        else                                       doc[key] = serialized(String(s.temps[i], 2));
    }

    serializeJson(doc, out);
    return true;
}

void publishTelemetry() {
    String out;
    if (!buildTelemetry(out)) return;
    if (client.publish(cfg.pubTopic, out.c_str())) {
        published++;
        lastError[0] = '\0';
    } else {
        failures++;
        // The usual cause is an oversized payload: PubSubClient silently refuses
        // anything past its buffer, which is why MQTT_BUFFER_BYTES is set.
        snprintf(lastError, sizeof(lastError), "publish refused (%u byte payload)", out.length());
        Serial.printf("MQTT: %s\n", lastError);
    }
}

void publishUpdateOutcome() {
    OTA::UrlState st = OTA::urlState();
    if (st != OTA::UrlState::DONE_OK && st != OTA::UrlState::DONE_FAIL) return;

    if (st == OTA::UrlState::DONE_OK) {
        // Reached only if the reboot has not happened yet; the real confirmation
        // comes from the new image reporting its own version once it boots.
        reportFwState("DOWNLOADED", nullptr);
    } else {
        // Nothing was committed -- the board is still on the old firmware and
        // perfectly healthy. Worth another go if the budget allows, on a timer
        // rather than waiting for a reconnect that may be hours away.
        strlcpy(fwNote, OTA::urlMessage(), sizeof(fwNote));
        reportFwState("FAILED", fwNote);
        if (fwTries < OTA_MAX_ATTEMPTS) {
            fwRetryAt = millis() + OTA_RETRY_MS;
            Serial.printf("MQTT: download failed (%u/%u), asking again in %us\n",
                          fwTries, OTA_MAX_ATTEMPTS, (unsigned)(OTA_RETRY_MS / 1000));
        }
    }
    OTA::clearUrlState();
}

bool tryConnect() {
    if (WiFi.status() != WL_CONNECTED) return false;

    client.setServer(cfg.host, cfg.port);
    client.setBufferSize(MQTT_BUFFER_BYTES);
    client.setKeepAlive(MQTT_KEEPALIVE_S);
    client.setSocketTimeout(5);
    client.setCallback(onMessage);

    Serial.printf("MQTT: connecting to %s:%u as %s\n", cfg.host, cfg.port, cfg.clientId);
    // ThingsBoard authenticates with the access token as the username and no
    // password at all, so an empty password must stay empty rather than becoming
    // an empty string the broker then rejects.
    bool ok = client.connect(cfg.clientId,
                             cfg.user[0] ? cfg.user : nullptr,
                             cfg.pass[0] ? cfg.pass : nullptr);
    if (!ok) {
        failures++;
        snprintf(lastError, sizeof(lastError), "connect failed, state %d", client.state());
        Serial.printf("MQTT: %s\n", lastError);
        return false;
    }

    Serial.println("MQTT: connected");
    lastError[0] = '\0';
    if (cfg.subTopic[0]) {
        client.subscribe(cfg.subTopic);
        Serial.printf("MQTT: listening on %s\n", cfg.subTopic);
    }
    publishAttributes();

    // Shared attributes come two ways and the board needs both: pushed on the
    // attribute topic when someone changes them, and returned on .../response/
    // to a request we make now -- otherwise a board that was offline when the
    // target changed would never learn about it.
    if (cfg.autoUpdate && cfg.attrTopic[0]) {
        char sub[96], req[96];
        snprintf(sub, sizeof(sub), "%s/response/+", cfg.attrTopic);
        client.subscribe(cfg.attrTopic);
        client.subscribe(sub);
        snprintf(req, sizeof(req), "%s/request/1", cfg.attrTopic);
        client.publish(req, "{\"sharedKeys\":\"fw_title,fw_version,fw_url,fw_target\"}");
        Serial.printf("MQTT: asked for fw_target/fw_url on %s\n", req);
    }

    reportCurrentFirmware();   // ThingsBoard's OTA page reads this
    lastPublishMs = 0;   // send the first reading immediately, not in 30s
    return true;
}

void task(void *) {
    for (;;) {
        if (!cfg.enabled || !cfg.host[0]) {
            if (client.connected()) client.disconnect();
            connectedFlag = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!client.connected()) {
            connectedFlag = false;
            uint32_t now = millis();
            if (now - lastAttemptMs >= MQTT_RECONNECT_MS || lastAttemptMs == 0) {
                lastAttemptMs = now;
                tryConnect();
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        connectedFlag = true;
        client.loop();
        publishUpdateOutcome();

        // Re-ask for the firmware attributes. The server answers with the same
        // target, checkVersion sees attempts left, and the download starts over.
        if (fwRetryAt && millis() >= fwRetryAt) {
            fwRetryAt = 0;
            if (cfg.autoUpdate && cfg.attrTopic[0]) {
                char req[96];
                snprintf(req, sizeof(req), "%s/request/1", cfg.attrTopic);
                client.publish(req, "{\"sharedKeys\":\"fw_title,fw_version,fw_url,fw_target\"}");
                fwNote[0] = '\0';
                Serial.println("MQTT: retrying the firmware update");
            }
        }

        uint32_t now = millis();
        if (lastPublishMs == 0 || now - lastPublishMs >= (uint32_t)cfg.intervalS * 1000UL) {
            lastPublishMs = now;
            publishTelemetry();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
void Mqtt::begin(SlotNameFn nameOf) {
    slotName = nameOf;
    load();
    lock = xSemaphoreCreateMutex();
    if (!lock) {
        Serial.println("MQTT: could not create the snapshot mutex -- MQTT disabled");
        return;
    }
    xTaskCreate(task, "mqtt", MQTT_TASK_STACK, nullptr, 1, nullptr);
    Serial.printf("MQTT: %s, broker %s:%u, every %us\n",
                  cfg.enabled ? "on" : "off",
                  cfg.host[0] ? cfg.host : "(not set)", cfg.port, cfg.intervalS);
}

void Mqtt::sample(bool meterOk, float volts, float amps, float watts, float hertz,
                  float energyKWh, const float *temps, uint8_t tempCount) {
    if (!lock) return;
    // Non-blocking on purpose: this runs on the loop task and a missed snapshot
    // costs one stale reading, while waiting on the lock would cost a beat of the
    // sampling clock.
    if (xSemaphoreTake(lock, 0) != pdTRUE) return;
    shared.valid = true;
    shared.meterOk = meterOk;
    shared.volts = volts;
    shared.amps = amps;
    shared.watts = watts;
    shared.hertz = hertz;
    shared.energy = energyKWh;
    shared.tempCount = tempCount < DS18B20_COUNT ? tempCount : DS18B20_COUNT;
    for (uint8_t i = 0; i < shared.tempCount; i++) shared.temps[i] = temps[i];
    xSemaphoreGive(lock);
}

void Mqtt::registerRoutes(AsyncWebServer &server) {
    server.on("/api/mqtt", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["enabled"] = cfg.enabled;
        doc["host"] = cfg.host;
        doc["port"] = cfg.port;
        doc["user"] = cfg.user;
        doc["clientId"] = cfg.clientId;
        doc["pubTopic"] = cfg.pubTopic;
        doc["subTopic"] = cfg.subTopic;
        doc["attrTopic"] = cfg.attrTopic;
        doc["interval"] = cfg.intervalS;
        doc["passSet"] = cfg.pass[0] != '\0';   // the password itself never leaves
        doc["connected"] = connectedFlag;
        doc["published"] = published;
        doc["failures"] = failures;
        if (lastError[0]) doc["error"] = lastError;
        doc["autoUpdate"] = cfg.autoUpdate;
        doc["fwBase"] = cfg.fwBase;
        doc["fwTitle"] = FIRMWARE_TITLE;
        doc["fw"] = FIRMWARE_VERSION;
        if (fwTried[0]) doc["fwTried"] = fwTried;
        if (fwTries) doc["fwTries"] = fwTries;
        doc["fwMaxTries"] = OTA_MAX_ATTEMPTS;
        if (fwNote[0]) doc["fwNote"] = fwNote;

        // Nested as real JSON, not as an escaped string, so the page can lay it
        // out readably instead of showing a wall of backslashes. Both are built
        // fresh on every request: what is shown is what the next publish sends,
        // which is the question being asked.
        String tele, attrs;
        if (buildTelemetry(tele)) doc["payload"] = serialized(tele);
        buildAttributes(attrs);
        doc["attrPayload"] = serialized(attrs);

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    auto *handler = new AsyncCallbackJsonWebHandler("/api/mqtt",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject o = json.as<JsonObject>();

            cfg.enabled = o["enabled"] | false;
            cfg.autoUpdate = o["autoUpdate"] | false;
            strlcpy(cfg.fwBase, o["fwBase"] | "", sizeof(cfg.fwBase));
            cfg.port = o["port"] | (uint16_t)MQTT_DEF_PORT;
            cfg.intervalS = o["interval"] | (uint16_t)MQTT_DEF_INTERVAL_S;
            if (cfg.intervalS < 5) cfg.intervalS = 5;
            strlcpy(cfg.host, o["host"] | "", sizeof(cfg.host));
            strlcpy(cfg.user, o["user"] | "", sizeof(cfg.user));
            strlcpy(cfg.pubTopic,  o["pubTopic"]  | MQTT_DEF_PUB_TOPIC,  sizeof(cfg.pubTopic));
            strlcpy(cfg.subTopic,  o["subTopic"]  | "", sizeof(cfg.subTopic));
            strlcpy(cfg.attrTopic, o["attrTopic"] | "", sizeof(cfg.attrTopic));
            strlcpy(cfg.clientId,  o["clientId"]  | "", sizeof(cfg.clientId));
            if (!cfg.clientId[0]) defaultClientId(cfg.clientId, sizeof(cfg.clientId));

            if (cfg.enabled && !cfg.host[0]) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"a broker address is required\"}");
                return;
            }
            if (!cfg.pubTopic[0]) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"a publish topic is required\"}");
                return;
            }

            // Same rule as the LINE token: blank keeps what is stored, because the
            // page is never shown the saved value and so cannot send it back.
            const char *pass = o["pass"] | "";
            if (pass[0]) strlcpy(cfg.pass, pass, sizeof(cfg.pass));
            if (o["clearPass"] | false) cfg.pass[0] = '\0';

            save();
            // Drop the link so the task rebuilds it with the new settings on its
            // next pass rather than carrying on against the old broker.
            if (client.connected()) client.disconnect();
            lastAttemptMs = 0;
            Serial.printf("MQTT: saved (%s)\n", cfg.enabled ? "enabled" : "disabled");
            request->send(200, "application/json", "{\"ok\":true}");
        });
    server.addHandler(handler);
}
