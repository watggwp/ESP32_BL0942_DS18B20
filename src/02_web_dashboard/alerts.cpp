#include "alerts.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <time.h>

#include "config.h"

namespace {

const char *LINE_PUSH_URL = "https://api.line.me/v2/bot/message/push";

// CARD PALETTE. A light card on a gradient header, which is what a modern LINE
// card looks like and what reads best in a chat list. Deliberately NOT the
// dashboard's dark scheme: a phone notification is glanced at in daylight, at
// arm's length, by someone who is not looking for it.
//
// Colours are written with explicit hex on every element, including the alpha
// forms (#RRGGBBAA), so the card renders identically whether the recipient's LINE
// is in light or dark mode -- an unstyled element would flip with their theme and
// disappear against the gradient.
const char *GRAD_ALERT_A   = "#FF512F";   // hot orange-red, top-left
const char *GRAD_ALERT_B   = "#DD2476";   // magenta, bottom-right
const char *GRAD_CLEAR_A   = "#11998E";   // teal
const char *GRAD_CLEAR_B   = "#38EF7D";   // green
const char *BAR_ALERT      = "#FF512F";
const char *BAR_CLEAR      = "#11998E";
const char *CARD_BG        = "#FFFFFF";
const char *CARD_FOOT      = "#F7F9FC";
const char *CARD_TRACK     = "#EDF1F7";   // the unfilled part of the meter
const char *CARD_ROW       = "#F5F7FA";
const char *INK            = "#1F2430";
const char *INK_MUTED      = "#8A94A6";
const char *ON_GRAD        = "#FFFFFF";
const char *ON_GRAD_SOFT   = "#FFFFFFCC";
const char *ON_GRAD_CHIP   = "#FFFFFF2E";

Preferences prefs;   // "alerts"

struct Config {
    bool  enabled     = false;
    bool  onRecover   = true;
    uint16_t cooldown = ALERT_DEF_COOLDOWN_MIN;   // minutes, per alert
    uint8_t  dailyCap = ALERT_DEF_DAILY_CAP;
    bool  tempOn = true;  float tempMax = ALERT_DEF_TEMP_MAX;
    bool  voltOn = true;  float voltMin = ALERT_DEF_VOLT_MIN, voltMax = ALERT_DEF_VOLT_MAX;
    bool  freqOn = true;  float freqMin = ALERT_DEF_FREQ_MIN, freqMax = ALERT_DEF_FREQ_MAX;
    bool  ampOn  = true;  float ampMax  = ALERT_DEF_AMP_MAX;
    bool  wattOn = true;  float wattMax = ALERT_DEF_WATT_MAX;
    char  to[64] = "";
};
Config cfg;

// Kept out of Config so it never goes anywhere near the JSON the settings page
// reads back: the page is unauthenticated on the LAN, and a channel access token
// is a credential for the whole LINE account, not just this board.
char token[300] = "";

// One gate per thing that can go wrong. Electrical gates are fixed; the
// temperature gates are per slot, because two probes can be hot for entirely
// different reasons and one clearing says nothing about the other.
enum GateId : uint8_t {
    G_VOLT_LOW, G_VOLT_HIGH, G_FREQ_LOW, G_FREQ_HIGH, G_AMP_HIGH, G_WATT_HIGH,
    G_ELECTRICAL_COUNT
};

struct Gate {
    bool     active   = false;   // currently in the alarm state
    uint8_t  breach   = 0;       // consecutive out-of-range samples
    uint8_t  clear    = 0;       // consecutive back-in-range samples
    uint32_t lastSent = 0;       // millis() of the last message from this gate
    bool     everSent = false;
};
Gate gates[G_ELECTRICAL_COUNT];
Gate tempGates[DS18B20_COUNT];

// What gets queued is the FACTS of an alert, not a finished sentence, so the card
// can be laid out at send time -- the headline number stays a number instead of
// being flattened into a line of prose.
//   rows: "label\tvalue\n" per line, rendered as the detail list
//   fill: where the reading sits against its limit, 0-100, drawn as the meter
struct Msg {
    bool    recovered = false;
    char    icon[8]   = "";
    char    title[40] = "";
    char    value[16] = "";   // "72.4"
    char    unit[8]   = "";   // "°C"
    char    delta[72] = "";   // the chip under the number
    char    limit[48] = "";   // "limit 60.0 °C"
    uint8_t fill      = 0;
    char    rows[320] = "";
    char    when[24]  = "";
};
QueueHandle_t queue = nullptr;

// Quota accounting. capDay is the calendar day the counter belongs to; before the
// clock syncs there is no calendar, so it rolls on a 24h millis timer instead and
// a board without NTP still cannot run away.
volatile uint16_t sentToday = 0;
int      capDay      = -1;
uint32_t capStartMs  = 0;
volatile uint32_t lastPushMs = 0;
volatile int      lastPushCode = 0;   // HTTP status of the most recent attempt
// LINE says exactly what it disliked in the response body, and that sentence is
// the difference between "HTTP 400" and "the ID you pasted is not valid". Kept so
// the settings page can show it instead of making someone open a serial monitor.
char lastError[192] = "";

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
void load() {
    prefs.begin("alerts", false);
    cfg.enabled   = prefs.getBool ("on",      false);
    cfg.onRecover = prefs.getBool ("recover", true);
    cfg.cooldown  = prefs.getUShort("cool",   ALERT_DEF_COOLDOWN_MIN);
    cfg.dailyCap  = prefs.getUChar("cap",     ALERT_DEF_DAILY_CAP);
    cfg.tempOn    = prefs.getBool ("tOn",  true);   cfg.tempMax = prefs.getFloat("tMax", ALERT_DEF_TEMP_MAX);
    cfg.voltOn    = prefs.getBool ("vOn",  true);   cfg.voltMin = prefs.getFloat("vMin", ALERT_DEF_VOLT_MIN);
                                                    cfg.voltMax = prefs.getFloat("vMax", ALERT_DEF_VOLT_MAX);
    cfg.freqOn    = prefs.getBool ("fOn",  true);   cfg.freqMin = prefs.getFloat("fMin", ALERT_DEF_FREQ_MIN);
                                                    cfg.freqMax = prefs.getFloat("fMax", ALERT_DEF_FREQ_MAX);
    cfg.ampOn     = prefs.getBool ("aOn",  true);   cfg.ampMax  = prefs.getFloat("aMax", ALERT_DEF_AMP_MAX);
    cfg.wattOn    = prefs.getBool ("wOn",  true);   cfg.wattMax = prefs.getFloat("wMax", ALERT_DEF_WATT_MAX);
    prefs.getString("to", cfg.to, sizeof(cfg.to));
    prefs.getString("token", token, sizeof(token));
}

void save() {
    prefs.putBool  ("on",      cfg.enabled);
    prefs.putBool  ("recover", cfg.onRecover);
    prefs.putUShort("cool",    cfg.cooldown);
    prefs.putUChar ("cap",     cfg.dailyCap);
    prefs.putBool  ("tOn", cfg.tempOn);  prefs.putFloat("tMax", cfg.tempMax);
    prefs.putBool  ("vOn", cfg.voltOn);  prefs.putFloat("vMin", cfg.voltMin);
                                         prefs.putFloat("vMax", cfg.voltMax);
    prefs.putBool  ("fOn", cfg.freqOn);  prefs.putFloat("fMin", cfg.freqMin);
                                         prefs.putFloat("fMax", cfg.freqMax);
    prefs.putBool  ("aOn", cfg.ampOn);   prefs.putFloat("aMax", cfg.ampMax);
    prefs.putBool  ("wOn", cfg.wattOn);  prefs.putFloat("wMax", cfg.wattMax);
    prefs.putString("to", cfg.to);
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------
bool clockReady(struct tm *out = nullptr) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 120) return false;   // still 1970: SNTP has not answered
    if (out) *out = tm;
    return true;
}

// A real time once the clock is up, uptime before that -- a card with no time on
// it at all is the one thing that is never useful.
void stamp(char *out, size_t size) {
    struct tm tm;
    if (clockReady(&tm)) {
        strftime(out, size, "%d %b %H:%M", &tm);
        return;
    }
    uint32_t s = millis() / 1000;
    snprintf(out, size, "uptime %luh%02lum", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60));
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------
bool quotaAllows() {
    struct tm tm;
    if (clockReady(&tm)) {
        if (capDay != tm.tm_yday) { capDay = tm.tm_yday; sentToday = 0; }
    } else {
        if (capStartMs == 0) capStartMs = millis();
        if (millis() - capStartMs > 86400000UL) { capStartMs = millis(); sentToday = 0; }
    }
    return sentToday < cfg.dailyCap;
}

void enqueue(Msg &m) {
    if (!queue) return;
    if (!quotaAllows()) {
        Serial.printf("Alerts: daily cap of %u reached, dropping: %s\n", cfg.dailyCap, m.title);
        return;
    }
    stamp(m.when, sizeof(m.when));
    // Never block the caller: this runs on the loop task, and a full queue means
    // the network is already struggling, which is not a reason to stall sampling.
    if (xQueueSend(queue, &m, 0) != pdTRUE) {
        Serial.println("Alerts: send queue full, message dropped");
    }
    // The daily cap is counted on DELIVERY, in push(). A rejected message never
    // reached anyone and never touched the LINE quota, so charging it here would
    // mean twenty failed attempts while setting the destination up locks the
    // board out for the rest of the day -- exactly when you need to keep trying.
}

// ---------------------------------------------------------------------------
// The card
//
// A Flex bubble, laid out entirely from JSON. That constraint is the whole reason
// this design works on this board: an image-based card would need somewhere to
// host the image, and an ESP32 on a factory LAN has nowhere to put one.
//
//   gradient header  icon + metric name, the reading at 4xl, and a chip saying
//                    how far past the limit it went
//   meter            the reading drawn against its limit, so "how bad" lands
//                    before any number is read
//   detail rows      which slots, or the other readings at that moment
//   footer           which board, and when
// ---------------------------------------------------------------------------
void addText(JsonObject o, const char *text, const char *size, const char *color) {
    o["type"] = "text";
    o["text"] = text;
    o["size"] = size;
    o["color"] = color;
}

void buildBubble(JsonObject msg, const Msg &m, char *rowScratch, size_t scratchSize) {
    const char *gradA = m.recovered ? GRAD_CLEAR_A : GRAD_ALERT_A;
    const char *gradB = m.recovered ? GRAD_CLEAR_B : GRAD_ALERT_B;
    const char *bar   = m.recovered ? BAR_CLEAR    : BAR_ALERT;

    char alt[180];
    snprintf(alt, sizeof(alt), "%s %s %s%s%s - %s", m.icon, m.title,
             m.value, m.unit[0] ? " " : "", m.unit, DEVICE_HOSTNAME);
    msg["type"] = "flex";
    msg["altText"] = alt;   // the notification-list preview, and the fallback

    JsonObject bubble = msg["contents"].to<JsonObject>();
    bubble["type"] = "bubble";

    JsonObject body = bubble["body"].to<JsonObject>();
    body["type"] = "box";
    body["layout"] = "vertical";
    body["paddingAll"] = "0px";
    body["backgroundColor"] = CARD_BG;
    JsonArray outer = body["contents"].to<JsonArray>();

    // ---- gradient header --------------------------------------------------
    JsonObject head = outer.add<JsonObject>();
    head["type"] = "box";
    head["layout"] = "vertical";
    head["paddingAll"] = "20px";
    JsonObject grad = head["background"].to<JsonObject>();
    grad["type"] = "linearGradient";
    grad["angle"] = "135deg";
    grad["startColor"] = gradA;
    grad["endColor"] = gradB;
    JsonArray hc = head["contents"].to<JsonArray>();

    JsonObject titleRow = hc.add<JsonObject>();
    titleRow["type"] = "box";
    titleRow["layout"] = "baseline";
    JsonArray tr = titleRow["contents"].to<JsonArray>();
    JsonObject icon = tr.add<JsonObject>();
    addText(icon, m.icon, "sm", ON_GRAD);
    icon["flex"] = 0;
    JsonObject name = tr.add<JsonObject>();
    addText(name, m.title, "sm", ON_GRAD_SOFT);
    name["weight"] = "bold";
    name["margin"] = "sm";

    // One text, two spans: the number carries the weight and the unit steps back,
    // which a separate text element cannot do without breaking the baseline.
    JsonObject reading = hc.add<JsonObject>();
    reading["type"] = "text";
    reading["margin"] = "md";
    reading["adjustMode"] = "shrink-to-fit";   // a five-digit wattage still fits
    JsonArray spans = reading["contents"].to<JsonArray>();
    JsonObject big = spans.add<JsonObject>();
    big["type"] = "span";
    big["text"] = m.value;
    big["size"] = "4xl";
    big["weight"] = "bold";
    big["color"] = ON_GRAD;
    if (m.unit[0]) {
        JsonObject unit = spans.add<JsonObject>();
        unit["type"] = "span";
        unit["text"] = " ";
        unit["size"] = "xl";
        unit["color"] = ON_GRAD_SOFT;
        JsonObject unit2 = spans.add<JsonObject>();
        unit2["type"] = "span";
        unit2["text"] = m.unit;
        unit2["size"] = "xl";
        unit2["weight"] = "bold";
        unit2["color"] = ON_GRAD_SOFT;
    }

    if (m.delta[0]) {
        JsonObject chip = hc.add<JsonObject>();
        chip["type"] = "box";
        chip["layout"] = "vertical";
        chip["margin"] = "md";
        chip["cornerRadius"] = "14px";
        chip["backgroundColor"] = ON_GRAD_CHIP;
        chip["paddingAll"] = "7px";
        chip["paddingStart"] = "13px";
        chip["paddingEnd"] = "13px";
        JsonObject chipText = chip["contents"].to<JsonArray>().add<JsonObject>();
        addText(chipText, m.delta, "xs", ON_GRAD);
        chipText["weight"] = "bold";
        chipText["wrap"] = true;
    }

    // ---- meter + detail ---------------------------------------------------
    JsonObject pad = outer.add<JsonObject>();
    pad["type"] = "box";
    pad["layout"] = "vertical";
    pad["paddingAll"] = "20px";
    JsonArray c = pad["contents"].to<JsonArray>();

    JsonObject track = c.add<JsonObject>();
    track["type"] = "box";
    track["layout"] = "horizontal";
    track["height"] = "8px";
    track["backgroundColor"] = CARD_TRACK;
    track["cornerRadius"] = "4px";
    JsonArray trackContents = track["contents"].to<JsonArray>();
    JsonObject fill = trackContents.add<JsonObject>();
    fill["type"] = "box";
    fill["layout"] = "vertical";
    char widthPct[8];
    snprintf(widthPct, sizeof(widthPct), "%u%%", m.fill < 3 ? 3 : m.fill);   // never a zero-width sliver
    fill["width"] = widthPct;
    fill["backgroundColor"] = bar;
    fill["cornerRadius"] = "4px";
    fill["contents"].to<JsonArray>().add<JsonObject>()["type"] = "filler";

    if (m.limit[0]) {
        JsonObject lim = c.add<JsonObject>();
        addText(lim, m.limit, "xxs", INK_MUTED);
        lim["margin"] = "sm";
        lim["align"] = "end";
        lim["wrap"] = true;
    }

    if (m.rows[0]) {
        JsonObject list = c.add<JsonObject>();
        list["type"] = "box";
        list["layout"] = "vertical";
        list["margin"] = "lg";
        list["spacing"] = "sm";
        JsonArray la = list["contents"].to<JsonArray>();

        strlcpy(rowScratch, m.rows, scratchSize);
        char *save = nullptr;
        for (char *line = strtok_r(rowScratch, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
            // Split the row before either half is handed over: the tab becomes
            // the label's terminator, so both sides are plain strings from here.
            char *tab = strchr(line, '\t');
            const char *reading = " ";
            if (tab) {
                *tab = '\0';
                reading = tab + 1;
            }

            JsonObject row = la.add<JsonObject>();
            row["type"] = "box";
            row["layout"] = "horizontal";
            row["backgroundColor"] = CARD_ROW;
            row["cornerRadius"] = "10px";
            row["paddingAll"] = "11px";
            JsonArray rc = row["contents"].to<JsonArray>();

            JsonObject label = rc.add<JsonObject>();
            addText(label, line, "sm", INK_MUTED);
            label["flex"] = 1;
            label["wrap"] = true;

            JsonObject val = rc.add<JsonObject>();
            addText(val, reading, "sm", INK);
            val["weight"] = "bold";
            val["align"] = "end";
            val["flex"] = 0;
        }
    }

    // ---- footer -----------------------------------------------------------
    JsonObject foot = outer.add<JsonObject>();
    foot["type"] = "box";
    foot["layout"] = "horizontal";
    foot["backgroundColor"] = CARD_FOOT;
    foot["paddingAll"] = "14px";
    foot["paddingStart"] = "20px";
    foot["paddingEnd"] = "20px";
    JsonArray fa = foot["contents"].to<JsonArray>();
    JsonObject who = fa.add<JsonObject>();
    addText(who, DEVICE_HOSTNAME, "xxs", INK_MUTED);
    who["flex"] = 1;
    who["wrap"] = true;
    JsonObject when = fa.add<JsonObject>();
    addText(when, m.when, "xxs", INK_MUTED);
    when["align"] = "end";
    when["flex"] = 0;
}

bool push(const Msg &m) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Alerts: no network, message dropped");
        return false;
    }
    if (!token[0] || !cfg.to[0]) {
        Serial.println("Alerts: no token or destination set");
        return false;
    }
    if (ESP.getFreeHeap() < ALERT_MIN_FREE_HEAP) {
        Serial.printf("Alerts: only %u bytes of heap, skipping the handshake\n", ESP.getFreeHeap());
        return false;
    }

    WiFiClientSecure client;
    // No certificate check. api.line.me is reached over TLS either way, so this
    // is not plaintext, but a machine that can intercept the connection could
    // impersonate LINE and collect the token. Pin a root CA here if the board
    // sits on a network where that matters.
    client.setInsecure();
    client.setTimeout(12);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(12000);
    if (!http.begin(client, LINE_PUSH_URL)) {
        Serial.println("Alerts: HTTPClient.begin() failed");
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + token);

    // Built with ArduinoJson rather than sprintf: sensor names are whatever the
    // operator typed, quotes and Thai included, and they have to survive the trip
    // as valid JSON.
    JsonDocument doc;
    doc["to"] = cfg.to;
    JsonObject msg = doc["messages"].to<JsonArray>().add<JsonObject>();
    char scratch[sizeof(m.rows)];   // outlives serialization; strtok_r chews it up
    buildBubble(msg, m, scratch, sizeof(scratch));
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    lastPushCode = code;
    lastPushMs = millis();
    if (code == 200) {
        lastError[0] = '\0';
        sentToday++;   // only a delivered message costs quota
    } else {
        // The reason is almost always one of four: a stale token, an ID that is
        // not valid for this channel, a recipient who has not added the account
        // as a friend, or the monthly quota being spent. LINE names which.
        String why = http.getString();
        strlcpy(lastError, why.c_str(), sizeof(lastError));
        Serial.printf("Alerts: LINE push failed, HTTP %d: %s\n", code, lastError);
    }
    http.end();
    return code == 200;
}

void senderTask(void *) {
    Msg m;
    for (;;) {
        if (xQueueReceive(queue, &m, portMAX_DELAY) == pdTRUE) push(m);
    }
}

// ---------------------------------------------------------------------------
// Gate machinery
// ---------------------------------------------------------------------------
bool cooledDown(const Gate &g) {
    if (!g.everSent) return true;
    return (millis() - g.lastSent) >= (uint32_t)cfg.cooldown * 60000UL;
}

// Runs one gate and reports whether it just changed state. `bad` is the reading
// being outside its limit; `good` is it being back inside by the hysteresis
// margin. They are deliberately not each other's inverse -- the space between
// them is what a value sitting on the threshold falls into, where nothing at all
// happens.
enum class Edge : uint8_t { NONE, FIRED, CLEARED };

Edge step(Gate &g, bool bad, bool good) {
    if (!g.active) {
        g.clear = 0;
        g.breach = bad ? g.breach + 1 : 0;
        if (g.breach >= ALERT_CONFIRM_SAMPLES && cooledDown(g)) {
            g.active = true;
            g.breach = 0;
            g.lastSent = millis();
            g.everSent = true;
            return Edge::FIRED;
        }
        if (g.breach > ALERT_CONFIRM_SAMPLES) g.breach = ALERT_CONFIRM_SAMPLES;   // no overflow while muted
        return Edge::NONE;
    }
    g.breach = 0;
    g.clear = good ? g.clear + 1 : 0;
    if (g.clear >= ALERT_CLEAR_SAMPLES) {
        g.active = false;
        g.clear = 0;
        return Edge::CLEARED;
    }
    return Edge::NONE;
}

// The meter is the reading as a share of its limit, capped. At or past the limit
// the bar is simply full -- how far past is what the chip says in words, and a
// bar that could overflow its own track would just look broken.
uint8_t meterFill(float reading, float limit) {
    if (limit <= 0) return 100;
    float pct = (reading / limit) * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

void queueAlert(bool recovered, const char *icon, const char *title, const char *value,
                const char *unit, const char *delta, const char *limit, uint8_t fill,
                const char *rows) {
    Msg m;
    m.recovered = recovered;
    m.fill = fill;
    strlcpy(m.icon,  icon,  sizeof(m.icon));
    strlcpy(m.title, title, sizeof(m.title));
    strlcpy(m.value, value, sizeof(m.value));
    strlcpy(m.unit,  unit,  sizeof(m.unit));
    if (delta) strlcpy(m.delta, delta, sizeof(m.delta));
    if (limit) strlcpy(m.limit, limit, sizeof(m.limit));
    if (rows)  strlcpy(m.rows,  rows,  sizeof(m.rows));
    enqueue(m);
}

// One band, both edges, in one place -- every electrical alert is the same shape,
// and writing it out five times is how the low and high cases drift apart.
void band(Gate &g, bool bad, bool good, bool high, const char *icon, const char *what,
          const char *unit, float reading, float limit, uint8_t decimals, const char *rows) {
    char value[16], delta[72], limitText[48];
    snprintf(value, sizeof(value), "%.*f", decimals, reading);

    switch (step(g, bad, good)) {
        case Edge::FIRED: {
            char title[40];
            snprintf(title, sizeof(title), "%s %s", what, high ? "HIGH" : "LOW");
            float off = high ? reading - limit : limit - reading;
            snprintf(delta, sizeof(delta), "%s %.*f %s %s the limit",
                     high ? "\xE2\x96\xB2" : "\xE2\x96\xBC", decimals, off, unit,
                     high ? "over" : "under");
            snprintf(limitText, sizeof(limitText), "limit %.*f %s", decimals, limit, unit);
            queueAlert(false, icon, title, value, unit, delta, limitText,
                       meterFill(reading, limit), rows);
            break;
        }
        case Edge::CLEARED:
            if (cfg.onRecover) {
                char title[40];
                snprintf(title, sizeof(title), "%s RECOVERED", what);
                snprintf(limitText, sizeof(limitText), "limit %.*f %s", decimals, limit, unit);
                queueAlert(true, "\xE2\x9C\x85", title, value, unit, "back inside the safe range",
                           limitText, meterFill(reading, limit), nullptr);
            }
            break;
        default: break;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
void Alerts::begin() {
    load();
    configTzTime(NTP_TZ, NTP_SERVER_1, NTP_SERVER_2);
    queue = xQueueCreate(4, sizeof(Msg));
    if (!queue) {
        Serial.println("Alerts: could not create the send queue -- alerts disabled");
        return;
    }
    xTaskCreate(senderTask, "line", ALERT_TASK_STACK, nullptr, 1, nullptr);
    Serial.printf("Alerts: %s, token %s, destination %s\n",
                  cfg.enabled ? "on" : "off",
                  token[0] ? "set" : "MISSING",
                  cfg.to[0] ? cfg.to : "MISSING");
}

void Alerts::evaluate(bool meterOk, float volts, float amps, float watts, float hertz,
                      const float *temps, uint8_t tempCount, SlotNameFn nameOf) {
    if (!cfg.enabled) return;

    // ---- electrical -------------------------------------------------------
    // A failed meter read reports zeros, which would look exactly like a total
    // power failure. Holding the gates still is the honest answer: this board
    // knows the reading is missing, not that the voltage is gone.
    if (meterOk) {
        char context[320];

        if (cfg.voltOn) {
            snprintf(context, sizeof(context), "Current\t%.3f A\nPower\t%.0f W", amps, watts);
            band(gates[G_VOLT_LOW], volts < cfg.voltMin, volts >= cfg.voltMin + ALERT_HYST_VOLT_V,
                 false, "\xF0\x9F\x94\x8C", "VOLTAGE", "V", volts, cfg.voltMin, 1, context);
            band(gates[G_VOLT_HIGH], volts > cfg.voltMax, volts <= cfg.voltMax - ALERT_HYST_VOLT_V,
                 true, "\xF0\x9F\x94\x8C", "VOLTAGE", "V", volts, cfg.voltMax, 1, context);
        }

        if (cfg.freqOn) {
            snprintf(context, sizeof(context), "Voltage\t%.1f V\nPower\t%.0f W", volts, watts);
            band(gates[G_FREQ_LOW], hertz < cfg.freqMin, hertz >= cfg.freqMin + ALERT_HYST_FREQ_HZ,
                 false, "\xF0\x9F\x94\x84", "FREQUENCY", "Hz", hertz, cfg.freqMin, 2, context);
            band(gates[G_FREQ_HIGH], hertz > cfg.freqMax, hertz <= cfg.freqMax - ALERT_HYST_FREQ_HZ,
                 true, "\xF0\x9F\x94\x84", "FREQUENCY", "Hz", hertz, cfg.freqMax, 2, context);
        }

        if (cfg.ampOn) {
            snprintf(context, sizeof(context), "Voltage\t%.1f V\nPower\t%.0f W", volts, watts);
            band(gates[G_AMP_HIGH], amps > cfg.ampMax, amps <= cfg.ampMax - ALERT_HYST_AMP_A,
                 true, "\xE2\x9A\xA1", "CURRENT", "A", amps, cfg.ampMax, 3, context);
        }

        if (cfg.wattOn) {
            snprintf(context, sizeof(context), "Voltage\t%.1f V\nCurrent\t%.3f A", volts, amps);
            band(gates[G_WATT_HIGH], watts > cfg.wattMax, watts <= cfg.wattMax - ALERT_HYST_WATT_W,
                 true, "\xF0\x9F\x93\x8A", "POWER", "W", watts, cfg.wattMax, 0, context);
        }
    }

    // ---- temperatures -----------------------------------------------------
    // Every slot is judged, then whatever fired in this same cycle goes out as
    // ONE card. Nine probes on one heatsink tend to cross a limit together, and
    // nine separate pushes for one event is how a month of quota disappears.
    if (!cfg.tempOn || !temps) return;

    char hot[320] = "", cool[320] = "";
    float hottest = 0, coolest = 0;
    uint8_t n = tempCount < DS18B20_COUNT ? tempCount : DS18B20_COUNT;

    for (uint8_t i = 0; i < n; i++) {
        float c = temps[i];
        if (isnan(c)) continue;   // offline probe: nothing to judge, hold the gate

        const char *name = nameOf ? nameOf(i) : "";
        char line[72];
        Edge e = step(tempGates[i], c > cfg.tempMax, c <= cfg.tempMax - ALERT_HYST_TEMP_C);
        if (e == Edge::NONE || (e == Edge::CLEARED && !cfg.onRecover)) continue;

        snprintf(line, sizeof(line), "Slot %u%s%s\t%.1f\xC2\xB0" "C\n",
                 i + 1, name && name[0] ? "  " : "", name ? name : "", c);
        if (e == Edge::FIRED) {
            if (!hot[0] || c > hottest) hottest = c;
            strlcat(hot, line, sizeof(hot));
        } else {
            if (!cool[0] || c > coolest) coolest = c;
            strlcat(cool, line, sizeof(cool));
        }
    }

    char value[16], delta[72], limitText[48];
    snprintf(limitText, sizeof(limitText), "limit %.1f \xC2\xB0" "C", cfg.tempMax);
    if (hot[0]) {
        snprintf(value, sizeof(value), "%.1f", hottest);
        snprintf(delta, sizeof(delta), "\xE2\x96\xB2 %.1f \xC2\xB0" "C over the limit", hottest - cfg.tempMax);
        queueAlert(false, "\xF0\x9F\x94\xA5", "TEMPERATURE HIGH", value, "\xC2\xB0" "C",
                   delta, limitText, meterFill(hottest, cfg.tempMax), hot);
    }
    if (cool[0]) {
        snprintf(value, sizeof(value), "%.1f", coolest);
        queueAlert(true, "\xE2\x9C\x85", "TEMPERATURE RECOVERED", value, "\xC2\xB0" "C",
                   "back inside the safe range", limitText, meterFill(coolest, cfg.tempMax), cool);
    }
}

void Alerts::registerRoutes(AsyncWebServer &server) {
    // Sub-path first: a plain-string route matches its own children too.
    server.on("/api/alerts/test", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!token[0] || !cfg.to[0]) {
            request->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"set a token and a destination first\"}");
            return;
        }
        // Sent in the recovered style, so a test can never be mistaken for a real
        // fault by whoever else is in that group.
        queueAlert(true, "\xF0\x9F\x94\x94", "TEST MESSAGE", "OK", "",
                   "Alerts are wired up correctly", "this is what a real one looks like", 100,
                   "Temperature\twatching\nVoltage\twatching\nPower\twatching");
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/alerts", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["enabled"]   = cfg.enabled;
        doc["onRecover"] = cfg.onRecover;
        doc["cooldown"]  = cfg.cooldown;
        doc["dailyCap"]  = cfg.dailyCap;
        doc["tempOn"] = cfg.tempOn;  doc["tempMax"] = cfg.tempMax;
        doc["voltOn"] = cfg.voltOn;  doc["voltMin"] = cfg.voltMin;  doc["voltMax"] = cfg.voltMax;
        doc["freqOn"] = cfg.freqOn;  doc["freqMin"] = cfg.freqMin;  doc["freqMax"] = cfg.freqMax;
        doc["ampOn"]  = cfg.ampOn;   doc["ampMax"]  = cfg.ampMax;
        doc["wattOn"] = cfg.wattOn;  doc["wattMax"] = cfg.wattMax;
        doc["to"] = cfg.to;
        // The token is never sent back -- see the note where it is declared.
        doc["tokenSet"] = token[0] != '\0';
        doc["sentToday"] = sentToday;
        char when[24];
        stamp(when, sizeof(when));
        doc["now"] = when;
        doc["clockOk"] = clockReady();
        if (lastPushMs) doc["lastCode"] = lastPushCode;
        if (lastError[0]) doc["lastError"] = lastError;
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    auto *handler = new AsyncCallbackJsonWebHandler("/api/alerts",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonObject o = json.as<JsonObject>();

            cfg.enabled   = o["enabled"]   | false;
            cfg.onRecover = o["onRecover"] | true;
            cfg.cooldown  = o["cooldown"]  | (uint16_t)ALERT_DEF_COOLDOWN_MIN;
            cfg.dailyCap  = o["dailyCap"]  | (uint8_t)ALERT_DEF_DAILY_CAP;
            if (cfg.cooldown < 1)   cfg.cooldown = 1;
            if (cfg.dailyCap < 1)   cfg.dailyCap = 1;

            cfg.tempOn = o["tempOn"] | false;  cfg.tempMax = o["tempMax"] | ALERT_DEF_TEMP_MAX;
            cfg.voltOn = o["voltOn"] | false;  cfg.voltMin = o["voltMin"] | ALERT_DEF_VOLT_MIN;
                                               cfg.voltMax = o["voltMax"] | ALERT_DEF_VOLT_MAX;
            cfg.freqOn = o["freqOn"] | false;  cfg.freqMin = o["freqMin"] | ALERT_DEF_FREQ_MIN;
                                               cfg.freqMax = o["freqMax"] | ALERT_DEF_FREQ_MAX;
            cfg.ampOn  = o["ampOn"]  | false;  cfg.ampMax  = o["ampMax"]  | ALERT_DEF_AMP_MAX;
            cfg.wattOn = o["wattOn"] | false;  cfg.wattMax = o["wattMax"] | ALERT_DEF_WATT_MAX;

            // An inverted band would arm both gates at once and alert forever.
            if (cfg.voltMin >= cfg.voltMax || cfg.freqMin >= cfg.freqMax) {
                request->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"min must be below max\"}");
                return;
            }

            strlcpy(cfg.to, o["to"] | "", sizeof(cfg.to));

            // Empty means "keep what is stored": the page cannot show the saved
            // token, so it cannot send it back, and a blank field must not wipe it.
            const char *newToken = o["token"] | "";
            if (newToken[0]) {
                strlcpy(token, newToken, sizeof(token));
                prefs.putString("token", token);
            }

            save();
            Serial.printf("Alerts: saved (%s)\n", cfg.enabled ? "enabled" : "disabled");
            request->send(200, "application/json", "{\"ok\":true}");
        });
    server.addHandler(handler);
}
