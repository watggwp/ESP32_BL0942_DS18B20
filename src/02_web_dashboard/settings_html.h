#pragma once
// Sensor setup page (GET /settings). Assigns each DS18B20's ROM address to a
// fixed dashboard slot and gives it a name, so "slot 3" keeps meaning the same
// physical sensor across reboots -- see the slot table in main.cpp.
//
// Same self-contained rules as dashboard_html.h: inline CSS/JS only, no CDN.
// The live temperature column rides on the dashboard's existing /events stream,
// which is what makes a sensor identifiable at all: pinch one and watch which
// row moves.

static const char SETTINGS_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sensor Setup &middot; ESP32 Power Meter</title>
<style>
:root{
  --bg:#0b0f17; --panel:#121826; --panel2:#161d2e; --border:#232b3d;
  --text:#e8ecf4; --muted:#8a93a6;
  --accent:#5ee1c9; --accent2:#8b7bf0; --warn:#f2b84b; --bad:#ef6b6b;
  --radius:16px;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0;background:var(--bg);color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;}
body{padding:20px;max-width:760px;margin:0 auto}

header{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;margin-bottom:18px}
h1{font-size:1.25rem;font-weight:700;margin:0;letter-spacing:.2px}
h1 span{color:var(--accent)}
header a{color:var(--muted);font-size:.82rem;text-decoration:none;border-bottom:1px dotted var(--border)}
header a:hover{color:var(--accent);border-bottom-color:var(--accent)}

.card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--border);
  border-radius:var(--radius);padding:18px;margin-bottom:16px}
.card h2{font-size:.78rem;text-transform:uppercase;letter-spacing:1.2px;color:var(--muted);
  margin:0 0 12px 0;font-weight:600}
.tip{color:var(--muted);font-size:.82rem;line-height:1.6}
.tip b{color:var(--text);font-weight:600}

.row{display:grid;grid-template-columns:34px 78px 1fr 76px;gap:12px;align-items:center;
  padding:11px 10px;border-radius:12px;border:1px solid transparent;transition:.15s}
.row+.row{margin-top:4px}
.row:hover{background:var(--bg);border-color:var(--border)}
.row.off{opacity:.5}
.slot{width:30px;height:30px;border-radius:9px;background:var(--bg);border:1px solid var(--border);
  display:flex;align-items:center;justify-content:center;font-weight:700;font-size:.85rem;color:var(--accent)}
.temp{font-variant-numeric:tabular-nums;font-weight:700;font-size:1rem;text-align:right;transition:color .8s}
.temp.none{color:var(--muted);font-weight:400;font-size:.8rem}
.meta{min-width:0}
.meta input{background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:9px;
  padding:8px 10px;font-size:.9rem;width:100%}
.meta input:focus{outline:none;border-color:var(--accent)}
.addr{color:var(--muted);font-size:.7rem;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;
  margin-top:4px;letter-spacing:.5px}
.move{display:flex;gap:5px;justify-content:flex-end}

.btn{background:var(--panel2);border:1px solid var(--border);color:var(--text);padding:9px 16px;
  border-radius:10px;cursor:pointer;font-size:.85rem;transition:.15s}
.btn:hover:not(:disabled){border-color:var(--accent);color:var(--accent)}
.btn:disabled{opacity:.3;cursor:default}
.btn.tiny{padding:5px 9px;font-size:.7rem;line-height:1}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#08221d;font-weight:700}
.btn.primary:hover:not(:disabled){color:#08221d;filter:brightness(1.08)}
.actions{display:flex;gap:10px;flex-wrap:wrap;justify-content:space-between;align-items:center}
.empty{color:var(--muted);font-size:.85rem;padding:14px 10px;text-align:center}

.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%) translateY(20px);opacity:0;
  background:var(--panel2);border:1px solid var(--accent);color:var(--text);padding:10px 18px;border-radius:10px;
  font-size:.85rem;transition:.25s;pointer-events:none;z-index:9}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
@media(max-width:520px){
  .row{grid-template-columns:30px 62px 1fr 70px;gap:8px}
  .addr{font-size:.62rem}
}
</style>
</head>
<body>

<header>
  <h1>&#127777; Sensor <span>Setup</span></h1>
  <a href="/">&larr; Back to dashboard</a>
</header>

<div class="card">
  <h2>How to tell the sensors apart</h2>
  <div class="tip">A DS18B20 has no LED to blink, so the only way to identify one is to heat it.
    <b>Pinch a sensor between your fingers and watch which row's temperature climbs</b> &mdash; that is
    the one. Name it, then move on to the next. The readings below are live off the dashboard's own
    stream, so you never have to walk back to the screen.</div>
</div>

<div class="card">
  <h2>Slots</h2>
  <div id="list"><div class="empty">loading&hellip;</div></div>
</div>

<div class="card">
  <div class="actions">
    <button class="btn" id="rescan">Rescan bus</button>
    <button class="btn primary" id="save">Save order &amp; names</button>
  </div>
  <div class="tip" style="margin-top:12px">The slot number is what fixes a sensor's position on the
    dashboard &mdash; it follows the ROM address, not the wiring order, so it survives reboots. A sensor
    that goes missing keeps its slot and shows as offline instead of shuffling everything below it.
    <b>Rescan bus</b> picks up sensors plugged in after boot and appends them to the end.</div>
</div>

<div class="toast" id="toast"></div>

<script src="/thermal.js"></script>
<script>
const $ = id => document.getElementById(id);
let rows = [], maxSlots = 0;
let tMin = 10, tMax = 80;   // thermal range, overwritten by /api/sensors

function toast(msg){
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast._h); toast._h = setTimeout(()=>t.classList.remove('show'), 2400);
}

// Pull the names out of the DOM before any reorder or save, so edits in flight
// are not lost when the list re-renders.
function syncNames(){
  rows.forEach((r,i)=>{ const el = $('n'+i); if(el) r.name = el.value; });
}

function render(){
  const box = $('list');
  if(!rows.length){
    box.innerHTML = '<div class="empty">no DS18B20 found on the bus &mdash; check the 4.7k pull-up, then Rescan</div>';
    return;
  }
  box.innerHTML = rows.map((r,i)=>`
    <div class="row${r.online?'':' off'}">
      <div class="slot">${i + 1}</div>
      <div class="temp none" id="t${i}">&mdash;</div>
      <div class="meta">
        <input id="n${i}" maxlength="15" autocomplete="off" placeholder="name this sensor">
        <div class="addr">${r.addr}</div>
      </div>
      <div class="move">
        <button class="btn tiny" data-up="${i}"${i===0?' disabled':''}>&#9650;</button>
        <button class="btn tiny" data-dn="${i}"${i===rows.length-1?' disabled':''}>&#9660;</button>
      </div>
    </div>`).join('');
  // value via property, not markup: a name is user text, not HTML
  rows.forEach((r,i)=>{ $('n'+i).value = r.name; });
  box.querySelectorAll('[data-up]').forEach(b=>b.onclick=()=>move(+b.dataset.up, -1));
  box.querySelectorAll('[data-dn]').forEach(b=>b.onclick=()=>move(+b.dataset.dn, 1));
}

function move(i, d){
  const j = i + d;
  if(j < 0 || j >= rows.length) return;
  syncNames();
  const t = rows[i]; rows[i] = rows[j]; rows[j] = t;
  render();
}

function load(){
  fetch('/api/sensors').then(r=>r.json()).then(d=>{
    maxSlots = d.max || 0;
    if(typeof d.tmin === 'number') tMin = d.tmin;
    if(typeof d.tmax === 'number') tMax = d.tmax;
    // src remembers where this sensor sits in the firmware's slot order, which
    // is what /events is indexed by -- without it the live column would point
    // at the wrong sensor as soon as you move a row.
    rows = (d.sensors||[]).map((s,i)=>({addr:s.addr, name:s.name||'', online:s.online, src:i}));
    render();
  }).catch(()=>toast('Could not read the sensor list'));
}

function save(){
  syncNames();
  $('save').disabled = true;
  fetch('/api/sensors', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({sensors: rows.map(r=>({addr:r.addr, name:r.name}))})
  }).then(r=>r.json()).then(d=>{
    $('save').disabled = false;
    if(d.ok){ toast('Saved'); load(); } else { toast('Failed: ' + (d.error||'rejected')); }
  }).catch(()=>{ $('save').disabled = false; toast('Save failed'); });
}

$('save').addEventListener('click', save);
$('rescan').addEventListener('click', ()=>{
  syncNames();
  fetch('/api/sensors/rescan', {method:'POST'})
    .then(()=>{ toast('Rescanning the bus…'); setTimeout(load, 2500); })
    .catch(()=>toast('Rescan failed'));
});

const es = new EventSource('/events');
es.addEventListener('data', e=>{
  let d; try{ d = JSON.parse(e.data); }catch(err){ return; }
  const temps = d.temps || [];
  rows.forEach((r,i)=>{
    const el = $('t'+i); if(!el) return;
    const v = temps[r.src];
    if(v === null || v === undefined){
      el.textContent = 'offline'; el.className = 'temp none'; el.style.color = '';
      return;
    }
    el.textContent = v.toFixed(2) + '°';
    el.className = 'temp';
    // Same ramp as the dashboard -- identifying a sensor by its colour only
    // works if both screens agree on what that colour means.
    el.style.color = thermalPaint((v - tMin) / ((tMax - tMin) || 1)).text;
  });
});

load();
</script>
</body>
</html>
)HTMLPAGE";
