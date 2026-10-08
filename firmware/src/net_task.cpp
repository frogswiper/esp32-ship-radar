#include "net_task.h"
#include "settings.h"
#include "ais.h"
#include "ntp.h"
#include "notify.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static QueueHandle_t     g_q      = nullptr;
static SemaphoreHandle_t g_mtx    = nullptr;
static volatile uint32_t g_events = 0;
static volatile bool     g_busy   = false;
static char              g_status[96] = "Starting...";
static CityList          g_cities;
static LocalInfo         g_local = {};

static void set_status(const char* s) {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    strlcpy(g_status, s, sizeof(g_status));
    g_events |= EV_STATUS;
    xSemaphoreGive(g_mtx);
    Serial.printf("[NET] %s\n", s);
}
static void post(uint32_t ev) {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_events |= ev;
    xSemaphoreGive(g_mtx);
}

static bool do_connect_wifi() {
    Settings& s = settings_get();
    if (!strlen(s.wifi_ssid)) { set_status("No WiFi credentials"); return false; }
    char m[96]; snprintf(m, sizeof(m), "Connecting to %s...", s.wifi_ssid); set_status(m);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, false);
    delay(100);
    WiFi.begin(s.wifi_ssid, s.wifi_password);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(m, sizeof(m), "WiFi OK  %s", WiFi.localIP().toString().c_str());
        set_status(m);
        return true;
    }
    set_status("WiFi failed - check SSID/password");
    return false;
}

static void finish_location(const char* label) {
    Settings& s = settings_get();
    strlcpy(s.place, label, sizeof(s.place));
    settings_save();
    ais_clear();
    char m[96]; snprintf(m, sizeof(m), "Location: %s (%.3f, %.3f)", s.place, s.lat, s.lon);
    set_status(m);
    post(EV_LOCATED);
}

static void net_task(void*) {
    NetMsg msg;
    for (;;) {
        if (xQueueReceive(g_q, &msg, portMAX_DELAY) != pdTRUE) continue;
        g_busy = true;
        Settings& s = settings_get();
        switch (msg.cmd) {
        case NC_CONNECT_WIFI:
            post(do_connect_wifi() ? EV_WIFI_OK : EV_WIFI_FAIL);
            break;

        case NC_LOCATE_IP: {
            set_status("Locating by public IP...");
            float lat, lon; char label[48];
            if (geo_locate_by_ip(lat, lon, label, sizeof(label))) {
                s.lat = lat; s.lon = lon; s.loc_mode = LOC_AUTO_IP;
                finish_location(label);
            } else { set_status("IP geolocation failed"); post(EV_LOCATE_FAIL); }
            break;
        }
        case NC_FETCH_CITIES: {
            char m[96]; snprintf(m, sizeof(m), "Loading cities for %s...", msg.arg1); set_status(m);
            CityList tmp;
            bool ok = geo_fetch_cities(msg.arg1, tmp);
            xSemaphoreTake(g_mtx, portMAX_DELAY);
            if (ok) g_cities = tmp; else g_cities.count = 0;
            xSemaphoreGive(g_mtx);
            if (ok) { snprintf(m, sizeof(m), "%d cities loaded - pick one", tmp.count); set_status(m); post(EV_CITIES); }
            else    { set_status("City list unavailable - type the city instead"); post(EV_CITIES_FAIL); }
            break;
        }
        case NC_GEOCODE: {
            char m[96]; snprintf(m, sizeof(m), "Looking up %s...", msg.arg1); set_status(m);
            float lat, lon; char label[48];
            if (geo_geocode(msg.arg1, msg.arg2, lat, lon, label, sizeof(label))) {
                s.lat = lat; s.lon = lon; s.loc_mode = LOC_CITY;
                strlcpy(s.city, msg.arg1, sizeof(s.city));
                strlcpy(s.country_cc, msg.arg2, sizeof(s.country_cc));
                finish_location(label);
            } else { set_status("City not found"); post(EV_LOCATE_FAIL); }
            break;
        }
        case NC_APPLY_MANUAL: {
            s.lat = msg.f1; s.lon = msg.f2; s.man_lat = msg.f1; s.man_lon = msg.f2; s.loc_mode = LOC_MANUAL;
            char label[48]; snprintf(label, sizeof(label), "%.3f, %.3f", msg.f1, msg.f2);
            finish_location(label);
            break;
        }
        case NC_NOTIFY:
            notify_deliver(msg.arg1, msg.text, (NotifyKind)msg.kind);
            break;
        case NC_LOCAL_INFO: {
            LocalInfo li = {};
            if (geo_fetch_local_info(s.lat, s.lon, li)) {
                xSemaphoreTake(g_mtx, portMAX_DELAY); g_local = li; xSemaphoreGive(g_mtx);
                if (s.utc_offset_seconds != li.utc_offset_seconds) { s.utc_offset_seconds = li.utc_offset_seconds; settings_save(); }
                ntp_apply_offset(li.utc_offset_seconds);
                if (!ntp_is_synced()) ntp_sync();
                post(EV_LOCAL_INFO);
            }
            break;
        }
        }
        g_busy = false;
    }
}

void net_task_start() {
    g_q   = xQueueCreate(8, sizeof(NetMsg));
    g_mtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(net_task, "net", 20480, nullptr, 1, nullptr, 0);
}

bool net_send(NetCmd cmd, const char* a1, const char* a2, float f1, float f2) {
    NetMsg m = {}; m.cmd = cmd; m.f1 = f1; m.f2 = f2;
    if (a1) strlcpy(m.arg1, a1, sizeof(m.arg1));
    if (a2) strlcpy(m.arg2, a2, sizeof(m.arg2));
    if (xQueueSend(g_q, &m, 0) != pdTRUE) return false;
    return true;
}
bool net_send_notify(const char* title, const char* message, uint8_t kind) {
    NetMsg m = {}; m.cmd = NC_NOTIFY; m.kind = kind;
    strlcpy(m.arg1, title, sizeof(m.arg1)); strlcpy(m.text, message, sizeof(m.text));
    return xQueueSend(g_q, &m, 0) == pdTRUE;
}
bool net_busy()           { return g_busy; }
uint32_t net_take_events() {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    uint32_t e = g_events; g_events = 0;
    xSemaphoreGive(g_mtx);
    return e;
}
void net_status(char* buf, size_t n) {
    xSemaphoreTake(g_mtx, portMAX_DELAY); strlcpy(buf, g_status, n); xSemaphoreGive(g_mtx);
}
bool net_wifi_connected() { return WiFi.status() == WL_CONNECTED; }
int  net_wifi_rssi()      { return WiFi.RSSI(); }
void net_wifi_ip(char* buf, size_t n) { strlcpy(buf, WiFi.localIP().toString().c_str(), n); }
const CityList&  net_cities()     { return g_cities; }
const LocalInfo& net_local_info() { return g_local; }
