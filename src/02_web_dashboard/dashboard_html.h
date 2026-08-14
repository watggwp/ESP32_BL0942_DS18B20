#pragma once
// Self-contained dashboard page: no CDN, no external fonts/scripts (the ESP32
// has no guarantee of internet access). Pure inline CSS + vanilla JS, updated
// live over Server-Sent Events (GET /events). The only shared asset is
// /thermal.js, served by this same firmware.
//
// NOTE ON SIZE: everything between the R"HTMLPAGE( ... )HTMLPAGE" delimiters is
// stored in flash byte for byte and shipped to the browser. Comments in there
// cost real flash, unlike these C comments, which the compiler discards. So the
// design rationale lives up here and the in-page comments stay terse.
//
// Cards on the page:
//   Active Power gauge, stat tiles, power sparkline (last 90 s)
//   Accumulated energy + reset
//   Thermal Map    -- see below
//   DS18B20 Temperatures -- 3x3 cards in mounting order
//   BL0942 calibration
//
// SERPENTINE LAYOUT. The nine probes are mounted in a serpentine so the cable
// never has to jump back across the array: rows 0 and 2 run left to right, row 1
// runs right to left.
//
//     1  2  3
//     6  5  4      <- reversed
//     7  8  9
//
// Both the thermal map and the temperature cards are laid out in that order, so
// a card's position on screen is the probe's position in the real world. serp()
// does the mapping and is its own inverse. Get it wrong and half the display
// mirrors, pointing at the wrong end of the machine while still looking normal.
//
// THERMAL MAP. A continuous field interpolated from the nine probes by inverse
// distance weighting (Shepard, power 2). The property that matters: the result is
// a weighted average of real readings, so it can never fabricate a hotspot hotter
// than the hottest probe. It is still an estimate between the probes, and the
// caption on the card says so. The field is drawn as a wash so the nine measured
// circles stay the loud thing on the card.
//
// COLOUR. One ramp everywhere (thermal.js), encoding TEMPERATURE, never sensor
// identity -- nine mutually distinguishable hues do not exist, so identity comes
// from slot numbers, names and hover instead. Because the ramp is semantic heat,
// the card carries a gradient scale legend to read it against. Its ends come from
// TEMP_COLOR_MIN_C / TEMP_COLOR_MAX_C in config.h via /api/sensors, so the colour
// and the little bar under each card are scaled from one number, never two.

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
.hnav{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.navbtn{display:inline-flex;align-items:center;gap:6px;background:var(--panel2);
  border:1px solid var(--border);color:var(--text);padding:8px 14px;border-radius:10px;
  font-size:.82rem;text-decoration:none;white-space:nowrap;transition:.15s}
.navbtn:hover{border-color:var(--accent);color:var(--accent)}

.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:16px;}
.card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--border);
  border-radius:var(--radius);padding:18px;position:relative;overflow:hidden}
.card h2{font-size:.78rem;text-transform:uppercase;letter-spacing:1.2px;color:var(--muted);margin:0 0 10px 0;font-weight:600}
.card h2 .sub{text-transform:none;letter-spacing:0;font-weight:400;color:var(--muted);margin-left:8px}
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

.heatmap{grid-column:span 12}
.hm-wrap{display:flex;flex-direction:column;align-items:center;margin-top:2px}
.hm-plot{position:relative;width:100%;max-width:560px}
/* pan-y, not none: the page must still scroll when a finger starts on the map */
.hm-plot canvas{width:100%;aspect-ratio:4/3;display:block;border-radius:12px;
  cursor:crosshair;touch-action:pan-y}
.hm-note{color:var(--muted);font-size:.75rem;line-height:1.6;margin-top:12px;max-width:560px}
.hm-note b{color:var(--text);font-weight:600}
.scale{display:flex;align-items:center;gap:8px;margin-top:10px;color:var(--muted);font-size:.68rem;
  font-variant-numeric:tabular-nums}
.scalebar{flex:1;height:6px;border-radius:3px;display:block}

.ttip{position:absolute;pointer-events:none;background:var(--panel2);border:1px solid var(--border);
  border-radius:10px;padding:9px 11px;font-size:.72rem;opacity:0;transition:opacity .12s;
  box-shadow:0 8px 26px rgba(0,0,0,.5);z-index:5;min-width:132px;left:0;top:0}
.ttip.show{opacity:1}
.ttip .tth{color:var(--muted);font-size:.66rem;margin-bottom:6px;font-variant-numeric:tabular-nums}
.ttip .ttr{display:grid;grid-template-columns:12px auto 1fr;gap:8px;align-items:center}
.ttip .ttk{height:2px;border-radius:1px;display:block;background:var(--muted)}
.ttip .ttv{color:var(--text);font-weight:700;font-variant-numeric:tabular-nums;text-align:right}
.ttip .ttn{color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}

.temps{grid-column:span 12}
.temps .row{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-top:6px}
.tcard{background:var(--panel2);border:1px solid var(--border);border-radius:12px;padding:12px;text-align:center;
  transition:background-image .8s,border-color .8s,box-shadow .8s}
.tcard .tv{font-size:1.25rem;font-weight:700;transition:color .8s}
/* overflow-wrap: Thai has no spaces, so a name cannot line-break on its own and
   would run straight out of a fixed-width grid cell */
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
@media(max-width:820px){.hero{grid-column:span 12}.stats{grid-column:span 12;grid-template-columns:repeat(3,1fr)}}
@media(max-width:520px){.stats{grid-template-columns:repeat(2,1fr)}}
/* Keep the temperature grid at 3 columns on a phone by tightening the cards
   rather than reflowing them -- the 3x3 IS the mounting layout, at every width */
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
  <div class="hnav">
    <span class="status"><span class="dot" id="dot"></span><span id="statusText">connecting&hellip;</span></span>
    <a class="navbtn" href="/settings">&#127777; Sensors</a>
    <a class="navbtn" href="/wifi">&#128246; Wi-Fi</a>
  </div>
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
    <h2>Power History <span class="sub">last 90 s</span></h2>
    <canvas id="sparkline"></canvas>
  </div>

  <div class="card energy">
    <div><h2 style="margin-bottom:4px">Accumulated Energy</h2><div class="val" id="eVal">0.000 kWh</div></div>
    <button class="btn" id="resetEnergy">Reset counter</button>
  </div>

  <div class="card heatmap">
    <h2>Thermal Map <span class="sub">serpentine &middot; 1 2 3 / 6 5 4 / 7 8 9</span>
      <a class="cfglink" href="/settings">configure</a></h2>
    <div class="hm-wrap">
      <div class="hm-plot">
        <canvas id="heatmap"></canvas>
        <div class="ttip" id="hmTip"></div>
        <div class="scale">
          <span id="scaleMin">&mdash;</span>
          <i class="scalebar" id="scaleBar"></i>
          <span id="scaleMax">&mdash;</span>
        </div>
      </div>
      <div class="hm-note">The nine circles are measured. Everything between them is
        <b>interpolated</b> &mdash; a distance-weighted estimate, not a reading. It can never
        invent a spot hotter than the hottest probe, but the further apart the probes sit,
        the rougher the guess in between.</div>
    </div>
  </div>

  <div class="card temps">
    <h2>DS18B20 Temperatures <span class="sub">in mounting order</span>
      <a class="cfglink" href="/settings">configure</a></h2>
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

<footer>ESP32 &middot; BL0942 &middot; 9&times; DS18B20 &middot; <span id="fw" title="">firmware &mdash;</span></footer>
<div class="toast" id="toast"></div>

<script src="/thermal.js"></script>
<script>
const $ = id => document.getElementById(id);
const gauge = $('gauge').getContext('2d');
const spark = $('sparkline').getContext('2d');
let history = [];
const HISTORY_MAX = 90;

let sensorNames = [], labelsDirty = false, cfgVersion = -1, lastTemps = null;
let tMin = 10, tMax = 80;   // overwritten by /api/sensors

let CHART_INK, CHART_SURFACE;
(function readTokens(){
  const cs = getComputedStyle(document.documentElement);
  const pick = (n, f) => cs.getPropertyValue(n).trim() || f;
  CHART_INK     = pick('--muted', '#8a93a6');
  CHART_SURFACE = pick('--panel', '#121826');
})();

// Serpentine: row 1 runs right-to-left, so slot 4 is the RIGHT of the middle row
// and slot 6 the left. Its own inverse, so it maps slot->position and back.
function serp(i){
  const row = Math.floor(i / 3);
  return row * 3 + (row % 2 ? 2 - (i % 3) : i % 3);
}

function toast(msg){
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast._h); toast._h = setTimeout(()=>t.classList.remove('show'), 2200);
}

function loadSensorConfig(){
  fetch('/api/sensors').then(r=>r.json()).then(d=>{
    sensorNames = (d.sensors||[]).map(s=>s.name||'');
    if(d.fw){
      $('fw').textContent = 'firmware v' + d.fw;
      if(d.build) $('fw').title = 'built ' + d.build;
    }
    if(typeof d.tmin === 'number') tMin = d.tmin;
    if(typeof d.tmax === 'number') tMax = d.tmax;
    labelsDirty = true;
    renderScale();
    if(lastTemps){ renderTemps(lastTemps); drawHeatmap(); }
  }).catch(()=>{});
}

function renderScale(){
  $('scaleBar').style.background = thermalGradientCss();
  $('scaleMin').textContent = tMin + '°C';
  $('scaleMax').textContent = tMax + '°C';
}

function sensorLabel(i){ return sensorNames[i] || ('Sensor ' + (i + 1)); }

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
  // Cards sit in mounting order, so a card's place on screen is the probe's place
  // on the machine. Only a full set of nine forms the serpentine 3x3.
  const order = temps.length === 9 ? [0,1,2,3,4,5,6,7,8].map(serp) : temps.map((_,i)=>i);
  if(row.children.length !== temps.length || labelsDirty){
    labelsDirty = false;
    row.innerHTML = order.map(s=>`
      <div class="tcard" id="tc${s}">
        <div class="tv" id="tv${s}">&mdash;</div>
        <div class="tl" id="tn${s}"></div>
        <div class="tbar"><i id="ti${s}"></i></div>
      </div>`).join('');
    // textContent, not markup: sensor names are whatever the user typed
    order.forEach(s=>{ $('tn'+s).textContent = sensorLabel(s); });
  }
  temps.forEach((c,i)=>{
    const card = $('tc'+i), val = $('tv'+i), bar = $('ti'+i);
    if(!card) return;
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
    // One normalised position drives the colour AND the bar length
    const p = thermalPaint((c - tMin) / ((tMax - tMin) || 1));
    val.style.color = p.text;
    card.style.backgroundImage = p.tint;
    card.style.borderColor = p.border;
    card.style.boxShadow = p.glow;
    bar.style.width = (p.t * 100) + '%';
    bar.style.background = p.vivid;
  });
}

const HM_GW = 44, HM_GH = 33;   // field computed coarse, then bilinearly upscaled
let hmBuf = null, hmHover = null, hmPts = [];

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

  const cells = [];
  for(let i = 0; i < 9; i++){
    const v = lastTemps ? lastTemps[i] : undefined;
    const pos = serp(i);
    cells.push({
      i, v: typeof v === 'number' ? v : null,
      fx: ((pos % 3) + 0.5) / 3,
      fy: (Math.floor(pos / 3) + 0.5) / 3
    });
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
        const w = 1 / d2;
        num += w * p.v; den += w;
      }
      const v = exact !== null ? exact : num / den;
      const col = thermalRamp((v - tMin) / ((tMax - tMin) || 1));
      const o = (y * HM_GW + x) * 4;
      img.data[o] = col[0]; img.data[o+1] = col[1]; img.data[o+2] = col[2];
      img.data[o+3] = 168;
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

    if(p.v === null){                       // slot exists, sensor does not
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI*2);
      g.lineWidth = 1.5; g.strokeStyle = CHART_INK; g.globalAlpha = .6; g.stroke();
      g.globalAlpha = 1;
      g.fillStyle = CHART_INK; g.font = '11px sans-serif'; g.textBaseline = 'middle';
      g.fillText('—', cx, cy);
    } else {
      const rgb = thermalRamp((p.v - tMin) / ((tMax - tMin) || 1));
      if(hmHover === p.i){
        g.beginPath(); g.arc(cx, cy, r + 6, 0, Math.PI*2);
        g.fillStyle = thermalCss(rgb, 0.25); g.fill();
      }
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI*2);
      g.fillStyle = thermalCss(rgb); g.fill();
      g.lineWidth = 2; g.strokeStyle = CHART_SURFACE; g.stroke();
      // ink picked from the fill's luminance, so it clears contrast at both ends
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
    // hit target generously bigger than the circle -- nobody lands dead-centre
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
    const key = document.createElement('i'); key.className = 'ttk';
    key.style.background = thermalCss(thermalRamp((best.v - tMin) / ((tMax - tMin) || 1)));
    const val = document.createElement('span'); val.className = 'ttv';
    val.textContent = best.v.toFixed(2) + '°C';
    const nm = document.createElement('span'); nm.className = 'ttn';
    nm.textContent = sensorLabel(best.i);
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

window.addEventListener('resize', ()=>{ drawSpark(); drawHeatmap(); });
renderScale();
loadCalibration();
loadSensorConfig();
connect();
</script>
</body>
</html>
)HTMLPAGE";
