#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_VESSELS 200
#define TRAIL_LEN   20

enum ShipClass : uint8_t { SC_CARGO = 0, SC_TANKER = 1, SC_PASSENGER = 2, SC_FISHING = 3, SC_PLEASURE = 4, SC_OTHER = 5 };

struct Vessel {
    uint32_t mmsi;
    char     name[21];
    char     callsign[8];
    char     dest[21];
    uint8_t  shiptype;      // AIS ship type code (0 = unknown)
    uint8_t  navstat;       // 0 underway engine, 1 anchored, 5 moored, 15 n/a ...
    bool     class_b;
    float    lat, lon;
    float    sog_kt;        // speed over ground
    float    cog;           // course over ground, 360 = n/a
    int16_t  heading;       // true heading, 511 = n/a
    uint16_t length, width; // metres (0 = unknown)
    float    draught;       // metres
    uint8_t  eta_month, eta_day, eta_hour, eta_min;
    uint32_t last_pos_ms, first_seen_ms;
    float    dist_km, bearing;
    float    cpa_km, cpa_min;
    bool     alert;
    uint8_t  cls;           // ShipClass
    bool     watch;         // matches the watchlist
    bool     notified;      // arrival notification sent
    float    trail_lat[TRAIL_LEN], trail_lon[TRAIL_LEN];
    uint8_t  trail_sog[TRAIL_LEN];   // knots * 4
    uint8_t  trail_n, trail_head;
};

struct AisStats {
    int      count;          // vessels after filters
    int      moving;         // sog >= 0.5 kt
    int      nearest, fastest;
    int      alerts;
    int      watch_count;
    bool     connected;
    uint32_t last_msg_ms;
    uint32_t msgs_total;     // sentences decoded
    float    msgs_per_s;
    int      cache_names;    // static-data cache entries
    uint32_t reconnects;
    char     source[32];
};

void ais_init();                 // allocate PSRAM
void ais_task_start();           // start the NMEA stream task (waits for WiFi itself)

void             ais_lock();
void             ais_unlock();
Vessel*          ais_list();
int              ais_count();
const AisStats&  ais_stats();
int              ais_find_mmsi(uint32_t mmsi);
void             ais_clear();
void             ais_recompute(float lat, float lon);   // distances, CPA, stats, stale removal

const char* ais_type_name(uint8_t t);        // "Cargo", "Tanker", ...
const char* ais_navstat_name(uint8_t s);     // "underway", "at anchor", ...
const char* ais_flag(uint32_t mmsi);         // "NO", "SE", ... from the MID
bool        ais_is_tanker(uint8_t t);
bool        ais_is_special(uint8_t t);       // SAR, law enforcement, military
uint8_t     ais_class_of(uint8_t shiptype);
const char* ais_class_name(uint8_t cls);
bool        ais_visible(const Vessel& v);    // passes hide-stationary + class filters
const char* ais_compass16(float bearing);

float geo_distance_km(float lat1, float lon1, float lat2, float lon2);
float geo_bearing_deg(float lat1, float lon1, float lat2, float lon2);
