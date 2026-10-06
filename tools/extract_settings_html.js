// Pulls the Settings page out of the firmware exactly as the board serves it.
//
//   node tools/extract_settings_html.js
//
// Writes two files into docs/:
//   settings.html       the raw string in src/02_web_dashboard/settings_html.h,
//                       byte for byte. Opened from disk it shows "loading..."
//                       because /api/* and /events only exist on the board.
//   settings-demo.html  the same markup, CSS and JS, with /thermal.js inlined and
//                       fetch()/EventSource replaced by sample data, so it renders
//                       fully for screenshots in the manual. Nothing in the page's
//                       own code is edited; the shim sits in front of it.

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const read = f => fs.readFileSync(path.join(root, f), 'utf8');

function rawString(src, tag) {
  const open = 'R"' + tag + '(', close = ')' + tag + '"';
  const a = src.indexOf(open), b = src.indexOf(close, a);
  if (a < 0 || b < 0) throw new Error('raw string ' + tag + ' not found');
  return src.slice(a + open.length, b);
}

const html = rawString(read('src/02_web_dashboard/settings_html.h'), 'HTMLPAGE');
const thermal = rawString(read('src/02_web_dashboard/thermal_js.h'), 'JSFILE');
const cfg = read('include/config.h');
const def = name => (cfg.match(new RegExp('#define\\s+' + name + '\\s+"([^"]*)"')) || [])[1] || '';
const fw = def('FIRMWARE_VERSION');
const fwTitle = def('FIRMWARE_TITLE');

fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/settings.html'), html);

// --- demo shim: sample data shaped like the real /api/* replies -----------
const shim = `
// DEMO SHIM: replaces the board's HTTP and SSE endpoints with sample data so this
// file renders when opened from disk. Everything below this block is the page as
// the firmware serves it.
(function(){
  var FW = ${JSON.stringify(fw)}, BUILD = 'Sep 25 2026 10:12:44';
  // All nine slots taken, slot 4 dead, one replacement probe already on the bus:
  // the state the "On the bus, no slot free" card exists for. The firmware only
  // lists a probe as spare once every slot is spoken for (see main.cpp), so the
  // demo has to fill them all to show that card at all.
  var sensors = [
    {slot:0, addr:'28FF3A1C9116047B', name:'Inlet water',   online:true},
    {slot:1, addr:'28FF9D02A116032E', name:'Outlet water',  online:true},
    {slot:2, addr:'28FF6177C4160519', name:'Bearing DE',    online:true},
    {slot:3, addr:'28FF08B5331604C0', name:'Ambient',       online:false},
    {slot:4, addr:'28FF12E04A160388', name:'Bearing NDE',   online:true},
    {slot:5, addr:'28FF7C09D116015D', name:'Motor housing', online:true},
    {slot:6, addr:'28FFB3465E1604F2', name:'Panel air',     online:true},
    {slot:7, addr:'28FF2A9C88160261', name:'Transformer',   online:true},
    {slot:8, addr:'28FFE5D1071603A9', name:'',              online:true}
  ];
  // Every probe answering on the 1-Wire bus right now, with its reading. The
  // dead one (slot 4) is simply absent. Anything here that holds no slot is a spare.
  var bus = {
    '28FF3A1C9116047B':27.6, '28FF9D02A116032E':31.9, '28FF6177C4160519':42.3,
    '28FF12E04A160388':39.8, '28FF7C09D116015D':36.1, '28FFB3465E1604F2':29.4,
    '28FF2A9C88160261':44.7, '28FFE5D1071603A9':28.2,
    '28FFC25E071602A4':33.2   // the replacement probe, just plugged in
  };
  var newProbes = ['28FF4D70B2160117', '28FF9A2E5C1605E3'];   // handed out by Rescan
  function spareList(){ return Object.keys(bus).filter(function(a){ return !sensors.some(function(s){ return s.addr===a; }); }); }
  var calib = {kI:1.0000, kV:1.0030, kP:1.0000};
  var energy = 1234.567;
  var mqtt = {
    enabled:true, host:'thingsboard.cloud', port:1883, user:'A1B2C3D4E5F6G7H8I9J0', clientId:'powermeter-7B',
    pubTopic:'v1/devices/me/telemetry', subTopic:'v1/devices/me/rpc/request/+', attrTopic:'v1/devices/me/attributes',
    interval:30, confirm:2, minInterval:5, dbTemp:1.0, dbAmps:0.5, early:12, passSet:false,
    connected:true, published:1842, failures:0, fwBase:'', fwTitle:${JSON.stringify(fwTitle)}, fw:FW,
    fwMaxTries:1, fwWindow:'closed'
  };
  var wifi = {portal:false, connected:true, host:'powermeter', ap:'PowerMeter-Setup', saved:'SYNTECH-IoT',
              fw:FW, build:BUILD, ssid:'SYNTECH-IoT', ip:'192.168.1.57', rssi:-58};
  var nets = [{ssid:'SYNTECH-IoT',rssi:-58,lock:true},{ssid:'SYNTECH-Office',rssi:-67,lock:true},
              {ssid:'Guest',rssi:-74,lock:false},{ssid:'TP-Link_7F21',rssi:-83,lock:true}];

  function payload(){
    var p = {v:+(229.8+Math.random()*.4-.2).toFixed(1), i:+(1.245+Math.random()*.01-.005).toFixed(3)};
    p.p = +(p.v*p.i*0.99).toFixed(1); p.e = +energy.toFixed(3);
    var jit = function(t){ return +(t+Math.random()*.1-.05).toFixed(2); };
    p.temps = sensors.map(function(s){ return s.addr in bus ? jit(bus[s.addr]) : null; });
    p.stemps = spareList().map(function(a){ return jit(bus[a]); });
    return p;
  }
  function mqttDoc(){
    var d = JSON.parse(JSON.stringify(mqtt)), p = payload();
    d.payload = {voltage:p.v, current:p.i, power:p.p, energy:p.e, rssi:-58};
    d.attrPayload = {fw:FW, build:BUILD, ip:'192.168.1.57'};
    sensors.forEach(function(s,i){ d.payload['temp'+(i+1)] = p.temps[i]; d.attrPayload['temp'+(i+1)+'_name'] = s.name; });
    return d;
  }
  var routes = {
    'GET /api/sensors': function(){
      return {version:3, fw:FW, build:BUILD, max:9, scanMax:16, tmin:20, tmax:50, sensors:sensors,
              spare:spareList().map(function(a){ return {addr:a, temp:bus[a]}; })};
    },
    // Same rule as the board: a slot is online if its address answers on the bus.
    'POST /api/sensors': function(b){
      sensors = b.sensors.map(function(s,i){ return {slot:i, addr:s.addr, name:s.name, online:s.addr in bus}; });
      return {ok:true};
    },
    // Each Rescan "plugs in" one more new probe, so the replacement flow can be
    // walked through more than once without reloading the page.
    'POST /api/sensors/rescan': function(){
      if(!spareList().length && newProbes.length) bus[newProbes.shift()] = 30.5;
      return {ok:true};
    },
    'GET /api/calibration': function(){ return calib; },
    'POST /api/calibration': function(b){ calib = b; return {ok:true}; },
    'POST /api/energy/reset': function(){ energy = 0; return {ok:true}; },
    'GET /api/mqtt': mqttDoc,
    'POST /api/mqtt': function(b){ Object.assign(mqtt, b); return {ok:true}; },
    'GET /api/ota': function(){ return {fw:FW, build:BUILD, running:'app0', target:'app1', targetSize:1966080, sketch:1310720, verify:'confirmed'}; },
    'GET /api/wifi': function(){ return wifi; },
    'GET /api/wifi/scan': function(){ return {scanning:false, networks:nets}; },
    'POST /api/wifi': function(){ return {ok:true}; },
    'POST /api/wifi/forget': function(){ return {ok:true}; }
  };
  window.fetch = function(url, opt){
    var m = ((opt && opt.method) || 'GET').toUpperCase();
    var key = m + ' ' + String(url).split('?')[0];
    var h = routes[key];
    return new Promise(function(res, rej){
      setTimeout(function(){
        if(!h) return rej(new Error('no route ' + key));
        var body = h(opt && opt.body ? JSON.parse(opt.body) : {});
        res({ok:true, status:200, json:function(){ return Promise.resolve(body); }});
      }, 120);
    });
  };
  window.EventSource = function(){
    var ls = {};
    this.addEventListener = function(n, f){ ls[n] = f; };
    this.close = function(){};
    setInterval(function(){ if(ls.data) ls.data({data: JSON.stringify(payload())}); }, 1000);
  };
})();
`;

const tag = '<script src="/thermal.js"></script>';
if (html.indexOf(tag) < 0) throw new Error('thermal.js tag not found');
const demo = html.replace(tag, '<script>' + shim + thermal + '</script>');
fs.writeFileSync(path.join(root, 'docs/settings-demo.html'), demo);

console.log('docs/settings.html      ' + html.length + ' bytes (exact copy of SETTINGS_HTML, firmware v' + fw + ')');
console.log('docs/settings-demo.html ' + demo.length + ' bytes (with sample data shim)');
