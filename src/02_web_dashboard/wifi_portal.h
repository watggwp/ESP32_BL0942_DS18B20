#pragma once
// Wi-Fi credentials in NVS plus a captive-portal fallback, so the board can be
// moved to a new network from a phone instead of a rebuild.
//
// NVS is the ONLY source of credentials: nothing is compiled into the image, so
// every board off the flasher behaves identically and no password ever sits in
// the source tree. A board with nothing stored comes up as a setup AP.
//
// The portal rides on the dashboard's existing AsyncWebServer -- the only extra
// moving part is a DNSServer answering every lookup with the AP's own address,
// which is what makes the "Sign in to network" sheet appear by itself.
//
// Nothing here blocks except the initial connect attempt, so the sensor loop
// keeps reading and accumulating energy the whole time the portal is up.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

enum class WiFiPortalState : uint8_t {
    CONNECTING,  // joining (or rejoining) the saved network
    CONNECTED,   // on the LAN, dashboard reachable
    PORTAL,      // setup AP is up, waiting for credentials
};

namespace WiFiPortal {

// Load credentials and bring Wi-Fi up, falling back to the setup AP. `tick` is
// called roughly every 250ms during the connect wait so the caller can keep its
// status LED animating.
void begin(const char *hostname, void (*tick)() = nullptr);

// Pump the portal DNS, run deferred NVS writes, and track link state. Must be
// called on EVERY loop() iteration -- put it above any early return, or the
// captive portal stops answering.
void loop();

// Registers /wifi, /api/wifi*, and the catch-all that redirects captive-portal
// probes. Call before server.begin().
void registerRoutes(AsyncWebServer &server);

WiFiPortalState state();

// Setup AP name, e.g. "P1-Setup-3F7C". Empty until the portal has been started.
const char *apSsid();

}  // namespace WiFiPortal
