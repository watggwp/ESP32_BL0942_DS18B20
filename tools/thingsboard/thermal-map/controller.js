/* Thermal Map -- a ThingsBoard "Latest values" widget.
 *
 * The same picture the board serves at / , drawn from telemetry instead: nine
 * probes on a 3x3 serpentine, the field between them filled by inverse-distance
 * weighting. The palette is the one in the firmware's thermal.js, stop for stop,
 * so the widget and the board's own page cannot drift apart in what a colour
 * means -- which is the only reason a heat colour is readable at all.
 *
 * DATA. One key per probe, temp1..temp9. Which slot a key lands in comes from
 * the number at the end of its name, not from the order the keys were added, so
 * temp7 is slot 7 whatever the datasource looks like. Turn that off in the
 * settings and the datasource order is used instead. A key that is missing, or
 * whose latest value is null, draws as an empty ring: the board publishes every
 * slot every time and sends null for a probe that is not fitted, and "asked,
 * nothing there" is worth showing as itself rather than as a gap.
 *
 * WHAT IS MEASURED AND WHAT IS NOT. Only the nine circles are readings.
 * Everything between them is interpolated -- it can never invent a spot hotter
 * than the hottest probe, and the further apart the probes sit the rougher the
 * guess in between.
 */

// Cold -> hot, the way a thermal camera reads: brightness and "heat" both climb
// monotonically, and the ramp deliberately skips green. Green on a gauge reads
// as "all good" no matter what number sits next to it, which is precisely the
// wrong signal at 45 degrees on a bearing.
var TM_STOPS = [
  [0.00,  47,  75, 143],   // deep blue
  [0.20,  47, 143, 214],   // blue
  [0.38,  79, 217, 208],   // cyan
  [0.55, 255, 216,  77],   // yellow
  [0.72, 255, 147,  38],   // orange
  [0.87, 240,  82,  28],   // red-orange
  [1.00, 192,  20,  20]    // deep red
];

var TM_GW = 44, TM_GH = 33;   // field computed coarse, then bilinearly upscaled

function tmClamp(t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

function tmRamp(t) {
  t = tmClamp(t);
  for (var i = 1; i < TM_STOPS.length; i++) {
    var a = TM_STOPS[i - 1], b = TM_STOPS[i];
    if (t <= b[0]) {
      var f = (t - a[0]) / (b[0] - a[0]);
      return [a[1] + (b[1] - a[1]) * f,
              a[2] + (b[2] - a[2]) * f,
              a[3] + (b[3] - a[3]) * f];
    }
  }
  var last = TM_STOPS[TM_STOPS.length - 1];
  return [last[1], last[2], last[3]];
}

function tmLum(c) { return (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255; }

function tmCss(c, alpha) {
  var r = c[0] | 0, g = c[1] | 0, b = c[2] | 0;
  return alpha === undefined ? 'rgb(' + r + ',' + g + ',' + b + ')'
                             : 'rgba(' + r + ',' + g + ',' + b + ',' + alpha + ')';
}

function tmGradientCss() {
  var parts = [];
  for (var i = 0; i < TM_STOPS.length; i++) {
    var s = TM_STOPS[i];
    parts.push(tmCss([s[1], s[2], s[3]]) + ' ' + Math.round(s[0] * 100) + '%');
  }
  return 'linear-gradient(90deg,' + parts.join(',') + ')';
}

// Serpentine: row 1 runs left-to-right, row 2 right-to-left, and so on, because
// that is how the cable runs on the machine. Its own inverse, so the one
// function maps slot -> position and position -> slot.
function tmSerp(i, cols, serpentine) {
  if (!serpentine) return i;
  var row = Math.floor(i / cols);
  var col = i % cols;
  return row * cols + (row % 2 ? cols - 1 - col : col);
}

// The dashboard theme decides what is legible here, and a widget cannot know it
// in advance: the same board can be on a white card or a near-black one. Both
// colours are read back off the rendered element rather than guessed.
function tmTheme(el) {
  var ink = '#8a93a6', surface = '#ffffff';
  try {
    var cs = getComputedStyle(el);
    if (cs.color) ink = cs.color;
    var node = el;
    while (node) {
      var bg = getComputedStyle(node).backgroundColor;
      if (bg && bg !== 'transparent' && !/rgba\(0,\s*0,\s*0,\s*0\)/.test(bg)) { surface = bg; break; }
      node = node.parentElement;
    }
  } catch (e) { /* a detached element during teardown: the fallbacks are fine */ }
  return { ink: ink, surface: surface };
}

self.onInit = function () {
  var ctx = self.ctx;
  var root = ctx.$container[0];
  var s = ctx.settings || {};

  var st = {
    canvas:  root.querySelector('.tm-canvas'),
    tip:     root.querySelector('.tm-tip'),
    plot:    root.querySelector('.tm-plot'),
    scale:   root.querySelector('.tm-scale'),
    bar:     root.querySelector('.tm-scale-bar'),
    lo:      root.querySelector('.tm-scale-min'),
    hi:      root.querySelector('.tm-scale-max'),
    buf:     null,
    hover:   null,
    pts:     [],
    values:  [],
    labels:  [],
    cols:        Math.max(1, Math.round(s.cols || 3)),
    rows:        Math.max(1, Math.round(s.rows || 3)),
    tMin:        typeof s.minTemp === 'number' ? s.minTemp : 10,
    tMax:        typeof s.maxTemp === 'number' ? s.maxTemp : 80,
    serpentine:  s.serpentine !== false,
    bySuffix:    s.slotFromKeyNumber !== false,
    showNumbers: s.showSlotNumbers !== false,
    showTooltip: s.showTooltip !== false,
    decimals:    typeof s.decimals === 'number' ? s.decimals : 1
  };
  st.slots = st.cols * st.rows;
  ctx.tmState = st;

  if (s.showScale === false) {
    st.scale.style.display = 'none';
  } else {
    st.bar.style.background = tmGradientCss();
    st.lo.textContent = st.tMin + '°C';
    st.hi.textContent = st.tMax + '°C';
  }

  if (st.showTooltip) {
    st.onMove = function (e) {
      var rect = st.canvas.getBoundingClientRect();
      var x = e.clientX - rect.left, y = e.clientY - rect.top;
      var best = null, bestD = 1e9;
      for (var i = 0; i < st.pts.length; i++) {
        var p = st.pts[i];
        var dx = x - p.fx * rect.width, dy = y - p.fy * rect.height;
        var d = Math.sqrt(dx * dx + dy * dy);
        if (d < bestD) { bestD = d; best = p; }
      }
      // hit target generously bigger than the circle -- nobody lands dead-centre
      if (!best || bestD > 46 || best.v === null) {
        if (st.hover !== null) { st.hover = null; self.tmDraw(); }
        st.tip.classList.remove('tm-show');
        return;
      }
      if (st.hover !== best.i) { st.hover = best.i; self.tmDraw(); }

      var span = (st.tMax - st.tMin) || 1;
      st.tip.innerHTML = '';
      var head = document.createElement('div');
      head.className = 'tm-tth';
      head.textContent = 'slot ' + (best.i + 1);
      var row = document.createElement('div'); row.className = 'tm-ttr';
      var key = document.createElement('i'); key.className = 'tm-ttk';
      key.style.background = tmCss(tmRamp((best.v - st.tMin) / span));
      var val = document.createElement('span'); val.className = 'tm-ttv';
      val.textContent = best.v.toFixed(2) + '°C';
      var nm = document.createElement('span'); nm.className = 'tm-ttn';
      nm.textContent = st.labels[best.i] || ('Sensor ' + (best.i + 1));
      row.appendChild(key); row.appendChild(val); row.appendChild(nm);
      st.tip.appendChild(head); st.tip.appendChild(row);
      st.tip.classList.add('tm-show');

      var left = x + 16, top = y - st.tip.offsetHeight - 12;
      if (left + st.tip.offsetWidth > rect.width) left = x - st.tip.offsetWidth - 16;
      if (left < 0) left = 0;
      if (top < 0) top = y + 16;
      st.tip.style.left = left + 'px';
      st.tip.style.top = top + 'px';
    };
    st.onLeave = function () {
      st.hover = null;
      st.tip.classList.remove('tm-show');
      self.tmDraw();
    };
    st.canvas.addEventListener('pointermove', st.onMove);
    st.canvas.addEventListener('pointerleave', st.onLeave);
  }

  self.onDataUpdated();
};

// One latest value per slot, plus the label to call it by. A key with no data
// yet, or a null latest value, stays null -- see the header.
self.tmRead = function () {
  var ctx = self.ctx, st = ctx.tmState;
  var values = new Array(st.slots).fill(null);
  var labels = new Array(st.slots).fill(null);
  var data = ctx.data || [];
  for (var i = 0; i < data.length; i++) {
    var dk = data[i].dataKey || {};
    var idx = i;
    if (st.bySuffix) {
      var m = /(\d+)\s*$/.exec(dk.name || '');
      if (m) idx = parseInt(m[1], 10) - 1;
    }
    if (idx < 0 || idx >= st.slots) continue;
    labels[idx] = dk.label || dk.name || null;
    var rows = data[i].data;
    if (!rows || !rows.length) continue;
    var raw = rows[rows.length - 1][1];
    if (raw === null || raw === undefined || raw === '') continue;
    var v = Number(raw);
    if (!isNaN(v)) values[idx] = v;
  }
  st.values = values;
  st.labels = labels;
};

self.tmDraw = function () {
  var ctx = self.ctx, st = ctx.tmState;
  if (!st || !st.canvas) return;

  var cssW = st.canvas.clientWidth;
  var cssH = st.canvas.clientHeight;
  if (!cssW || !cssH) return;

  var theme = tmTheme(st.canvas);
  var dpr = window.devicePixelRatio || 1;
  st.canvas.width = Math.round(cssW * dpr);
  st.canvas.height = Math.round(cssH * dpr);
  var g = st.canvas.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, cssW, cssH);

  var cells = [];
  for (var i = 0; i < st.slots; i++) {
    var pos = tmSerp(i, st.cols, st.serpentine);
    var v = st.values[i];
    cells.push({
      i: i,
      v: typeof v === 'number' ? v : null,
      fx: ((pos % st.cols) + 0.5) / st.cols,
      fy: (Math.floor(pos / st.cols) + 0.5) / st.rows
    });
  }
  st.pts = cells;

  var live = cells.filter(function (p) { return p.v !== null; });
  if (!live.length) {
    g.fillStyle = theme.ink;
    g.font = '12px sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText('no sensors reporting', cssW / 2, cssH / 2);
    return;
  }

  // The field is computed at 44x33 and drawn up to full size by the canvas.
  // Inverse-distance weighting at screen resolution costs nine distance terms
  // per pixel for a picture whose whole point is that it is smooth.
  if (!st.buf) {
    st.buf = document.createElement('canvas');
    st.buf.width = TM_GW;
    st.buf.height = TM_GH;
  }
  var span = (st.tMax - st.tMin) || 1;
  var bg = st.buf.getContext('2d');
  var img = bg.createImageData(TM_GW, TM_GH);
  for (var y = 0; y < TM_GH; y++) {
    var fy = (y + 0.5) / TM_GH;
    for (var x = 0; x < TM_GW; x++) {
      var fx = (x + 0.5) / TM_GW;
      var num = 0, den = 0, exact = null;
      for (var k = 0; k < live.length; k++) {
        var p = live[k];
        var dx = fx - p.fx, dy = fy - p.fy;
        var d2 = dx * dx + dy * dy;
        if (d2 < 1e-6) { exact = p.v; break; }
        var w = 1 / d2;
        num += w * p.v; den += w;
      }
      var val = exact !== null ? exact : num / den;
      var col = tmRamp((val - st.tMin) / span);
      var o = (y * TM_GW + x) * 4;
      img.data[o] = col[0]; img.data[o + 1] = col[1]; img.data[o + 2] = col[2];
      img.data[o + 3] = 168;
    }
  }
  bg.putImageData(img, 0, 0);
  g.imageSmoothingEnabled = true;
  g.imageSmoothingQuality = 'high';
  g.drawImage(st.buf, 0, 0, TM_GW, TM_GH, 0, 0, cssW, cssH);

  var r = Math.max(14, Math.min(26, Math.min(cssW / (st.cols * 3.2), cssH / (st.rows * 3.2))));
  g.textAlign = 'center';
  cells.forEach(function (p) {
    var cx = p.fx * cssW, cy = p.fy * cssH;

    if (p.v === null) {                      // slot exists, sensor does not
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2);
      g.lineWidth = 1.5; g.strokeStyle = theme.ink; g.globalAlpha = .6; g.stroke();
      g.globalAlpha = 1;
      g.fillStyle = theme.ink; g.font = '11px sans-serif'; g.textBaseline = 'middle';
      g.fillText('—', cx, cy);
    } else {
      var rgb = tmRamp((p.v - st.tMin) / span);
      if (st.hover === p.i) {
        g.beginPath(); g.arc(cx, cy, r + 6, 0, Math.PI * 2);
        g.fillStyle = tmCss(rgb, 0.25); g.fill();
      }
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2);
      g.fillStyle = tmCss(rgb); g.fill();
      g.lineWidth = 2; g.strokeStyle = theme.surface; g.stroke();
      // ink picked from the fill's luminance, so it clears contrast at both ends
      g.fillStyle = tmLum(rgb) > 0.6 ? '#0b0f17' : '#ffffff';
      g.font = '600 ' + Math.round(r * 0.55) + 'px -apple-system,system-ui,sans-serif';
      g.textBaseline = 'middle';
      g.fillText(p.v.toFixed(st.decimals) + '°', cx, cy);
    }

    if (st.showNumbers) {
      g.fillStyle = theme.ink;
      g.font = '10px ui-monospace,SFMono-Regular,Consolas,monospace';
      g.textBaseline = 'bottom';
      g.fillText(String(p.i + 1), cx, cy - r - 5);
    }
  });
};

self.onDataUpdated = function () {
  self.tmRead();
  self.tmDraw();
};

self.onResize = function () {
  self.tmDraw();
};

self.onDestroy = function () {
  var st = self.ctx.tmState;
  if (!st) return;
  if (st.onMove) st.canvas.removeEventListener('pointermove', st.onMove);
  if (st.onLeave) st.canvas.removeEventListener('pointerleave', st.onLeave);
  st.buf = null;
  self.ctx.tmState = null;
};

self.typeParameters = function () {
  // Nine probes, and nothing sensible to draw from none of them.
  return { maxDatasources: 1, maxDataKeys: 64, dataKeysOptional: false, singleEntity: true };
};
