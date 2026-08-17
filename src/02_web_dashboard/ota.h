#pragma once
// Over-the-air firmware update: upload a .bin from a browser and the board
// reflashes itself and reboots into it. Served from the Firmware tab at
// /settings, so a board in a cabinet can be updated by whoever is standing in
// front of it with a phone, which is the same reason Wi-Fi and the sensor map
// are set from there rather than compiled in.
//
// The image lands in whichever app slot is NOT running -- partitions_p1.csv keeps
// two of 1856K each -- so a failed or interrupted upload leaves the working
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
void registerRoutes(AsyncWebServer &server);

// Carries out the reboot that a finished upload asks for. Called from loop()
// because restarting inside a request handler kills the connection before the
// browser is told the upload succeeded.
void loop();

}  // namespace OTA
