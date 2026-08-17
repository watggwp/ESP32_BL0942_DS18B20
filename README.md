# ESP32 Power &amp; Temperature Monitor — BL0942 + 9× DS18B20

PlatformIO / Arduino firmware for an ESP32 that reads a **BL0942** single-phase
energy metering IC (voltage, current, active power, frequency, accumulated
energy) over UART, plus up to **9× DS18B20** temperature sensors on one OneWire
bus, with a GPIO status LED.

Two ready-to-flash firmwares are included as separate PlatformIO environments:
a plain Serial printout, and a live Wi-Fi web dashboard with a thermal-camera
style temperature grid and per-sensor configuration.

<p>
  <img alt="platform" src="https://img.shields.io/badge/platform-ESP32-blue">
  <img alt="framework" src="https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-orange">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
</p>

---

## Quick start

1. Install [PlatformIO](https://platformio.org/) (VS Code extension, or `pip install platformio`).
2. Open **this folder** in VS Code so PlatformIO picks up `platformio.ini`.
3. Build, upload and watch:
   ```sh
   pio run -e serial_monitor -t upload -t monitor
   # or
   pio run -e web_dashboard  -t upload -t monitor
   ```
4. For the web dashboard, tell the board which Wi-Fi to join. There is nothing
   to edit and no credentials file: a board with no network stored comes up as
   its own Wi-Fi access point named **`P1-Setup-XXXX`**. Join it from a phone
   and the setup page opens by itself — see
   [Wi-Fi setup portal](#wi-fi-setup-portal).

`pio run` with no `-e` builds both environments — a quick "does everything still
compile" check.

---

## Hardware

Built around the "POWER METER" schematic this firmware was developed against
(BL0942 `U36`, ZMPT107-1 voltage sensor, CT-based current sensing).

| Signal | BL0942 pin | ESP32 pin | Notes |
|---|---|---|---|
| TX/SDO | 14 | **GPIO16** (RXD1) | BL0942 → ESP32 |
| RX/SDI | 13 | **GPIO17** (TXD1) | ESP32 → BL0942 |
| SEL | 11 | GND | selects UART mode (VDD would be SPI) |
| SCLK_BPS | 12 | — | **4800 bps** measured, despite the schematic's "9600bps" label |
| A1 / A2_NCS | 6 / 7 | **HIGH / HIGH** | IC address = `(A2<<1)\|A1` = **3**, not 0 |
| CF1 / CF2 / ZX | 10 / 8 / 9 | not connected | everything is read over UART instead |

| Sensor | ESP32 pin | Notes |
|---|---|---|
| DS18B20 × 9 (shared data line) | **GPIO4** | needs a 4.7 kΩ pull-up to 3V3 |
| Status LED | **GPIO2** | onboard LED on most ESP32 DevKit boards |

**Current sensing:** CT ratio **2000:1**, burden resistors **R30 = 1Ω + R31 =
1Ω** (2Ω total). A 1000:1 CT with 0.5Ω + 0.5Ω gives the same 1 mV/A into the
BL0942 current channel — no firmware change, only recalibration.

**Voltage sensing:** ZMPT107-1 isolated voltage transformer, 100 kΩ series
resistor from Line into the primary (~2.3 mA at 230 V), secondary burden
R11 = 24.9Ω, R10 + C13 as an anti-aliasing filter.

Datasheets:
- BL0942 — https://www.belling.com.cn/media/file_object/bel_product/BL0942/datasheet/BL0942_V1.06_en.pdf
- DS18B20 — https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf

> ⚠️ Two things on this board did **not** match the schematic: the BL0942 address
> straps (HIGH/HIGH → address 3) and the actual UART baud (4800, not 9600). Both
> are already set correctly in `include/config.h`. If you build another unit from
> the same schematic and it doesn't respond, **measure those pins** rather than
> trusting either the labels or this repo.

---

## Example 1 — Serial monitor

`pio run -e serial_monitor -t upload -t monitor`

Reads the BL0942 and every detected DS18B20 once a second and prints a table to
Serial at 115200 baud. On boot it also prints the **64-bit ROM address of every
DS18B20 it finds**, which is the tool you need for mapping "which sensor is
physically where":

```
DS18B20: found 9 device(s) on GPIO4
  #0  addr=28FF641F8A2B3C11
  #1  addr=28AA0C5E19130255
  ...
```

No Wi-Fi, no web server — the simplest check that wiring and calibration are
sane.

---

## Example 2 — Web dashboard

`pio run -e web_dashboard -t upload -t monitor`

Everything from Example 1 plus an ESPAsyncWebServer dashboard at
`http://<device-ip>/` or `http://esp32-powermeter.local/` (mDNS).

- Animated arc gauge for active power, rolling ~5-minute power sparkline
- Voltage / current / frequency / apparent VA / free heap / uptime tiles
- Accumulated energy (kWh), integrated in software from active power — persists
  across reboots in NVS
- **3×3 temperature grid** with a thermal-camera colour ramp (below)
- Updates pushed once a second over Server-Sent Events (`GET /events`)

The dashboard is **read-only**: it holds no control that a passer-by can press,
which matters for the screen that gets left open on a wall. Everything you set
once lives behind `/settings`, as three tabs of one page:

| Tab | What it does |
|---|---|
| **Sensors** | fix each DS18B20 to a slot and name it (below), with live temperatures beside each row |
| **Calibration** | tune BL0942 kI/kV/kP against a real meter next to a live V/A/W readout; reset the kWh total |
| **Alerts** | push a LINE message when temperature, voltage, frequency, current or power leaves its band (below) |
| **Wi-Fi** | join a new network from a phone, see what the board is connected to (below) |
| **Firmware** | upload a new `.bin` over the air, with an optional upload password (below) |

`/wifi` opens that same page on the Wi-Fi tab — the captive portal and the
setup-mode redirect point there, so labels and QR codes printed with that URL
keep working. The scan only runs while the tab is open, since it takes the radio
off the network for seconds at a time.

Both pages are self-contained HTML with inline CSS/JS and **no CDN or external
font**, because the device has no guaranteed internet access. The one shared
asset is `/thermal.js`, served from the same firmware.

> mDNS (`.local`) works on Windows, macOS and iOS. **Most Android versions do not
> support it** — on Android use the IP address printed to Serial at boot.

### Sensor slots — why they exist

OneWire has no concept of physical position: `DallasTemperature::getAddress(i)`
returns the *i*-th device found by the ROM search, which walks the sensors'
64-bit factory addresses. Two consequences:

- The order has **nothing to do** with where a sensor sits on the cable.
- Pull one sensor out and **every sensor after it renumbers**, silently. The card
  you labelled "inlet pipe" in your head is now a different probe.

So this firmware addresses sensors by **slot**, not by bus index. A slot owns a
ROM address and a name, stored in NVS:

| Situation | Behaviour |
|---|---|
| All sensors present | Exactly the order you configured |
| One sensor dies or unplugs | **Its own slot shows `offline`. Nothing else moves.** |
| A new sensor appears | Appended to the first free slot, unnamed |
| Never configured | Falls back to bus order — same as before, nothing breaks |
| Stored map unreadable | Logs a warning, falls back to bus order |

Open `/settings` (or the *configure* link on the temperature card) to reorder
slots with ▲▼ and name each sensor. Names are UTF-8, so Thai works.

**Identifying a sensor:** a DS18B20 has no LED to blink, so the only way is to
heat it. The settings page shows a **live temperature next to every row**, fed by
the same `/events` stream the dashboard uses — pinch a sensor, watch which row
climbs, name it, move on. You never have to walk back to the screen.

*Rescan bus* picks up sensors plugged in after boot. The OneWire search blocks
for tens of milliseconds, so it runs from `loop()` rather than on the web
server's task; the page refetches shortly after.

### Thermal colour ramp

Each temperature card is coloured by a continuous ramp — number, card tint,
border, glow and the bar underneath all scale together:

```
TEMP_COLOR_MIN_C ─────────────────────────────────── TEMP_COLOR_MAX_C
   deep blue   blue   cyan   yellow   orange   red-orange   deep red
```

The ramp **deliberately contains no green.** On a gauge, green reads as "all
good" regardless of the number beside it — which is exactly the wrong signal at
45 °C on a bearing. Brightness climbs monotonically with heat instead, so the
grid reads as intensity the way a thermal camera does. Cards past the yellow
stop pick up a glow that strengthens with temperature, so an overheating sensor
is visible from across the room.

Colour is never the *only* channel: the numeric value and the bar length carry
the same information, which matters for the ~8% of men with red–green colour
blindness.

Range and palette live in one place each:

- **Range** — `TEMP_COLOR_MIN_C` / `TEMP_COLOR_MAX_C` in `include/config.h`,
  served to the pages via `/api/sensors`. The colour *and* the bar length are
  both derived from it, so they cannot drift apart.
- **Palette** — `THERMAL_STOPS` in `src/02_web_dashboard/thermal_js.h`, shared by
  both pages over `/thermal.js`.

At the hot end the *text* is blended toward white just enough to stay readable on
a dark card (`THERMAL_TEXT_FLOOR`, default `0.45`); the border, tint, glow and bar
keep the full-saturation deep red. Lower the floor for punchier numbers at the
cost of legibility.

### Wi-Fi setup portal

Credentials live in NVS and **nothing about the network is compiled into the
firmware**. There is no credentials header to copy and no password anywhere in
the source tree, so every board off the flasher behaves identically and the same
binary can be moved between sites.

A board with nothing stored goes straight to the setup AP. One with a saved
network tries it first; if it cannot join within `WIFI_CONNECT_TIMEOUT_MS`
(20 s) it raises an open AP named `P1-Setup-XXXX` (last two bytes of its MAC) at
`192.168.4.1`, with a DNS server that answers every lookup with its own address.
That is what makes a phone pop its **"Sign in to network"** sheet on its own and
land on `/wifi` — pick a network, type the password, save. The board reboots
into it, and comes back to the setup AP by itself if the password was wrong.

The station keeps retrying the saved network underneath the portal, and the
portal folds itself away if that retry lands — a router that was simply slower
to boot than the meter will not leave a cabinet-mounted board stuck in setup
mode until someone notices.

The page is reachable at `/wifi` any time the board is on the LAN, from the
**📶 Wi-Fi** button in the header of every page. **Forget saved network** erases
the credentials and restarts into setup mode.

**Sensors keep running the whole time the portal is up.** Nothing in the portal
blocks: the DNS is pumped from `loop()` and the web server runs on its own task,
so the BL0942 is still read once a second and the kWh total still accumulates
and persists — you just have nobody watching the SSE stream. This is also why
DS18B20 conversions are non-blocking (`setWaitForConversion(false)`, requested
one cycle and collected the next): a blocking 750 ms conversion would starve the
portal's DNS for three quarters of every second.

**Why the network list is scanned before the AP goes up.** A scan sweeps every
channel, which takes the radio away from the setup AP for seconds at a time and
drops the phone that is sitting on the page waiting for the list. So the scan
runs once at boot, before `softAP()` is called, and the portal opens with the
list already in hand.

Two things make a *manual* rescan work anyway, both of which cost real debugging
to find:

- `esp_wifi_scan_start()` **fails outright while the station is mid-connect**
  (`wifi:sta is connecting, return error`) — exactly the state a board is in
  right after someone mistypes a password, since it keeps retrying that network
  underneath the portal. `WiFi.disconnect()` alone does not fix it: the call
  returns before the driver has finished the handshake step it is on, so the
  scan a moment later is still refused. Dropping the station interface and
  putting it back (`enableSTA(false)` / `enableSTA(true)`) is what reliably ends
  a connect attempt. The AP is a separate interface and survives it.
- A refused scan leaves `scanComplete()` at `-2` forever, which is
  indistinguishable from "never ran". Without a flag recording the refusal, the
  page polls `scanning…` for eternity. It now reports the refusal instead.
- The AP name comes from `esp_read_mac(…, ESP_MAC_WIFI_SOFTAP)`, straight out of
  eFuse. `WiFi.softAPmacAddress()` returns all zeros until the AP interface
  exists — which is *after* the name is needed — so every board would come up
  calling itself `P1-Setup-0000`.

The page also keeps retrying a dropped poll rather than giving up: mid-sweep the
AP is off-channel, so a failed request there is the normal case, not an error.

> The same off-channel effect means a dashboard open on the LAN may drop one SSE
> beat while `/wifi` rescans. It reconnects on its own.

### LINE alerts

The board pushes a LINE message when a reading leaves its band. **LINE Notify is
gone** — the service shut down on 31 March 2025, so anything built on
`notify-api.line.me` no longer works. This talks to the **Messaging API**
instead: create a LINE Official Account, then paste its channel access token and
a destination ID (`U…` user, `C…` group, `R…` room) into the Alerts tab. Nothing
about your LINE account is compiled in — token, destination and every threshold
live in NVS.

Watched, each switchable on its own: temperature per slot, voltage (low *and*
high), frequency (low and high), current, active power.

Messages go out as a **Flex Message** — a card laid out entirely from JSON, which
is the only way to get something designed onto a phone from a board that has
nowhere to host an image. A gradient header carries the reading at display size,
a chip says how far past the limit it went, a meter bar shows it against that
limit, and the readings behind it follow as rows. Red-magenta for a fault,
teal-green for a recovery, so the two are told apart before a word is read.

`docs/flex_card_preview.json` is the same card as static JSON — paste it into the
[Flex Message Simulator](https://developers.line.biz/flex-simulator/) to see it,
or to try a different layout before changing `buildBubble()` in `alerts.cpp`.

**Quota is the binding constraint, not flash.** A LINE Official Account on the
free plan sends only a few hundred messages a month and pushes count against it,
so a reading hovering on a threshold could spend a month's allowance in an
afternoon. Four things prevent that:

| Guard | Effect | Where |
|---|---|---|
| Debounce | a reading must be out of range for `ALERT_CONFIRM_SAMPLES` reads (~5 s) before anything fires | `config.h` |
| Hysteresis | it must come back a margin *inside* the limit before that alert can fire again | `config.h` |
| Cooldown | minimum minutes between two messages from the same alert | Alerts tab |
| Daily cap | a hard stop per calendar day | Alerts tab |

Temperatures that cross together are reported as **one** message — nine probes on
one heatsink tend to go over at the same moment, and nine separate pushes for one
event is exactly how the quota disappears.

Two more things worth knowing:

- **A failed meter read holds the electrical alerts still.** A BL0942 that will
  not answer reports zeros, which is indistinguishable from a total power
  failure. The board knows the reading is *missing*, not that the voltage is
  gone, so it says nothing rather than crying wolf.
- **The TLS handshake runs on its own FreeRTOS task.** It blocks for a second or
  more; on the loop task that would stall the SSE push and the portal DNS, and in
  a request handler it would stall every open browser. `evaluate()` only queues
  text.

> The connection to `api.line.me` is TLS but the certificate is **not verified**
> (`setInsecure()`). Traffic is encrypted, but a machine positioned to intercept
> it could impersonate LINE and collect the token. Pin a root CA in
> `alerts.cpp` if the board sits somewhere that matters.

Alerts carry a wall-clock time, so the firmware runs an SNTP client
(`NTP_SERVER_1`, `NTP_TZ` in `config.h`). Before the first sync a message falls
back to `uptime 3h12m` rather than claiming a time it does not know.

### Firmware updates (OTA)

The Firmware tab takes a `.bin` from the browser and reflashes the board. Upload
**`firmware.bin`** and nothing else:

```
.pio/build/web_dashboard/
  firmware.bin      ← this one
  bootloader.bin    USB only, 0x1000
  partitions.bin    USB only, 0x8000
```

`bootloader.bin` and `partitions.bin` live outside the app slots and OTA cannot
write there. Uploading one would be a way to brick a board, so the handler checks
the ESP32 magic byte (`0xE9`) on the first chunk and refuses anything else before
erasing a single byte.

The image goes into whichever slot is **not** running. A failed upload, a dropped
Wi-Fi link or a power cut halfway through leaves the working firmware untouched
and the board still boots — the bootloader only switches slots once a complete,
verified image has been written. The reboot itself is deferred to `loop()`,
because restarting inside the request handler would drop the socket before the
browser learns the upload worked.

An **upload password** is available and off by default. Every other setting on
that page is a threshold or a credential; this one is arbitrary code execution,
so it is the one worth locking on a shared network. It is sent as an `X-OTA-Key`
header and checked before the first byte is written.

> Changing `partitions_p1.csv` still needs a USB cable — see *Flash layout*.

### HTTP endpoints

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | dashboard page (redirects to `/wifi` in setup mode) |
| GET | `/settings` | settings page — Sensors / Calibration / Alerts / Wi-Fi tabs |
| GET | `/wifi` | the same page, opened on the Wi-Fi tab |
| GET | `/thermal.js` | shared colour-ramp helpers |
| GET | `/events` | Server-Sent Events stream of live samples |
| GET | `/api/calibration` | current `{kI, kV, kP}` multipliers |
| POST | `/api/calibration` | set `{kI, kV, kP}`, persists to NVS |
| POST | `/api/energy/reset` | zero the accumulated kWh counter |
| GET | `/api/sensors` | `{version, fw, build, max, tmin, tmax, sensors:[{slot, addr, name, online}]}` |
| POST | `/api/sensors` | `{"sensors":[{"addr","name"}]}` in slot order, persists to NVS |
| POST | `/api/sensors/rescan` | re-run the OneWire scan and append new sensors |
| GET | `/api/alerts` | thresholds + `{tokenSet, sentToday, clockOk, now, lastCode}` — **never the token itself** |
| POST | `/api/alerts` | set thresholds; `token` is only written when non-empty, so a blank field keeps the saved one |
| POST | `/api/alerts/test` | queue a test LINE message |
| GET | `/api/ota` | `{fw, build, running, target, targetSize, sketch, keySet}` |
| POST | `/api/ota` | multipart `firmware.bin`; `X-OTA-Key` header when a password is set |
| POST | `/api/ota/key` | `{"key":"…"}`, empty clears it |
| GET | `/api/wifi` | `{portal, connected, ssid, ip, rssi, host, ap, saved, fw, build}` |
| POST | `/api/wifi` | `{"ssid","pass"}`, persists to NVS and reboots |
| GET | `/api/wifi/scan` | last scan result, or `{"scanning":true}`; `?force=1` restarts it |
| POST | `/api/wifi/forget` | erase credentials and reboot into the setup portal |

The `/events` payload carries a `cfg` counter that increments whenever the sensor
map changes. Open pages watch it and refetch their labels, so editing names on a
phone updates a dashboard already open on a desktop — no reload.

---

## Calibration

The BL0942 driver converts raw register counts using the datasheet formulas
(`I_RMS`, `V_RMS`, `WATT`, sections 2.2 / 2.5) combined with this board's CT
ratio and burden resistors. That is a **reasonable starting point, not accuracy**:
CT and VT tolerances, burden resistor tolerance, and the BL0942's actual on-chip
reference (nominally 1.218 V, not laser-trimmed) all shift the real conversion
factor per physical board. Every practical implementation of this class of chip
calibrates per unit.

1. Flash either example and run a known, stable load — a resistive heater or
   incandescent lamp. Avoid switch-mode loads for this step.
2. Compare the displayed Voltage / Current / Power against a trusted meter on the
   same load.
3. Compute `k = true_value / displayed_value` for each.
4. **Web dashboard:** enter the three values in the *BL0942 Calibration* panel
   and Save — persists to NVS, no reflash.
   **Serial example:** call `meter.setCalibration(kI, kV, kP)` after
   `meter.configure()`.

Frequency needs no calibration: `f = 1000000 / FREQ` is exact per the datasheet,
independent of the analog front end.

---

## Configuration reference

All of `include/config.h`, shared by both environments:

| Setting | Default | Meaning |
|---|---|---|
| `FIRMWARE_VERSION` | `"1.0.0"` | bump per release; shown in the boot banner and on every page |
| `FIRMWARE_BUILD` | `__DATE__ " " __TIME__` | compile timestamp, so two builds of one version are distinguishable |
| `BL0942_RX_PIN` / `BL0942_TX_PIN` | 16 / 17 | UART2 pins to the metering IC |
| `BL0942_BAUD` | 4800 | must match the SCLK_BPS strap — measured, not assumed |
| `BL0942_ADDRESS` | 3 | from the A1 / A2_NCS straps |
| `BL0942_AC_FREQ_60HZ` | `false` | `true` for 60 Hz mains |
| `BL0942_DEBUG` | `true` | dump raw bytes + checksum on a failed read |
| `ONEWIRE_PIN` | 4 | DS18B20 shared data line |
| `DS18B20_COUNT` | 9 | max sensors; also the number of dashboard slots |
| `DS18B20_RESOLUTION` | 12 | bits (9–12); 12 = 0.0625 °C steps, 750 ms conversion |
| `STATUS_LED_PIN` | 2 | status LED |
| `STATUS_LED_ACTIVE_HIGH` | `true` | invert for boards that sink the LED |
| `SENSOR_READ_INTERVAL_MS` | 1000 | sampling / SSE push period |
| `ALERT_CONFIRM_SAMPLES` | 5 | consecutive out-of-range reads before an alert fires |
| `ALERT_CLEAR_SAMPLES` | 5 | consecutive in-range reads before it clears |
| `ALERT_HYST_*` | 2 °C / 5 V / 0.2 Hz / 0.5 A / 100 W | how far back inside the band a value must come before that alert can fire again |
| `ALERT_MIN_FREE_HEAP` | 60000 | skip the push below this — a TLS handshake needs room |
| `ALERT_TASK_STACK` | 8192 | sender task stack; mbedTLS handshakes are stack-hungry |
| `ALERT_DEF_*` | 60 °C, 200–250 V, 49–51 Hz, 20 A, 4000 W | first-boot thresholds; editable at `/settings` afterwards |
| `NTP_SERVER_1` / `NTP_SERVER_2` | pool.ntp.org / time.google.com | clock for alert timestamps and the daily cap |
| `NTP_TZ` | `"ICT-7"` | POSIX TZ — Thailand, UTC+7, no DST (sign is inverted) |
| `TEMP_COLOR_MIN_C` | 10 | coldest end of the thermal ramp |
| `TEMP_COLOR_MAX_C` | 80 | hottest end (deep red at or above) |
| `DEVICE_HOSTNAME` | `"esp32-powermeter"` | mDNS name, `http://esp32-powermeter.local/` |
| `WIFI_CONNECT_TIMEOUT_MS` | 20000 | give up on the saved network and open the portal |
| `WIFI_PORTAL_AP_PREFIX` | `"P1-Setup"` | setup AP name; gets `-<last 2 MAC bytes>` |
| `WIFI_PORTAL_AP_PASSWORD` | `""` | under 8 characters means an open network |

Wi-Fi credentials are not in this table, or anywhere else in the source: they
are entered at `/wifi` and stored in NVS. The same goes for the LINE channel
access token and destination — `/settings` writes both, and the API never reads
them back out.

### Flash layout

The board carries a 4 MB chip. Arduino's stock table hands the application 1280K
of it and parks **1408K in a SPIFFS partition this firmware never opens** — the
web pages are compiled into the binary as PROGMEM strings and every setting lives
in NVS. A third of the chip did nothing while the app slot sat at 95% full, which
is why `partitions_p1.csv` replaces it:

| | stock `default.csv` | `partitions_p1.csv` |
|---|---|---|
| app0 / app1 | 1280K each | **1856K each** |
| filesystem | 1408K SPIFFS, unused | 256K LittleFS |
| app slot used by `web_dashboard` | 95.1% | **65.6%** |
| headroom | 64 KB | **654 KB** |

Two app slots are kept, so OTA still works — the running image is never the one
being overwritten. The 256K is for telemetry that could not be delivered: once
MQTT is pushing to ThingsBoard, a dropped link would otherwise be a hole in the
history, and ThingsBoard accepts records with their original timestamps, so a
backfill lands in the right place on the graph.

**`nvs` keeps its offset and its size**, which is what makes this safe to apply
to a board already in service: calibration, the kWh total, the sensor slot map,
Wi-Fi credentials and the LINE settings all survive. Move `nvs` by one byte and
every one of them is gone.

> A table change cannot be delivered over OTA — OTA writes app slots, not the
> table at `0x8000`. The first flash after this change has to go over USB
> (`pio run -e web_dashboard -t upload`). OTA works normally from then on.

To read the table off a board rather than trusting the source:

```
python esptool.py --port COM12 read_flash 0x8000 0xc00 parts.bin
python gen_esp32part.py parts.bin
```

### What is stored in flash (NVS)

| Namespace | Keys | Written when |
|---|---|---|
| `meter` | `kI`, `kV`, `kP`, `kwh` | on calibration save, energy reset, and every 60 s |
| `sensors` | `map` (JSON: addresses + names in slot order) | on save from `/settings` |
| `wifi` | `ssid`, `pass` | on save or forget from `/wifi` |
| `alerts` | thresholds, `to`, `token` | on save from the Alerts tab |
| `ota` | `key` | on save from the Firmware tab |

`pio run -t upload` does **not** touch NVS — it only rewrites the app partition,
so calibration, sensor names and the Wi-Fi network all survive reflashing. You
therefore run the setup portal once per board, not once per build.
`pio run -t erase` wipes all three namespaces along with the firmware, and the
board comes back up as `P1-Setup-XXXX`.

---

## Status LED (GPIO2)

| Pattern | Meaning |
|---|---|
| Fast blink (5 Hz) | Booting / connecting to Wi-Fi |
| Slow blink (1 Hz) | Serial example: normal heartbeat. Dashboard: **setup portal is up, waiting for Wi-Fi credentials** |
| Solid on | Healthy — Wi-Fi connected (dashboard example) |
| Double-blink burst | BL0942 read failure — check UART wiring |

In the dashboard example a BL0942 fault outranks the Wi-Fi state: a board that
is on the network but not reading is the more urgent of the two problems.

---

## Troubleshooting BL0942 reads

`BL0942_DEBUG` is `true` by default, so a failed read prints diagnostics: raw
bytes on a checksum mismatch, or a timeout with the byte count actually received.
Set it to `false` once things work, to keep the log quiet.

**Timeout, 0 bytes received**

- TX/RX swapped — BL0942 TX/SDO → GPIO16, BL0942 RX/SDI → GPIO17
- SEL not actually LOW (that selects SPI instead of UART)
- GPIO16/17 reserved for PSRAM — **WROVER modules use those two pins
  internally**; use a WROOM module or move the UART
- GND not shared between the ESP32 and the meter board
- BL0942 VDD not actually at 3.3 V
- Wrong `BL0942_BAUD` or `BL0942_ADDRESS` — measure the straps

**Checksum mismatch every time, full 23 bytes received**

Re-check any local changes to the checksum against datasheet section 3.2.6:

```
checksum = ~(readHead + 0x55 + data[1..21]) & 0xFF
```

The `0xAA` sent as the second command byte only selects packet-read mode — it is
*not* part of the checksum sum.

**No DS18B20 found, or fewer than expected**

Check the 4.7 kΩ pull-up from GPIO4 to 3V3, then power and ground at each probe.
Long parasitic-power runs are unreliable — prefer 3-wire.

---

## Project layout

```
platformio.ini              two environments: serial_monitor, web_dashboard
partitions_p1.csv           4MB layout: 1856K app slots + 256K LittleFS
include/
  config.h                  pin map + tunables shared by both examples
lib/
  BL0942/                   UART driver for the metering IC (protocol + calibration)
  StatusLED/                non-blocking GPIO2 status LED patterns
src/
  01_serial_monitor/
    main.cpp                Example 1
  02_web_dashboard/
    main.cpp                Example 2: sensor slot map, routes, SSE push
    wifi_portal.h/.cpp      NVS credentials + captive-portal fallback (API only)
    alerts.h/.cpp           threshold engine + LINE Messaging API sender task
    ota.h/.cpp              browser firmware upload into the spare app slot
    dashboard_html.h        the read-only dashboard page
    settings_html.h         /settings and /wifi -- sensors, calibration, alerts, Wi-Fi
    thermal_js.h            shared thermal colour ramp, served at /thermal.js
```

The three `*_html.h` files hold complete pages as `PROGMEM` string literals. That
keeps the firmware a single binary with no filesystem image to flash separately,
at the cost of a reflash to change the UI.

---

## License

MIT.
