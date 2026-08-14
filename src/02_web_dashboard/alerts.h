#pragma once
// LINE alerts: watches the readings this board already takes and pushes a message
// when one leaves its band. Thresholds, the channel access token and the
// destination ID are set at /settings and stored in NVS -- see config.h for the
// engine's own tuning (debounce, hysteresis, cooldown, daily cap).
//
// WHY THIS IS NOT JUST AN HTTP CALL IN loop(). A TLS handshake blocks for a
// second or more. Doing that on the loop task would stall the SSE push and the
// portal DNS along with it, and doing it inside an AsyncTCP request handler would
// stall every open browser. So evaluate() only ever queues text, and a separate
// FreeRTOS task does the talking.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace Alerts {

// Looks up the operator's name for a slot, for the message body. Returning an
// empty string is fine -- the message falls back to the slot number alone.
typedef const char *(*SlotNameFn)(uint8_t slot);

// Load the saved config, start the sender task and the NTP client. Safe to call
// before Wi-Fi is up: SNTP syncs on its own once there is a network.
void begin();

// /api/alerts (GET config + status, POST to change) and /api/alerts/test.
void registerRoutes(AsyncWebServer &server);

// Feed one sensor cycle. `meterOk` false means the electrical readings are not
// trustworthy, so only the temperatures are judged. A temperature that was not
// read comes in as NAN.
void evaluate(bool meterOk, float volts, float amps, float watts, float hertz,
              const float *temps, uint8_t tempCount, SlotNameFn nameOf);

}  // namespace Alerts
