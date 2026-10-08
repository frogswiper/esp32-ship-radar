#include "ui_detail.h"
#include "ui_main.h"
#include "ui_radar.h"
#include "../board_config.h"
#include "../settings.h"
#include "../ais.h"
#include <Arduino.h>

#define C_HDR  lv_color_hex(0x3DF25A)
#define C_TXT  lv_color_hex(0x33D650)
#define C_DIM  lv_color_hex(0x1E9A45)
#define C_LINE lv_color_hex(0x1FA84A)
#define C_WARN lv_color_hex(0xFFE44D)
#define C_EMG  lv_color_hex(0xFF3B3B)

static uint32_t  g_mmsi = 0;
static lv_obj_t *g_title, *g_sub, *g_body, *g_spark, *g_spark_lbl, *g_sq, *g_cpa;
static lv_color_t* g_spark_buf = nullptr;
static const int SPARK_W = 252, SPARK_H = 56;

static void back_cb(lv_event_t*)  { ui_main_goto_tab(PAGE_RADAR); }
static void radar_cb(lv_event_t*) { ui_radar_select(g_mmsi); ui_main_goto_tab(PAGE_RADAR); }

static lv_obj_t* label(lv_obj_t* p, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(p);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}
static lv_obj_t* button(lv_obj_t* p, const char* t, lv_event_cb_t cb, int w) {
    lv_obj_t* b = lv_btn_create(p);
    lv_obj_set_size(b, w, 34);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x07200F), 0);
    lv_obj_set_style_border_color(b, C_LINE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, C_HDR, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void ui_detail_build(lv_obj_t* parent) {
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, DISPLAY_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_set_style_bg_color(col, lv_color_black(), 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_radius(col, 0, 0);
    lv_obj_set_style_pad_all(col, 8, 0);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_style_bg_color(col, C_LINE, LV_PART_SCROLLBAR);

    lv_obj_t* row = lv_obj_create(col);
    lv_obj_set_size(row, LV_PCT(100), 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    button(row, LV_SYMBOL_LEFT " Back", back_cb, 90);
    button(row, LV_SYMBOL_GPS " Show on radar", radar_cb, 150);

    g_title = label(col, &lv_font_montserrat_24, C_HDR);
    g_sub   = label(col, &lv_font_montserrat_14, C_TXT);
    g_body  = label(col, &lv_font_montserrat_12, C_TXT);

    g_spark_lbl = label(col, &lv_font_montserrat_12, C_DIM);
    g_spark_buf = (lv_color_t*)heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(SPARK_W, SPARK_H), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_spark = lv_canvas_create(col);
    lv_canvas_set_buffer(g_spark, g_spark_buf, SPARK_W, SPARK_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_spark, lv_color_black(), LV_OPA_COVER);

    g_cpa = label(col, &lv_font_montserrat_12, C_WARN);
    g_sq  = label(col, &lv_font_montserrat_12, C_DIM);
}

void ui_detail_show(uint32_t mmsi) {
    g_mmsi = mmsi;
    ui_detail_refresh();
}

static void draw_spark(const Vessel& a) {
    lv_canvas_fill_bg(g_spark, lv_color_black(), LV_OPA_COVER);
    int n = a.trail_n; float vals[TRAIL_LEN + 1]; int m = 0;
    for (int j = 0; j < n; j++) vals[m++] = a.trail_sog[(a.trail_head - a.trail_n + j + TRAIL_LEN) % TRAIL_LEN] / 4.0f;
    vals[m++] = a.sog_kt;
    float lo = vals[0], hi = vals[0];
    for (int i = 1; i < m; i++) { if (vals[i] < lo) lo = vals[i]; if (vals[i] > hi) hi = vals[i]; }
    if (hi - lo < 1.0f) { hi += 0.5f; lo -= 0.5f; } if (lo < 0) lo = 0;
    lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld); ld.color = C_DIM; ld.width = 1; ld.dash_width = 2; ld.dash_gap = 3;
    lv_point_t base[2] = {{0, SPARK_H - 1}, {SPARK_W - 1, SPARK_H - 1}}; lv_canvas_draw_line(g_spark, base, 2, &ld);
    lv_point_t top[2]  = {{0, 0}, {SPARK_W - 1, 0}}; lv_canvas_draw_line(g_spark, top, 2, &ld);
    ld.color = C_HDR; ld.width = 2; ld.dash_width = 0;
    lv_point_t pts[TRAIL_LEN + 1];
    for (int i = 0; i < m; i++) {
        pts[i].x = (m > 1) ? (lv_coord_t)(i * (SPARK_W - 1) / (m - 1)) : SPARK_W / 2;
        pts[i].y = (lv_coord_t)(SPARK_H - 2 - (int)((vals[i] - lo) * (SPARK_H - 4) / (hi - lo)));
    }
    if (m > 1) lv_canvas_draw_line(g_spark, pts, m, &ld);
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd); rd.bg_color = C_WARN; rd.radius = LV_RADIUS_CIRCLE; rd.border_width = 0;
    lv_canvas_draw_rect(g_spark, pts[m - 1].x - 3, pts[m - 1].y - 3, 7, 7, &rd);
    char t[64];
    snprintf(t, sizeof(t), "Speed trend  %.1f - %.1f kt  (%d samples)", lo, hi, m);
    lv_label_set_text(g_spark_lbl, t);
    lv_obj_invalidate(g_spark);
}

void ui_detail_refresh() {
    if (!g_title) return;
    const Settings& s = settings_get();
    bool av = s.units == UNITS_AVIATION;
    ais_lock();
    int idx = g_mmsi ? ais_find_mmsi(g_mmsi) : -1;
    if (idx < 0) {
        ais_unlock();
        char t[32]; snprintf(t, sizeof(t), "MMSI %lu", (unsigned long)g_mmsi);
        lv_label_set_text(g_title, g_mmsi ? t : "No vessel");
        lv_label_set_text(g_sub, g_mmsi ? "Out of range / no longer tracked" : "Tap a vessel row on the radar or list");
        lv_label_set_text(g_body, ""); lv_label_set_text(g_spark_lbl, ""); lv_label_set_text(g_cpa, ""); lv_label_set_text(g_sq, "");
        lv_canvas_fill_bg(g_spark, lv_color_black(), LV_OPA_COVER);
        return;
    }
    Vessel a = ais_list()[idx];   // copy, then release the lock
    ais_unlock();

    char t[200];
    if (a.name[0]) lv_label_set_text(g_title, a.name);
    else { snprintf(t, sizeof(t), "MMSI %lu", (unsigned long)a.mmsi); lv_label_set_text(g_title, t); }
    lv_obj_set_style_text_color(g_title, ais_is_special(a.shiptype) ? C_EMG : (a.alert || a.watch) ? C_WARN : C_HDR, 0);
    const char* flag = ais_flag(a.mmsi);
    snprintf(t, sizeof(t), "%s  -  %s%s%s  -  class %s", ais_type_name(a.shiptype), flag[0] ? "flag " : "", flag[0] ? flag : "flag unknown", "", a.class_b ? "B" : "A");
    lv_label_set_text(g_sub, t);

    char dims[64];
    if (a.length) snprintf(dims, sizeof(dims), "Length %d m   beam %d m%s", a.length, a.width, a.draught > 0 ? "" : "");
    else strlcpy(dims, "Dimensions unknown", sizeof(dims));
    char dr[32]; if (a.draught > 0) snprintf(dr, sizeof(dr), "   draught %.1f m", a.draught); else dr[0] = 0;
    char eta[48];
    if (a.eta_month >= 1 && a.eta_month <= 12 && a.eta_day >= 1 && a.eta_day <= 31 && a.eta_hour < 24)
        snprintf(eta, sizeof(eta), "   ETA %02d/%02d %02d:%02d", a.eta_day, a.eta_month, a.eta_hour, a.eta_min < 60 ? a.eta_min : 0);
    else eta[0] = 0;
    const char* stn = ais_navstat_name(a.navstat);
    float dd = av ? a.dist_km / 1.852f : a.dist_km;
    char body[520];
    snprintf(body, sizeof(body),
        "MMSI %lu   call sign %s\n%s%s\n\nDestination: %s%s\nStatus: %s\n\nSpeed %.1f kt (%.0f km/h)   course %03.0f\xc2\xb0   heading %s\nDistance %.1f %s   bearing %03.0f\xc2\xb0\nPosition %.4f, %.4f",
        (unsigned long)a.mmsi, a.callsign[0] ? a.callsign : "-", dims, dr,
        a.dest[0] ? a.dest : "-", eta, stn[0] ? stn : (a.sog_kt < 0.5f ? "stationary" : "moving"),
        a.sog_kt, a.sog_kt * 1.852f, a.cog < 360 ? a.cog : 0, a.heading != 511 ? (snprintf(t, sizeof(t), "%03d\xc2\xb0", a.heading), t) : "n/a",
        dd, av ? "nm" : "km", a.bearing, a.lat, a.lon);
    lv_label_set_text(g_body, body);

    draw_spark(a);

    char look[64]; snprintf(look, sizeof(look), LV_SYMBOL_EYE_OPEN "  Look %s (%03.0f\xc2\xb0)", ais_compass16(a.bearing), a.bearing);
    if (a.alert)
        snprintf(t, sizeof(t), "%s\n" LV_SYMBOL_WARNING "  APPROACHING  closest %.1f km in %.0f min", look, a.cpa_km, a.cpa_min);
    else if (a.sog_kt >= 0.5f && a.cpa_min > 0.5f && a.cpa_km < a.dist_km * 0.9f)
        snprintf(t, sizeof(t), "%s\nClosest approach %.1f km in %.0f min", look, a.cpa_km, a.cpa_min);
    else snprintf(t, sizeof(t), "%s", look);
    if (a.watch) strlcat(t, "\n" LV_SYMBOL_BELL "  On your watchlist", sizeof(t));
    lv_label_set_text(g_cpa, t);
    lv_obj_set_style_text_color(g_cpa, (a.alert || a.watch) ? C_WARN : C_TXT, 0);

    snprintf(t, sizeof(t), "Last position %lu s ago   tracked %lu min", (unsigned long)((millis() - a.last_pos_ms) / 1000), (unsigned long)((millis() - a.first_seen_ms) / 60000));
    lv_label_set_text(g_sq, t);
    lv_obj_set_style_text_color(g_sq, C_DIM, 0);
}
