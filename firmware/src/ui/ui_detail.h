#pragma once
#include <lvgl.h>
#include <stdint.h>
void ui_detail_build(lv_obj_t* parent);
void ui_detail_show(uint32_t mmsi);     // select vessel to display (caller switches to the page)
void ui_detail_refresh();               // re-read live data (call ~1 s while visible)
