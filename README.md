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
3. For the web dashboard only — create your Wi-Fi credentials file:
   ```sh
   cp include/secrets.h.example include/secrets.h
   ```
   Fill in `WIFI_SSID` / `WIFI_PASSWORD`. `include/secrets.h` is gitignored, so
   your password never gets committed.
4. Build, upload and watch:
   ```sh
   pio run -e serial_monitor -t upload -t monitor
   # or
   pio run -e web_dashboard  -t upload -t monitor
   ```

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
sane. `include/secrets.h` is not needed for this environment.

---

## Example 2 — Web dashboard

`pio run -e web_dashboard -t upload -t monitor`

Everything from Example 1 plus an ESPAsyncWebServer dashboard at
`http://<device-ip>/` or `http://esp32-powermeter.local/` (mDNS).

- Animated arc gauge for active power, rolling ~5-minute power sparkline
- Voltage / current / frequency / apparent VA / free heap / uptime tiles
- Accumulated energy (kWh), integrated in software from active power, with a
  reset button — persists across reboots in NVS
- **3×3 temperature grid** with a thermal-camera colour ramp (below)
- **Sensor setup page** at `/settings` — fixed slots and names (below)
- Calibration panel to tune BL0942 readings against a real meter, persists to NVS
- Updates pushed once a second over Server-Sent Events (`GET /events`)

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

### HTTP endpoints

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | dashboard page |
| GET | `/settings` | sensor setup page |
| GET | `/thermal.js` | shared colour-ramp helpers |
| GET | `/events` | Server-Sent Events stream of live samples |
| GET | `/api/calibration` | current `{kI, kV, kP}` multipliers |
| POST | `/api/calibration` | set `{kI, kV, kP}`, persists to NVS |
| POST | `/api/energy/reset` | zero the accumulated kWh counter |
| GET | `/api/sensors` | `{version, max, tmin, tmax, sensors:[{slot, addr, name, online}]}` |
| POST | `/api/sensors` | `{"sensors":[{"addr","name"}]}` in slot order, persists to NVS |
| POST | `/api/sensors/rescan` | re-run the OneWire scan and append new sensors |

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
| `TEMP_COLOR_MIN_C` | 10 | coldest end of the thermal ramp |
| `TEMP_COLOR_MAX_C` | 80 | hottest end (deep red at or above) |

Wi-Fi credentials and the mDNS hostname live in `include/secrets.h` — copy
`include/secrets.h.example` and edit. That file is gitignored.

### What is stored in flash (NVS)

| Namespace | Keys | Written when |
|---|---|---|
| `meter` | `kI`, `kV`, `kP`, `kwh` | on calibration save, energy reset, and every 60 s |
| `sensors` | `map` (JSON: addresses + names in slot order) | on save from `/settings` |

`pio run -t erase` wipes both, along with the firmware.

---

## Status LED (GPIO2)

| Pattern | Meaning |
|---|---|
| Fast blink (5 Hz) | Booting / connecting to Wi-Fi |
| Slow blink (1 Hz) | Normal operation heartbeat (Serial example) |
| Solid on | Healthy — Wi-Fi connected (dashboard example) |
| Double-blink burst | BL0942 read failure — check UART wiring |

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
include/
  config.h                  pin map + tunables shared by both examples
  secrets.h.example          copy to secrets.h and fill in your Wi-Fi (gitignored)
lib/
  BL0942/                   UART driver for the metering IC (protocol + calibration)
  StatusLED/                non-blocking GPIO2 status LED patterns
src/
  01_serial_monitor/
    main.cpp                Example 1
  02_web_dashboard/
    main.cpp                Example 2: sensor slot map, routes, SSE push
    dashboard_html.h        the dashboard page
    settings_html.h         the /settings sensor setup page
    thermal_js.h            shared thermal colour ramp, served at /thermal.js
```

The two `*_html.h` files hold complete pages as `PROGMEM` string literals. That
keeps the firmware a single binary with no filesystem image to flash separately,
at the cost of a reflash to change the UI.

---

## License

MIT.
