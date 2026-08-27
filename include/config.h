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
// The package title ThingsBoard matches against. A firmware package in its
// OTA repository carries a title AND a version, and the device is only
// considered up to date when both agree -- so this string has to be typed
// identically into the Title field there.
#define FIRMWARE_TITLE   "PEA-PowerMeter"
#define FIRMWARE_VERSION "3.0.100"
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

// ---- MQTT / ThingsBoard (example 2) ---------------------------------------------
// Publishes the same readings the dashboard shows to an MQTT broker, and listens
// for a firmware-update command carrying a URL. Broker address, credentials and
// topics are entered at /settings and live in NVS -- only the defaults are here.
//
// The defaults are ThingsBoard's own topic names, because that is where these
// boards report. A plain broker works just as well: change the topics.
//
// RUNS ON ITS OWN TASK. A broker that stops answering makes connect() block for
// seconds, and PubSubClient has no non-blocking connect. On the loop task that
// would stall the SSE push, the captive-portal DNS and the sampling clock every
// retry, so the whole client lives on a separate task and loop() only ever hands
// it a copy of the latest reading.
#define MQTT_DEF_PORT        1883
#define MQTT_DEF_INTERVAL_S  30     // seconds between telemetry publishes
#define MQTT_DEF_PUB_TOPIC   "v1/devices/me/telemetry"
#define MQTT_DEF_SUB_TOPIC   "v1/devices/me/rpc/request/+"
#define MQTT_DEF_ATTR_TOPIC  "v1/devices/me/attributes"
#define MQTT_TASK_STACK      6144
#define MQTT_BUFFER_BYTES    1024   // PubSubClient defaults to 256, far too small
                                    // for nine temperatures plus the electricals
#define MQTT_RECONNECT_MS    8000   // gap between connection attempts
#define MQTT_KEEPALIVE_S     30

// Downloading and flashing an image takes tens of seconds, which is why it gets
// a task of its own rather than running wherever the command arrived.
// How many times one target version may be attempted before the board gives
// up on it. A download that never finished -- a dropped link, a power cut
// halfway, a server that was not up yet -- deserves another go. An image
// that flashes fine but does not contain the version it claims does not,
// and that is the case that would otherwise loop for ever, so the same
// counter bounds both.
// How long a freshly flashed image must run before it is accepted for good.
// ESP-IDF's bootloader marks an OTA image as pending on its first boot and
// rolls back to the previous slot unless the app confirms it -- Arduino
// normally confirms immediately, which only ever catches an image that
// cannot boot at all. Holding the confirmation until the board has been up
// this long AND has joined Wi-Fi covers what actually goes wrong instead:
// a crash a few seconds in, a boot loop, an image that comes up but can no
// longer reach the network.
//
// The cost of the window: a power cut inside it looks exactly like a failed
// image, and the board rolls back an update that was in fact fine. Long
// enough to prove the firmware, short enough that the odds of a power cut
// landing inside it stay small.
#define OTA_VERIFY_UPTIME_S  60
#define OTA_MAX_ATTEMPTS     3
// Gap before retrying a failed download, rather than waiting for the next
// reconnect -- which on a healthy link may be hours away.
#define OTA_RETRY_MS         60000
#define OTA_URL_TASK_STACK   12288  // https download: mbedTLS is stack-hungry

// ---- Clock (example 2) ---------------------------------------------------------
// Wall-clock time, which uptime cannot give. Synced by SNTP once there is a
// network; nothing on the board blocks waiting for it.
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
