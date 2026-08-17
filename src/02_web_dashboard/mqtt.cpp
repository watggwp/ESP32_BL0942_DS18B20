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
};
Config cfg;

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
    cfg.enabled   = prefs.getBool("on", false);
    cfg.port      = prefs.getUShort("port", MQTT_DEF_PORT);
    cfg.intervalS = prefs.getUShort("every", MQTT_DEF_INTERVAL_S);
    prefs.getString("host", cfg.host, sizeof(cfg.host));
    prefs.getString("user", cfg.user, sizeof(cfg.user));
    prefs.getString("pass", cfg.pass, sizeof(cfg.pass));
    prefs.getString("cid",  cfg.clientId, sizeof(cfg.clientId));
    if (!prefs.getString("pub",  cfg.pubTopic,  sizeof(cfg.pubTopic)))  strlcpy(cfg.pubTopic,  MQTT_DEF_PUB_TOPIC,  sizeof(cfg.pubTopic));
    if (!prefs.getString("sub",  cfg.subTopic,  sizeof(cfg.subTopic)))  strlcpy(cfg.subTopic,  MQTT_DEF_SUB_TOPIC,  sizeof(cfg.subTopic));
    if (!prefs.isKey("attr")) strlcpy(cfg.attrTopic, MQTT_DEF_ATTR_TOPIC, sizeof(cfg.attrTopic));
    else prefs.getString("attr", cfg.attrTopic, sizeof(cfg.attrTopic));
    if (!cfg.clientId[0]) defaultClientId(cfg.clientId, sizeof(cfg.clientId));
    if (cfg.intervalS < 5) cfg.intervalS = 5;
}

void save() {
    prefs.putBool("on", cfg.enabled);
    prefs.putUShort("port", cfg.port);
    prefs.putUShort("every", cfg.intervalS);
    prefs.putString("host", cfg.host);
    prefs.putString("user", cfg.user);
    prefs.putString("pass", cfg.pass);
    prefs.putString("cid",  cfg.clientId);
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
void publishAttributes() {
    if (!cfg.attrTopic[0]) return;
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
    String out;
    serializeJson(doc, out);
    client.publish(cfg.attrTopic, out.c_str());
}

void publishTelemetry() {
    Snapshot s;
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s = shared;
    xSemaphoreGive(lock);
    if (!s.valid) return;

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
    for (uint8_t i = 0; i < s.tempCount && i < DS18B20_COUNT; i++) {
        char key[10];
        snprintf(key, sizeof(key), "temp%u", i + 1);
        if (isnan(s.temps[i])) doc[key] = nullptr;
        else                   doc[key] = serialized(String(s.temps[i], 2));
    }

    String out;
    serializeJson(doc, out);
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

    JsonDocument doc;
    doc["fw_state"] = st == OTA::UrlState::DONE_OK ? "UPDATED" : "FAILED";
    doc["fw_message"] = OTA::urlMessage();
    if (st == OTA::UrlState::DONE_OK) doc["fw_version"] = FIRMWARE_VERSION;
    String out;
    serializeJson(doc, out);
    client.publish(cfg.pubTopic, out.c_str());
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
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    auto *handler = new AsyncCallbackJsonWebHandler("/api/mqtt",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject o = json.as<JsonObject>();

            cfg.enabled = o["enabled"] | false;
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
