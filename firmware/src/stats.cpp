#include "stats.h"
#include "ais.h"
#include "ntp.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static Stats g = {};
static Preferences prefs;
static uint32_t g_last_save = 0;
static bool g_dirty = false;
#define SEEN_N 800
static uint32_t g_seen[SEEN_N]; static int g_seen_n = 0, g_seen_head = 0;
static bool seen_today(uint32_t v) { for (int i = 0; i < g_seen_n; i++) if (g_seen[i] == v) return true; return false; }
static void seen_add(uint32_t v) { g_seen[g_seen_head] = v; g_seen_head = (g_seen_head + 1) % SEEN_N; if (g_seen_n < SEEN_N) g_seen_n++; }
static void top_add(TopEntry* t, const char* key) {
    if (!key || !key[0]) return;
    for (int i = 0; i < STATS_TOP; i++) if (!strcmp(t[i].key, key)) { t[i].count++; return; }
    int w = -1; for (int i = 0; i < STATS_TOP; i++) if (!t[i].key[0]) { w = i; break; }
    if (w < 0) { w = 0; for (int i = 1; i < STATS_TOP; i++) if (t[i].count < t[w].count) w = i; if (t[w].count > 3) return; }
    strlcpy(t[w].key, key, sizeof(t[w].key)); t[w].count = 1;
}
static uint16_t today_key() { struct tm t = ntp_get_local_time(); if (t.tm_year < 100) return g.day_key; return (uint16_t)((t.tm_year % 100) * 400 + t.tm_yday); }
void stats_init() {
    prefs.begin("stats1", true); size_t n = prefs.getBytes("blob", &g, sizeof(g)); prefs.end();
    if (n != sizeof(g)) memset(&g, 0, sizeof(g));
    g.boot_count++; g_dirty = true;
}
static void new_day() {
    memset(g.hourly, 0, sizeof(g.hourly)); g.unique_today = 0; g.max_tracked = 0; g.max_sog_kt = 0; g.max_sog_who[0] = 0;
    g.max_len = 0; g.max_len_who[0] = 0; g.min_dist_km = 0; g.min_dist_who[0] = 0; g.specials = g.alerts = 0;
    memset(g.classes, 0, sizeof(g.classes)); memset(g.flags, 0, sizeof(g.flags)); g_seen_n = 0; g_seen_head = 0;
}
void stats_tick() {
    uint16_t dk = today_key();
    if (dk != g.day_key && dk != 0) { if (g.day_key) new_day(); g.day_key = dk; }
    struct tm t = ntp_get_local_time();
    ais_lock();
    Vessel* vs = ais_list(); int n = ais_count(); int tracked = 0;
    for (int i = 0; i < n; i++) {
        Vessel& v = vs[i];
        if (!v.mmsi) continue;
        tracked++;
        char who[22]; if (v.name[0]) strlcpy(who, v.name, sizeof(who)); else snprintf(who, sizeof(who), "%lu", (unsigned long)v.mmsi);
        // count a vessel once its static data has arrived (type known) or after 10 min, so classes are right
        if (!seen_today(v.mmsi) && (v.shiptype || millis() - v.first_seen_ms > 10UL * 60UL * 1000UL)) {
            seen_add(v.mmsi); g.unique_today++;
            if (t.tm_year >= 100) g.hourly[t.tm_hour % 24]++;
            top_add(g.classes, ais_class_name(v.cls));
            const char* fl = ais_flag(v.mmsi); top_add(g.flags, fl[0] ? fl : "??");
            if (ais_is_special(v.shiptype)) g.specials++;
        }
        if (v.sog_kt > g.max_sog_kt) { g.max_sog_kt = v.sog_kt; strlcpy(g.max_sog_who, who, sizeof(g.max_sog_who)); }
        if (v.length > g.max_len)    { g.max_len = v.length;   strlcpy(g.max_len_who, who, sizeof(g.max_len_who)); }
        if (v.sog_kt >= 0.5f && (g.min_dist_km == 0 || v.dist_km < g.min_dist_km)) { g.min_dist_km = v.dist_km; strlcpy(g.min_dist_who, who, sizeof(g.min_dist_who)); }
    }
    ais_unlock();
    if (tracked > g.max_tracked) g.max_tracked = tracked;
    g_dirty = true;
}
void stats_note_alert()     { g.alerts++; g_dirty = true; }
void stats_note_emergency() { g.specials++; g_dirty = true; }
const Stats& stats_get()    { return g; }
void stats_save_if_due() {
    if (!g_dirty || millis() - g_last_save < 5UL * 60UL * 1000UL) return;
    prefs.begin("stats1", false); prefs.putBytes("blob", &g, sizeof(g)); prefs.end();
    g_last_save = millis(); g_dirty = false;
}
void stats_reset() { uint32_t bc = g.boot_count; memset(&g, 0, sizeof(g)); g.boot_count = bc; g_seen_n = 0; g_seen_head = 0; g_dirty = true; g_last_save = 0; stats_save_if_due(); }
