#pragma once
// Wi-Fi setup page (GET /wifi), and the page a captive-portal probe gets
// redirected to. Same self-contained rules as the other pages: inline CSS/JS,
// no CDN -- in portal mode there is no internet to fetch anything from.
//
// Saving reboots the board, so the page cannot report the outcome: it tells the
// user where to look instead. If the new credentials fail, the portal comes
// back up on its own.

static const char WIFI_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Wi-Fi Setup &middot; ESP32 Power Meter</title>
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
body{padding:20px;max-width:620px;margin:0 auto}

header{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;margin-bottom:18px}
h1{font-size:1.25rem;font-weight:700;margin:0;letter-spacing:.2px}
h1 span{color:var(--accent)}
header nav{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.navbtn{display:inline-flex;align-items:center;gap:6px;background:var(--panel2);
  border:1px solid var(--border);color:var(--text);padding:8px 14px;border-radius:10px;
  font-size:.82rem;text-decoration:none;white-space:nowrap;transition:.15s}
.navbtn:hover{border-color:var(--accent);color:var(--accent)}

.card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--border);
  border-radius:var(--radius);padding:18px;margin-bottom:16px}
.card h2{font-size:.78rem;text-transform:uppercase;letter-spacing:1.2px;color:var(--muted);
  margin:0 0 12px 0;font-weight:600;display:flex;align-items:center;justify-content:space-between;gap:10px}
.tip{color:var(--muted);font-size:.82rem;line-height:1.6}
.tip b{color:var(--text);font-weight:600}

.state{display:flex;align-items:center;gap:10px;font-size:.95rem;font-weight:600}
.dot{width:9px;height:9px;border-radius:50%;background:var(--muted);flex:none}
.dot.ok{background:var(--accent);box-shadow:0 0 9px var(--accent)}
.dot.portal{background:var(--warn);box-shadow:0 0 9px var(--warn)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:5px 14px;margin-top:12px;font-size:.83rem}
.kv dt{color:var(--muted)}
.kv dd{margin:0;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;word-break:break-all}

.net{display:flex;align-items:center;gap:12px;width:100%;text-align:left;cursor:pointer;
  background:none;border:1px solid transparent;border-radius:12px;padding:10px;color:var(--text);
  font-size:.9rem;font-family:inherit;transition:.15s}
.net:hover{background:var(--bg);border-color:var(--border)}
.net.sel{background:var(--bg);border-color:var(--accent)}
.net .nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net .lk{color:var(--muted);font-size:.75rem;flex:none}
.bars{display:flex;align-items:flex-end;gap:2px;height:14px;flex:none}
.bars i{width:3px;background:var(--border);border-radius:1px}
.bars i:nth-child(1){height:25%} .bars i:nth-child(2){height:50%}
.bars i:nth-child(3){height:75%} .bars i:nth-child(4){height:100%}
.bars i.on{background:var(--accent)}
.empty{color:var(--muted);font-size:.85rem;padding:14px 10px;text-align:center}

label{display:block;font-size:.78rem;color:var(--muted);margin:0 0 6px 2px}
input{background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:9px;
  padding:10px 12px;font-size:.92rem;width:100%;font-family:inherit}
input:focus{outline:none;border-color:var(--accent)}
.field+.field{margin-top:14px}
.pw{position:relative}
.pw input{padding-right:60px}
.pw button{position:absolute;right:6px;top:50%;transform:translateY(-50%);background:none;border:none;
  color:var(--muted);font-size:.72rem;cursor:pointer;padding:6px 8px;font-family:inherit}
.pw button:hover{color:var(--accent)}

.btn{background:var(--panel2);border:1px solid var(--border);color:var(--text);padding:9px 16px;
  border-radius:10px;cursor:pointer;font-size:.85rem;font-family:inherit;transition:.15s}
.btn:hover:not(:disabled){border-color:var(--accent);color:var(--accent)}
.btn:disabled{opacity:.3;cursor:default}
.btn.tiny{padding:5px 9px;font-size:.7rem;line-height:1;text-transform:none;letter-spacing:0}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#08221d;font-weight:700}
.btn.primary:hover:not(:disabled){color:#08221d;filter:brightness(1.08)}
.btn.danger:hover:not(:disabled){border-color:var(--bad);color:var(--bad)}
.actions{display:flex;gap:10px;flex-wrap:wrap;justify-content:space-between;align-items:center}

.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%) translateY(20px);opacity:0;
  background:var(--panel2);border:1px solid var(--accent);color:var(--text);padding:10px 18px;border-radius:10px;
  font-size:.85rem;transition:.25s;pointer-events:none;z-index:9}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}

.done{position:fixed;inset:0;background:rgba(11,15,23,.96);display:none;align-items:center;
  justify-content:center;padding:24px;z-index:20}
.done.show{display:flex}
.done .card{max-width:420px;margin:0;text-align:center}
.done h3{margin:0 0 10px 0;font-size:1.05rem}
</style>
</head>
<body>

<header>
  <h1>&#128246; Wi-Fi <span>Setup</span></h1>
  <nav>
    <a class="navbtn" href="/settings">&#127777; Sensors</a>
    <a class="navbtn" href="/">&larr; Dashboard</a>
  </nav>
</header>

<div class="card">
  <h2>Status</h2>
  <div class="state"><span class="dot" id="dot"></span><span id="stateText">checking&hellip;</span></div>
  <dl class="kv" id="kv"></dl>
</div>

<div class="card">
  <h2>Networks <button class="btn tiny" id="rescan">Scan again</button></h2>
  <div id="list"><div class="empty">scanning&hellip;</div></div>
</div>

<div class="card">
  <h2>Connect to</h2>
  <div class="field">
    <label for="ssid">Network name (SSID)</label>
    <input id="ssid" maxlength="32" autocomplete="off" autocapitalize="off" spellcheck="false"
           placeholder="pick one above, or type it">
  </div>
  <div class="field">
    <label for="pass">Password</label>
    <div class="pw">
      <input id="pass" type="password" maxlength="63" autocomplete="off" placeholder="leave empty for an open network">
      <button type="button" id="reveal">show</button>
    </div>
  </div>
  <div class="actions" style="margin-top:16px">
    <button class="btn danger" id="forget">Forget saved network</button>
    <button class="btn primary" id="save">Save &amp; reboot</button>
  </div>
  <div class="tip" style="margin-top:12px">Credentials are stored on the board itself, so this survives
    reflashing only if you keep the NVS partition &mdash; and it survives a move to a new site with no
    rebuild at all. <b>Sensors keep reading and totalling energy the whole time this page is up.</b></div>
</div>

<div class="toast" id="toast"></div>

<div class="done" id="done">
  <div class="card">
    <h3 id="doneTitle">Rebooting&hellip;</h3>
    <div class="tip" id="doneBody"></div>
  </div>
</div>

<script>
const $ = id => document.getElementById(id);
let info = {}, pollTimer = 0, scanTries = 0;

function toast(msg){
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast._h); toast._h = setTimeout(()=>t.classList.remove('show'), 2600);
}

function status(){
  fetch('/api/wifi').then(r=>r.json()).then(d=>{
    info = d;
    const dot = $('dot');
    dot.className = 'dot' + (d.connected ? ' ok' : d.portal ? ' portal' : '');
    $('stateText').textContent = d.connected ? 'Connected to ' + (d.ssid || '?')
                               : d.portal   ? 'Setup mode — not on a network yet'
                                            : 'Reconnecting…';
    const kv = [];
    if(d.ip)   kv.push(['IP address', d.ip]);
    if(d.host) kv.push(['Hostname', 'http://' + d.host + '.local/']);
    if(typeof d.rssi === 'number') kv.push(['Signal', d.rssi + ' dBm']);
    if(d.portal && d.ap) kv.push(['Setup AP', d.ap]);
    kv.push(['Saved network', d.saved || '(none)']);
    $('kv').innerHTML = kv.map(([k,v])=>`<dt></dt><dd></dd>`).join('');
    // text via property, not markup: an SSID is user-controlled
    const dts = $('kv').querySelectorAll('dt'), dds = $('kv').querySelectorAll('dd');
    kv.forEach(([k,v],i)=>{ dts[i].textContent = k; dds[i].textContent = v; });
    if(!$('ssid').value && d.saved) $('ssid').value = d.saved;
  }).catch(()=>{});
}

function bars(rssi){
  const n = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  return '<span class="bars">' + [1,2,3,4].map(i=>`<i class="${i<=n?'on':''}"></i>`).join('') + '</span>';
}

function renderNets(nets){
  const box = $('list');
  if(!nets.length){
    box.innerHTML = '<div class="empty">no networks found &mdash; try Scan again</div>';
    return;
  }
  box.innerHTML = nets.map((n,i)=>
    `<button class="net" data-i="${i}">${bars(n.rssi)}<span class="nm"></span>` +
    `<span class="lk">${n.lock?'&#128274;':'open'}</span></button>`).join('');
  const names = box.querySelectorAll('.nm');
  nets.forEach((n,i)=>{ names[i].textContent = n.ssid; });   // SSID is user text
  box.querySelectorAll('.net').forEach(b=>b.onclick=()=>{
    box.querySelectorAll('.net').forEach(o=>o.classList.remove('sel'));
    b.classList.add('sel');
    $('ssid').value = nets[+b.dataset.i].ssid;
    $('pass').focus();
  });
}

function scan(force){
  clearTimeout(pollTimer);
  if(force){ scanTries = 0; $('rescan').disabled = true; }
  fetch('/api/wifi/scan' + (force ? '?force=1' : '')).then(r=>r.json()).then(d=>{
    scanTries = 0;
    if(d.scanning){
      // Keep any list already on screen: blanking it makes a routine rescan
      // look like everything vanished.
      if(!$('list').querySelector('.net')) $('list').innerHTML = '<div class="empty">scanning&hellip;</div>';
      pollTimer = setTimeout(()=>scan(false), 1400);
      return;
    }
    $('rescan').disabled = false;
    if(d.error && !(d.networks||[]).length){
      $('list').innerHTML = '<div class="empty">' + d.error + ' &mdash; press Scan again</div>';
      return;
    }
    renderNets(d.networks || []);
  }).catch(()=>{
    // A scan takes the radio off the setup AP for seconds at a time, so a
    // dropped poll is the normal case mid-sweep, not a failure. Keep asking.
    if(++scanTries <= 15){ pollTimer = setTimeout(()=>scan(false), 1500); return; }
    $('rescan').disabled = false;
    $('list').innerHTML = '<div class="empty">lost contact with the board &mdash; rejoin its Wi-Fi, then press Scan again</div>';
  });
}

function finish(title, body){
  $('doneTitle').textContent = title;
  $('doneBody').innerHTML = body;
  $('done').classList.add('show');
}

$('reveal').onclick = ()=>{
  const p = $('pass');
  p.type = p.type === 'password' ? 'text' : 'password';
  $('reveal').textContent = p.type === 'password' ? 'show' : 'hide';
};

$('rescan').onclick = ()=>scan(true);

$('save').onclick = ()=>{
  const ssid = $('ssid').value.trim(), pass = $('pass').value;
  if(!ssid){ toast('Pick a network or type its name'); return; }
  if(pass.length > 0 && pass.length < 8){ toast('A password must be at least 8 characters'); return; }
  $('save').disabled = true;
  fetch('/api/wifi', {method:'POST', headers:{'Content-Type':'application/json'},
                      body: JSON.stringify({ssid, pass})})
    .then(r=>r.json()).then(d=>{
      if(!d.ok){ $('save').disabled = false; toast('Rejected: ' + (d.error||'unknown')); return; }
      const host = info.host || 'esp32-powermeter';
      finish('Saved — rebooting',
        'The board is restarting and will join <b>' + ssid.replace(/[<>&]/g,'') + '</b>.<br><br>' +
        'Reconnect your phone or laptop to that same network, then open ' +
        '<b>http://' + host + '.local/</b><br><br>' +
        'If it cannot join, the <b>' + (info.ap || 'setup') + '</b> network comes back in about 30 seconds ' +
        'so you can try again. The status LED blinks slowly while it is waiting for you.');
    })
    .catch(()=>{ $('save').disabled = false; toast('Could not reach the board'); });
};

$('forget').onclick = ()=>{
  if(!confirm('Erase the saved Wi-Fi credentials and restart into setup mode?')) return;
  $('forget').disabled = true;
  fetch('/api/wifi/forget', {method:'POST'}).then(r=>r.json()).then(()=>{
    finish('Credentials erased',
      'The board is restarting into setup mode. Join its <b>' + (info.ap || 'P1-Setup-XXXX') +
      '</b> network in about 30 seconds &mdash; the setup page opens by itself.');
  }).catch(()=>{ $('forget').disabled = false; toast('Could not reach the board'); });
};

status();
scan(false);
setInterval(status, 5000);
</script>
</body>
</html>
)HTMLPAGE";
