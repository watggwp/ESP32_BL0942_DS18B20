# Thermal Map — ThingsBoard widget

The heat picture from the board's own dashboard, drawn from telemetry instead:
nine DS18B20 probes on a serpentine 3×3, the field between them filled by
inverse-distance weighting, in the same palette the firmware serves at
`/thermal.js` — stop for stop, so a colour cannot come to mean two things.

> Only the nine circles are measured. Everything between them is **interpolated**
> — it can never invent a spot hotter than the hottest probe, and the further
> apart the probes sit, the rougher the guess in between.

## Install

**Import (fastest).** ThingsBoard → **Widgets Library** → **+** → *Import widget*
→ pick `thermal-map-widget.json` → put it in a bundle of yours.

**By hand,** if the import is refused (the export format moved between 3.4 and
3.6, and this file is written for current ThingsBoard):

1. Widgets Library → your bundle → **+ Create new widget** → type **Latest values**
2. Paste each file into the tab of the same name:
   | Tab | File |
   |---|---|
   | HTML | `template.html` |
   | CSS | `template.css` |
   | JavaScript | `controller.js` |
   | Settings schema | `settings-schema.json` |
3. Save.

## Put it on a dashboard

Add the widget, point its datasource at the device, and add the keys
**`temp1` … `temp9`** as *timeseries*. The board publishes all nine on every
telemetry message and sends `null` for a slot with no probe fitted, so every key
exists from the first message — a slot with nothing in it draws as an empty ring
rather than vanishing.

Which slot a key lands in comes from **the number ending its name** (`temp7` →
slot 7), not from the order the keys were added, so the picture is right however
the datasource was built. Rename the keys' *labels* to whatever the probes are
called on the machine; the labels are what the tooltip shows.

## Settings

| Setting | Default | |
|---|---|---|
| Scale minimum / maximum | 10 / 80 °C | the ends of the ramp — match `TEMP_COLOR_MIN_C` / `TEMP_COLOR_MAX_C` in `include/config.h` |
| Columns / Rows | 3 / 3 | any grid; 9 keys in a 3×3 is what this board is |
| Serpentine | on | every second row runs right to left, the way the cable does |
| Slot from key number | on | off = use datasource order instead |
| Colour scale, slot numbers, tooltip | on | |
| Decimals | 1 | on the circles; the tooltip always shows two |

## Editing

`template.html`, `template.css`, `controller.js` and `settings-schema.json` are
the source. After changing any of them, repack:

```
python tools/thingsboard/thermal-map/build_widget.py
```

which rewrites `thermal-map-widget.json`. Editing that file directly means
editing JS inside a JSON string — it works right up until it doesn't.
