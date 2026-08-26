#pragma once
// Over-the-air firmware update, from ThingsBoard only. Something on the server
// side names an image -- a package assigned to this device, or an fwUpdate RPC
// carrying a URL -- and the board fetches and flashes it itself. There is no
// upload from the web UI and no upload password: /api/ota reports what is
// running and nothing served over HTTP can write flash.
//
// The image lands in whichever app slot is NOT running -- partitions_p1.csv keeps
// two of 1856K each -- so a failed or interrupted download leaves the working
// firmware untouched and the board still boots. The bootloader only switches
// over once a complete, verified image has been written.
//
// WHAT CANNOT BE DONE THIS WAY: the bootloader and the partition table live
// outside the app slots and OTA never touches them. Changing partitions_p1.csv
// still means a USB cable.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace OTA {

void begin();

// GET /api/ota only: version, build, which slot is running, and where the
// rollback window stands. Read-only -- see the note above.
void registerRoutes(AsyncWebServer &server);

// ---- rollback -------------------------------------------------------------
// Where the running image stands with the bootloader. PENDING means this is the
// first run of a freshly flashed image and it has not been accepted yet: reset
// before it is, and the bootloader goes back to the previous slot on its own.
enum class Verify : uint8_t { NOT_APPLICABLE, PENDING, CONFIRMED };
Verify verifyState();
uint32_t verifySecondsLeft();

// ---- update from a URL ----------------------------------------------------
// The only way in: the MQTT handler works out where the image is -- from
// ThingsBoard's fw_title/fw_version attributes or an fwUpdate RPC -- and the
// board fetches it itself, so a fleet is updated from the server.
//
// The download runs on a task of its own -- it takes tens of seconds, and the
// caller here is an MQTT callback that has a keepalive to answer.
enum class UrlState : uint8_t { IDLE, RUNNING, DONE_OK, DONE_FAIL };

// false if a download is already in flight or the URL is unusable.
bool startFromUrl(const char *url);

UrlState urlState();
const char *urlMessage();   // why it failed, or what it flashed
// Marks the finished state as reported, so a watcher publishes it once.
void clearUrlState();

// Closes the rollback window once the image has proved itself, and carries out
// the reboot a finished download asks for. Called from loop() so the result can
// reach ThingsBoard before the board restarts.
void loop();

}  // namespace OTA
