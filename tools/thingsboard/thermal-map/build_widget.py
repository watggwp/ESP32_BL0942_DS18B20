#!/usr/bin/env python3
"""Pack template.html / template.css / controller.js / settings-schema.json into
thermal-map-widget.json, the file ThingsBoard's "Import widget" accepts.

The four source files are the ones to edit -- an editor understands them, and a
diff of them is readable. This script only escapes them into the one JSON string
field each that ThingsBoard stores them in. Re-run it after every edit:

    python tools/thingsboard/thermal-map/build_widget.py
"""

import json
import pathlib

HERE = pathlib.Path(__file__).parent
OUT = HERE / "thermal-map-widget.json"

SLOTS = 9
# Distinct enough to tell apart in the datasource editor; the widget itself never
# uses a key's colour, it draws every circle from the thermal ramp.
KEY_COLORS = ["#2196f3", "#4caf50", "#f44336", "#ff9800", "#9c27b0",
              "#00bcd4", "#795548", "#607d8b", "#e91e63"]


def data_keys():
    keys = []
    for i in range(SLOTS):
        keys.append({
            "name": "temp%d" % (i + 1),
            "type": "timeseries",
            "label": "Sensor %d" % (i + 1),
            "color": KEY_COLORS[i % len(KEY_COLORS)],
            "settings": {},
            "_hash": round(0.1 + i / 100.0, 4),
            "aggregationType": None,
            "units": None,
            "decimals": None,
            "funcBody": None,
            "usePostProcessing": None,
            "postFuncBody": None,
        })
    return keys


def main():
    html = (HERE / "template.html").read_text(encoding="utf-8")
    css = (HERE / "template.css").read_text(encoding="utf-8")
    js = (HERE / "controller.js").read_text(encoding="utf-8")
    schema = json.loads((HERE / "settings-schema.json").read_text(encoding="utf-8"))

    default_config = {
        "datasources": [{
            "type": "entity",
            "name": None,
            "entityAliasId": None,
            "filterId": None,
            "dataKeys": data_keys(),
        }],
        "timewindow": {"realtime": {"timewindowMs": 60000}},
        "showTitle": True,
        "backgroundColor": "rgb(255, 255, 255)",
        "color": "rgba(0, 0, 0, 0.87)",
        "padding": "8px",
        "settings": {
            "minTemp": 10,
            "maxTemp": 80,
            "cols": 3,
            "rows": 3,
            "serpentine": True,
            "slotFromKeyNumber": True,
            "showScale": True,
            "showSlotNumbers": True,
            "showTooltip": True,
            "decimals": 1,
        },
        "title": "Thermal Map",
        "dropShadow": True,
        "enableFullscreen": True,
        "widgetStyle": {},
        "titleStyle": {"fontSize": "16px", "fontWeight": 400},
    }

    widget = {
        # fqn is what current ThingsBoard reads; alias is the pre-3.6 name for
        # the same thing, and an unknown field is ignored either way.
        "fqn": "thermal_map",
        "alias": "thermal_map",
        "name": "Thermal Map",
        "deprecated": False,
        "image": None,
        "description": "Nine DS18B20 probes on a serpentine 3x3, with the field "
                       "between them interpolated. Same palette as the meter's own "
                       "web dashboard.",
        "descriptor": {
            "type": "latest",
            "sizeX": 7.5,
            "sizeY": 6,
            "resources": [],
            "templateHtml": html,
            "templateCss": css,
            "controllerScript": js,
            "settingsSchema": json.dumps(schema, ensure_ascii=False),
            "dataKeySettingsSchema": "{}\n",
            "defaultConfig": json.dumps(default_config, ensure_ascii=False),
        },
        "tags": ["temperature", "heatmap", "esp32"],
    }

    OUT.write_text(json.dumps(widget, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("wrote %s (%d bytes)" % (OUT.name, OUT.stat().st_size))


if __name__ == "__main__":
    main()
