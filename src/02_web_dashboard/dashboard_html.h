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
@media(max-width:820px){.hero{grid-column:span 12}.stats{grid-column:span 12;grid-template-columns:repeat(3,1fr)}}
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
    <h2>Power History (last ~5 min)</h2>
    <canvas id="sparkline"></canvas>
  </div>

  <div class="card energy">
    <div><h2 style="margin-bottom:4px">Accumulated Energy</h2><div class="val" id="eVal">0.000 kWh</div></div>
    <button class="btn" id="resetEnergy">Reset counter</button>
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
    labelsDirty = true;
    if(lastTemps) renderTemps(lastTemps);
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
loadCalibration();
loadSensorConfig();   // names + thermal range, before the first frame lands
connect();
</script>
</body>
</html>
)HTMLPAGE";
