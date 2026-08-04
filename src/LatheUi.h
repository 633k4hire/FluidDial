#pragma once

#include "FluidNCModel.h"
#include "LatheUiModel.h"
#include "Text.h"
enum class LatheNavItem : uint8_t {
    Status = 0,
    Home,
    Jog,
    Probe,
    Tools,
    Files,
    Macros,
    About,
};

int lathe_ui_bg();
int lathe_ui_panel();
int lathe_ui_panel_alt();
int lathe_ui_blue();
int lathe_ui_green();
int lathe_ui_amber();
int lathe_ui_coral();
int lathe_ui_text();
int lathe_ui_muted();

bool lathe_ui_enabled();

void lathe_ui_main_surface(const char* title, state_t shown_state = state);
void lathe_ui_detail_surface(const char* title);
void lathe_ui_state_pill(int x, int y, state_t shown_state = state);
void lathe_ui_badge(int x, int y, int width, const char* label, int color);
void lathe_ui_dro_row(int y, char axis, pos_t value, bool focused = false);
void lathe_ui_value_row(int y, const char* label, const char* value, int color);
void lathe_ui_preview_dro_row(int y, char axis, pos_t value, bool focused = false);
void lathe_ui_preview_value_row(int y, const char* label, const char* value, int color);
void lathe_ui_action_legends(const char* left, const char* right, const char* dial);
void lathe_ui_footer_banner(const char* label, int color);
void lathe_ui_round_clip();
void lathe_ui_fit_text(const char* value, int x, int y, int width, int color, fontnum_t font = TINY, int datum = middle_left);
void lathe_ui_orbital_rail(int selected, int animation_phase = 0, int direction = 0);
int  lathe_ui_rail_item_at(int selected, int x, int y);
void lathe_ui_nav_icon(LatheNavItem item, int x, int y, int color, int scale = 1);
void lathe_ui_tool_icon(LatheToolType type, int x, int y, int color, int scale = 1);

const char* lathe_ui_position(pos_t value, int digits = 3);
