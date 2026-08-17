#!/usr/bin/env python3
"""Build a standalone demo copy of the dashboard, for showing the product where
there is no board and no sensors to point at.

WHY THIS IS GENERATED RATHER THAN HAND-WRITTEN. A mockup drawn separately starts
out looking like the product and stops looking like it within a fortnight -- the
real page keeps changing and the copy does not. This one is assembled from the
firmware's own dashboard_html.h and thermal_js.h, so the markup, the CSS, the
gauge, the serpentine layout and the colour ramp are not "the same as" the
product: they ARE the product. Only the data source is swapped.

The swap is a shim injected ahead of the page's own script. It replaces the two
things that would otherwise reach for a board -- EventSource('/events') and
fetch('/api/...') -- and nothing else, so the page cannot tell the difference.

    python tools/make_mockup.py

Writes docs/dashboard-mockup.html: one file, no server, no network. Open it and
it runs. Re-run this whenever the dashboard changes.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "02_web_dashboard"
OUT = ROOT / "docs" / "dashboard-mockup.html"


def extract(path: Path, var: str, delim: str) -> str:
    """Pull a raw string literal out of a header.

    Anchored on the assignment, not on the delimiter: the explanatory comments at
    the top of these headers quote the delimiter too, and a lazy regex would
    happily return a paragraph of English about flash usage.
    """
    text = path.read_text(encoding="utf-8")
    start_marker = f'{var}[] PROGMEM = R"{delim}('
    start = text.find(start_marker)
    if start < 0:
        sys.exit(f"could not find {var} in {path.name}")
    start += len(start_marker)
    end = text.rfind(f'){delim}"')
    if end < start:
        sys.exit(f"could not find the end of {var} in {path.name}")
    return text[start:end]


# The simulator. Nine probes with a hot spot in the middle of the array, a load
# that wanders and occasionally steps, and mains figures that drift the way real
# ones do rather than sitting on a constant. Edit SIM_NAMES and SIM_BASE to match
# whatever machine is being demonstrated.
SHIM = r"""
<script>
// ---------------------------------------------------------------------------
// DEMO DATA SOURCE -- replaces the board. Nothing below this point exists in
// the firmware; everything above and after it is the real dashboard, byte for
// byte, so what is on screen is what a customer gets.
// ---------------------------------------------------------------------------
(function () {
  // Tailor these to the site being shown. Names appear on the cards and in the
  // thermal map tooltip; bases are the resting temperature of each probe, laid
  // out in slot order (the page arranges them serpentine on its own).
  var SIM_NAMES = ['หม้อแปลง A', 'หม้อแปลง B', 'บัสบาร์ L1',
                   'เบรกเกอร์หลัก', 'จุดร้อน C', 'บัสบาร์ L2',
                   'มอเตอร์ 1', 'มอเตอร์ 2', 'ตู้ควบคุม'];
  var SIM_BASE  = [28, 31, 29,
                   34, 41, 36,
                   30, 33, 29];
  var TMIN = 20, TMAX = 50;      // must match TEMP_COLOR_MIN_C / MAX_C
  var NOMINAL_AMPS = 12.5;

  var tick = 0;
  var load = 0.55;               // fraction of nominal current
  var energy = 128.4;            // kWh on the counter when the demo starts

  function reading() {
    tick++;
    // A step every now and then, plus a slow wander in between: a flat line
    // makes the sparkline and the gauge look broken rather than idle.
    if (Math.random() < 0.02) load = 0.25 + Math.random() * 0.6;
    load += (Math.random() - 0.5) * 0.02;
    load = Math.max(0.05, Math.min(1, load));

    var v = 229.5 + Math.sin(tick / 37) * 1.6 + (Math.random() - 0.5) * 0.4;
    var i = NOMINAL_AMPS * load + (Math.random() - 0.5) * 0.05;
    var p = v * i * 0.93;
    var f = 50.0 + Math.sin(tick / 91) * 0.03 + (Math.random() - 0.5) * 0.01;
    energy += p / 3600 / 1000;

    // Probes track the load with a lag, so the thermal map warms up after a
    // step instead of jumping with it -- which is what metal actually does.
    var temps = SIM_BASE.map(function (base, n) {
      return base + load * 14
                  + Math.sin((tick + n * 40) / 120) * 1.2
                  + (Math.random() - 0.5) * 0.15;
    });

    return {
      v: v, i: i, p: p, f: f, e: energy,
      noLoad: p < 8,
      uptime: 4520000 + tick * 1000,
      heap: 54000 + Math.round(Math.sin(tick / 15) * 900),
      cfg: 1,
      temps: temps
    };
  }

  // ---- fake /api/* ---------------------------------------------------------
  function json(obj) {
    return Promise.resolve({ ok: true, json: function () { return Promise.resolve(obj); } });
  }
  window.fetch = function (url) {
    url = String(url);
    if (url.indexOf('/api/sensors') === 0) {
      return json({
        version: 1, max: 9, tmin: TMIN, tmax: TMAX,
        fw: 'DEMO', build: 'simulated data',
        sensors: SIM_NAMES.map(function (n, k) {
          return { slot: k, addr: '28DEMO0000000' + k, name: n, online: true };
        })
      });
    }
    if (url.indexOf('/api/calibration') === 0) return json({ kI: 1.0, kV: 1.0, kP: 1.0 });
    return json({ ok: true });
  };

  // ---- fake /events --------------------------------------------------------
  window.EventSource = function () {
    var self = this;
    var handlers = [];
    this.addEventListener = function (type, fn) { if (type === 'data') handlers.push(fn); };
    this.close = function () { clearInterval(self._timer); };
    function push() {
      var payload = JSON.stringify(reading());
      handlers.forEach(function (fn) { fn({ data: payload }); });
    }
    // A beat before "live" appears, so the connecting state is visible rather
    // than flashing past -- it is part of what the product does.
    setTimeout(function () {
      if (self.onopen) self.onopen();
      push();
      self._timer = setInterval(push, 1000);
    }, 500);
  };
})();
</script>
<style>
/* Says what this is, in the one place nobody can crop out by accident. Leave it
   on: a screenshot of this page is otherwise indistinguishable from a reading
   off a real installation. */
.demo-flag{display:inline-flex;align-items:center;gap:6px;background:#f2b84b1f;
  border:1px solid #f2b84b;color:#f2b84b;padding:5px 11px;border-radius:999px;
  font-size:.72rem;font-weight:700;letter-spacing:.6px;white-space:nowrap}
</style>
"""

DEMO_BADGE = '<span class="demo-flag">DEMO &middot; ข้อมูลจำลอง</span>\n    '


def main() -> None:
    page = extract(SRC / "dashboard_html.h", "DASHBOARD_HTML", "HTMLPAGE")
    thermal = extract(SRC / "thermal_js.h", "THERMAL_JS", "JSFILE")

    # /thermal.js is served by the firmware; inline it so the file stands alone.
    tag = '<script src="/thermal.js"></script>'
    if tag not in page:
        sys.exit("the dashboard no longer pulls in /thermal.js -- update this script")
    page = page.replace(tag, "<script>\n" + thermal + "\n</script>")

    # The shim has to be parsed before the page's own script runs, and the badge
    # belongs next to the live indicator where the eye already goes.
    anchor = '<span class="status">'
    if anchor not in page:
        sys.exit("the dashboard header changed -- update the badge anchor")
    page = page.replace(anchor, DEMO_BADGE + anchor, 1)
    page = page.replace("</head>", SHIM + "</head>", 1)

    # Nav links point at pages this single file does not contain.
    page = re.sub(r'href="/(settings|wifi)"', 'href="#"', page)
    page = page.replace("<title>", "<title>DEMO &middot; ", 1)

    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(page, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}  ({len(page.encode('utf-8')):,} bytes)")


if __name__ == "__main__":
    main()
