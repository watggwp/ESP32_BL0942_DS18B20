#pragma once
// Self-contained dashboard page: no CDN, no external fonts/scripts (the ESP32
// has no guarantee of internet access). Pure inline CSS + vanilla JS, updated
// live over Server-Sent Events (GET /events). Canvas is used for the power
// gauge and the rolling power sparkline; everything else is plain DOM.

static const char DASHBOARD_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Power &amp; Temperature Monitor</title>
<style>
:root{
  --bg:#0b0f17; --panel:#121826; --panel2:#161d2e; --border:#232b3d;
  --text:#e8ecf4; --muted:#8a93a6;
  --accent:#5ee1c9; --accent2:#8b7bf0; --warn:#f2b84b; --bad:#ef6b6b;
  --radius:16px;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0;background:var(--bg);color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  overflow-x:hidden;}
body{padding:20px;max-width:1200px;margin:0 auto;}
header{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;margin-bottom:20px;}
h1{font-size:1.3rem;font-weight:700;margin:0;letter-spacing:.2px}
h1 span{color:var(--accent)}
.status{display:flex;align-items:center;gap:8px;font-size:.85rem;color:var(--muted)}
.dot{width:9px;height:9px;border-radius:50%;background:var(--bad);box-shadow:0 0 8px var(--bad);transition:.3s}
.dot.live{background:var(--accent);box-shadow:0 0 10px var(--accent)}

.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:16px;}
.card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--border);
  border-radius:var(--radius);padding:18px;position:relative;overflow:hidden}
.card h2{font-size:.78rem;text-transform:uppercase;letter-spacing:1.2px;color:var(--muted);margin:0 0 10px 0;font-weight:600}
.cfglink{float:right;color:var(--muted);text-decoration:none;border-bottom:1px dotted var(--border);
  text-transform:none;letter-spacing:0;font-weight:400}
.cfglink:hover{color:var(--accent);border-bottom-color:var(--accent)}

.hero{grid-column:span 5;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:280px}
.hero canvas{width:100%;max-width:260px;height:auto}
.hero .value{font-size:2.6rem;font-weight:800;margin-top:-6px}
.hero .unit{color:var(--muted);font-size:.95rem;margin-top:2px}

.stats{grid-column:span 7;display:grid;grid-template-columns:repeat(3,1fr);gap:16px}
.stat{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--border);
  border-radius:var(--radius);padding:16px;text-align:center}
.stat .icon{font-size:1.3rem;opacity:.85}
.stat .val{font-size:1.7rem;font-weight:700;margin-top:6px}
.stat .lbl{color:var(--muted);font-size:.78rem;text-transform:uppercase;letter-spacing:1px;margin-top:4px}

.spark{grid-column:span 12}
.spark canvas{width:100%;height:110px;display:block}

.energy{grid-column:span 12;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px}
.energy .val{font-size:1.6rem;font-weight:800;color:var(--accent2)}
.btn{background:var(--panel2);border:1px solid var(--border);color:var(--text);padding:9px 16px;
  border-radius:10px;cursor:pointer;font-size:.85rem;transition:.15s}
.btn:hover{border-color:var(--accent);color:var(--accent)}

/* Temperature history: all sensors on one time/temperature plot. Colour encodes
   temperature (same ramp as the cards), never sensor identity -- nine
   distinguishable hues do not exist. Identity comes from the legend and hover. */
.thermo{grid-column:span 12}
.thermo h2 .sub{text-transform:none;letter-spacing:0;font-weight:400;color:var(--muted);margin-left:8px}
.thermo-wrap{display:grid;grid-template-columns:1fr 200px;gap:16px;margin-top:2px}
.thermo-plot{position:relative;min-width:0}
/* pan-y, not none: dragging sideways scrubs the crosshair, but the page must
   still scroll vertically when a finger starts on the chart. */
.thermo-plot canvas{width:100%;height:230px;display:block;cursor:crosshair;touch-action:pan-y}
.scale{display:flex;align-items:center;gap:8px;margin-top:10px;color:var(--muted);font-size:.68rem;
  font-variant-numeric:tabular-nums}
.scalebar{flex:1;height:6px;border-radius:3px;display:block}
.thermo-legend{display:flex;flex-direction:column;gap:1px;min-width:0}
.lrow{display:grid;grid-template-columns:14px 1fr auto;gap:8px;align-items:center;padding:5px 7px;
  border-radius:8px;border:1px solid transparent;transition:background .12s,border-color .12s,opacity .12s}
.lrow:hover,.lrow.on{background:var(--bg);border-color:var(--border)}
.lrow.dim{opacity:.4}
.lkey{height:2px;border-radius:1px;display:block;background:var(--muted)}
.lname{color:var(--muted);font-size:.74rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.lval{color:var(--text);font-size:.78rem;font-weight:600;font-variant-numeric:tabular-nums}
.ttip{position:absolute;pointer-events:none;background:var(--panel2);border:1px solid var(--border);
  border-radius:10px;padding:9px 11px;font-size:.72rem;opacity:0;transition:opacity .12s;
  box-shadow:0 8px 26px rgba(0,0,0,.5);z-index:5;min-width:132px;left:0;top:0}
.ttip.show{opacity:1}
.ttip .tth{color:var(--muted);font-size:.66rem;margin-bottom:6px;font-variant-numeric:tabular-nums}
.ttip .ttr{display:grid;grid-template-columns:12px auto 1fr;gap:8px;align-items:center;margin-top:3px}
.ttip .ttv{color:var(--text);font-weight:700;font-variant-numeric:tabular-nums;text-align:right}
.ttip .ttn{color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}

/* Thermal map: a continuous field interpolated from the nine probes, laid out in
   the 3x3 pattern they are physically mounted in (slot 1 top-left, 9 bottom-right
   -- the order set on /settings). */
.heatmap{grid-column:span 12}
.heatmap h2 .sub{text-transform:none;letter-spacing:0;font-weight:400;color:var(--muted);margin-left:8px}
.hm-wrap{display:flex;flex-direction:column;align-items:center;margin-top:2px}
.hm-plot{position:relative;width:100%;max-width:560px}
.hm-plot canvas{width:100%;aspect-ratio:4/3;display:block;border-radius:12px;
  cursor:crosshair;touch-action:pan-y}
.hm-note{color:var(--muted);font-size:.75rem;line-height:1.6;margin-top:12px;max-width:560px}
.hm-note b{color:var(--text);font-weight:600}

.temps{grid-column:span 12}
.temps .row{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-top:6px}
.tcard{background:var(--panel2);border:1px solid var(--border);border-radius:12px;padding:12px;text-align:center;
  transition:background-image .8s,border-color .8s,box-shadow .8s}
.tcard .tv{font-size:1.25rem;font-weight:700;transition:color .8s}
.tcard .tl{color:var(--muted);font-size:.72rem;margin-top:2px;text-transform:uppercase;letter-spacing:.5px;
  overflow-wrap:anywhere}
.tbar{height:5px;border-radius:3px;background:#232b3d;margin-top:8px;overflow:hidden}
.tbar i{display:block;height:100%;width:0%;background:#4fa8f2;transition:width .8s,background .8s}
.tcard.off{opacity:.35}

.calib{grid-column:span 12}
.calib .row{display:flex;gap:14px;flex-wrap:wrap;align-items:flex-end;margin-top:8px}
.field{display:flex;flex-direction:column;gap:4px}
.field label{font-size:.72rem;color:var(--muted);text-transform:uppercase;letter-spacing:.5px}
.field input{background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:8px;
  padding:8px 10px;width:110px;font-size:.9rem}
.hint{color:var(--muted);font-size:.78rem;margin-top:10px;line-height:1.5}
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%) translateY(20px);opacity:0;
  background:var(--panel2);border:1px solid var(--accent);color:var(--text);padding:10px 18px;border-radius:10px;
  font-size:.85rem;transition:.25s;pointer-events:none}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}

footer{margin-top:22px;color:var(--muted);font-size:.75rem;text-align:center}
@media(max-width:820px){.hero{grid-column:span 12}.stats{grid-column:span 12;grid-template-columns:repeat(3,1fr)}
  .thermo-wrap{grid-template-columns:1fr}
  .thermo-legend{display:grid;grid-template-columns:repeat(2,1fr);gap:2px}}
@media(max-width:520px){.stats{grid-template-columns:repeat(2,1fr)}}
/* Keep the temperature grid at 3 columns on a phone by tightening the cards
   rather than reflowing them -- 3x3 is the layout, at every width. */
@media(max-width:460px){
  .temps .row{gap:8px}
  .tcard{padding:10px 6px}
  .tcard .tv{font-size:1rem}
  .tcard .tl{font-size:.62rem;letter-spacing:0}
}
</style>
</head>
<body>
<header>
  <h1>&#9889; ESP32 <span>Power</span> &amp; Temperature Monitor</h1>
  <div class="status"><span class="dot" id="dot"></span><span id="statusText">connecting&hellip;</span></div>
</header>

<div class="grid">
  <div class="card hero">
    <h2>Active Power</h2>
    <canvas id="gauge" width="260" height="180"></canvas>
    <div class="value" id="pVal">0</div>
    <div class="unit">watts <span id="noLoad" style="color:var(--warn);display:none">&nbsp;&middot; no-load</span></div>
  </div>

  <div class="stats">
    <div class="stat"><div class="icon">&#128267;</div><div class="val" id="vVal">0.0</div><div class="lbl">Volts</div></div>
    <div class="stat"><div class="icon">&#9889;</div><div class="val" id="iVal">0.000</div><div class="lbl">Amps</div></div>
    <div class="stat"><div class="icon">&#128260;</div><div class="val" id="fVal">0.0</div><div class="lbl">Hertz</div></div>
    <div class="stat"><div class="icon">&#128202;</div><div class="val" id="pfVal">&mdash;</div><div class="lbl">Apparent VA</div></div>
    <div class="stat"><div class="icon">&#128267;</div><div class="val" id="heapVal">&mdash;</div><div class="lbl">Free Heap KB</div></div>
    <div class="stat"><div class="icon">&#9200;</div><div class="val" id="upVal">&mdash;</div><div class="lbl">Uptime</div></div>
  </div>

  <div class="card spark">
    <h2>Power History <span style="text-transform:none;letter-spacing:0;font-weight:400">(last 90 s)</span></h2>
    <canvas id="sparkline"></canvas>
  </div>

  <div class="card thermo">
    <h2>Temperature History <span class="sub" id="thermoSub"></span></h2>
    <div class="thermo-wrap">
      <div class="thermo-plot">
        <canvas id="thermo"></canvas>
        <div class="ttip" id="thermoTip"></div>
        <div class="scale">
          <span id="scaleMin">&mdash;</span>
          <i class="scalebar" id="scaleBar"></i>
          <span id="scaleMax">&mdash;</span>
        </div>
      </div>
      <div class="thermo-legend" id="thermoLegend"></div>
    </div>
  </div>

  <div class="card energy">
    <div><h2 style="margin-bottom:4px">Accumulated Energy</h2><div class="val" id="eVal">0.000 kWh</div></div>
    <button class="btn" id="resetEnergy">Reset counter</button>
  </div>

  <div class="card heatmap">
    <h2>Thermal Map <span class="sub" id="hmLayout"></span>
      <a class="cfglink" href="/settings">configure</a></h2>
    <div class="hm-wrap">
      <div class="hm-plot">
        <canvas id="heatmap"></canvas>
        <div class="ttip" id="hmTip"></div>
        <div class="scale">
          <span id="hmScaleMin">&mdash;</span>
          <i class="scalebar" id="hmScaleBar"></i>
          <span id="hmScaleMax">&mdash;</span>
        </div>
      </div>
      <div class="hm-note">The nine circles are measured. Everything between them is
        <b>interpolated</b> &mdash; a distance-weighted estimate, not a reading. It can never
        invent a spot hotter than the hottest probe, but the further apart the probes sit,
        the rougher the guess in between.</div>
    </div>
  </div>

  <div class="card temps">
    <h2>DS18B20 Temperatures <a class="cfglink" href="/settings">configure</a></h2>
    <div class="row" id="tempRow"></div>
  </div>

  <div class="card calib">
    <h2>BL0942 Calibration</h2>
    <div class="row">
      <div class="field"><label>Current kI</label><input id="kI" type="number" step="0.0001"></div>
      <div class="field"><label>Voltage kV</label><input id="kV" type="number" step="0.0001"></div>
      <div class="field"><label>Power kP</label><input id="kP" type="number" step="0.0001"></div>
      <button class="btn" id="saveCalib">Save</button>
    </div>
    <div class="hint">Nominal factory constants are only a starting point. Measure Voltage/Current/Power with a
      trusted meter under a known load, then set e.g. <code>kV = true_volts / displayed_volts</code> (same idea
      for kI, kP) and Save &mdash; values persist in flash across reboots.</div>
  </div>
</div>

<footer>ESP32 &middot; BL0942 &middot; 9&times; DS18B20 &middot; <span id="ip"></span></footer>
<div class="toast" id="toast"></div>

<script src="/thermal.js"></script>
<script>
const $ = id => document.getElementById(id);
const gauge = $('gauge').getContext('2d');
const spark = $('sparkline').getContext('2d');
let history = [];
const HISTORY_MAX = 90;

// Sensor names and the thermal range live in NVS/config.h, not in the 1 Hz
// stream. The stream carries a config version instead; when it moves, someone
// edited /settings and we refetch.
let sensorNames = [], labelsDirty = false, cfgVersion = -1, lastTemps = null;
let tMin = 10, tMax = 80;   // overwritten by /api/sensors

function loadSensorConfig(){
  fetch('/api/sensors').then(r=>r.json()).then(d=>{
    sensorNames = (d.sensors||[]).map(s=>s.name||'');
    if(typeof d.tmin === 'number') tMin = d.tmin;
    if(typeof d.tmax === 'number') tMax = d.tmax;
    if(typeof d.serp === 'boolean') hmSerpentine = d.serp;
    labelsDirty = true;
    legendSig = '';   // names changed -- force the legend to rebuild its labels
    renderThermoScale();
    if(lastTemps){
      renderTemps(lastTemps);
      renderLegend(lastTemps.length);
      updateLegendValues(lastTemps);
      drawThermo();
      drawHeatmap();
    }
  }).catch(()=>{});
}

function toast(msg){
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast._h); toast._h = setTimeout(()=>t.classList.remove('show'), 2200);
}

function drawGauge(watts, maxWatts){
  const c = $('gauge'); const w = c.width, h = c.height;
  gauge.clearRect(0,0,w,h);
  const cx = w/2, cy = h-10, r = 100;
  const start = Math.PI, end = 0;
  const frac = Math.max(0, Math.min(1, watts / maxWatts));

  gauge.lineWidth = 16; gauge.lineCap = 'round';
  gauge.strokeStyle = '#232b3d';
  gauge.beginPath(); gauge.arc(cx, cy, r, start, end); gauge.stroke();

  const grad = gauge.createLinearGradient(cx-r, cy, cx+r, cy);
  grad.addColorStop(0, '#5ee1c9'); grad.addColorStop(.6, '#8b7bf0'); grad.addColorStop(1, '#ef6b6b');
  gauge.strokeStyle = grad;
  gauge.beginPath(); gauge.arc(cx, cy, r, start, start + (end-start)*frac); gauge.stroke();

  gauge.fillStyle = '#8a93a6'; gauge.font = '11px sans-serif'; gauge.textAlign='center';
  gauge.fillText('0', cx-r+4, cy+16);
  gauge.fillText(maxWatts+'W', cx+r-14, cy+16);
}

function drawSpark(){
  const c = $('sparkline'); const dpr = window.devicePixelRatio||1;
  const cssW = c.clientWidth || 600, cssH = 110;
  c.width = cssW*dpr; c.height = cssH*dpr;
  spark.setTransform(dpr,0,0,dpr,0,0);
  spark.clearRect(0,0,cssW,cssH);
  if(history.length < 2) return;
  const max = Math.max(10, ...history);
  const min = 0;
  const stepX = cssW / (HISTORY_MAX-1);
  spark.beginPath();
  history.forEach((v,i)=>{
    const x = i*stepX;
    const y = cssH - ((v-min)/(max-min||1))*(cssH-10) - 4;
    i===0 ? spark.moveTo(x,y) : spark.lineTo(x,y);
  });
  spark.lineTo((history.length-1)*stepX, cssH);
  spark.lineTo(0, cssH);
  spark.closePath();
  const fill = spark.createLinearGradient(0,0,0,cssH);
  fill.addColorStop(0,'rgba(94,225,201,.35)'); fill.addColorStop(1,'rgba(94,225,201,0)');
  spark.fillStyle = fill; spark.fill();

  spark.beginPath();
  history.forEach((v,i)=>{
    const x = i*stepX;
    const y = cssH - ((v-min)/(max-min||1))*(cssH-10) - 4;
    i===0 ? spark.moveTo(x,y) : spark.lineTo(x,y);
  });
  spark.strokeStyle = '#5ee1c9'; spark.lineWidth = 2; spark.stroke();
}

function renderTemps(temps){
  const row = $('tempRow');
  if(row.children.length !== temps.length || labelsDirty){
    labelsDirty = false;
    row.innerHTML = temps.map((_,i)=>`
      <div class="tcard" id="tc${i}">
        <div class="tv" id="tv${i}">&mdash;</div>
        <div class="tl" id="tn${i}"></div>
        <div class="tbar"><i id="ti${i}"></i></div>
      </div>`).join('');
    // textContent, not markup: sensor names are whatever the user typed
    temps.forEach((_,i)=>{ $('tn'+i).textContent = sensorNames[i] || ('Sensor ' + i); });
  }
  temps.forEach((c,i)=>{
    const card = $('tc'+i), val = $('tv'+i), bar = $('ti'+i);
    if(c===null){
      card.classList.add('off');
      card.style.backgroundImage = ''; card.style.borderColor = ''; card.style.boxShadow = '';
      val.style.color = '';
      val.innerHTML = 'offline';
      bar.style.width = '0%';
      return;
    }
    card.classList.remove('off');
    val.textContent = c.toFixed(2)+'°C';

    // One normalised position drives the colour AND the bar length, so the two
    // can never disagree the way they did when each had its own hardcoded range.
    const p = thermalPaint((c - tMin) / ((tMax - tMin) || 1));
    val.style.color = p.text;
    card.style.backgroundImage = p.tint;
    card.style.borderColor = p.border;
    card.style.boxShadow = p.glow;
    bar.style.width = (p.t * 100) + '%';
    bar.style.background = p.vivid;
  });
}

// ---------------------------------------------------------------------------
// Temperature history plot
//
// Nine series on one pair of axes. Hue carries TEMPERATURE, not sensor identity:
// nine mutually distinguishable hues do not exist (a 9th is indistinguishable
// from an earlier one under colour-blind vision), so identity is carried by the
// legend and by hover emphasis instead, and hue is free to mean the same thing it
// means on the cards. That makes it a semantic-heat scale, which obliges the
// gradient scale legend underneath.
// ---------------------------------------------------------------------------
const TEMP_HISTORY_MAX = 300;   // one sample/second -> 5 minutes
let tempHistory = [], focusSlot = null, hoverIdx = null, thermoGeo = null, legendSig = '';
let CHART_GRID, CHART_INK, CHART_SURFACE;

(function readChartTokens(){
  const cs = getComputedStyle(document.documentElement);
  const pick = (name, fallback) => cs.getPropertyValue(name).trim() || fallback;
  CHART_GRID    = pick('--border', '#232b3d');
  CHART_INK     = pick('--muted',  '#8a93a6');
  CHART_SURFACE = pick('--panel',  '#121826');
})();

function pushTempHistory(temps){
  if(tempHistory.length !== temps.length) tempHistory = temps.map(()=>[]);
  temps.forEach((v,i)=>{
    const a = tempHistory[i];
    a.push(typeof v === 'number' ? v : null);
    if(a.length > TEMP_HISTORY_MAX) a.shift();
  });
}

function lastReading(a){
  for(let k = a.length - 1; k >= 0; k--) if(typeof a[k] === 'number') return a[k];
  return null;
}

function seriesColor(i){
  const v = tempHistory[i] ? lastReading(tempHistory[i]) : null;
  if(v === null) return CHART_INK;
  return thermalPaint((v - tMin) / ((tMax - tMin) || 1)).vivid;
}

// Autoscale to the data, not to the colour range: nine probes sitting between 28
// and 31 degrees would be a flat line on a 10-80 axis. The floor on the span
// stops sensor noise from filling the plot and looking like an event.
function thermoRange(){
  let lo = Infinity, hi = -Infinity;
  tempHistory.forEach(a=>a.forEach(v=>{
    if(typeof v === 'number'){ if(v < lo) lo = v; if(v > hi) hi = v; }
  }));
  if(lo === Infinity) return [20, 30];
  const span = Math.max(hi - lo, 4);
  const mid = (lo + hi) / 2;
  return [mid - span/2 - span*0.18, mid + span/2 + span*0.18];
}

function niceStep(span){
  const raw = span / 4;
  for(const s of [0.5,1,2,5,10,20,50]) if(raw <= s) return s;
  return 100;
}

function drawThermo(){
  const c = $('thermo');
  const cssW = c.clientWidth || 600, cssH = 230, dpr = window.devicePixelRatio || 1;
  c.width = cssW * dpr; c.height = cssH * dpr;
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, cssW, cssH);

  const padL = 44, padR = 14, padT = 10, padB = 24;
  const plotW = cssW - padL - padR, plotH = cssH - padT - padB;
  const n = tempHistory.length ? tempHistory[0].length : 0;
  thermoGeo = {padL, plotW, plotH, padT, n};

  if(n < 2){
    g.fillStyle = CHART_INK; g.font = '12px sans-serif'; g.textAlign = 'center';
    g.fillText('collecting…', cssW/2, cssH/2);
    return;
  }

  const [lo, hi] = thermoRange();
  const step = niceStep(hi - lo);
  const X = i => padL + (i / (n - 1)) * plotW;
  const Y = v => padT + plotH - ((v - lo) / (hi - lo)) * plotH;

  // Grid and axes: solid hairlines one step off the surface, never dashed.
  g.strokeStyle = CHART_GRID; g.lineWidth = 1;
  g.fillStyle = CHART_INK;
  g.font = '10px ui-monospace,SFMono-Regular,Consolas,monospace';
  g.textAlign = 'right'; g.textBaseline = 'middle';
  for(let v = Math.ceil(lo/step)*step; v <= hi; v += step){
    const y = Math.round(Y(v)) + 0.5;
    g.beginPath(); g.moveTo(padL, y); g.lineTo(padL + plotW, y); g.stroke();
    g.fillText(v.toFixed(step < 1 ? 1 : 0) + '°', padL - 8, y);
  }
  g.textBaseline = 'top';
  g.textAlign = 'left';  g.fillText('−' + (n-1) + 's', padL, padT + plotH + 8);
  g.textAlign = 'right'; g.fillText('now', padL + plotW, padT + plotH + 8);

  if(hoverIdx !== null && hoverIdx < n){
    const x = Math.round(X(hoverIdx)) + 0.5;
    g.strokeStyle = CHART_INK; g.lineWidth = 1;
    g.beginPath(); g.moveTo(x, padT); g.lineTo(x, padT + plotH); g.stroke();
  }

  g.lineJoin = 'round'; g.lineCap = 'round';
  const strokeSeries = a => {
    g.beginPath();
    let pen = false;
    for(let k = 0; k < a.length; k++){
      const v = a[k];
      if(typeof v !== 'number'){ pen = false; continue; }   // gap, don't bridge it
      const x = X(k), y = Y(v);
      pen ? g.lineTo(x, y) : g.moveTo(x, y);
      pen = true;
    }
    g.stroke();
  };

  tempHistory.forEach((a, i) => {
    const lit = focusSlot === null || focusSlot === i;
    const col = seriesColor(i);
    if(lit){   // the glow: one wide low-alpha pass under the line
      g.globalAlpha = 0.14; g.lineWidth = 7; g.strokeStyle = col; strokeSeries(a);
    }
    g.globalAlpha = lit ? 1 : 0.15;
    g.lineWidth = 2; g.strokeStyle = col; strokeSeries(a);
    g.globalAlpha = 1;
  });

  // Markers last so they sit above every line, each with a 2px surface ring.
  const dot = (x, y, col) => {
    g.beginPath(); g.arc(x, y, 4, 0, Math.PI*2);
    g.fillStyle = col; g.fill();
    g.lineWidth = 2; g.strokeStyle = CHART_SURFACE; g.stroke();
  };
  tempHistory.forEach((a, i) => {
    if(focusSlot !== null && focusSlot !== i) return;
    let k = a.length - 1;
    while(k >= 0 && typeof a[k] !== 'number') k--;
    if(k >= 0) dot(X(k), Y(a[k]), seriesColor(i));
    if(hoverIdx !== null && typeof a[hoverIdx] === 'number'){
      dot(X(hoverIdx), Y(a[hoverIdx]), seriesColor(i));
    }
  });
}

// The legend is also the table view: every current value is readable without
// hovering anything, so the tooltip only ever enhances.
function syncLegendFocus(){
  const rows = $('thermoLegend').children;
  for(let i = 0; i < rows.length; i++){
    rows[i].classList.toggle('on',  focusSlot === i);
    rows[i].classList.toggle('dim', focusSlot !== null && focusSlot !== i);
  }
}

function renderLegend(count){
  const box = $('thermoLegend');
  const sig = count + '|' + sensorNames.slice(0, count).join('');
  if(sig === legendSig) return;
  legendSig = sig;
  box.innerHTML = '';
  for(let i = 0; i < count; i++){
    const row = document.createElement('div');
    row.className = 'lrow';
    const key = document.createElement('i');  key.className = 'lkey';
    const name = document.createElement('span'); name.className = 'lname';
    name.textContent = sensorNames[i] || ('Sensor ' + i);   // user text, never innerHTML
    const val = document.createElement('span'); val.className = 'lval'; val.textContent = '—';
    row.append(key, name, val);
    row.addEventListener('pointerenter', ()=>{ focusSlot = i; syncLegendFocus(); drawThermo(); });
    row.addEventListener('pointerleave', ()=>{ focusSlot = null; syncLegendFocus(); drawThermo(); });
    box.appendChild(row);
  }
}

function updateLegendValues(temps){
  const rows = $('thermoLegend').children;
  temps.forEach((v,i)=>{
    const row = rows[i]; if(!row) return;
    row.children[0].style.background = seriesColor(i);
    row.children[2].textContent = typeof v === 'number' ? v.toFixed(2) + '°C' : 'offline';
  });
}

function hideThermoTip(){ $('thermoTip').classList.remove('show'); }

function showThermoTip(px, py, idx){
  const tip = $('thermoTip');
  const rows = [];
  tempHistory.forEach((a,i)=>{ if(typeof a[idx] === 'number') rows.push({i, v:a[idx]}); });
  if(!rows.length){ hideThermoTip(); return; }
  rows.sort((a,b)=>b.v - a.v);

  tip.innerHTML = '';
  const head = document.createElement('div');
  head.className = 'tth';
  const back = tempHistory[0].length - 1 - idx;
  head.textContent = back === 0 ? 'now' : '−' + back + ' s';
  tip.appendChild(head);
  // Value leads, name follows: the reader already has the series and wants the number.
  rows.forEach(r=>{
    const row = document.createElement('div'); row.className = 'ttr';
    const key = document.createElement('i'); key.className = 'lkey';
    key.style.background = seriesColor(r.i);
    const val = document.createElement('span'); val.className = 'ttv';
    val.textContent = r.v.toFixed(2) + '°';
    const nm = document.createElement('span'); nm.className = 'ttn';
    nm.textContent = sensorNames[r.i] || ('Sensor ' + r.i);
    row.append(key, val, nm);
    tip.appendChild(row);
  });

  tip.classList.add('show');
  const wrap = $('thermo').parentElement;
  let left = px + 16, top = py - tip.offsetHeight - 12;
  if(left + tip.offsetWidth > wrap.clientWidth) left = px - tip.offsetWidth - 16;
  if(left < 0) left = 0;
  if(top < 0) top = py + 16;
  tip.style.left = left + 'px';
  tip.style.top  = top + 'px';
}

(function bindThermoHover(){
  const cv = $('thermo');
  // The crosshair snaps to the nearest sample, so the reader aims at a moment in
  // time rather than trying to land on a 2px line.
  cv.addEventListener('pointermove', e=>{
    if(!thermoGeo || thermoGeo.n < 2) return;
    const r = cv.getBoundingClientRect();
    const x = e.clientX - r.left, y = e.clientY - r.top;
    let idx = Math.round(((x - thermoGeo.padL) / thermoGeo.plotW) * (thermoGeo.n - 1));
    hoverIdx = Math.max(0, Math.min(thermoGeo.n - 1, idx));
    showThermoTip(x, y, hoverIdx);
    drawThermo();
  });
  cv.addEventListener('pointerleave', ()=>{ hoverIdx = null; hideThermoTip(); drawThermo(); });
})();

function renderThermoScale(){
  $('hmLayout').textContent = hmSerpentine
    ? 'mounted 3×3 serpentine · 1 2 3 / 6 5 4 / 7 8 9'
    : 'mounted 3×3 · slot 1 top-left';
  const grad = thermalGradientCss();
  $('scaleBar').style.background = grad;
  $('scaleMin').textContent = tMin + '°C';
  $('scaleMax').textContent = tMax + '°C';
  $('hmScaleBar').style.background = grad;
  $('hmScaleMin').textContent = tMin + '°C';
  $('hmScaleMax').textContent = tMax + '°C';
}

// ---------------------------------------------------------------------------
// Thermal map
//
// Nine probes mounted in a 3x3 pattern. Slot order (set on /settings) IS the
// layout: slot 1 top-left, 5 centre, 9 bottom-right -- so no coordinates need
// storing anywhere. The field between them is inverse-distance weighted, which
// has the property that matters here: the result is a weighted average of the
// real readings, so it can never fabricate a hotspot hotter than the hottest
// probe. It is still a guess, and the caption says so.
// ---------------------------------------------------------------------------
const HM_GW = 44, HM_GH = 33;   // field is computed coarse, then bilinearly upscaled
let hmBuf = null, hmHover = null, hmPts = [], hmSerpentine = true;

// Slot -> where that probe physically sits. In a serpentine run every other row
// is mounted right-to-left, so slot 4 is the RIGHT of the middle row and slot 6
// the left. Getting this backwards would mirror half the map and point at the
// wrong end of the machine.
function slotCell(i){
  const row = Math.floor(i / 3);
  const col = (hmSerpentine && row % 2 === 1) ? 2 - (i % 3) : (i % 3);
  return { fx: (col + 0.5) / 3, fy: (row + 0.5) / 3 };
}

function drawHeatmap(){
  const c = $('heatmap');
  const cssW = c.clientWidth || 480;
  const cssH = c.clientHeight || Math.round(cssW * 0.75);
  if(!cssW) return;
  const dpr = window.devicePixelRatio || 1;
  c.width = cssW * dpr; c.height = cssH * dpr;
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, cssW, cssH);

  // Lattice position of every slot, whether or not it is reporting.
  const cells = [];
  for(let i = 0; i < 9; i++){
    const v = lastTemps ? lastTemps[i] : undefined;
    const at = slotCell(i);
    cells.push({ i, v: typeof v === 'number' ? v : null, fx: at.fx, fy: at.fy });
  }
  hmPts = cells;
  const live = cells.filter(p => p.v !== null);

  if(!live.length){
    g.fillStyle = CHART_INK; g.font = '12px sans-serif';
    g.textAlign = 'center'; g.textBaseline = 'middle';
    g.fillText('no sensors reporting', cssW/2, cssH/2);
    return;
  }

  if(!hmBuf){
    hmBuf = document.createElement('canvas');
    hmBuf.width = HM_GW; hmBuf.height = HM_GH;
  }
  const bg = hmBuf.getContext('2d');
  const img = bg.createImageData(HM_GW, HM_GH);
  for(let y = 0; y < HM_GH; y++){
    const fy = (y + 0.5) / HM_GH;
    for(let x = 0; x < HM_GW; x++){
      const fx = (x + 0.5) / HM_GW;
      let num = 0, den = 0, exact = null;
      for(let k = 0; k < live.length; k++){
        const p = live[k];
        const dx = fx - p.fx, dy = fy - p.fy;
        const d2 = dx*dx + dy*dy;
        if(d2 < 1e-6){ exact = p.v; break; }
        const w = 1 / d2;                      // Shepard, power 2
        num += w * p.v; den += w;
      }
      const v = exact !== null ? exact : num / den;
      const col = thermalRamp((v - tMin) / ((tMax - tMin) || 1));
      const o = (y * HM_GW + x) * 4;
      img.data[o] = col[0]; img.data[o+1] = col[1]; img.data[o+2] = col[2];
      img.data[o+3] = 168;   // a wash, so the measured circles stay the loud thing
    }
  }
  bg.putImageData(img, 0, 0);
  g.imageSmoothingEnabled = true;
  g.imageSmoothingQuality = 'high';
  g.drawImage(hmBuf, 0, 0, HM_GW, HM_GH, 0, 0, cssW, cssH);

  const r = Math.max(16, Math.min(26, cssW / 20));
  g.textAlign = 'center';
  cells.forEach(p => {
    const cx = p.fx * cssW, cy = p.fy * cssH;
    const focused = hmHover === p.i;

    if(p.v === null){                       // slot exists, sensor does not
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI*2);
      g.lineWidth = 1.5; g.strokeStyle = CHART_INK; g.globalAlpha = .6; g.stroke();
      g.globalAlpha = 1;
      g.fillStyle = CHART_INK; g.font = '11px sans-serif'; g.textBaseline = 'middle';
      g.fillText('—', cx, cy);
    } else {
      const rgb = thermalRamp((p.v - tMin) / ((tMax - tMin) || 1));
      if(focused){
        g.beginPath(); g.arc(cx, cy, r + 6, 0, Math.PI*2);
        g.fillStyle = thermalCss(rgb, 0.25); g.fill();
      }
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI*2);
      g.fillStyle = thermalCss(rgb); g.fill();
      g.lineWidth = 2; g.strokeStyle = CHART_SURFACE; g.stroke();
      // A label sitting inside a coloured fill picks its ink from that fill's
      // luminance, so it clears contrast at both ends of the ramp.
      g.fillStyle = thermalLum(rgb) > 0.6 ? '#0b0f17' : '#ffffff';
      g.font = '600 ' + Math.round(r * 0.55) + 'px -apple-system,system-ui,sans-serif';
      g.textBaseline = 'middle';
      g.fillText(p.v.toFixed(1) + '°', cx, cy);
    }

    g.fillStyle = CHART_INK;
    g.font = '10px ui-monospace,SFMono-Regular,Consolas,monospace';
    g.textBaseline = 'bottom';
    g.fillText(String(p.i + 1), cx, cy - r - 5);
  });
}

(function bindHeatmapHover(){
  const cv = $('heatmap');
  cv.addEventListener('pointermove', e => {
    const rect = cv.getBoundingClientRect();
    const x = e.clientX - rect.left, y = e.clientY - rect.top;
    let best = null, bestD = 1e9;
    hmPts.forEach(p => {
      const dx = x - p.fx * rect.width, dy = y - p.fy * rect.height;
      const d = Math.sqrt(dx*dx + dy*dy);
      if(d < bestD){ bestD = d; best = p; }
    });
    // Hit target is generously bigger than the circle -- nobody lands dead-centre.
    if(!best || bestD > 46 || best.v === null){
      if(hmHover !== null){ hmHover = null; drawHeatmap(); }
      $('hmTip').classList.remove('show');
      return;
    }
    if(hmHover !== best.i){ hmHover = best.i; drawHeatmap(); }

    const tip = $('hmTip');
    tip.innerHTML = '';
    const head = document.createElement('div');
    head.className = 'tth';
    head.textContent = 'slot ' + (best.i + 1);
    const row = document.createElement('div'); row.className = 'ttr';
    const key = document.createElement('i'); key.className = 'lkey';
    key.style.background = thermalCss(thermalRamp((best.v - tMin) / ((tMax - tMin) || 1)));
    const val = document.createElement('span'); val.className = 'ttv';
    val.textContent = best.v.toFixed(2) + '°C';
    const nm = document.createElement('span'); nm.className = 'ttn';
    nm.textContent = sensorNames[best.i] || ('Sensor ' + best.i);
    row.append(key, val, nm);
    tip.append(head, row);
    tip.classList.add('show');

    let left = x + 16, top = y - tip.offsetHeight - 12;
    if(left + tip.offsetWidth > rect.width) left = x - tip.offsetWidth - 16;
    if(left < 0) left = 0;
    if(top < 0) top = y + 16;
    tip.style.left = left + 'px';
    tip.style.top = top + 'px';
  });
  cv.addEventListener('pointerleave', () => {
    hmHover = null;
    $('hmTip').classList.remove('show');
    drawHeatmap();
  });
})();

function fmtUptime(ms){
  let s = Math.floor(ms/1000);
  const d = Math.floor(s/86400); s%=86400;
  const h = Math.floor(s/3600); s%=3600;
  const m = Math.floor(s/60); s%=60;
  return d>0 ? `${d}d ${h}h` : (h>0 ? `${h}h ${m}m` : `${m}m ${s}s`);
}

function update(data){
  $('pVal').textContent = data.p.toFixed(1);
  $('vVal').textContent = data.v.toFixed(1);
  $('iVal').textContent = data.i.toFixed(3);
  $('fVal').textContent = data.f.toFixed(2);
  $('pfVal').textContent = (data.v*data.i).toFixed(1);
  $('eVal').textContent = data.e.toFixed(3)+' kWh';
  $('noLoad').style.display = data.noLoad ? 'inline' : 'none';
  $('heapVal').textContent = (data.heap/1024).toFixed(0);
  $('upVal').textContent = fmtUptime(data.uptime);

  history.push(data.p); if(history.length>HISTORY_MAX) history.shift();
  const gaugeMax = Math.max(500, Math.ceil((Math.max(...history,data.p)+50)/500)*500);
  drawGauge(data.p, gaugeMax);
  drawSpark();
  if(data.cfg !== cfgVersion){ cfgVersion = data.cfg; loadSensorConfig(); }
  lastTemps = data.temps;
  renderTemps(data.temps);

  drawHeatmap();
  pushTempHistory(data.temps);
  renderLegend(data.temps.length);
  updateLegendValues(data.temps);
  const span = tempHistory.length ? tempHistory[0].length : 0;
  $('thermoSub').textContent = span < 2 ? '' :
    'last ' + (span >= 60 ? Math.round(span/60) + ' min' : span + ' s') +
    ' · ' + data.temps.length + ' sensors';
  drawThermo();
}

function connect(){
  const es = new EventSource('/events');
  es.onopen = ()=>{ $('dot').classList.add('live'); $('statusText').textContent='live'; };
  es.onerror = ()=>{ $('dot').classList.remove('live'); $('statusText').textContent='reconnecting…'; };
  es.addEventListener('data', e=>{ try{ update(JSON.parse(e.data)); }catch(err){} });
}

function loadCalibration(){
  fetch('/api/calibration').then(r=>r.json()).then(c=>{
    $('kI').value=c.kI; $('kV').value=c.kV; $('kP').value=c.kP;
  });
}

$('saveCalib').addEventListener('click', ()=>{
  const body = { kI: parseFloat($('kI').value), kV: parseFloat($('kV').value), kP: parseFloat($('kP').value) };
  fetch('/api/calibration', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)})
    .then(r=>r.ok ? toast('Calibration saved') : toast('Save failed'));
});

$('resetEnergy').addEventListener('click', ()=>{
  if(!confirm('Reset accumulated energy counter to 0?')) return;
  fetch('/api/energy/reset', {method:'POST'}).then(()=>toast('Energy counter reset'));
});

$('ip').textContent = location.host;
window.addEventListener('resize', ()=>{ drawSpark(); drawThermo(); drawHeatmap(); });
renderThermoScale();
loadCalibration();
loadSensorConfig();   // names + thermal range, before the first frame lands
connect();
</script>
</body>
</html>
)HTMLPAGE";
