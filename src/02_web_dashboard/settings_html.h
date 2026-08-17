#pragma once
// Every setting on the board, on one page (GET /settings), split into tabs:
//
//   Sensors      -- assign each DS18B20's ROM address to a fixed dashboard slot
//                   and name it, so "slot 3" keeps meaning the same physical
//                   sensor across reboots (see the slot table in main.cpp)
//   Calibration  -- BL0942 kI/kV/kP against a live reading, plus the energy total
//   Wi-Fi        -- credentials, network scan, and what the board is joined to
//
// WHY ONE PAGE. The dashboard is a screen you leave open; these are things you
// set once and walk away from. Keeping them apart means the live view never has
// a Save button on it that a passer-by can press. Folding the three into one
// document also collapses three copies of the same CSS into one -- the tokens,
// .card, .btn and .toast rules used to be stored in flash three times over.
//
// SERVED AT TWO PATHS. /settings opens on Sensors; /wifi opens on the Wi-Fi tab,
// which is what the captive portal and the setup-mode redirect point at, so the
// links printed on a label or handed out at a site keep working. The tab is also
// mirrored into location.hash, so a reload stays where you were.
//
// THE WI-FI SCAN IS LAZY. Scanning takes the radio off the network for seconds
// at a time -- on the setup AP that means the phone briefly loses the page. So
// nothing Wi-Fi related runs until the Wi-Fi tab is actually opened.
//
// Same self-contained rules as dashboard_html.h: inline CSS/JS only, no CDN --
// in portal mode there is no internet to fetch anything from. The live readings
// on the Sensors and Calibration tabs ride on the dashboard's existing /events
// stream, which is what makes a sensor identifiable at all: pinch one and watch
// which row moves.

static const char SETTINGS_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Settings &middot; ESP32 Power Meter</title>
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

header{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;margin-bottom:16px}
h1{font-size:1.25rem;font-weight:700;margin:0;letter-spacing:.2px}
h1 span{color:var(--accent)}
header nav{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.navbtn{display:inline-flex;align-items:center;gap:6px;background:var(--panel2);
  border:1px solid var(--border);color:var(--text);padding:8px 14px;border-radius:10px;
  font-size:.82rem;text-decoration:none;white-space:nowrap;transition:.15s}
.navbtn:hover{border-color:var(--accent);color:var(--accent)}

.tabs{display:flex;gap:5px;background:var(--panel);border:1px solid var(--border);
  border-radius:14px;padding:5px;margin-bottom:16px;overflow-x:auto}
.tab{flex:1;min-width:104px;background:none;border:none;color:var(--muted);font-family:inherit;
  font-size:.85rem;padding:10px 12px;border-radius:10px;cursor:pointer;white-space:nowrap;transition:.15s}
.tab:hover{color:var(--text)}
.tab.on{background:var(--panel2);color:var(--accent);font-weight:600;box-shadow:inset 0 0 0 1px var(--border)}
.panel{display:none}
.panel.on{display:block}

.card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--border);
  border-radius:var(--radius);padding:18px;margin-bottom:16px}
.card h2{font-size:.78rem;text-transform:uppercase;letter-spacing:1.2px;color:var(--muted);
  margin:0 0 12px 0;font-weight:600;display:flex;align-items:center;justify-content:space-between;gap:10px}
.tip{color:var(--muted);font-size:.82rem;line-height:1.6}
.tip b{color:var(--text);font-weight:600}
.empty{color:var(--muted);font-size:.85rem;padding:14px 10px;text-align:center}
.actions{display:flex;gap:10px;flex-wrap:wrap;justify-content:space-between;align-items:center}

.btn{background:var(--panel2);border:1px solid var(--border);color:var(--text);padding:9px 16px;
  border-radius:10px;cursor:pointer;font-size:.85rem;font-family:inherit;transition:.15s}
.btn:hover:not(:disabled){border-color:var(--accent);color:var(--accent)}
.btn:disabled{opacity:.3;cursor:default}
.btn.tiny{padding:5px 9px;font-size:.7rem;line-height:1;text-transform:none;letter-spacing:0}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#08221d;font-weight:700}
.btn.primary:hover:not(:disabled){color:#08221d;filter:brightness(1.08)}
.btn.danger:hover:not(:disabled){border-color:var(--bad);color:var(--bad)}

label{display:block;font-size:.78rem;color:var(--muted);margin:0 0 6px 2px}
input{background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:9px;
  padding:9px 11px;font-size:.9rem;width:100%;font-family:inherit}
input:focus{outline:none;border-color:var(--accent)}

/* --- Sensors tab --- */
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
.addr{color:var(--muted);font-size:.7rem;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;
  margin-top:4px;letter-spacing:.5px}
.move{display:flex;gap:5px;justify-content:flex-end}

/* --- Calibration tab --- */
.live{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.live div{background:var(--bg);border:1px solid var(--border);border-radius:12px;padding:12px;text-align:center}
.live b{display:block;font-size:1.3rem;font-weight:700;font-variant-numeric:tabular-nums}
.live span{color:var(--muted);font-size:.68rem;text-transform:uppercase;letter-spacing:1px}
.knobs{display:flex;gap:14px;flex-wrap:wrap;align-items:flex-end}
.knob{display:flex;flex-direction:column}
.knob label{font-size:.72rem;text-transform:uppercase;letter-spacing:.5px;margin:0 0 5px 2px}
.knob input{width:112px}
.erow{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px}
.eval{font-size:1.6rem;font-weight:800;color:var(--accent2);font-variant-numeric:tabular-nums}

/* --- Alerts tab --- */
input[type=checkbox]{width:auto;margin:0;accent-color:var(--accent)}
.sw{display:inline-flex;align-items:center;gap:7px;margin:0;text-transform:none;letter-spacing:0;
  font-size:.78rem;color:var(--muted);cursor:pointer}
.lim{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;padding:11px 0}
.lim+.lim{border-top:1px solid var(--border)}
.lim .nm{display:flex;align-items:center;gap:9px;font-size:.9rem}
.lim .in{display:flex;gap:7px;align-items:center;color:var(--muted);font-size:.74rem;flex-wrap:wrap;
  justify-content:flex-end}
.lim input[type=number]{width:86px}
.lim.off{opacity:.45}
/* --- Firmware tab --- */
.slots{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:4px}
.slotbox{background:var(--bg);border:1px solid var(--border);border-radius:12px;padding:12px}
.slotbox .k{color:var(--muted);font-size:.7rem;text-transform:uppercase;letter-spacing:.5px}
.slotbox .v{font-size:.95rem;font-weight:700;margin-top:4px;font-variant-numeric:tabular-nums;
  overflow-wrap:anywhere}
.drop{border:1.5px dashed var(--border);border-radius:12px;padding:22px 16px;text-align:center;
  transition:.15s;cursor:pointer;margin-top:4px}
.drop:hover,.drop.over{border-color:var(--accent);background:var(--bg)}
.drop .big{font-size:.95rem;font-weight:600}
.drop .sub{color:var(--muted);font-size:.76rem;margin-top:5px;overflow-wrap:anywhere}
.prog{height:8px;border-radius:4px;background:var(--bg);border:1px solid var(--border);
  overflow:hidden;margin-top:14px;display:none}
.prog.on{display:block}
.prog i{display:block;height:100%;width:0;background:var(--accent);transition:width .2s}
.err{margin-top:12px;padding:10px 12px;border-radius:10px;background:rgba(239,107,107,.09);
  border:1px solid var(--bad);color:var(--bad);font-size:.78rem;line-height:1.5;
  font-family:ui-monospace,SFMono-Regular,Consolas,monospace;word-break:break-word}
@media(max-width:520px){
  .lim{grid-template-columns:1fr}
  .lim .in{justify-content:flex-start}
}

/* --- Wi-Fi tab --- */
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
.field+.field{margin-top:14px}
.pw{position:relative}
.pw input{padding-right:60px}
.pw button{position:absolute;right:6px;top:50%;transform:translateY(-50%);background:none;border:none;
  color:var(--muted);font-size:.72rem;cursor:pointer;padding:6px 8px;font-family:inherit}
.pw button:hover{color:var(--accent)}
.done{position:fixed;inset:0;background:rgba(11,15,23,.96);display:none;align-items:center;
  justify-content:center;padding:24px;z-index:20}
.done.show{display:flex}
.done .card{max-width:420px;margin:0;text-align:center}
.done h3{margin:0 0 10px 0;font-size:1.05rem}

.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%) translateY(20px);opacity:0;
  background:var(--panel2);border:1px solid var(--accent);color:var(--text);padding:10px 18px;border-radius:10px;
  font-size:.85rem;transition:.25s;pointer-events:none;z-index:9}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
footer{margin-top:22px;color:var(--muted);font-size:.75rem;text-align:center}
@media(max-width:520px){
  .row{grid-template-columns:30px 62px 1fr 70px;gap:8px}
  .addr{font-size:.62rem}
  .tab{min-width:0;font-size:.8rem;padding:10px 6px}
}
</style>
</head>
<body>

<header>
  <h1>&#9881;&#65039; Device <span>Settings</span></h1>
  <nav><a class="navbtn" href="/">&larr; Dashboard</a></nav>
</header>

<div class="tabs">
  <button class="tab on" data-tab="sensors">&#127777; Sensors</button>
  <button class="tab" data-tab="calibration">&#9889; Calibration</button>
  <button class="tab" data-tab="alerts">&#128276; Alerts</button>
  <button class="tab" data-tab="wifi">&#128246; Wi-Fi</button>
  <button class="tab" data-tab="firmware">&#128190; Firmware</button>
</div>

<div class="panel on" id="p-sensors">
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
</div>

<div class="panel" id="p-calibration">
  <div class="card">
    <h2>Live reading</h2>
    <div class="live">
      <div><b id="lv">&mdash;</b><span>Volts</span></div>
      <div><b id="li">&mdash;</b><span>Amps</span></div>
      <div><b id="lp">&mdash;</b><span>Watts</span></div>
    </div>
  </div>

  <div class="card">
    <h2>BL0942 calibration</h2>
    <div class="knobs">
      <div class="knob"><label for="kI">Current kI</label><input id="kI" type="number" step="0.0001"></div>
      <div class="knob"><label for="kV">Voltage kV</label><input id="kV" type="number" step="0.0001"></div>
      <div class="knob"><label for="kP">Power kP</label><input id="kP" type="number" step="0.0001"></div>
      <button class="btn primary" id="saveCalib">Save</button>
    </div>
    <div class="tip" style="margin-top:12px">Nominal factory constants are only a starting point. Measure
      Voltage/Current/Power with a trusted meter under a known load, then set e.g.
      <b>kV = true_volts / displayed_volts</b> (same idea for kI, kP) and Save. The numbers above update
      live, so you can watch the effect without leaving this tab &mdash; values persist in flash across
      reboots.</div>
  </div>

  <div class="card">
    <h2>Accumulated energy</h2>
    <div class="erow">
      <div class="eval" id="eVal">&mdash; kWh</div>
      <button class="btn danger" id="resetEnergy">Reset counter</button>
    </div>
    <div class="tip" style="margin-top:12px">The running kWh total, written to flash once a minute.
      Resetting cannot be undone &mdash; note the reading down first if it is being billed against.</div>
  </div>
</div>

<div class="panel" id="p-alerts">
  <div class="card">
    <h2>LINE connection</h2>
    <div class="tip" style="margin-bottom:14px">LINE Notify was shut down on 31 March 2025, so this uses the
      <b>Messaging API</b> instead: create a LINE Official Account, copy its <b>channel access token</b>, and
      paste the <b>user or group ID</b> the messages should go to. The board talks to LINE directly &mdash;
      no server in between.</div>
    <div class="field">
      <label for="atoken">Channel access token</label>
      <input id="atoken" type="password" maxlength="290" autocomplete="off"
             placeholder="paste to change it">
    </div>
    <div class="field">
      <label for="ato">Destination ID &mdash; user (U&hellip;), group (C&hellip;) or room (R&hellip;)</label>
      <input id="ato" maxlength="60" autocomplete="off" spellcheck="false" placeholder="U1234abcd&hellip;">
    </div>
    <div class="actions" style="margin-top:16px">
      <span class="tip" id="astat">&mdash;</span>
      <button class="btn" id="atest">Send test message</button>
    </div>
    <div class="err" id="aerr" style="display:none"></div>
  </div>

  <div class="card">
    <h2>What to watch <label class="sw"><input type="checkbox" id="aon"> alerts enabled</label></h2>

    <div class="lim" id="limTemp">
      <div class="nm"><input type="checkbox" id="tempOn"> &#127777; Temperature</div>
      <div class="in">above <input id="tempMax" type="number" step="0.5"> &deg;C</div>
    </div>
    <div class="lim" id="limVolt">
      <div class="nm"><input type="checkbox" id="voltOn"> &#128267; Voltage</div>
      <div class="in">below <input id="voltMin" type="number" step="1">
        &nbsp;above <input id="voltMax" type="number" step="1"> V</div>
    </div>
    <div class="lim" id="limFreq">
      <div class="nm"><input type="checkbox" id="freqOn"> &#128260; Frequency</div>
      <div class="in">below <input id="freqMin" type="number" step="0.1">
        &nbsp;above <input id="freqMax" type="number" step="0.1"> Hz</div>
    </div>
    <div class="lim" id="limAmp">
      <div class="nm"><input type="checkbox" id="ampOn"> &#9889; Current</div>
      <div class="in">above <input id="ampMax" type="number" step="0.1"> A</div>
    </div>
    <div class="lim" id="limWatt">
      <div class="nm"><input type="checkbox" id="wattOn"> &#128202; Power</div>
      <div class="in">above <input id="wattMax" type="number" step="50"> W</div>
    </div>

    <div class="tip" style="margin-top:14px">A reading has to stay outside its limit for several seconds
      running before anything is sent, and has to come back a margin <i>inside</i> the limit before that
      alert can fire again. That is what stops a value sitting exactly on the threshold from messaging you
      every second. Temperatures that cross together are reported as one message.</div>
  </div>

  <div class="card">
    <h2>Message budget</h2>
    <div class="knobs">
      <div class="knob"><label for="cooldown">Cooldown (min)</label><input id="cooldown" type="number" min="1" step="1"></div>
      <div class="knob"><label for="dailyCap">Max per day</label><input id="dailyCap" type="number" min="1" step="1"></div>
      <label class="sw" style="padding-bottom:10px"><input type="checkbox" id="onRecover"> tell me when it goes back to normal</label>
    </div>
    <div class="tip" style="margin-top:12px">A LINE Official Account on the free plan only sends a few hundred
      messages a month, and these pushes count against that. <b>Cooldown</b> is the least time between two
      messages from the same alert; <b>max per day</b> is a hard stop. Recovery notices are useful but they
      double the traffic &mdash; turn them off if the quota is tight.</div>
    <div class="actions" style="margin-top:16px">
      <span></span>
      <button class="btn primary" id="asave">Save alert settings</button>
    </div>
  </div>
</div>

<div class="panel" id="p-wifi">
  <div class="card">
    <h2>Status</h2>
    <div class="state"><span class="dot" id="dot"></span><span id="stateText">checking&hellip;</span></div>
    <dl class="kv" id="kv"></dl>
  </div>

  <div class="card">
    <h2>Networks <button class="btn tiny" id="wrescan">Scan again</button></h2>
    <div id="nets"><div class="empty">scanning&hellip;</div></div>
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
      <button class="btn primary" id="wsave">Save &amp; reboot</button>
    </div>
    <div class="tip" style="margin-top:12px">Credentials are stored on the board itself, so this survives
      reflashing only if you keep the NVS partition &mdash; and it survives a move to a new site with no
      rebuild at all. <b>Sensors keep reading and totalling energy the whole time this page is up.</b></div>
  </div>
</div>

<div class="panel" id="p-firmware">
  <div class="card">
    <h2>Running now</h2>
    <div class="slots">
      <div class="slotbox"><div class="k">Version</div><div class="v" id="oFw">&mdash;</div></div>
      <div class="slotbox"><div class="k">Built</div><div class="v" id="oBuilt">&mdash;</div></div>
      <div class="slotbox"><div class="k">Booted from</div><div class="v" id="oRun">&mdash;</div></div>
      <div class="slotbox"><div class="k">Image size</div><div class="v" id="oSize">&mdash;</div></div>
    </div>
  </div>

  <div class="card">
    <h2>Upload new firmware</h2>
    <input type="file" id="ofile" accept=".bin" style="display:none">
    <div class="drop" id="odrop">
      <div class="big" id="odropText">Choose a .bin file, or drop one here</div>
      <div class="sub" id="odropSub">.pio/build/web_dashboard/firmware.bin</div>
    </div>
    <div class="prog" id="oprog"><i id="oprogBar"></i></div>
    <div class="err" id="oerr" style="display:none"></div>
    <div class="actions" style="margin-top:16px">
      <span class="tip" id="oTarget">&mdash;</span>
      <button class="btn primary" id="oupload" disabled>Upload &amp; reboot</button>
    </div>
    <div class="tip" style="margin-top:12px">Upload <b>firmware.bin</b> only. <b>bootloader.bin</b> and
      <b>partitions.bin</b> live outside the app slots and can only be written over USB &mdash; the board
      rejects them here rather than bricking itself. The new image goes into the spare slot, so if the
      upload fails or the power drops halfway, the firmware running right now is untouched and the board
      still boots.</div>
  </div>

  <div class="card">
    <h2>Upload password <label class="sw" id="okeyState">&mdash;</label></h2>
    <div class="field">
      <input id="okey" type="password" maxlength="32" autocomplete="off"
             placeholder="leave empty for no password">
    </div>
    <div class="actions" style="margin-top:14px">
      <span class="tip">Anyone who can open this page can flash the board.</span>
      <button class="btn" id="okeySave">Save password</button>
    </div>
    <div class="tip" style="margin-top:12px">Optional, and off by default so the first update works without
      setting anything up. Worth turning on for a board sharing a network with people who have no business
      reflashing it &mdash; this endpoint hands over code execution, which the rest of this page does not.
      Saving an empty field clears it.</div>
  </div>
</div>

<footer><span id="fw" title="">firmware &mdash;</span></footer>

<div class="toast" id="toast"></div>

<div class="done" id="done">
  <div class="card">
    <h3 id="doneTitle">Rebooting&hellip;</h3>
    <div class="tip" id="doneBody"></div>
  </div>
</div>

<script src="/thermal.js"></script>
<script>
const $ = id => document.getElementById(id);

function toast(msg){
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast._h); toast._h = setTimeout(()=>t.classList.remove('show'), 2400);
}

// ---------------------------------------------------------------- tabs
const TABS = ['sensors','calibration','alerts','wifi','firmware'];

function showTab(id){
  if(TABS.indexOf(id) < 0) id = 'sensors';
  TABS.forEach(t=>$('p-'+t).classList.toggle('on', t === id));
  document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('on', b.dataset.tab === id));
  // replaceState, not location.hash: a reload lands back on the same tab without
  // filling the back button with tab switches
  history.replaceState(null, '', location.pathname + '#' + id);
  id === 'wifi' ? startWifi() : stopWifi();
}
document.querySelectorAll('.tab').forEach(b=>b.onclick = ()=>showTab(b.dataset.tab));

// ---------------------------------------------------------------- sensors
let rows = [];
let tMin = 10, tMax = 80;   // thermal range, overwritten by /api/sensors

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

function loadSensors(){
  fetch('/api/sensors').then(r=>r.json()).then(d=>{
    if(d.fw){
      $('fw').textContent = 'firmware v' + d.fw;
      if(d.build) $('fw').title = 'built ' + d.build;
    }
    if(typeof d.tmin === 'number') tMin = d.tmin;
    if(typeof d.tmax === 'number') tMax = d.tmax;
    // src remembers where this sensor sits in the firmware's slot order, which
    // is what /events is indexed by -- without it the live column would point
    // at the wrong sensor as soon as you move a row.
    rows = (d.sensors||[]).map((s,i)=>({addr:s.addr, name:s.name||'', online:s.online, src:i}));
    render();
  }).catch(()=>toast('Could not read the sensor list'));
}

function saveSensors(){
  syncNames();
  $('save').disabled = true;
  fetch('/api/sensors', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({sensors: rows.map(r=>({addr:r.addr, name:r.name}))})
  }).then(r=>r.json()).then(d=>{
    $('save').disabled = false;
    if(d.ok){ toast('Saved'); loadSensors(); } else { toast('Failed: ' + (d.error||'rejected')); }
  }).catch(()=>{ $('save').disabled = false; toast('Save failed'); });
}

$('save').addEventListener('click', saveSensors);
$('rescan').addEventListener('click', ()=>{
  syncNames();
  fetch('/api/sensors/rescan', {method:'POST'})
    .then(()=>{ toast('Rescanning the bus…'); setTimeout(loadSensors, 2500); })
    .catch(()=>toast('Rescan failed'));
});

// ---------------------------------------------------------------- calibration
function loadCalibration(){
  fetch('/api/calibration').then(r=>r.json()).then(c=>{
    $('kI').value = c.kI; $('kV').value = c.kV; $('kP').value = c.kP;
  }).catch(()=>{});
}

$('saveCalib').addEventListener('click', ()=>{
  const body = { kI: parseFloat($('kI').value), kV: parseFloat($('kV').value), kP: parseFloat($('kP').value) };
  if([body.kI, body.kV, body.kP].some(v=>!isFinite(v) || v <= 0)){
    toast('Each factor must be a number above 0');
    return;
  }
  fetch('/api/calibration', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)})
    .then(r=>toast(r.ok ? 'Calibration saved' : 'Save failed'))
    .catch(()=>toast('Save failed'));
});

$('resetEnergy').addEventListener('click', ()=>{
  if(!confirm('Reset accumulated energy counter to 0?')) return;
  fetch('/api/energy/reset', {method:'POST'})
    .then(()=>toast('Energy counter reset'))
    .catch(()=>toast('Reset failed'));
});

// ---------------------------------------------------------------- alerts
const LIMITS = [
  ['tempOn','limTemp',['tempMax']],
  ['voltOn','limVolt',['voltMin','voltMax']],
  ['freqOn','limFreq',['freqMin','freqMax']],
  ['ampOn', 'limAmp', ['ampMax']],
  ['wattOn','limWatt',['wattMax']]
];

function paintLimits(){
  LIMITS.forEach(([on, box, fields])=>{
    const live = $(on).checked;
    $(box).classList.toggle('off', !live);
    fields.forEach(f=>{ $(f).disabled = !live; });
  });
}
LIMITS.forEach(([on])=>$(on).addEventListener('change', paintLimits));

function alertStatus(d){
  const bits = [];
  bits.push(d.tokenSet ? 'token saved' : 'no token yet');
  bits.push(d.clockOk ? ('clock ' + d.now) : 'clock not synced');
  bits.push(d.sentToday + '/' + d.dailyCap + ' delivered today');
  if(typeof d.lastCode === 'number') bits.push('last push HTTP ' + d.lastCode);
  $('astat').textContent = bits.join(' · ');
  // What LINE actually objected to, in its own words -- an HTTP number on its
  // own sends people to a serial monitor to find out what it meant.
  const err = $('aerr');
  err.textContent = d.lastError || '';
  err.style.display = d.lastError ? 'block' : 'none';
}

function loadAlerts(){
  fetch('/api/alerts').then(r=>r.json()).then(d=>{
    $('aon').checked = d.enabled;
    $('onRecover').checked = d.onRecover;
    $('cooldown').value = d.cooldown;
    $('dailyCap').value = d.dailyCap;
    ['tempOn','voltOn','freqOn','ampOn','wattOn'].forEach(k=>{ $(k).checked = d[k]; });
    ['tempMax','voltMin','voltMax','freqMin','freqMax','ampMax','wattMax'].forEach(k=>{ $(k).value = d[k]; });
    $('ato').value = d.to || '';
    // The board never sends the token back, so the field stays empty and only
    // means something when someone types in it.
    $('atoken').placeholder = d.tokenSet ? 'saved — paste a new one to replace it' : 'paste the channel access token';
    paintLimits();
    alertStatus(d);
  }).catch(()=>toast('Could not read the alert settings'));
}

function saveAlerts(){
  const num = id => parseFloat($(id).value);
  const body = {
    enabled: $('aon').checked,
    onRecover: $('onRecover').checked,
    cooldown: parseInt($('cooldown').value, 10),
    dailyCap: parseInt($('dailyCap').value, 10),
    to: $('ato').value.trim(),
    tempOn: $('tempOn').checked, tempMax: num('tempMax'),
    voltOn: $('voltOn').checked, voltMin: num('voltMin'), voltMax: num('voltMax'),
    freqOn: $('freqOn').checked, freqMin: num('freqMin'), freqMax: num('freqMax'),
    ampOn:  $('ampOn').checked,  ampMax:  num('ampMax'),
    wattOn: $('wattOn').checked, wattMax: num('wattMax')
  };
  if(Object.values(body).some(v=>typeof v === 'number' && !isFinite(v))){
    toast('Every threshold needs a number');
    return;
  }
  if(body.voltMin >= body.voltMax || body.freqMin >= body.freqMax){
    toast('The low limit must be below the high one');
    return;
  }
  if(body.enabled && !body.to){ toast('A destination ID is required'); return; }
  // Only sent when someone actually typed one -- an empty field means keep.
  const tok = $('atoken').value.trim();
  if(tok) body.token = tok;

  $('asave').disabled = true;
  fetch('/api/alerts', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>{
      $('asave').disabled = false;
      if(!d.ok){ toast('Failed: ' + (d.error||'rejected')); return; }
      $('atoken').value = '';
      toast('Alert settings saved');
      loadAlerts();
    }).catch(()=>{ $('asave').disabled = false; toast('Save failed'); });
}

$('asave').addEventListener('click', saveAlerts);
$('atest').addEventListener('click', ()=>{
  $('atest').disabled = true;
  fetch('/api/alerts/test', {method:'POST'}).then(r=>r.json()).then(d=>{
    $('atest').disabled = false;
    // Queued, not delivered: the board answers before the handshake even starts,
    // so the honest thing to report is that it was sent, then show the result.
    toast(d.ok ? 'Test message queued — watch your LINE' : ('Failed: ' + (d.error||'rejected')));
    setTimeout(loadAlerts, 4000);
  }).catch(()=>{ $('atest').disabled = false; toast('Could not reach the board'); });
});

// ---------------------------------------------------------------- firmware
let otaFile = null, otaInfo = {};

function kb(n){ return n >= 1048576 ? (n/1048576).toFixed(2) + ' MB' : Math.round(n/1024) + ' KB'; }

function otaError(msg){
  const e = $('oerr');
  e.textContent = msg || '';
  e.style.display = msg ? 'block' : 'none';
}

function loadOta(){
  fetch('/api/ota').then(r=>r.json()).then(d=>{
    otaInfo = d;
    $('oFw').textContent = 'v' + d.fw;
    $('oBuilt').textContent = d.build;
    $('oRun').textContent = d.running;
    $('oSize').textContent = kb(d.sketch);
    $('oTarget').textContent = 'goes into ' + d.target + ' · ' + kb(d.targetSize) + ' available';
    $('okeyState').textContent = d.keySet ? 'password set' : 'no password';
    $('okey').placeholder = d.keySet ? 'saved — type a new one, or save empty to clear'
                                     : 'leave empty for no password';
  }).catch(()=>{});
}

function pickFile(f){
  otaFile = f || null;
  otaError('');
  if(!otaFile){ $('oupload').disabled = true; return; }
  $('odropText').textContent = otaFile.name;
  $('odropSub').textContent = kb(otaFile.size);
  // Caught here as well as on the board: a 3 MB file would spend a minute
  // uploading before the board could possibly refuse it.
  if(otaInfo.targetSize && otaFile.size > otaInfo.targetSize){
    otaError('That file is ' + kb(otaFile.size) + ', larger than the ' + kb(otaInfo.targetSize) + ' slot');
    $('oupload').disabled = true;
    return;
  }
  if(!/\.bin$/i.test(otaFile.name)){
    otaError('Firmware has to be a .bin file');
    $('oupload').disabled = true;
    return;
  }
  $('oupload').disabled = false;
}

$('odrop').addEventListener('click', ()=>$('ofile').click());
$('ofile').addEventListener('change', e=>pickFile(e.target.files[0]));
['dragenter','dragover'].forEach(ev=>$('odrop').addEventListener(ev, e=>{
  e.preventDefault(); $('odrop').classList.add('over');
}));
['dragleave','drop'].forEach(ev=>$('odrop').addEventListener(ev, e=>{
  e.preventDefault(); $('odrop').classList.remove('over');
}));
$('odrop').addEventListener('drop', e=>pickFile(e.dataTransfer.files[0]));

$('oupload').addEventListener('click', ()=>{
  if(!otaFile) return;
  if(!confirm('Flash ' + otaFile.name + ' and reboot the board?')) return;
  otaError('');
  $('oupload').disabled = true;
  $('oprog').classList.add('on');

  // XHR rather than fetch: only XHR reports upload progress, and a 1.2 MB
  // transfer to an ESP32 is slow enough that a bar is the difference between
  // waiting and pulling the power halfway through.
  const form = new FormData();
  form.append('firmware', otaFile, otaFile.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota');
  const key = $('okey').value;
  if(key) xhr.setRequestHeader('X-OTA-Key', key);
  xhr.upload.onprogress = e=>{
    if(!e.lengthComputable) return;
    const pct = Math.round(e.loaded / e.total * 100);
    $('oprogBar').style.width = pct + '%';
    $('oupload').textContent = 'Uploading ' + pct + '%';
  };
  xhr.onload = ()=>{
    let d = {};
    try{ d = JSON.parse(xhr.responseText); }catch(err){}
    if(xhr.status === 200 && d.ok){
      $('oupload').textContent = 'Rebooting…';
      toast('Flashed — the board is restarting');
      // It comes back on the same address; the page reloads itself once it does.
      setTimeout(waitForBoard, 4000);
    } else {
      $('oprog').classList.remove('on');
      $('oprogBar').style.width = '0';
      $('oupload').textContent = 'Upload & reboot';
      $('oupload').disabled = false;
      otaError(d.error || ('Upload failed (HTTP ' + xhr.status + ')'));
    }
  };
  xhr.onerror = ()=>{
    $('oprog').classList.remove('on');
    $('oupload').textContent = 'Upload & reboot';
    $('oupload').disabled = false;
    otaError('Lost contact with the board during the upload');
  };
  xhr.send(form);
});

function waitForBoard(){
  fetch('/api/ota', {cache:'no-store'})
    .then(r=>r.json())
    .then(()=>location.reload())
    .catch(()=>setTimeout(waitForBoard, 1500));
}

$('okeySave').addEventListener('click', ()=>{
  const key = $('okey').value;
  if(key && key.length < 4){ toast('Use at least 4 characters, or none at all'); return; }
  $('okeySave').disabled = true;
  fetch('/api/ota/key', {method:'POST', headers:{'Content-Type':'application/json'},
                         body: JSON.stringify({key})})
    .then(r=>r.json()).then(()=>{
      $('okeySave').disabled = false;
      $('okey').value = '';
      toast(key ? 'Upload password set' : 'Upload password cleared');
      loadOta();
    }).catch(()=>{ $('okeySave').disabled = false; toast('Could not reach the board'); });
});

// ---------------------------------------------------------------- wi-fi
let info = {}, pollTimer = 0, statusTimer = 0, scanTries = 0, scanned = false;

function startWifi(){
  if(statusTimer) return;
  status();
  statusTimer = setInterval(status, 5000);
  if(!scanned || !$('nets').querySelector('.net')){ scanned = true; scan(false); }
}

function stopWifi(){
  clearInterval(statusTimer); statusTimer = 0;
  clearTimeout(pollTimer);
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
    $('kv').innerHTML = kv.map(()=>'<dt></dt><dd></dd>').join('');
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
  const box = $('nets');
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
  if(force){ scanTries = 0; $('wrescan').disabled = true; }
  fetch('/api/wifi/scan' + (force ? '?force=1' : '')).then(r=>r.json()).then(d=>{
    scanTries = 0;
    if(d.scanning){
      // Keep any list already on screen: blanking it makes a routine rescan
      // look like everything vanished.
      if(!$('nets').querySelector('.net')) $('nets').innerHTML = '<div class="empty">scanning&hellip;</div>';
      pollTimer = setTimeout(()=>scan(false), 1400);
      return;
    }
    $('wrescan').disabled = false;
    if(d.error && !(d.networks||[]).length){
      $('nets').innerHTML = '<div class="empty">' + d.error + ' &mdash; press Scan again</div>';
      return;
    }
    renderNets(d.networks || []);
  }).catch(()=>{
    // A scan takes the radio off the setup AP for seconds at a time, so a
    // dropped poll is the normal case mid-sweep, not a failure. Keep asking.
    if(++scanTries <= 15){ pollTimer = setTimeout(()=>scan(false), 1500); return; }
    $('wrescan').disabled = false;
    $('nets').innerHTML = '<div class="empty">lost contact with the board &mdash; rejoin its Wi-Fi, then press Scan again</div>';
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

$('wrescan').onclick = ()=>scan(true);

$('wsave').onclick = ()=>{
  const ssid = $('ssid').value.trim(), pass = $('pass').value;
  if(!ssid){ toast('Pick a network or type its name'); return; }
  if(pass.length > 0 && pass.length < 8){ toast('A password must be at least 8 characters'); return; }
  $('wsave').disabled = true;
  fetch('/api/wifi', {method:'POST', headers:{'Content-Type':'application/json'},
                      body: JSON.stringify({ssid, pass})})
    .then(r=>r.json()).then(d=>{
      if(!d.ok){ $('wsave').disabled = false; toast('Rejected: ' + (d.error||'unknown')); return; }
      const host = info.host || 'esp32-powermeter';
      finish('Saved — rebooting',
        'The board is restarting and will join <b>' + ssid.replace(/[<>&]/g,'') + '</b>.<br><br>' +
        'Reconnect your phone or laptop to that same network, then open ' +
        '<b>http://' + host + '.local/</b><br><br>' +
        'If it cannot join, the <b>' + (info.ap || 'setup') + '</b> network comes back in about 30 seconds ' +
        'so you can try again. The status LED blinks slowly while it is waiting for you.');
    })
    .catch(()=>{ $('wsave').disabled = false; toast('Could not reach the board'); });
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

// ------------------------------------------------------- live stream (shared)
const es = new EventSource('/events');
es.addEventListener('data', e=>{
  let d; try{ d = JSON.parse(e.data); }catch(err){ return; }

  $('lv').textContent = d.v.toFixed(1);
  $('li').textContent = d.i.toFixed(3);
  $('lp').textContent = d.p.toFixed(1);
  $('eVal').textContent = d.e.toFixed(3) + ' kWh';

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

// /wifi is what the captive portal and setup-mode redirect point at, so that
// path opens on the Wi-Fi tab; everything else starts on Sensors.
showTab((location.hash || '').replace('#','') ||
        (location.pathname.indexOf('/wifi') === 0 ? 'wifi' : 'sensors'));
loadSensors();
loadCalibration();
loadAlerts();
loadOta();
</script>
</body>
</html>
)HTMLPAGE";
