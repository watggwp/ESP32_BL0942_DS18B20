#pragma once
// Shared pin map and tunable settings for both example firmwares.
// Matches the "POWER METER" schematic: BL0942 U36 on UART2, ZMPT107-1 voltage
// sensor, CT-based current sensing (2000:1, 1ohm+1ohm burden), 9x DS18B20 on
// one OneWire bus, status LED on GPIO2.

// ---- BL0942 energy metering IC (UART2) -------------------------------------
#define BL0942_RX_PIN      16   // ESP32 GPIO16 (RXD1) <- BL0942 pin14 TX/SDO
#define BL0942_TX_PIN      17   // ESP32 GPIO17 (TXD1) -> BL0942 pin13 RX/SDI
#define BL0942_BAUD        4800 // confirmed on hardware; SCLK_BPS pin strap
                                 // actually yields 4800bps here despite the
                                 // schematic's "9600bps" label
#define BL0942_ADDRESS     3    // A1=HIGH, A2_NCS=HIGH on this board -> (A2<<1)|A1 = 3
#define BL0942_AC_FREQ_60HZ false // set true for 60Hz mains (this board: 50Hz)

// Print raw bytes + checksum details to Serial whenever a BL0942 read fails.
// Handy while bringing up a new board; safe to leave on (only fires on error).
#define BL0942_DEBUG       true

// ---- DS18B20 temperature sensors (OneWire) ---------------------------------
#define ONEWIRE_PIN        4
#define DS18B20_COUNT      9
#define DS18B20_RESOLUTION 12   // bits (9-12); 12 = 750ms conversion, 0.0625C steps

// ---- Status LED -------------------------------------------------------------
#define STATUS_LED_PIN     2
#define STATUS_LED_ACTIVE_HIGH true

// ---- Sampling ----------------------------------------------------------------
#define SENSOR_READ_INTERVAL_MS 1000

// ---- Wi-Fi setup portal (example 2) -------------------------------------------
// Credentials live in NVS only and are entered from a phone at /wifi, so a board
// can be moved to a new site without a rebuild and no password is ever compiled
// into the image. A board with nothing stored comes up as its own setup AP.
#define DEVICE_HOSTNAME "esp32-powermeter"  // http://esp32-powermeter.local/ (mDNS)
#define WIFI_CONNECT_TIMEOUT_MS 10000  // give up on the saved network after this
#define WIFI_PORTAL_AP_PREFIX   "P1-Setup"  // AP name gets "-<last 2 MAC bytes>"
#define WIFI_PORTAL_AP_PASSWORD ""     // <8 chars = open network (portal is local-only)

// ---- Dashboard thermal colour range (example 2) -------------------------------
// Ends of the thermal ramp on the temperature cards: MIN and below is the coldest
// blue, MAX and above is deep red. Served to the page via /api/sensors, so this
// is the single place the range is defined -- the colour and the little bar under
// each card are both scaled from it.
#define TEMP_COLOR_MIN_C   10
#define TEMP_COLOR_MAX_C   80
