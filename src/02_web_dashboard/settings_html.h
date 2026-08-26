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

/* --- toggles, switches and the label/value rows they sit in --- */
input[type=checkbox]{width:auto;margin:0;accent-color:var(--accent)}
.sw{display:inline-flex;align-items:center;gap:7px;margin:0;text-transform:none;letter-spacing:0;
  font-size:.78rem;color:var(--muted);cursor:pointer}
.lim{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;padding:11px 0}
.lim+.lim{border-top:1px solid var(--border)}
.lim .nm{display:flex;align-items:center;gap:9px;font-size:.9rem}
.lim .in{display:flex;gap:7px;align-items:center;color:var(--muted);font-size:.74rem;flex-wrap:wrap;
  justify-content:flex-end}
/* --- Firmware tab --- */
.slots{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:4px}
.slotbox{background:var(--bg);border:1px solid var(--border);border-radius:12px;padding:12px}
.slotbox .k{color:var(--muted);font-size:.7rem;text-transform:uppercase;letter-spacing:.5px}
.slotbox .v{font-size:.95rem;font-weight:700;margin-top:4px;font-variant-numeric:tabular-nums;
  overflow-wrap:anywhere}
.payload{background:var(--bg);border:1px solid var(--border);border-radius:10px;padding:12px 13px;
  font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.72rem;line-height:1.6;
  color:var(--text);white-space:pre-wrap;word-break:break-word;margin:0;max-height:230px;overflow:auto}
.payload .k{color:var(--accent)}
.payload .n{color:var(--muted);font-style:italic}
.payload.empty{color:var(--muted);font-style:italic}
.plabel{color:var(--muted);font-size:.72rem;text-transform:uppercase;letter-spacing:.5px;margin:14px 0 6px}
.plabel:first-child{margin-top:0}
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
  <button class="tab" data-tab="mqtt">&#128225; MQTT</button>
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

<div class="panel" id="p-mqtt">
  <div class="card">
    <h2>Broker <label class="sw"><input type="checkbox" id="mqOn"> publishing enabled</label></h2>
    <div class="tip" style="margin-bottom:14px">Defaults are ThingsBoard's own topics: put the device
      <b>access token</b> in Username and leave Password empty. Any other broker works too &mdash; change
      the topics to whatever it expects.</div>
    <div class="knobs">
      <div class="knob" style="flex:2;min-width:200px"><label for="mqHost">Broker address</label>
        <input id="mqHost" maxlength="60" autocomplete="off" spellcheck="false" placeholder="thingsboard.cloud"></div>
      <div class="knob"><label for="mqPort">Port</label><input id="mqPort" type="number" min="1" max="65535"></div>
      <div class="knob"><label for="mqEvery">Every (s)</label><input id="mqEvery" type="number" min="5" max="3600"></div>
    </div>
    <div class="field" style="margin-top:14px">
      <label for="mqUser">Username &mdash; ThingsBoard device access token</label>
      <input id="mqUser" maxlength="90" autocomplete="off" spellcheck="false">
    </div>
    <div class="field">
      <label for="mqPass">Password</label>
      <input id="mqPass" type="password" maxlength="60" autocomplete="off" placeholder="leave empty for ThingsBoard">
    </div>
    <div class="field">
      <label for="mqCid">Client ID</label>
      <input id="mqCid" maxlength="38" autocomplete="off" spellcheck="false">
    </div>
  </div>

  <div class="card">
    <h2>Topics</h2>
    <div class="field">
      <label for="mqPub">Telemetry &mdash; readings are published here</label>
      <input id="mqPub" maxlength="78" autocomplete="off" spellcheck="false">
    </div>
    <div class="field">
      <label for="mqSub">Commands &mdash; firmware updates arrive here (empty = do not listen)</label>
      <input id="mqSub" maxlength="78" autocomplete="off" spellcheck="false">
    </div>
    <div class="field">
      <label for="mqAttr">Attributes &mdash; version, IP and sensor names on connect (empty = skip)</label>
      <input id="mqAttr" maxlength="78" autocomplete="off" spellcheck="false">
    </div>
    <div class="field">
      <label for="mqFwBase">Firmware download base &mdash; blank = <code>https://&lt;broker host&gt;</code></label>
      <input id="mqFwBase" maxlength="78" autocomplete="off" spellcheck="false"
             placeholder="only for self-hosted, e.g. http://192.168.1.5:8080">
    </div>
    <div class="lim" id="limAuto" style="border-top:1px solid var(--border);margin-top:8px">
      <div class="nm">&#128260; Follows the server's firmware version <b>at every restart</b></div>
      <div class="in" id="mqAutoState">&mdash;</div>
    </div>
    <div class="tip" style="margin-top:6px">There is no switch for this: a board that is told which version it
      should be on takes itself there. The server is already the thing that decides whether an update happens
      at all &mdash; assign a package, or do not.
      <br><br><b>The board only looks once, on the first broker connection after the power comes on or the
      reset button is pressed.</b> After that it stops asking and ignores a target pushed at it &mdash; a Wi-Fi
      link that drops and comes back never starts a download, so a flaky line cannot flash and reboot the meter
      unattended. A package assigned while the board is running is taken at the next restart. An
      <b>fwUpdate</b> command is never held back: that one is somebody deliberately asking for it.
      <br><br>Works straight out of <b>ThingsBoard &rarr; Advanced features &rarr;
      OTA updates</b>: upload a package there, assign it to this device, and ThingsBoard publishes
      <b>fw_title</b> and <b>fw_version</b> as shared attributes. If the version differs from the one running,
      the board downloads the package <b>from ThingsBoard itself</b> using its access token &mdash; nothing has
      to be hosted anywhere. A package created with <i>Use external URL</i> sends <b>fw_url</b> instead and that
      is used as given. On a plain broker, publish <b>fw_target</b> + <b>fw_url</b> to the attributes topic by
      hand (set the retain flag, or the board forgets it on the next reconnect).
      <br><br>The package <b>Title</b> in ThingsBoard must match <span id="mqFwTitle">this firmware's title</span>
      exactly &mdash; ThingsBoard treats title and version together, and a mismatched title just means nothing
      ever happens.
      <br><br><b>A version is only ever attempted once.</b> If the board flashes it and comes back still on the
      old version &mdash; a stale .bin, a version that was never bumped &mdash; it stops and reports
      <b>fw_state: FAILED</b> instead of flashing in a loop for ever. Change the version to try again.</div>

    <div class="tip" style="margin-top:12px">A command is JSON with a URL in it, either ThingsBoard RPC
      <b>{"method":"fwUpdate","params":{"url":"http://&hellip;/firmware.bin"}}</b> or plain
      <b>{"url":"http://&hellip;/firmware.bin"}</b>. The board answers straight away, downloads, flashes the
      spare slot and reboots, then reports <b>fw_state</b> as telemetry &mdash; an RPC call would time out
      long before a 1.2 MB download finishes, so the outcome cannot come back as the reply.</div>
    <div class="actions" style="margin-top:16px">
      <span class="tip" id="mqStat">&mdash;</span>
      <button class="btn primary" id="mqSave">Save &amp; reconnect</button>
    </div>
    <div class="err" id="mqErr" style="display:none"></div>
  </div>

  <div class="card">
    <h2>What gets published</h2>
    <div class="tip" style="margin-bottom:12px">Built the same way the real publish builds it, and shown
      whether or not a broker is connected &mdash; so you can see what will go out before pointing the board
      at anything. Refreshes while this tab is open.</div>
    <div class="plabel">Telemetry &mdash; every <span id="pEvery">30</span>s</div>
    <pre class="payload" id="pTele">&mdash;</pre>
    <div class="plabel">Attributes &mdash; once per connect</div>
    <pre class="payload" id="pAttr">&mdash;</pre>
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
      <div class="slotbox"><div class="k">Rollback</div><div class="v" id="oVerify">&mdash;</div></div>
    </div>
  </div>

  <div class="card">
    <h2>How this board is updated</h2>
    <div class="tip">Firmware comes from <b>ThingsBoard only</b> &mdash; there is no upload from this page
      and no password to keep. Upload a package under <b>Advanced features &rarr; OTA updates</b> and assign it
      to this device; the board sees the version, downloads the image itself and flashes it. No switch to turn
      on, nothing to press here. An <code>fwUpdate</code> RPC carrying a <code>url</code> does the same thing
      on a plain broker. Flashing over USB (<code>pio run -t upload</code>) still works with the board in hand.</div>
    <div class="tip" style="margin-top:12px">A freshly flashed image starts <b>on probation</b>: the
      bootloader keeps the old one in the other slot and puts it back unless this one runs for
      <b>two minutes with Wi-Fi up</b>. So an update that boots into a crash loop, or comes up unable to
      reach the network, undoes itself with nobody present. Careful: a power cut inside that window looks
      the same from the outside, and reverts an update that was actually fine.</div>
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
const TABS = ['sensors','calibration','mqtt','wifi','firmware'];

let current = 'sensors';

function showTab(id){
  if(TABS.indexOf(id) < 0) id = 'sensors';
  current = id;
  TABS.forEach(t=>$('p-'+t).classList.toggle('on', t === id));
  document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('on', b.dataset.tab === id));
  // replaceState, not location.hash: a reload lands back on the same tab without
  // filling the back button with tab switches
  history.replaceState(null, '', location.pathname + '#' + id);
  id === 'wifi' ? startWifi() : stopWifi();
  // Both carry live state worth refetching on the way in rather than showing
  // whatever was true minutes ago. MQTT refreshes its status line ONLY: the form
  // was filled at page load and may be half-edited by now.
  if(id === 'mqtt') pollMqtt();
  if(id === 'firmware') loadOta();
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

// ---------------------------------------------------------------- mqtt
// Pretty-printed with the keys picked out, because the whole point is reading it
// at a glance. textContent everywhere it could carry operator text -- a sensor
// name goes into the attributes payload, and that is user input.
function renderPayload(el, obj){
  // A newline built rather than typed: this file is a C++ raw string literal,
  // and an escape here is one more thing between the source and the browser.
  const NL = String.fromCharCode(10);
  if(!obj){ el.className = 'payload empty'; el.textContent = 'nothing yet'; return; }
  el.className = 'payload';
  el.textContent = '';
  Object.keys(obj).forEach((k, i, all)=>{
    const key = document.createElement('span');
    key.className = 'k';
    key.textContent = '  "' + k + '"';
    el.appendChild(document.createTextNode(i === 0 ? '{' + NL : ''));
    el.appendChild(key);
    const v = obj[k];
    const val = document.createElement('span');
    if(v === null){ val.className = 'n'; val.textContent = ': null'; }
    else val.textContent = ': ' + (typeof v === 'string' ? '"' + v + '"' : v);
    el.appendChild(val);
    el.appendChild(document.createTextNode((i < all.length - 1 ? ',' : '') + NL));
  });
  el.appendChild(document.createTextNode('}'));
}

function mqttStatus(d){
  $('pEvery').textContent = d.interval;
  $('limAuto').classList.toggle('off', !d.attrTopic);
  if(d.fwTitle) $('mqFwTitle').textContent = '"' + d.fwTitle + '"';
  const detail = d.fwNote ? d.fwNote
               : d.fwTried ? ('last tried ' + d.fwTried)
               : ('running v' + (d.fw || '?'));
  const win = d.fwWindow === 'open'   ? 'checks once on the next connect'
            : d.fwWindow === 'closed' ? 'window closed until the next restart' : '';
  $('mqAutoState').textContent = [detail, win].filter(Boolean).join(' · ');
  renderPayload($('pTele'), d.payload);
  renderPayload($('pAttr'), d.attrPayload);
  const bits = [];
  bits.push(d.enabled ? (d.connected ? 'connected' : 'not connected') : 'disabled');
  bits.push(d.published + ' published');
  if(d.failures) bits.push(d.failures + ' failed');
  $('mqStat').textContent = bits.join(' · ');
  const e = $('mqErr');
  e.textContent = d.error || '';
  e.style.display = d.error ? 'block' : 'none';
}

// Fills the form. Only ever called when there cannot be an edit in progress:
// on page load and straight after a save. Calling it on a timer would wipe
// whatever the operator was halfway through typing.
function loadMqtt(){
  fetch('/api/mqtt').then(r=>r.json()).then(d=>{
    $('mqOn').checked = d.enabled;
    $('mqHost').value = d.host || '';
    $('mqPort').value = d.port;
    $('mqEvery').value = d.interval;
    $('mqUser').value = d.user || '';
    $('mqCid').value = d.clientId || '';
    $('mqPub').value = d.pubTopic || '';
    $('mqSub').value = d.subTopic || '';
    $('mqAttr').value = d.attrTopic || '';
    $('mqFwBase').value = d.fwBase || '';
    $('mqPass').placeholder = d.passSet ? 'saved — type a new one to replace it'
                                        : 'leave empty for ThingsBoard';
    mqttStatus(d);
  }).catch(()=>{});
}

// The polling counterpart: reads the same endpoint but touches nothing except
// the status line, so it is safe to run while someone is filling the form in.
function pollMqtt(){
  fetch('/api/mqtt').then(r=>r.json()).then(mqttStatus).catch(()=>{});
}

$('mqSave').addEventListener('click', ()=>{
  const body = {
    enabled: $('mqOn').checked,
    host: $('mqHost').value.trim(),
    port: parseInt($('mqPort').value, 10) || 1883,
    interval: parseInt($('mqEvery').value, 10) || 30,
    user: $('mqUser').value.trim(),
    clientId: $('mqCid').value.trim(),
    pubTopic: $('mqPub').value.trim(),
    subTopic: $('mqSub').value.trim(),
    attrTopic: $('mqAttr').value.trim(),
    fwBase: $('mqFwBase').value.trim()
  };
  if(body.enabled && !body.host){ toast('A broker address is required'); return; }
  if(!body.attrTopic){ toast('An attributes topic is required -- it is how firmware versions arrive'); return; }
  if(!body.pubTopic){ toast('A telemetry topic is required'); return; }
  const pass = $('mqPass').value;
  if(pass) body.pass = pass;

  $('mqSave').disabled = true;
  fetch('/api/mqtt', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>{
      $('mqSave').disabled = false;
      if(!d.ok){ toast('Failed: ' + (d.error||'rejected')); return; }
      $('mqPass').value = '';
      toast('Saved — reconnecting to the broker');
      // Refill now, while nobody can be mid-edit, to pick up anything the board
      // normalised -- an empty client ID comes back filled in, for one.
      loadMqtt();
      // Then just the status: the task rebuilds the link on its next pass, and
      // asking any sooner always answers "not connected".
      setTimeout(pollMqtt, 3000);
    }).catch(()=>{ $('mqSave').disabled = false; toast('Save failed'); });
});

// Status only, and only while the tab is open -- a poll every few seconds on a
// page nobody is looking at is a request the board has better things to do than
// answer.
setInterval(()=>{ if(current === 'mqtt') pollMqtt(); }, 5000);

// ---------------------------------------------------------------- firmware
// Read-only: what is running and whether the bootloader has accepted it. The
// image itself arrives from ThingsBoard, so there is nothing to submit here.
function kb(n){ return n >= 1048576 ? (n/1048576).toFixed(2) + ' MB' : Math.round(n/1024) + ' KB'; }

function loadOta(){
  fetch('/api/ota').then(r=>r.json()).then(d=>{
    $('oFw').textContent = 'v' + d.fw;
    $('oBuilt').textContent = d.build;
    $('oRun').textContent = d.running;
    $('oSize').textContent = kb(d.sketch);
    // "on probation" is the state worth noticing: the bootloader will put the
    // previous image back if this one does not survive its window.
    $('oVerify').textContent = d.verify === 'pending'
        ? ('on probation · ' + (d.verifyLeft || 0) + 's left')
        : d.verify === 'confirmed' ? 'confirmed' : '—';
    $('oVerify').style.color = d.verify === 'pending' ? 'var(--warn)' : '';
  }).catch(()=>{});
}

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
loadMqtt();
loadOta();
</script>
</body>
</html>
)HTMLPAGE";
