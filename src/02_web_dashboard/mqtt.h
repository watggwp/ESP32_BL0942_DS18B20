#pragma once
// MQTT telemetry, with ThingsBoard's topics as the defaults. Publishes the same
// readings the dashboard draws, on its own interval, and listens on a command
// topic for a firmware update carrying a URL.
//
// Broker, credentials, topics and the interval are set at /settings and stored
// in NVS -- nothing about the site is compiled in, same as Wi-Fi and LINE.
//
// EVERYTHING HAPPENS ON A SEPARATE TASK. PubSubClient has no non-blocking
// connect, so a broker that stops answering would otherwise freeze loop() -- and
// with it the SSE stream, the portal DNS and the sampling clock -- for seconds at
// a time on every retry. loop() only ever hands over a copy of the last reading.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace Mqtt {

// Looks up the operator's name for a slot; published once per connect as an
// attribute, so a ThingsBoard dashboard can label a series "หม้อแปลง" instead of
// "temp3". Returning an empty string is fine.
typedef const char *(*SlotNameFn)(uint8_t slot);

void begin(SlotNameFn nameOf);
void registerRoutes(AsyncWebServer &server);   // /api/mqtt, /api/mqtt/test

// Hand over one sensor cycle. Cheap and non-blocking: copies into a snapshot the
// publisher task reads on its own schedule. `meterOk` false means the electrical
// figures are not trustworthy and are published as null rather than as zero.
void sample(bool meterOk, float volts, float amps, float watts, float hertz,
            float energyKWh, const float *temps, uint8_t tempCount);

}  // namespace Mqtt
