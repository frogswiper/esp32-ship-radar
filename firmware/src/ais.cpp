#include "ais.h"
#include "settings.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "notify.h"
#include "stats.h"

// Kystverket (Norwegian Coastal Administration) open AIS feed: raw NMEA 0183 with TAG blocks.
static const char* AIS_HOST = "153.44.253.27";   // Kystverket open feed
static const int   AIS_PORT = 5631;
// optional own receiver (settings.local_url = "host:port")
static bool local_source(char* host, size_t n, int& port) {
    const Settings& s = settings_get();
    if (!s.local_url[0]) return false;
    strlcpy(host, s.local_url, n);
    char* p = strstr(host, "://"); if (p) memmove(host, p + 3, strlen(p + 3) + 1);
    char* colon = strrchr(host, ':'); port = 5631;
    if (colon) { *colon = 0; port = atoi(colon + 1); }
    return host[0] != 0;
}

static Vessel*           g_v     = nullptr;
static int               g_n     = 0;
static AisStats          g_stats = {};
static SemaphoreHandle_t g_mutex = nullptr;

// static-data cache for every MMSI heard (names arrive only every 6 min)
#define NAME_CACHE 3000
struct NameEntry { uint32_t mmsi; char name[21]; char callsign[8]; char dest[21]; uint8_t shiptype; uint16_t length, width; float draught; uint8_t eta[4]; };
static NameEntry* g_cache = nullptr;
static int g_cache_n = 0, g_cache_next = 0;

void ais_init() {
    g_v     = (Vessel*)heap_caps_calloc(MAX_VESSELS, sizeof(Vessel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_cache = (NameEntry*)heap_caps_calloc(NAME_CACHE, sizeof(NameEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_mutex = xSemaphoreCreateMutex();
    g_stats.nearest = g_stats.fastest = -1;
    strlcpy(g_stats.source, "Kystverket AIS", sizeof(g_stats.source));
}
void ais_lock()   { xSemaphoreTake(g_mutex, portMAX_DELAY); }
void ais_unlock() { xSemaphoreGive(g_mutex); }
Vessel* ais_list() { return g_v; }
int ais_count()    { return g_n; }
const AisStats& ais_stats() { return g_stats; }

int ais_find_mmsi(uint32_t m) {
    for (int i = 0; i < g_n; i++) if (g_v[i].mmsi == m) return i;
    return -1;
}
void ais_clear() {
    ais_lock();
    memset(g_v, 0, sizeof(Vessel) * MAX_VESSELS); g_n = 0;
    g_stats.count = g_stats.moving = g_stats.alerts = 0; g_stats.nearest = g_stats.fastest = -1;
    ais_unlock();
}

float geo_distance_km(float lat1, float lon1, float lat2, float lon2) {
    const float R = 6371.0f;
    float dlat = radians(lat2 - lat1), dlon = radians(lon2 - lon1);
    float a = sinf(dlat / 2) * sinf(dlat / 2) + cosf(radians(lat1)) * cosf(radians(lat2)) * sinf(dlon / 2) * sinf(dlon / 2);
    return R * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
}
float geo_bearing_deg(float lat1, float lon1, float lat2, float lon2) {
    float p1 = radians(lat1), p2 = radians(lat2), dl = radians(lon2 - lon1);
    float y = sinf(dl) * cosf(p2), x = cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl);
    float b = degrees(atan2f(y, x)); if (b < 0) b += 360.0f; return b;
}

void ais_recompute(float lat, float lon) {
    const Settings& s = settings_get();
    uint32_t now = millis();
    g_stats.count = g_stats.moving = g_stats.alerts = g_stats.watch_count = 0; g_stats.nearest = g_stats.fastest = -1;
    float best_d = 1e9f, best_s = -1;
    for (int i = 0; i < g_n; i++) {
        Vessel& v = g_v[i];
        if (!v.mmsi) continue;
        if (now - v.last_pos_ms > 20UL * 60UL * 1000UL) { v.mmsi = 0; continue; }   // stale
        v.dist_km = geo_distance_km(lat, lon, v.lat, v.lon);
        v.bearing = geo_bearing_deg(lat, lon, v.lat, v.lon);
        v.cpa_km = v.dist_km; v.cpa_min = 0; v.alert = false;
        float crs = v.heading != 511 ? v.heading : v.cog;
        if (v.sog_kt >= 0.5f && crs < 360) {
            float br = radians(v.bearing), tr = radians(crs);
            float px = v.dist_km * sinf(br), py = v.dist_km * cosf(br);
            float spd = v.sog_kt * 1.852f / 60.0f;
            float vx = spd * sinf(tr), vy = spd * cosf(tr), vv = vx * vx + vy * vy;
            float t = vv > 0 ? -(px * vx + py * vy) / vv : 0; if (t < 0) t = 0;
            v.cpa_km = sqrtf((px + vx * t) * (px + vx * t) + (py + vy * t) * (py + vy * t)); v.cpa_min = t;
            if (s.alert_km > 0 && t <= 30.0f && v.cpa_km <= (float)s.alert_km) v.alert = true;
        } else if (s.alert_km > 0 && v.dist_km <= (float)s.alert_km) v.alert = true;
        if (!ais_visible(v)) continue;
        if (v.dist_km > s.range_km * 1.04f) continue;   // tracked for auto-range, but not "in range"
        g_stats.count++;
        if (v.watch) g_stats.watch_count++;
        if (v.sog_kt >= 0.5f) g_stats.moving++;
        if (v.alert) g_stats.alerts++;
        if (v.dist_km < best_d) { best_d = v.dist_km; g_stats.nearest = i; }
        if (v.sog_kt > best_s)  { best_s = v.sog_kt;  g_stats.fastest = i; }
    }
    while (g_n > 0 && !g_v[g_n - 1].mmsi) g_n--;
    g_stats.cache_names = g_cache_n;
}

// ── 6-bit payload decoding ───────────────────────────────────────────────────
struct Bits { const char* p; int n; };
static inline uint32_t ubits(const Bits& b, int off, int len) {
    uint32_t v = 0;
    for (int i = 0; i < len; i++) {
        int bit = off + i, ci = bit / 6, bi = 5 - (bit % 6);
        if (ci >= b.n) return v << (len - i);
        int c = b.p[ci] - 48; if (c > 40) c -= 8;
        v = (v << 1) | ((c >> bi) & 1);
    }
    return v;
}
static inline int32_t sbits(const Bits& b, int off, int len) {
    uint32_t v = ubits(b, off, len);
    if (v & (1u << (len - 1))) return (int32_t)v - (1 << len);
    return (int32_t)v;
}
static void sixbit_str(const Bits& b, int off, int chars, char* out, size_t n) {
    size_t o = 0;
    for (int i = 0; i < chars && o + 1 < n; i++) {
        int c = ubits(b, off + i * 6, 6);
        if (c == 0) break;   // '@' terminator
        out[o++] = (c < 32) ? (char)(c + 64) : (char)c;
    }
    out[o] = 0;
    while (o && out[o - 1] == ' ') out[--o] = 0;
}

// ── name cache ───────────────────────────────────────────────────────────────
static NameEntry* cache_find(uint32_t mmsi) {
    for (int i = 0; i < g_cache_n; i++) if (g_cache[i].mmsi == mmsi) return &g_cache[i];
    return nullptr;
}
static NameEntry* cache_get(uint32_t mmsi) {
    NameEntry* e = cache_find(mmsi);
    if (e) return e;
    if (g_cache_n < NAME_CACHE) e = &g_cache[g_cache_n++];
    else { e = &g_cache[g_cache_next]; g_cache_next = (g_cache_next + 1) % NAME_CACHE; }
    memset(e, 0, sizeof(*e)); e->mmsi = mmsi;
    return e;
}
static void annotate(Vessel& v);
static void apply_static(Vessel& v, const NameEntry& e) {
    if (e.name[0])     strlcpy(v.name, e.name, sizeof(v.name));
    if (e.callsign[0]) strlcpy(v.callsign, e.callsign, sizeof(v.callsign));
    if (e.dest[0])     strlcpy(v.dest, e.dest, sizeof(v.dest));
    if (e.shiptype)    v.shiptype = e.shiptype;
    if (e.length)      { v.length = e.length; v.width = e.width; }
    if (e.draught > 0) v.draught = e.draught;
    v.eta_month = e.eta[0]; v.eta_day = e.eta[1]; v.eta_hour = e.eta[2]; v.eta_min = e.eta[3];
    annotate(v);
}

// ── classification, watchlist, filters ──────────────────────────────────────
uint8_t ais_class_of(uint8_t t) {
    if (t >= 70 && t <= 79) return SC_CARGO;
    if (t >= 80 && t <= 89) return SC_TANKER;
    if ((t >= 60 && t <= 69) || (t >= 40 && t <= 49)) return SC_PASSENGER;
    if (t == 30) return SC_FISHING;
    if (t == 36 || t == 37) return SC_PLEASURE;
    return SC_OTHER;
}
const char* ais_class_name(uint8_t c) {
    switch (c) { case SC_CARGO: return "cargo"; case SC_TANKER: return "tanker"; case SC_PASSENGER: return "passenger"; case SC_FISHING: return "fishing"; case SC_PLEASURE: return "pleasure"; default: return "other"; }
}
bool ais_visible(const Vessel& v) {
    const Settings& s = settings_get();
    if (s.hide_ground && v.sog_kt < 0.5f) return false;
    if (!(s.class_mask & (1 << (v.cls & 7)))) return false;
    return true;
}
const char* ais_compass16(float b) {
    static const char* n[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
    return n[((int)lroundf(b / 22.5f)) & 15];
}
static bool watch_match(const Vessel& v) {
    const Settings& s = settings_get();
    if (!s.watchlist[0]) return false;
    char list[96]; strlcpy(list, s.watchlist, sizeof(list));
    char mm[12]; snprintf(mm, sizeof(mm), "%lu", (unsigned long)v.mmsi);
    for (char* tok = strtok(list, ", "); tok; tok = strtok(nullptr, ", ")) {
        size_t l = strlen(tok); if (!l) continue;
        if (!strncasecmp(v.name, tok, l) || !strncmp(mm, tok, l) || !strncasecmp(v.callsign, tok, l)) return true;
    }
    return false;
}
// called with the lock held whenever a vessel gets (new) static data or appears
static void annotate(Vessel& v) {
    v.cls = ais_class_of(v.shiptype);
    v.watch = watch_match(v);
    if (!v.notified && (v.watch || ais_is_special(v.shiptype)) && (v.name[0] || v.watch)) {
        v.notified = true;
        char title[40], msg[96], nm[22];
        if (v.name[0]) strlcpy(nm, v.name, sizeof(nm)); else snprintf(nm, sizeof(nm), "MMSI %lu", (unsigned long)v.mmsi);
        snprintf(title, sizeof(title), "%s %s in range", v.watch ? "Watchlist" : ais_type_name(v.shiptype), nm);
        snprintf(msg, sizeof(msg), "%s %s, %.1f kt, %.1f km %s%s%s", ais_type_name(v.shiptype), ais_flag(v.mmsi), v.sog_kt, v.dist_km, ais_compass16(v.bearing), v.dest[0] ? ", to " : "", v.dest);
        char key[12]; snprintf(key, sizeof(key), "%lu", (unsigned long)v.mmsi);
        notify_event(v.watch ? NK_WATCHLIST : NK_EMERGENCY, key, title, msg);
    }
}

// ── message handling ─────────────────────────────────────────────────────────
static void handle_position(uint32_t mmsi, float lat, float lon, float sog, float cog, int hdg, int navstat, bool class_b) {
    const Settings& s = settings_get();
    if (!settings_has_location()) return;
    if (lat > 90 || lon > 180) return;   // 91 / 181 = not available
    float d = geo_distance_km(s.lat, s.lon, lat, lon);
    float keep = (s.auto_range ? 200.0f : (float)s.range_km) * 1.1f;
    if (d > keep) return;
    uint32_t now = millis();
    ais_lock();
    int idx = ais_find_mmsi(mmsi);
    if (idx < 0) {
        for (int i = 0; i < MAX_VESSELS; i++) if (!g_v[i].mmsi) { idx = i; break; }
        if (idx < 0) {   // table full: replace the farthest vessel if this one is closer
            int far = -1;
            for (int i = 0; i < g_n; i++) if (g_v[i].mmsi && (far < 0 || g_v[i].dist_km > g_v[far].dist_km)) far = i;
            if (far >= 0 && g_v[far].dist_km > d) idx = far;
        }
        if (idx >= 0) {
            if (idx >= g_n) g_n = idx + 1;
            Vessel& v = g_v[idx]; memset(&v, 0, sizeof(v));
            v.mmsi = mmsi; v.first_seen_ms = now; v.heading = 511; v.cog = 360;
            v.dist_km = d; v.bearing = geo_bearing_deg(s.lat, s.lon, lat, lon); v.sog_kt = sog;
            NameEntry* e = cache_find(mmsi); if (e) apply_static(v, *e); else annotate(v);
        }
    }
    if (idx >= 0) {
        Vessel& v = g_v[idx];
        if (v.last_pos_ms && (fabsf(v.lat - lat) > 2e-4f || fabsf(v.lon - lon) > 2e-4f) && now - v.last_pos_ms > 30000) {
            v.trail_lat[v.trail_head] = v.lat; v.trail_lon[v.trail_head] = v.lon;
            v.trail_sog[v.trail_head] = (uint8_t)min(255, (int)(v.sog_kt * 4));
            v.trail_head = (v.trail_head + 1) % TRAIL_LEN; if (v.trail_n < TRAIL_LEN) v.trail_n++;
        }
        v.lat = lat; v.lon = lon; v.sog_kt = sog; v.cog = cog; v.heading = hdg; v.navstat = navstat; v.class_b = class_b;
        v.last_pos_ms = now;
        v.dist_km = d; v.bearing = geo_bearing_deg(s.lat, s.lon, lat, lon);
    }
    ais_unlock();
}

static void decode(const char* payload, int len, int fill) {
    Bits b = {payload, len};
    int type = ubits(b, 0, 6);
    uint32_t mmsi = ubits(b, 8, 30);
    g_stats.msgs_total++;
    g_stats.last_msg_ms = millis();
    if (type == 1 || type == 2 || type == 3) {
        int navstat = ubits(b, 38, 4);
        float sog = ubits(b, 50, 10) / 10.0f;
        float lon = sbits(b, 61, 28) / 600000.0f, lat = sbits(b, 89, 27) / 600000.0f;
        float cog = ubits(b, 116, 12) / 10.0f; int hdg = ubits(b, 128, 9);
        if (sog >= 102.3f) sog = 0;
        handle_position(mmsi, lat, lon, sog, cog, hdg, navstat, false);
    } else if (type == 18 || type == 19) {
        float sog = ubits(b, 46, 10) / 10.0f;
        float lon = sbits(b, 57, 28) / 600000.0f, lat = sbits(b, 85, 27) / 600000.0f;
        float cog = ubits(b, 112, 12) / 10.0f; int hdg = ubits(b, 124, 9);
        if (sog >= 102.3f) sog = 0;
        handle_position(mmsi, lat, lon, sog, cog, hdg, 15, true);
        if (type == 19) {
            ais_lock(); NameEntry* e = cache_get(mmsi);
            sixbit_str(b, 143, 20, e->name, sizeof(e->name)); e->shiptype = ubits(b, 263, 8);
            int A = ubits(b, 271, 9), B = ubits(b, 280, 9), C = ubits(b, 289, 6), D = ubits(b, 295, 6);
            e->length = A + B; e->width = C + D;
            int idx = ais_find_mmsi(mmsi); if (idx >= 0) apply_static(g_v[idx], *e);
            ais_unlock();
        }
    } else if (type == 5) {
        ais_lock(); NameEntry* e = cache_get(mmsi);
        sixbit_str(b, 70, 7, e->callsign, sizeof(e->callsign));
        sixbit_str(b, 112, 20, e->name, sizeof(e->name));
        e->shiptype = ubits(b, 232, 8);
        int A = ubits(b, 240, 9), B = ubits(b, 249, 9), C = ubits(b, 258, 6), D = ubits(b, 264, 6);
        e->length = A + B; e->width = C + D;
        e->eta[0] = ubits(b, 274, 4); e->eta[1] = ubits(b, 278, 5); e->eta[2] = ubits(b, 283, 5); e->eta[3] = ubits(b, 288, 6);
        e->draught = ubits(b, 294, 8) / 10.0f;
        sixbit_str(b, 302, 20, e->dest, sizeof(e->dest));
        int idx = ais_find_mmsi(mmsi); if (idx >= 0) apply_static(g_v[idx], *e);
        ais_unlock();
    } else if (type == 24) {
        int part = ubits(b, 38, 2);
        ais_lock(); NameEntry* e = cache_get(mmsi);
        if (part == 0) sixbit_str(b, 40, 20, e->name, sizeof(e->name));
        else {
            e->shiptype = ubits(b, 40, 8);
            sixbit_str(b, 90, 7, e->callsign, sizeof(e->callsign));
            int A = ubits(b, 132, 9), B = ubits(b, 141, 9), C = ubits(b, 150, 6), D = ubits(b, 156, 6);
            e->length = A + B; e->width = C + D;
        }
        int idx = ais_find_mmsi(mmsi); if (idx >= 0) apply_static(g_v[idx], *e);
        ais_unlock();
    }
}

// ── NMEA line handling with multipart reassembly ─────────────────────────────
struct Frag { char key; int total; int got; char payload[520]; uint32_t ms; };
static Frag g_frag[4];

static void handle_line(char* line) {
    char* s = strchr(line, '!');
    if (!s) return;
    // !AIVDM,total,num,seq,chan,payload,fill*cs
    char* f[8]; int nf = 0;
    for (char* p = s; p && nf < 8; ) { f[nf++] = p; p = strchr(p, ','); if (p) *p++ = 0; }
    if (nf < 7) return;
    int total = atoi(f[1]), num = atoi(f[2]); char seq = f[3][0]; const char* payload = f[5];
    char* star = strchr(f[6], '*'); int fill = atoi(f[6]);
    (void)star;
    if (total <= 1) { decode(payload, strlen(payload), fill); return; }
    // multipart
    Frag* fr = nullptr;
    for (int i = 0; i < 4; i++) if (g_frag[i].total && g_frag[i].key == seq) fr = &g_frag[i];
    if (num == 1) {
        if (!fr) { for (int i = 0; i < 4; i++) if (!g_frag[i].total || millis() - g_frag[i].ms > 5000) { fr = &g_frag[i]; break; } }
        if (!fr) fr = &g_frag[0];
        fr->key = seq; fr->total = total; fr->got = 1; fr->ms = millis();
        strlcpy(fr->payload, payload, sizeof(fr->payload));
        return;
    }
    if (!fr || fr->got != num - 1) { if (fr) fr->total = 0; return; }
    strlcat(fr->payload, payload, sizeof(fr->payload));
    fr->got = num;
    if (fr->got == fr->total) { decode(fr->payload, strlen(fr->payload), fill); fr->total = 0; }
}

static void ais_task(void*) {
    WiFiClient client;
    static char line[600]; int ll = 0;
    uint32_t backoff = 2000, win_t0 = millis(), win_n = 0;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) { g_stats.connected = false; vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        if (!client.connected()) {
            g_stats.connected = false;
            char host[80]; int port; bool local = local_source(host, sizeof(host), port);
            const char* h = local ? host : AIS_HOST; int pp = local ? port : AIS_PORT;
            strlcpy(g_stats.source, local ? "local AIS rx" : "Kystverket AIS", sizeof(g_stats.source));
            Serial.printf("[AIS] connecting to %s:%d\n", h, pp);
            if (!client.connect(h, pp, 8000)) {
                Serial.println("[AIS] connect failed");
                vTaskDelay(pdMS_TO_TICKS(backoff)); backoff = min<uint32_t>(backoff * 2, 60000); continue;
            }
            client.setNoDelay(true);
            g_stats.connected = true; g_stats.reconnects++; backoff = 2000; ll = 0;
            Serial.println("[AIS] connected");
        }
        int avail = client.available();
        if (avail <= 0) {
            if (millis() - g_stats.last_msg_ms > 60000 && g_stats.last_msg_ms) { Serial.println("[AIS] stream silent, reconnecting"); client.stop(); }
            vTaskDelay(pdMS_TO_TICKS(50)); continue;
        }
        while (client.available()) {
            int c = client.read();
            if (c < 0) break;
            if (c == '\n' || c == '\r') {
                if (ll > 0) { line[ll] = 0; handle_line(line); win_n++; }
                ll = 0;
            } else if (ll < (int)sizeof(line) - 1) line[ll++] = (char)c;
        }
        uint32_t now = millis();
        if (now - win_t0 >= 5000) { g_stats.msgs_per_s = win_n * 1000.0f / (now - win_t0); win_t0 = now; win_n = 0; }
    }
}

void ais_task_start() {
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreatePinnedToCore(ais_task, "ais", 12288, nullptr, 1, nullptr, 0);
}

// ── lookups ──────────────────────────────────────────────────────────────────
const char* ais_type_name(uint8_t t) {
    if (t == 30) return "Fishing";
    if (t == 31 || t == 32) return "Towing";
    if (t == 33) return "Dredger";
    if (t == 34) return "Diving ops";
    if (t == 35) return "Military";
    if (t == 36) return "Sailing";
    if (t == 37) return "Pleasure craft";
    if (t == 50) return "Pilot";
    if (t == 51) return "SAR";
    if (t == 52) return "Tug";
    if (t == 53) return "Port tender";
    if (t == 54) return "Anti-pollution";
    if (t == 55) return "Law enforcement";
    if (t == 58) return "Medical";
    if (t >= 20 && t <= 29) return "Wing in ground";
    if (t >= 40 && t <= 49) return "High-speed craft";
    if (t >= 60 && t <= 69) return "Passenger";
    if (t >= 70 && t <= 79) return "Cargo";
    if (t >= 80 && t <= 89) return "Tanker";
    if (t >= 90 && t <= 99) return "Other";
    return "Unknown";
}
bool ais_is_tanker(uint8_t t)  { return t >= 80 && t <= 89; }
bool ais_is_special(uint8_t t) { return t == 35 || t == 51 || t == 55; }

const char* ais_navstat_name(uint8_t s) {
    switch (s) {
        case 0: return "underway"; case 1: return "at anchor"; case 2: return "not under command";
        case 3: return "restricted manoeuvre"; case 4: return "constrained by draught"; case 5: return "moored";
        case 6: return "aground"; case 7: return "fishing"; case 8: return "sailing";
        case 14: return "AIS-SART"; default: return "";
    }
}

const char* ais_flag(uint32_t mmsi) {
    int mid = mmsi / 1000000;
    if (mid < 200) { // special: 8MIDxxxxx (diver), 98MIDxxxx (craft), 99MIDxxxx (AtoN), 111MIDxxx (SAR aircraft)
        if (mmsi >= 970000000) return "SART";
        if (mmsi >= 111000000 && mmsi < 112000000) mid = (mmsi / 1000) % 1000;
        else if (mmsi >= 98000000 && mmsi < 100000000) mid = (mmsi / 10000) % 1000;
        else if (mmsi >= 8000000 && mmsi < 9000000) mid = (mmsi / 10000) % 1000;
    }
    switch (mid) {
        case 257: case 258: case 259: return "NO";
        case 265: case 266: return "SE";
        case 219: case 220: return "DK";
        case 230: return "FI"; case 231: return "FO"; case 251: return "IS";
        case 211: case 218: return "DE"; case 244: case 245: case 246: return "NL";
        case 232: case 233: case 234: case 235: return "GB"; case 250: return "IE";
        case 205: return "BE"; case 226: case 227: case 228: return "FR"; case 224: case 225: return "ES";
        case 263: return "PT"; case 247: return "IT"; case 237: case 239: case 240: case 241: return "GR";
        case 261: return "PL"; case 275: return "LV"; case 276: return "EE"; case 277: return "LT";
        case 273: return "RU"; case 272: return "UA"; case 271: return "TR"; case 212: case 209: case 210: return "CY";
        case 248: case 249: case 215: case 229: case 256: return "MT"; case 255: case 204: return "PT";
        case 311: return "BS"; case 538: return "MH"; case 636: case 637: return "LR";
        case 351: case 352: case 353: case 354: case 355: case 356: case 357: case 370: case 371: case 372: case 373: case 374: return "PA";
        case 477: return "HK"; case 563: case 564: case 565: case 566: return "SG"; case 412: case 413: case 414: return "CN";
        case 303: case 338: case 366: case 367: case 368: case 369: return "US"; case 316: return "CA";
        case 308: case 309: case 314: return "BM"; case 319: return "KY"; case 378: return "VG"; case 305: return "AG";
        case 304: return "AG"; case 341: return "KN"; case 376: return "VC"; case 339: return "JM";
        case 269: return "CH"; case 203: return "AT"; case 253: return "LU"; case 236: return "GI"; case 232 + 1000: return "";
        default: return "";
    }
}
