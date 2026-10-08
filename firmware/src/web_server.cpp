#include "web_server.h"
#include "settings.h"
#include "ais.h"
#include "net_task.h"
#include "notify.h"
#include "stats.h"
#include "ntp.h"
#include "ui/ui_main.h"
#include "ui/ui_radar.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "net_util.h"
#include "ports.h"

static WebServer g_srv(80);
static bool      g_started = false;
static uint32_t  g_ota_until = 0;
static const char* HOSTNAME = "esp32-shipradar";

const char* web_server_hostname() { return HOSTNAME; }
bool web_server_started() { return g_started; }
void web_server_arm_ota(uint32_t seconds) { g_ota_until = millis() + seconds * 1000UL; }
bool web_server_ota_armed() { return g_ota_until && (int32_t)(g_ota_until - millis()) > 0; }

static bool auth() {
    const Settings& s = settings_get();
    if (!s.panel_pass[0]) return true;
    if (g_srv.authenticate("admin", s.panel_pass)) return true;
    g_srv.requestAuthentication(BASIC_AUTH, "esp32-shipradar");
    return false;
}

// ── embedded panel page ──────────────────────────────────────────────────────
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Ship Radar</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"><script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script><style>
body{margin:0;background:#000;color:#33d650;font:14px/1.4 system-ui,sans-serif}a{color:#3df25a}
header{padding:10px 14px;border-bottom:1px solid #1fa84a;display:flex;gap:14px;align-items:center;flex-wrap:wrap}
header h1{font-size:18px;margin:0;color:#3df25a}nav button{background:#07200f;color:#3df25a;border:1px solid #1fa84a;border-radius:6px;padding:6px 12px;margin-right:6px}
nav button.on{background:#1fa84a;color:#000}section{padding:12px 14px;display:none}section.on{display:block}
table{border-collapse:collapse;width:100%}td,th{padding:4px 6px;border-bottom:1px solid #0e5a27;text-align:left;white-space:nowrap}th{color:#1e9a45;font-weight:normal}
tr.emg td{color:#ff3b3b}tr.watch td{color:#ffe44d}tr.mil td{color:#ffa030}
label{display:block;margin:8px 0 2px;color:#1e9a45}input,select{width:100%;max-width:420px;background:#04120a;color:#33d650;border:1px solid #1fa84a;border-radius:4px;padding:6px;box-sizing:border-box}
input[type=checkbox]{width:auto}.row{display:flex;gap:16px;flex-wrap:wrap}.row>div{flex:1;min-width:220px}
button.save{margin-top:14px;background:#1fa84a;color:#000;border:0;border-radius:6px;padding:10px 18px;font-weight:bold}
.muted{color:#1e9a45}.kv span{display:inline-block;min-width:160px;color:#1e9a45}pre{white-space:pre-wrap;color:#33d650}
img.shot{border:1px solid #1fa84a;image-rendering:pixelated;width:272px;height:480px}
#map{height:calc(100vh - 70px);min-height:420px;background:#000}.leaflet-tile{filter:invert(1) hue-rotate(90deg) saturate(.4) brightness(.75)}
.sh{color:#8cff7a;font-size:16px;line-height:16px;text-shadow:0 0 3px #000}.sh.sel{color:#ffe44d}.sh.watch{color:#ffe44d}.sh.tank{color:#ffa030}.sh.spec{color:#ff3b3b}.sh.still{color:#5e8a6a}
.shlbl{color:#33d650;font:11px system-ui;white-space:nowrap;text-shadow:0 0 3px #000;margin-left:12px}.port{color:#1e9a45;font:11px system-ui;white-space:nowrap}
.leaflet-popup-content-wrapper,.leaflet-popup-tip{background:#04120a;color:#33d650;border:1px solid #1fa84a}
</style></head><body>
<header><h1>ESP32 Ship Radar</h1><nav><button data-s="live" class="on">Live</button><button data-s="map">Map</button><button data-s="alerts">Alerts</button><button data-s="stats">Stats</button><button data-s="settings">Settings</button><button data-s="api">API</button></nav><span id="hdr" class="muted"></span></header>
<section id="live" class="on"><div class="row"><div><div class="kv" id="summary"></div><table id="tbl"><thead><tr><th>Name</th><th>MMSI</th><th>Type</th><th>Flag</th><th>Len m</th><th>Dest</th><th>Spd kt</th><th>COG</th><th>Dist km</th><th>Brg</th><th>Status</th><th>Class</th></tr></thead><tbody></tbody></table></div>
<div style="flex:0 0 290px"><img class="shot" id="shot" alt="screen"><br><button onclick="document.getElementById('shot').src='/screen.bmp?'+Date.now()">Refresh screenshot</button></div></div></section>
<section id="map" style="padding:0"><div id="map"></div></section>
<section id="alerts"><table id="atbl"><thead><tr><th>Time</th><th>Kind</th><th>Title</th><th>Message</th></tr></thead><tbody></tbody></table></section>
<section id="stats"><div id="stbox"></div></section>
<section id="settings"><form id="f" onsubmit="return save(event)"><div class="row">
<div><h3>Location</h3><label>Mode</label><select name="loc_mode"><option value="0">Auto (public IP)</option><option value="1">City</option><option value="2">Coordinates</option></select>
<label>City</label><input name="city"><label>Country code (ISO-2)</label><input name="country_cc" maxlength="2"><label>Latitude</label><input name="lat" type="number" step="0.0001"><label>Longitude</label><input name="lon" type="number" step="0.0001">
<h3>Radar</h3><label>Range km</label><select name="range_km"><option>5</option><option>10</option><option>20</option><option>30</option><option>50</option><option>75</option><option>100</option><option>150</option><option>200</option></select>
<label>Update every (s)</label><select name="update_s"><option>2</option><option>3</option><option>5</option><option>10</option><option>15</option><option>30</option><option>60</option></select>
<label>Distances</label><select name="units"><option value="0">Kilometres</option><option value="1">Nautical miles</option></select>
<label>Own AIS receiver (host:port of an NMEA TCP stream, blank = Kystverket feed)</label><input name="local_url" placeholder="192.168.1.50:10110">
<label>Approach alert radius km (0 = off)</label><input name="alert_km" type="number" min="0" max="10">
<label>Brightness 10-255</label><input name="brightness" type="number" min="10" max="255">
<label><input type="checkbox" name="sweep"> Sweep</label><label><input type="checkbox" name="trails"> Trails</label><label><input type="checkbox" name="labels"> Labels</label><label><input type="checkbox" name="hide_ground"> Hide stationary vessels</label><label><input type="checkbox" name="auto_range"> Auto range</label><label><input type="checkbox" name="night_dim"> Dim at night</label><label><input type="checkbox" name="alert_bright"> Full brightness on alert</label><label><input type="checkbox" name="map_underlay"> Map underlay</label><label><input type="checkbox" name="airports"> Harbour markers</label><label><input type="checkbox" name="highlight_mil"> Highlight tankers</label>
<h3>Classes shown</h3><label><input type="checkbox" name="cls0"> Cargo</label><label><input type="checkbox" name="cls1"> Tankers</label><label><input type="checkbox" name="cls2"> Passenger / ferries</label><label><input type="checkbox" name="cls3"> Fishing</label><label><input type="checkbox" name="cls4"> Pleasure craft</label><label><input type="checkbox" name="cls5"> Other</label></div>
<div><h3>Wi-Fi</h3><label>SSID</label><input name="wifi_ssid" list="ssids"><datalist id="ssids"></datalist><button type="button" onclick="scan()">Scan networks</button><label>Password (leave blank to keep)</label><input name="wifi_password" type="password">
<h3>Integrations</h3><label>Watchlist (comma-separated name / MMSI / call sign prefixes)</label><input name="watchlist" placeholder="COLOR,257,LAJT">
<label>ntfy.sh topic</label><input name="ntfy_topic"><label>Webhook URL</label><input name="webhook_url"><label>MQTT broker URI</label><input name="mqtt_uri" placeholder="mqtt://user:pass@192.168.1.50:1883">
<label><input type="checkbox" name="notify_emergency"> Push SAR / military / police vessels</label><label><input type="checkbox" name="notify_watch"> Push watchlist</label><label><input type="checkbox" name="notify_alert"> Push approach alerts</label>
<h3>Panel</h3><label>Panel password (user admin, blank = open)</label><input name="panel_pass" type="password">
<h3>Firmware update</h3><p class="muted" id="otastate"></p><input type="file" id="fw" accept=".bin"><button type="button" onclick="ota()">Upload firmware</button><p class="muted">OTA must be armed from the device (Info page) first. Use firmware.bin, not the merged installer image.</p>
</div></div><button class="save">Save settings</button> <span id="msg" class="muted"></span></form></section>
<section id="api"><pre>GET  /api/state      live JSON: vessels, feed stats, weather, location
GET  /api/config     settings (passwords omitted)
POST /api/config     JSON subset of settings; saves, applies, re-locates when needed
GET  /api/alerts     alert log JSON
GET  /api/stats      session / daily statistics JSON
GET  /api/wifi/scan  nearby networks JSON
GET  /api/ports      harbour markers JSON
GET  /screen.bmp     live screenshot (272x480)
GET  /metrics        Prometheus metrics
POST /ota            firmware.bin upload (403 unless armed on the device)
POST /api/action     {"action":"locate"|"reboot"|"reset_stats"|"page","page":0-4}</pre></section>
<script>
const $=s=>document.querySelector(s);document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.remove('on'));document.querySelectorAll('section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.s).classList.add('on');if(b.dataset.s=='alerts')alerts();if(b.dataset.s=='stats')stats();if(b.dataset.s=='settings')cfg();if(b.dataset.s=='map')initMap();});
let S=null,SEL=null,MAP=null,LAYER=null,PORTS=[];
async function live(){try{const d=await (await fetch('/api/state')).json();S=d;if(MAP)drawMap();$('#hdr').textContent=d.location.place+' · '+d.stats.count+' vessels · '+d.stats.source+' '+(d.stats.connected?'connected':'down')+' · '+(d.weather.valid?d.weather.temp+'°C '+d.weather.cond:'');
if(!$('#shot').src)$('#shot').src='/screen.bmp';$('#summary').innerHTML=`<div><span>Location</span>${d.location.place} (${d.location.lat.toFixed(4)}, ${d.location.lon.toFixed(4)})</div><div><span>Wind</span>${d.weather.valid?d.weather.wind+' m/s '+d.weather.wind_dir+' gust '+d.weather.gust:'-'}</div><div><span>Feed</span>${d.stats.msgs_per_s.toFixed(0)} msg/s, last message ${d.stats.age_s}s ago, ${d.stats.names_cached} names cached</div>`;
const tb=$('#tbl tbody');tb.innerHTML='';d.vessels.forEach(a=>{const tr=document.createElement('tr');tr.className=a.special?'emg':a.watch?'watch':a.cls=='tanker'?'mil':'';tr.onclick=()=>{SEL=a.mmsi;document.querySelector('nav button[data-s=map]').click();};tr.innerHTML=`<td>${a.name||('MMSI '+a.mmsi)}</td><td>${a.mmsi}</td><td>${a.type}</td><td>${a.flag}</td><td>${a.length||''}</td><td>${a.dest}</td><td>${a.sog_kt.toFixed(1)}</td><td>${a.cog.toFixed(0)}</td><td>${a.dist_km.toFixed(1)}</td><td>${a.bearing.toFixed(0)}</td><td>${a.status}</td><td>${a.cls}</td>`;tb.appendChild(tr);});}catch(e){}}
async function alerts(){const d=await (await fetch('/api/alerts')).json();const tb=$('#atbl tbody');tb.innerHTML='';d.forEach(a=>{const tr=document.createElement('tr');tr.innerHTML=`<td>${a.time}</td><td>${a.kind}</td><td>${a.title}</td><td>${a.message}</td>`;tb.appendChild(tr);});}
async function stats(){const d=await (await fetch('/api/stats')).json();let h=`<div class="kv"><div><span>Unique today</span>${d.unique_today}</div><div><span>Max tracked</span>${d.max_tracked}</div><div><span>Fastest</span>${d.max_sog_who} ${d.max_sog_kt.toFixed(1)} kt</div><div><span>Largest</span>${d.max_len_who} ${d.max_len} m</div><div><span>Closest moving</span>${d.min_dist_who} ${d.min_dist_km.toFixed(1)} km</div><div><span>Special / alerts</span>${d.specials} / ${d.alerts}</div><div><span>Boots</span>${d.boot_count}</div></div><h3>Per hour</h3><pre>`;d.hourly.forEach((v,i)=>{h+=String(i).padStart(2,'0')+' '+'#'.repeat(Math.min(v,60))+' '+v+'\n'});h+='</pre><h3>Types</h3><pre>'+d.classes.map(a=>a.key+' '+a.count).join('\n')+'</pre><h3>Flags</h3><pre>'+d.flags.map(a=>a.key+' '+a.count).join('\n')+'</pre>';$('#stbox').innerHTML=h;}
async function cfg(){const d=await (await fetch('/api/config')).json();const f=$('#f');for(const k in d){const el=f.elements[k];if(!el||el.type=='file'||el.tagName=='BUTTON')continue;if(el.type=='checkbox')el.checked=!!d[k];else el.value=d[k];}for(let i=0;i<6;i++)f.elements['cls'+i].checked=!!(d.class_mask&(1<<i));$('#otastate').textContent=d.ota_armed?'OTA armed':'OTA locked (arm it on the device: Info page)';}
async function save(e){e.preventDefault();const f=$('#f');const o={};for(const el of f.elements){if(!el.name)continue;if(el.name.startsWith('cls'))continue;if(el.type=='checkbox')o[el.name]=el.checked;else if(el.value!=='')o[el.name]=isNaN(el.value)||el.name=='city'||el.name=='country_cc'||el.name.includes('url')||el.name.includes('_')&&typeof el.value=='string'&&!/^-?\d+(\.\d+)?$/.test(el.value)?el.value:Number(el.value);}
let m=0;for(let i=0;i<6;i++)if(f.elements['cls'+i].checked)m|=1<<i;o.class_mask=m;const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});$('#msg').textContent=await r.text();}
async function scan(){$('#msg').textContent='scanning...';const d=await (await fetch('/api/wifi/scan')).json();const dl=$('#ssids');dl.innerHTML='';d.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' dBm';dl.appendChild(o);});$('#msg').textContent=d.length+' networks found - pick one in the SSID field';}
async function ota(){const fl=$('#fw').files[0];if(!fl){alert('choose firmware.bin');return;}const fd=new FormData();fd.append('firmware',fl);$('#msg').textContent='uploading '+fl.size+' bytes...';const r=await fetch('/ota',{method:'POST',body:fd});$('#msg').textContent=await r.text();}

async function initMap(){if(MAP){setTimeout(()=>MAP.invalidateSize(),50);drawMap();return;}if(!window.L){$('#map').textContent='Leaflet failed to load (no internet?)';return;}
const c=S?[S.location.lat,S.location.lon]:[60,10];MAP=L.map('map').setView(c,9);L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:18,attribution:'&copy; OpenStreetMap contributors'}).addTo(MAP);
LAYER=L.layerGroup().addTo(MAP);if(!PORTS.length)PORTS=await (await fetch('/api/ports')).json();PORTS.forEach(p=>L.marker([p.lat,p.lon],{icon:L.divIcon({className:'port',html:'&#9632; '+p.name})}).addTo(MAP));drawMap();}
function drawMap(){if(!MAP||!S)return;LAYER.clearLayers();const c=[S.location.lat,S.location.lon];L.circleMarker(c,{radius:5,color:'#3df25a'}).addTo(LAYER);L.circle(c,{radius:(S.stats.range_km||50)*1000,color:'#1fa84a',weight:1,fill:false}).addTo(LAYER);
S.vessels.forEach(a=>{const sel=a.mmsi===SEL,moving=a.sog_kt>=0.5;const cls=sel?'sel':a.special?'spec':a.watch?'watch':a.cls=='tanker'?'tank':moving?'':'still';const crs=(a.heading!==511&&a.heading!==undefined)?a.heading:a.cog;
const html=moving?`<span class="sh ${cls}" style="display:inline-block;transform:rotate(${crs}deg)">&#11165;</span>`:`<span class="sh ${cls}">&#9670;</span>`;
const m=L.marker([a.lat,a.lon],{icon:L.divIcon({className:'',html:html+`<span class="shlbl">${a.name||a.mmsi}</span>`,iconSize:[16,16],iconAnchor:[8,8]})}).addTo(LAYER);
m.bindPopup(`<b>${a.name||('MMSI '+a.mmsi)}</b> ${a.flag}<br>${a.type}${a.length?' '+a.length+' m':''} ${a.callsign}<br>${a.dest?'to '+a.dest+'<br>':''}${a.sog_kt.toFixed(1)} kt, cog ${a.cog.toFixed(0)}°, ${a.status}<br>${a.dist_km.toFixed(1)} km, brg ${a.bearing.toFixed(0)}°`);m.on('click',()=>{SEL=a.mmsi;drawMap();});
if(a.trail&&a.trail.length)L.polyline([...a.trail,[a.lat,a.lon]],{color:sel?'#ffe44d':'#1fa84a',weight:sel?2:1,opacity:.8}).addTo(LAYER);});}
live();setInterval(live,5000);if(location.hash){const b=document.querySelector('nav button[data-s='+location.hash.slice(1)+']');if(b)setTimeout(()=>b.click(),300);}
</script></body></html>)HTML";

// ── JSON helpers ─────────────────────────────────────────────────────────────
// Serialize into PSRAM (a String would not fit internal RAM for big states), then send in one piece.
static PsramBuffer g_json;
static void send_json(JsonDocument& doc) {
    g_json.clear();
    serializeJson(doc, g_json);
    g_srv.sendHeader("Access-Control-Allow-Origin", "*");
    g_srv.setContentLength(g_json.size());
    g_srv.send(200, "application/json", "");
    g_srv.sendContent(g_json.data(), g_json.size());
}

static void handle_state() {
    if (!auth()) return;
    const Settings& s = settings_get();
    JsonDocument doc(&g_psram_alloc);
    JsonObject loc = doc["location"].to<JsonObject>();
    loc["place"] = s.place; loc["lat"] = s.lat; loc["lon"] = s.lon; loc["mode"] = s.loc_mode;
    const LocalInfo& li = net_local_info();
    JsonObject wx = doc["weather"].to<JsonObject>();
    wx["valid"] = li.valid; wx["temp"] = li.temp_c; wx["cond"] = geo_wmo_short(li.wmo_code); wx["wind"] = li.wind_mps; wx["gust"] = li.gust_mps; wx["wind_dir"] = ais_compass16(li.wind_dir); wx["tz"] = li.tz_name;
    ais_lock();
    const AisStats& st = ais_stats();
    JsonObject js = doc["stats"].to<JsonObject>();
    js["count"] = st.count; js["moving"] = st.moving; js["alerts"] = st.alerts; js["watch"] = st.watch_count; js["source"] = st.source; js["connected"] = st.connected;
    js["age_s"] = st.last_msg_ms ? (millis() - st.last_msg_ms) / 1000 : -1; js["msgs_per_s"] = st.msgs_per_s; js["msgs_total"] = st.msgs_total; js["names_cached"] = st.cache_names; js["reconnects"] = st.reconnects;
    js["heap_kb"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024; js["uptime_s"] = millis() / 1000; js["fw"] = FW_VERSION; js["range_km"] = s.range_km;
    JsonArray arr = doc["vessels"].to<JsonArray>();
    Vessel* vs = ais_list(); int n = ais_count();
    for (int i = 0; i < n; i++) {
        const Vessel& v = vs[i];
        if (!v.mmsi || !ais_visible(v)) continue;
        JsonObject o = arr.add<JsonObject>();
        o["mmsi"] = v.mmsi; o["name"] = v.name; o["callsign"] = v.callsign; o["type"] = ais_type_name(v.shiptype); o["type_code"] = v.shiptype; o["cls"] = ais_class_name(v.cls);
        o["flag"] = ais_flag(v.mmsi); o["class_b"] = v.class_b; o["lat"] = v.lat; o["lon"] = v.lon; o["sog_kt"] = v.sog_kt; o["cog"] = v.cog < 360 ? v.cog : 0; o["heading"] = v.heading;
        o["length"] = v.length; o["width"] = v.width; o["draught"] = v.draught; o["dest"] = v.dest; o["status"] = ais_navstat_name(v.navstat);
        o["dist_km"] = v.dist_km; o["bearing"] = v.bearing; o["cpa_km"] = v.cpa_km; o["cpa_min"] = v.cpa_min; o["alert"] = v.alert; o["watch"] = v.watch; o["special"] = ais_is_special(v.shiptype);
        o["age_s"] = (millis() - v.last_pos_ms) / 1000;
        JsonArray tr = o["trail"].to<JsonArray>();
        for (int j = 0; j < v.trail_n; j++) { int idx = (v.trail_head - v.trail_n + j + TRAIL_LEN) % TRAIL_LEN; JsonArray p = tr.add<JsonArray>(); p.add(v.trail_lat[idx]); p.add(v.trail_lon[idx]); }
    }
    ais_unlock();
    send_json(doc);
}

static void config_to_json(JsonDocument& doc) {
    const Settings& s = settings_get();
    doc["wifi_ssid"] = s.wifi_ssid; doc["loc_mode"] = s.loc_mode; doc["city"] = s.city; doc["country_cc"] = s.country_cc; doc["place"] = s.place;
    doc["lat"] = s.lat; doc["lon"] = s.lon; doc["range_km"] = s.range_km; doc["update_s"] = s.update_s; doc["units"] = s.units; doc["source"] = s.source;
    doc["sweep"] = s.sweep; doc["trails"] = s.trails; doc["labels"] = s.labels; doc["hide_ground"] = s.hide_ground; doc["auto_range"] = s.auto_range; doc["night_dim"] = s.night_dim;
    doc["highlight_mil"] = s.highlight_mil; doc["brightness"] = s.brightness; doc["alert_km"] = s.alert_km; doc["alert_bright"] = s.alert_bright; doc["map_underlay"] = s.map_underlay; doc["airports"] = s.airports;
    doc["watchlist"] = s.watchlist; doc["ntfy_topic"] = s.ntfy_topic; doc["webhook_url"] = s.webhook_url; doc["mqtt_uri"] = s.mqtt_uri[0] ? "(set)" : ""; doc["local_url"] = s.local_url;
    doc["class_mask"] = s.class_mask; doc["notify_emergency"] = s.notify_emergency; doc["notify_watch"] = s.notify_watch; doc["notify_alert"] = s.notify_alert;
    doc["panel_pass_set"] = s.panel_pass[0] != 0; doc["ota_armed"] = web_server_ota_armed(); doc["fw"] = FW_VERSION; doc["hostname"] = HOSTNAME;
}
static void handle_config_get() { if (!auth()) return; JsonDocument doc(&g_psram_alloc); config_to_json(doc); send_json(doc); }

static void handle_config_post() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_srv.arg("plain"))) { g_srv.send(400, "text/plain", "bad json"); return; }
    Settings& s = settings_get();
    bool relocate = false, reconnect = false, redraw = false;
    auto str = [&](const char* k, char* dst, size_t n) { if (doc[k].is<const char*>()) strlcpy(dst, doc[k] | "", n); };
    if (doc["wifi_ssid"].is<const char*>() && strcmp(s.wifi_ssid, doc["wifi_ssid"] | "")) { str("wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid)); reconnect = true; }
    if (doc["wifi_password"].is<const char*>() && strlen(doc["wifi_password"] | "")) { str("wifi_password", s.wifi_password, sizeof(s.wifi_password)); reconnect = true; }
    if (doc["loc_mode"].is<int>()) { uint8_t m = doc["loc_mode"]; if (m != s.loc_mode) { s.loc_mode = m; relocate = true; } }
    if (doc["city"].is<const char*>() && strcmp(s.city, doc["city"] | "")) { str("city", s.city, sizeof(s.city)); relocate = true; }
    if (doc["country_cc"].is<const char*>()) str("country_cc", s.country_cc, sizeof(s.country_cc));
    if (doc["lat"].is<float>() && doc["lon"].is<float>() && s.loc_mode == LOC_MANUAL) { float la = doc["lat"], lo = doc["lon"]; if (fabsf(la - s.man_lat) > 1e-5f || fabsf(lo - s.man_lon) > 1e-5f) { s.man_lat = la; s.man_lon = lo; relocate = true; } }
    if (doc["range_km"].is<int>())  { s.range_km = doc["range_km"]; redraw = true; }
    if (doc["update_s"].is<int>())  s.update_s = max(2, (int)doc["update_s"]);
    if (doc["units"].is<int>())     { s.units = doc["units"]; redraw = true; }
    if (doc["source"].is<int>())    s.source = doc["source"];
    if (doc["alert_km"].is<int>())  s.alert_km = doc["alert_km"];
    if (doc["brightness"].is<int>()) s.brightness = max(10, min(255, (int)doc["brightness"]));
    if (doc["class_mask"].is<int>()) { s.class_mask = doc["class_mask"]; redraw = true; }
    auto bl = [&](const char* k, bool& dst) { if (doc[k].is<bool>()) dst = doc[k]; };
    bl("sweep", s.sweep); bl("trails", s.trails); bl("labels", s.labels); bl("hide_ground", s.hide_ground); bl("auto_range", s.auto_range); bl("night_dim", s.night_dim);
    bl("highlight_mil", s.highlight_mil); bl("alert_bright", s.alert_bright); bl("map_underlay", s.map_underlay); bl("airports", s.airports);
    bl("notify_emergency", s.notify_emergency); bl("notify_watch", s.notify_watch); bl("notify_alert", s.notify_alert);
    str("watchlist", s.watchlist, sizeof(s.watchlist)); str("ntfy_topic", s.ntfy_topic, sizeof(s.ntfy_topic)); str("webhook_url", s.webhook_url, sizeof(s.webhook_url));
    if (doc["mqtt_uri"].is<const char*>() && strcmp(doc["mqtt_uri"] | "", "(set)")) str("mqtt_uri", s.mqtt_uri, sizeof(s.mqtt_uri));
    str("local_url", s.local_url, sizeof(s.local_url));
    if (doc["panel_pass"].is<const char*>() && strlen(doc["panel_pass"] | "")) str("panel_pass", s.panel_pass, sizeof(s.panel_pass));
    settings_save();
    if (redraw) ui_radar_range_changed();
    if (reconnect) net_send(NC_CONNECT_WIFI);
    else if (relocate) {
        if (s.loc_mode == LOC_AUTO_IP) net_send(NC_LOCATE_IP);
        else if (s.loc_mode == LOC_CITY) net_send(NC_GEOCODE, s.city, s.country_cc);
        else net_send(NC_APPLY_MANUAL, nullptr, nullptr, s.man_lat, s.man_lon);
    }
    ui_main_request_fetch();
    g_srv.send(200, "text/plain", reconnect ? "saved - reconnecting WiFi" : relocate ? "saved - updating location" : "saved");
}

static void handle_alerts() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < notify_log_count(); i++) {
        const AlertEntry& e = notify_log_get(i);
        JsonObject o = arr.add<JsonObject>();
        char tbuf[24]; time_t t = e.epoch; struct tm tm; localtime_r(&t, &tm);
        if (e.epoch > 1600000000UL) strftime(tbuf, sizeof(tbuf), "%d.%m %H:%M", &tm); else snprintf(tbuf, sizeof(tbuf), "+%lus", (unsigned long)(e.ms / 1000));
        o["time"] = tbuf; o["kind"] = notify_kind_name((NotifyKind)e.kind); o["title"] = e.title; o["message"] = e.message;
    }
    send_json(doc);
}

static void handle_stats() {
    if (!auth()) return;
    const Stats& st = stats_get();
    JsonDocument doc(&g_psram_alloc);
    doc["unique_today"] = st.unique_today; doc["max_tracked"] = st.max_tracked; doc["max_sog_kt"] = st.max_sog_kt; doc["max_sog_who"] = st.max_sog_who;
    doc["max_len"] = st.max_len; doc["max_len_who"] = st.max_len_who; doc["min_dist_km"] = st.min_dist_km; doc["min_dist_who"] = st.min_dist_who;
    doc["specials"] = st.specials; doc["alerts"] = st.alerts; doc["boot_count"] = st.boot_count;
    JsonArray h = doc["hourly"].to<JsonArray>(); for (int i = 0; i < 24; i++) h.add(st.hourly[i]);
    JsonArray cl = doc["classes"].to<JsonArray>(); for (int i = 0; i < STATS_TOP; i++) if (st.classes[i].key[0]) { JsonObject o = cl.add<JsonObject>(); o["key"] = st.classes[i].key; o["count"] = st.classes[i].count; }
    JsonArray fl = doc["flags"].to<JsonArray>();   for (int i = 0; i < STATS_TOP; i++) if (st.flags[i].key[0])   { JsonObject o = fl.add<JsonObject>(); o["key"] = st.flags[i].key; o["count"] = st.flags[i].count; }
    send_json(doc);
}

static void handle_ports() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < PORT_COUNT; i++) { JsonObject o = arr.add<JsonObject>(); o["name"] = PORTS[i].name; o["lat"] = PORTS[i].lat; o["lon"] = PORTS[i].lon; }
    send_json(doc);
}

static void handle_scan() {
    if (!auth()) return;
    int n = WiFi.scanNetworks(false, false, false, 300);
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n && i < 30; i++) { JsonObject o = arr.add<JsonObject>(); o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN; }
    WiFi.scanDelete();
    send_json(doc);
}

static void handle_screen() {
    if (!auth()) return;
    lv_img_dsc_t* snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (!snap) { g_srv.send(500, "text/plain", "snapshot failed"); return; }
    int w = snap->header.w, h = snap->header.h;
    uint32_t row = (w * 3 + 3) & ~3, size = 54 + row * h;
    uint8_t hdr[54] = {'B','M'};
    auto put32 = [&](int o, uint32_t v) { hdr[o] = v; hdr[o+1] = v >> 8; hdr[o+2] = v >> 16; hdr[o+3] = v >> 24; };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, (uint32_t)(-h)); hdr[26] = 1; hdr[28] = 24; put32(34, row * h);
    g_srv.setContentLength(size);
    g_srv.sendHeader("Access-Control-Allow-Origin", "*");
    g_srv.send(200, "image/bmp", "");
    g_srv.sendContent((const char*)hdr, 54);
    static uint8_t line[272 * 3 + 4];
    const uint16_t* px = (const uint16_t*)snap->data;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t v = px[y * w + x];
            line[x * 3 + 0] = (v & 31) * 255 / 31; line[x * 3 + 1] = ((v >> 5) & 63) * 255 / 63; line[x * 3 + 2] = (v >> 11) * 255 / 31;
        }
        g_srv.sendContent((const char*)line, row);
    }
    lv_snapshot_free(snap);
}

static void handle_metrics() {
    if (!auth()) return;
    ais_lock(); AisStats st = ais_stats(); ais_unlock();
    const Stats& d = stats_get();
    char buf[600];
    snprintf(buf, sizeof(buf),
        "esp32shipradar_vessels %d\nesp32shipradar_moving %d\nesp32shipradar_alerts %d\nesp32shipradar_unique_today %lu\nesp32shipradar_max_tracked %u\n"
        "esp32shipradar_feed_connected %d\nesp32shipradar_feed_msgs_per_s %.1f\nesp32shipradar_feed_sentences_total %lu\nesp32shipradar_heap_free_bytes %u\nesp32shipradar_uptime_seconds %lu\nesp32shipradar_wifi_rssi %d\n",
        st.count, st.moving, st.alerts, (unsigned long)d.unique_today, d.max_tracked, st.connected ? 1 : 0, st.msgs_per_s, (unsigned long)st.msgs_total,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned long)(millis() / 1000), WiFi.RSSI());
    g_srv.send(200, "text/plain; version=0.0.4", buf);
}

static void handle_action() {
    if (!auth()) return;
    JsonDocument doc; deserializeJson(doc, g_srv.arg("plain"));
    const char* a = doc["action"] | "";
    if (!strcmp(a, "locate")) { net_send(NC_LOCATE_IP); g_srv.send(200, "text/plain", "locating"); }
    else if (!strcmp(a, "reboot")) { g_srv.send(200, "text/plain", "rebooting"); delay(200); ESP.restart(); }
    else if (!strcmp(a, "reset_stats")) { stats_reset(); g_srv.send(200, "text/plain", "stats reset"); }
    else if (!strcmp(a, "page")) { ui_main_goto_tab(doc["page"] | 0); g_srv.send(200, "text/plain", "ok"); }
    else g_srv.send(400, "text/plain", "unknown action");
}

static void handle_ota_done() {
    if (!web_server_ota_armed()) { g_srv.send(403, "text/plain", "OTA locked - arm it on the device (Info page)"); return; }
    bool ok = !Update.hasError();
    g_srv.send(ok ? 200 : 500, "text/plain", ok ? "update ok - rebooting" : "update failed");
    if (ok) { delay(300); ESP.restart(); }
}
static void handle_ota_upload() {
    if (!web_server_ota_armed()) return;
    HTTPUpload& up = g_srv.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] start %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[OTA] done, %u bytes\n", up.totalSize); else Update.printError(Serial);
    }
}

void web_server_start() {
    if (g_started) return;
    g_srv.on("/", HTTP_GET, []() { if (!auth()) return; g_srv.send_P(200, "text/html", PAGE); });
    g_srv.on("/api/state", HTTP_GET, handle_state);
    g_srv.on("/api/config", HTTP_GET, handle_config_get);
    g_srv.on("/api/config", HTTP_POST, handle_config_post);
    g_srv.on("/api/alerts", HTTP_GET, handle_alerts);
    g_srv.on("/api/stats", HTTP_GET, handle_stats);
    g_srv.on("/api/wifi/scan", HTTP_GET, handle_scan);
    g_srv.on("/api/ports", HTTP_GET, handle_ports);
    g_srv.on("/api/action", HTTP_POST, handle_action);
    g_srv.on("/screen.bmp", HTTP_GET, handle_screen);
    g_srv.on("/metrics", HTTP_GET, handle_metrics);
    g_srv.on("/ota", HTTP_POST, handle_ota_done, handle_ota_upload);
    g_srv.onNotFound([]() { g_srv.send(404, "text/plain", "not found"); });
    g_srv.begin();
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    g_started = true;
    Serial.printf("[WEB] panel at http://%s.local/  (%s)\n", HOSTNAME, WiFi.localIP().toString().c_str());
}
void web_server_loop() { if (g_started) g_srv.handleClient(); }
