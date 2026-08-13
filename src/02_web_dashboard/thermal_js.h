#pragma once
// Shared thermal colour ramp, served at GET /thermal.js and pulled in by both
// dashboard_html.h and settings_html.h. It lives in its own file rather than
// being pasted into each page so the palette has exactly one definition -- the
// two pages sit side by side, and a ramp that drifts between them is worse than
// no colour at all.

static const char THERMAL_JS[] PROGMEM = R"JSFILE(
// Cold -> hot, the way a thermal camera reads: brightness and "heat" both climb
// monotonically, and the ramp deliberately skips green. Green on a gauge reads
// as "all good" no matter what number sits next to it, which is precisely the
// wrong signal at 45 degrees on a bearing.
var THERMAL_STOPS = [
  [0.00,  47,  75, 143],   // deep blue
  [0.20,  47, 143, 214],   // blue
  [0.38,  79, 217, 208],   // cyan
  [0.55, 255, 216,  77],   // yellow
  [0.72, 255, 147,  38],   // orange
  [0.87, 240,  82,  28],   // red-orange
  [1.00, 192,  20,  20]    // deep red
];

// Legibility floor for text drawn in a ramp colour. Deep red is the whole point
// of the hot end but it is unreadable on a dark card, so text gets blended
// toward white until it clears this. Lower it for punchier numbers, at a cost.
var THERMAL_TEXT_FLOOR = 0.45;

function thermalClamp(t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

// Linear interpolation between the surrounding stops. Returns [r,g,b] floats.
function thermalRamp(t) {
  t = thermalClamp(t);
  for (var i = 1; i < THERMAL_STOPS.length; i++) {
    var a = THERMAL_STOPS[i - 1], b = THERMAL_STOPS[i];
    if (t <= b[0]) {
      var f = (t - a[0]) / (b[0] - a[0]);
      return [a[1] + (b[1] - a[1]) * f,
              a[2] + (b[2] - a[2]) * f,
              a[3] + (b[3] - a[3]) * f];
    }
  }
  var last = THERMAL_STOPS[THERMAL_STOPS.length - 1];
  return [last[1], last[2], last[3]];
}

function thermalLum(c) {
  return (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255;
}

// Relative luminance is linear in each channel, so the blend factor that lands
// exactly on the floor has a closed form -- no iterating toward it.
function thermalReadable(c, floor) {
  var L = thermalLum(c);
  if (L >= floor) return c;
  var f = (floor - L) / (1 - L);
  return [c[0] + (255 - c[0]) * f,
          c[1] + (255 - c[1]) * f,
          c[2] + (255 - c[2]) * f];
}

function thermalCss(c, alpha) {
  var r = c[0] | 0, g = c[1] | 0, b = c[2] | 0;
  return alpha === undefined ? 'rgb(' + r + ',' + g + ',' + b + ')'
                             : 'rgba(' + r + ',' + g + ',' + b + ',' + alpha + ')';
}

// CSS gradient built from the same stops, for the scale legend beside a chart.
// A semantic-heat ramp is only readable with a scale to read it against, and
// generating that scale from THERMAL_STOPS keeps it honest -- a hand-written
// gradient would quietly stop matching the lines the first time a stop moves.
function thermalGradientCss() {
  var parts = [];
  for (var i = 0; i < THERMAL_STOPS.length; i++) {
    var s = THERMAL_STOPS[i];
    parts.push(thermalCss([s[1], s[2], s[3]]) + ' ' + Math.round(s[0] * 100) + '%');
  }
  return 'linear-gradient(90deg,' + parts.join(',') + ')';
}

// Everything a card needs for one reading: t is the 0..1 position in the range.
function thermalPaint(t) {
  t = thermalClamp(t);
  var vivid = thermalRamp(t);
  return {
    t: t,
    vivid: thermalCss(vivid),
    text: thermalCss(thermalReadable(vivid, THERMAL_TEXT_FLOOR)),
    // Tint and border strengthen with heat; the glow only kicks in past the
    // yellow stop so a room-temperature grid stays calm instead of all aglow.
    tint: 'linear-gradient(180deg,' + thermalCss(vivid, (0.04 + 0.10 * t).toFixed(3)) +
          ',' + thermalCss(vivid, (0.10 + 0.20 * t).toFixed(3)) + ')',
    border: thermalCss(vivid, (0.30 + 0.45 * t).toFixed(3)),
    glow: t > 0.55 ? '0 0 ' + (((t - 0.55) * 42) | 0) + 'px ' +
                     thermalCss(vivid, (0.10 + 0.25 * t).toFixed(3)) : 'none'
  };
}
)JSFILE";
