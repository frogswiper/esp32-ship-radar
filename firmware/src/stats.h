#pragma once
#include <stdint.h>
// Session + daily vessel statistics, persisted to NVS every few minutes.
#define STATS_TOP 12
struct TopEntry { char key[8]; uint16_t count; };
struct Stats {
    uint16_t hourly[24];        // new vessels first seen per local hour (today)
    uint32_t unique_today;
    uint16_t max_tracked;
    float    max_sog_kt;  char max_sog_who[22];
    uint16_t max_len;     char max_len_who[22];
    float    min_dist_km; char min_dist_who[22];
    uint16_t specials, alerts;
    TopEntry classes[STATS_TOP];   // key = class name
    TopEntry flags[STATS_TOP];     // key = flag
    uint16_t day_key;
    uint32_t boot_count;
};
void   stats_init();
void   stats_tick();
void   stats_note_alert();
void   stats_note_emergency();
const Stats& stats_get();
void   stats_save_if_due();
void   stats_reset();
