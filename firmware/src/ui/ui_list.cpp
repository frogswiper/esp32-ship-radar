#include "ui_list.h"
#include "ui_main.h"
#include "ui_radar.h"
#include "ui_detail.h"
#include "../board_config.h"
#include "../settings.h"
#include "../ais.h"
#include "../net_task.h"
#include <Arduino.h>

#define MAX_ROWS 60
static lv_obj_t* g_hdr = nullptr;
static lv_obj_t* g_cont = nullptr;
static lv_obj_t* g_rows[MAX_ROWS] = {};
static uint32_t  g_row_mmsi[MAX_ROWS];

static void row_cb(lv_event_t* e) {
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_row_mmsi[k]) return;
    ui_radar_select(g_row_mmsi[k]);
    ui_detail_show(g_row_mmsi[k]);
    ui_main_goto_tab(PAGE_DETAIL);
}

void ui_list_build(lv_obj_t* parent) {
    g_hdr = lv_label_create(parent);
    lv_obj_set_pos(g_hdr, 6, 4);
    lv_obj_set_width(g_hdr, DISPLAY_WIDTH - 12);
    lv_label_set_long_mode(g_hdr, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_hdr, lv_color_hex(0x3DF25A), 0);
    lv_label_set_text(g_hdr, "Vessels by distance");

    g_cont = lv_obj_create(parent);
    lv_obj_set_size(g_cont, DISPLAY_WIDTH, CONTENT_HEIGHT - 36);
    lv_obj_set_pos(g_cont, 0, 36);
    lv_obj_set_style_bg_color(g_cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(g_cont, 0, 0);
    lv_obj_set_style_pad_all(g_cont, 0, 0);
    lv_obj_set_style_pad_row(g_cont, 1, 0);
    lv_obj_set_style_radius(g_cont, 0, 0);
    lv_obj_set_flex_flow(g_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_cont, LV_DIR_VER);
    lv_obj_set_style_bg_color(g_cont, lv_color_hex(0x1FA84A), LV_PART_SCROLLBAR);

    for (int k = 0; k < MAX_ROWS; k++) {
        lv_obj_t* row = lv_obj_create(g_cont);
        g_rows[k] = row;
        lv_obj_set_size(row, DISPLAY_WIDTH, 34);
        lv_obj_set_style_bg_color(row, (k % 2) ? lv_color_hex(0x04120A) : lv_color_black(), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_scroll_dir(row, LV_DIR_NONE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)k);
        lv_obj_t* l1 = lv_label_create(row);
        lv_obj_set_pos(l1, 0, 1);
        lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l1, lv_color_hex(0x3DF25A), 0);
        lv_obj_t* l2 = lv_label_create(row);
        lv_obj_set_pos(l2, 0, 19);
        lv_obj_set_style_text_font(l2, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(l2, lv_color_hex(0x1E9A45), 0);
        lv_obj_set_width(l1, DISPLAY_WIDTH - 8); lv_label_set_long_mode(l1, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(l2, DISPLAY_WIDTH - 8); lv_label_set_long_mode(l2, LV_LABEL_LONG_CLIP);
    }
}

void ui_list_refresh() {
    const Settings& s = settings_get();
    bool av = s.units == UNITS_AVIATION;
    ais_lock();
    Vessel* ac = ais_list(); int n = ais_count();
    const AisStats& st = ais_stats();
    int idx[MAX_VESSELS]; int m = 0;
    for (int i = 0; i < n; i++) {
        if (!ac[i].mmsi) continue;
        if (!ais_visible(ac[i])) continue;
        idx[m++] = i;
    }
    for (int i = 1; i < m; i++) { int v = idx[i], j = i - 1; while (j >= 0 && ac[idx[j]].dist_km > ac[v].dist_km) { idx[j + 1] = idx[j]; j--; } idx[j + 1] = v; }

    char h[120];
    if (m) {
        const Vessel& fa = ac[st.fastest >= 0 ? st.fastest : idx[0]];
        if (st.moving > 0 && fa.sog_kt >= 0.5f)
            snprintf(h, sizeof(h), "%d vessels within %d %s, %d moving.  Fastest %s %.1f kt.  Feed %.0f msg/s",
                     m, (int)(av ? s.range_km / 1.852f : s.range_km), av ? "nm" : "km", st.moving,
                     fa.name[0] ? fa.name : "?", fa.sog_kt, st.msgs_per_s);
        else
            snprintf(h, sizeof(h), "%d vessels within %d %s, none moving.  Feed %.0f msg/s",
                     m, (int)(av ? s.range_km / 1.852f : s.range_km), av ? "nm" : "km", st.msgs_per_s);
    } else {
        char stt[96]; net_status(stt, sizeof(stt));
        snprintf(h, sizeof(h), "No vessels.  %s  %s", st.connected ? "AIS feed connected." : "AIS feed not connected.", stt);
    }
    lv_label_set_text(g_hdr, h);

    for (int k = 0; k < MAX_ROWS; k++) {
        lv_obj_t* row = g_rows[k];
        if (k >= m) { lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN); g_row_mmsi[k] = 0; continue; }
        const Vessel& a = ac[idx[k]];
        g_row_mmsi[k] = a.mmsi;
        char l1[64], l2[96], nm[22];
        if (a.name[0]) strlcpy(nm, a.name, sizeof(nm)); else snprintf(nm, sizeof(nm), "MMSI %lu", (unsigned long)a.mmsi);
        snprintf(l1, sizeof(l1), "%s   %s%s%dm", nm, ais_type_name(a.shiptype), a.length ? "   " : "", a.length);
        if (!a.length) { char* p = strstr(l1, "0m"); if (p && p[2] == 0) *p = 0; }
        const char* stn = ais_navstat_name(a.navstat);
        if (a.sog_kt < 0.5f)
            snprintf(l2, sizeof(l2), "%.1f %s  %03.0f\xc2\xb0  %s  %s%s%s", av ? a.dist_km / 1.852f : a.dist_km, av ? "nm" : "km", a.bearing,
                     stn[0] ? stn : "stationary", ais_flag(a.mmsi), a.dest[0] ? "  to " : "", a.dest);
        else
            snprintf(l2, sizeof(l2), "%.1f %s  %03.0f\xc2\xb0  %.1f kt  cog %03.0f\xc2\xb0  %s%s%s", av ? a.dist_km / 1.852f : a.dist_km, av ? "nm" : "km", a.bearing,
                     a.sog_kt, a.cog < 360 ? a.cog : 0, ais_flag(a.mmsi), a.dest[0] ? "  to " : "", a.dest);
        lv_label_set_text(lv_obj_get_child(row, 0), l1);
        lv_label_set_text(lv_obj_get_child(row, 1), l2);
        lv_obj_set_style_text_color(lv_obj_get_child(row, 0),
            ais_is_special(a.shiptype) ? lv_color_hex(0xFF3B3B) : a.watch ? lv_color_hex(0xFFE44D) : (ais_is_tanker(a.shiptype) && s.highlight_mil) ? lv_color_hex(0xFFA030) : a.sog_kt < 0.5f ? lv_color_hex(0x1E9A45) : lv_color_hex(0x3DF25A), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
    ais_unlock();
}
