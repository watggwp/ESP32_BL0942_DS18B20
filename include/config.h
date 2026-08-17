#pragma once
// Shared pin map and tunable settings for both example firmwares.
// Matches the "POWER METER" schematic: BL0942 U36 on UART2, ZMPT107-1 voltage
// sensor, CT-based current sensing (2000:1, 1ohm+1ohm burden), 9x DS18B20 on
// one OneWire bus, status LED on GPIO2.

// ---- Firmware version -------------------------------------------------------
// Bump this on every release. It is the single source of truth: the boot banner,
// the dashboard footer, the sensor setup page and the Wi-Fi status list all read
// it from here, so a board in the field can be identified without a serial
// cable. FIRMWARE_BUILD stamps the compile time, which is what tells two builds
// of the same version apart while a change is being tested.
#define FIRMWARE_VERSION "2.2.3"
#define FIRMWARE_BUILD   __DATE__ " " __TIME__

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

// ---- LINE alerts (example 2) ---------------------------------------------------
// Pushes a LINE message when a reading leaves its band. The channel access token,
// the destination ID and every threshold are entered at /settings and live in NVS
// -- nothing about your LINE account is compiled in. What lives here is the
// alert ENGINE's behaviour, which is not something an operator should be tuning
// from a phone.
//
// LINE Notify is gone (the service shut down on 31 March 2025), so this talks to
// the Messaging API: create a LINE Official Account, then paste its channel
// access token and your user/group ID into the Alerts tab.
//
// QUOTA IS THE REAL LIMIT, not flash. A LINE OA on the free plan sends only a few
// hundred messages a month and pushes count against it, so a reading hovering on
// a threshold could burn a month of quota in an afternoon. Three things stop that:
// a reading must stay out of range for CONFIRM_SAMPLES before anything is sent, it
// must come back a full hysteresis margin inside before the alert can arm again,
// and each alert has its own cooldown plus a shared daily cap.
#define ALERT_CONFIRM_SAMPLES 5      // consecutive bad reads before an alert fires
#define ALERT_CLEAR_SAMPLES   5      // consecutive good reads before it clears
#define ALERT_HYST_TEMP_C     2.0f   // how far back inside the band a value must
#define ALERT_HYST_VOLT_V     5.0f   // come before that alert can fire again --
#define ALERT_HYST_FREQ_HZ    0.2f   // this is what stops a value sitting exactly
#define ALERT_HYST_AMP_A      0.5f   // on the limit from flapping on every sample
#define ALERT_HYST_WATT_W     100.0f
#define ALERT_MIN_FREE_HEAP   60000  // skip the push below this -- a TLS handshake
                                     // needs room and a failed one is worse than
                                     // a late message
#define ALERT_TASK_STACK      8192   // bytes; mbedTLS handshakes are stack-hungry

// First-boot defaults for the thresholds. Everything here is editable at
// /settings afterwards; these values only seed a board that has never been set up.
#define ALERT_DEF_TEMP_MAX     60.0f
#define ALERT_DEF_VOLT_MIN    200.0f
#define ALERT_DEF_VOLT_MAX    250.0f
#define ALERT_DEF_FREQ_MIN     49.0f
#define ALERT_DEF_FREQ_MAX     51.0f
#define ALERT_DEF_AMP_MAX      20.0f
#define ALERT_DEF_WATT_MAX   4000.0f
#define ALERT_DEF_COOLDOWN_MIN   15  // minimum minutes between messages per alert
#define ALERT_DEF_DAILY_CAP      20  // hard stop for one calendar day

// ---- Clock (example 2) ---------------------------------------------------------
// Alerts carry a wall-clock time, which uptime cannot give. Also what the daily
// message cap counts days against.
#define NTP_SERVER_1 "pool.ntp.org"

#define NTP_SERVER_2 "time.google.com"
#define NTP_TZ       "ICT-7"   // Thailand, UTC+7, no DST (POSIX TZ: sign inverted)

// ---- Dashboard thermal colour range (example 2) -------------------------------
// Ends of the thermal ramp on the temperature cards: MIN and below is the coldest
// blue, MAX and above is deep red. Served to the page via /api/sensors, so this
// is the single place the range is defined -- the colour and the little bar under
// each card are both scaled from it.
#define TEMP_COLOR_MIN_C   20
#define TEMP_COLOR_MAX_C   50
