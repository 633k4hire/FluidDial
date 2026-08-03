#include "LatheUi.h"

#include "Drawing.h"
#include "MachineProfile.h"
#include "System.h"
#include "polar.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr int UiBg       = 0x0000;
constexpr int UiPanel    = 0x0861;
constexpr int UiPanelAlt = 0x10A2;
constexpr int UiBlue     = 0x45BF;
constexpr int UiGreen    = 0xBFE0;
constexpr int UiAmber    = 0xFDE0;
constexpr int UiCoral    = 0xF9C7;
constexpr int UiText     = 0xFFDE;
constexpr int UiMuted    = 0x8410;

const char* state_label(state_t shown_state) {
    switch (shown_state) {
        case Idle: return "IDLE";
        case Alarm: return "ALARM";
        case CheckMode: return "CHECK";
        case Homing: return "HOME";
        case Cycle: return "CYCLE";
        case Hold: return "HOLD";
        case Jog: return "JOG";
        case DoorOpen: return "DOOR";
        case DoorClosed: return "DOOR";
        case GrblSleep: return "SLEEP";
        case ConfigAlarm: return "CONFIG";
        case Critical: return "FAULT";
        case Disconnected: return "N/C";
    }
    return "--";
}

int state_color(state_t shown_state) {
    if (shown_state == Idle) return UiGreen;
    if (shown_state == Alarm || shown_state == ConfigAlarm || shown_state == Critical || shown_state == Disconnected) return RED;
    return UiAmber;
}

void line(int x0, int y0, int x1, int y1, int color, int thickness = 2) {
    for (int i = 0; i < thickness; ++i) {
        canvas.drawLine(x0, y0 + i, x1, y1 + i, color);
    }
}
}

int lathe_ui_bg() { return UiBg; }
int lathe_ui_panel() { return UiPanel; }
int lathe_ui_panel_alt() { return UiPanelAlt; }
int lathe_ui_blue() { return UiBlue; }
int lathe_ui_green() { return UiGreen; }
int lathe_ui_amber() { return UiAmber; }
int lathe_ui_coral() { return UiCoral; }
int lathe_ui_text() { return UiText; }
int lathe_ui_muted() { return UiMuted; }

bool lathe_ui_enabled() {
#if defined(MAIJKER_XZACT_LATHE) && defined(USE_M5)
    return round_display && machine_profile_is_lathe();
#else
    return false;
#endif
}

void lathe_ui_state_pill(int x, int y, state_t shown_state) {
    int color = state_color(shown_state);
    canvas.fillRoundRect(x, y, 64, 22, 11, UiBg);
    canvas.drawRoundRect(x, y, 64, 22, 11, color);
    canvas.drawRoundRect(x + 1, y + 1, 62, 20, 10, color);
    canvas.fillCircle(x + 11, y + 11, 3, color);
    text(state_label(shown_state), x + 39, y + 13, color, TINY, middle_center);
}

void lathe_ui_main_surface(const char* title) {
    drawBackground(UiBg);
    canvas.fillRoundRect(8, 8, 179, 224, 22, UiPanel);
    canvas.drawRoundRect(8, 8, 179, 224, 22, UiMuted);
    canvas.drawRoundRect(9, 9, 177, 222, 21, UiMuted);
    text(title, 22, 22, UiText, TINY, middle_left);
    lathe_ui_state_pill(22, 32);
}

void lathe_ui_detail_surface(const char* title) {
    drawBackground(UiBg);
    canvas.fillCircle(120, 120, 118, UiPanel);
    canvas.drawCircle(120, 120, 117, UiMuted);
    canvas.drawCircle(120, 120, 116, UiMuted);
    text(title, 20, 22, UiText, TINY, middle_left);
    lathe_ui_state_pill(88, 10);
}

void lathe_ui_badge(int x, int y, int width, const char* label, int color) {
    canvas.fillRoundRect(x, y, width, 22, 11, UiBg);
    canvas.drawRoundRect(x, y, width, 22, 11, color);
    canvas.drawRoundRect(x + 1, y + 1, width - 2, 20, 10, color);
    text(label, x + width / 2, y + 13, color, TINY, middle_center);
}

const char* lathe_ui_position(pos_t value, int digits) {
#ifdef E4_POS_T
    return e4_to_cstr(value, digits);
#else
    static char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.*f", digits, static_cast<double>(value));
    return buffer;
#endif
}

void lathe_ui_fit_text(const char* value, int x, int y, int width, int color, fontnum_t font, int datum) {
    char buffer[80];
    const char* source = value ? value : "";
    size_t max_chars = static_cast<size_t>(width / (font == SMALL ? 10 : 7));
    if (max_chars >= sizeof(buffer)) max_chars = sizeof(buffer) - 1;
    size_t length = strlen(source);
    if (length <= max_chars) {
        text(source, x, y, color, font, datum);
        return;
    }
    if (max_chars < 4) max_chars = 4;
    memcpy(buffer, source, max_chars - 3);
    memcpy(buffer + max_chars - 3, "...", 3);
    buffer[max_chars] = '\0';
    text(buffer, x, y, color, font, datum);
}

void lathe_ui_dro_row(int y, char axis, pos_t value, bool focused) {
    int color = focused ? UiBlue : UiText;
    char axis_text[2] = { axis, '\0' };
    text(axis_text, 24, y, color, SMALL, middle_left);
    text(lathe_ui_position(value, 3), 174, y, color, SMALL_MONO, middle_right);
    canvas.drawFastHLine(22, y + 14, 150, focused ? UiBlue : UiPanelAlt);
}

void lathe_ui_value_row(int y, const char* label, const char* value, int color) {
    text(label, 24, y, UiMuted, TINY, middle_left);
    text(value, 174, y, color, TINY, middle_right);
}

void lathe_ui_nav_icon(LatheNavItem item, int x, int y, int color, int scale) {
    int r = 5 * scale;
    switch (item) {
        case LatheNavItem::Status:
            canvas.drawArc(x, y, r + 2, r, 210, 510, color);
            line(x, y, x + r - 1, y - r + 2, color, scale);
            canvas.fillCircle(x, y, scale + 1, color);
            break;
        case LatheNavItem::Home:
            canvas.drawRect(x - r, y - 2, r * 2, r + 2, color);
            line(x - r - 2, y - 2, x, y - r - 4, color, scale);
            line(x, y - r - 4, x + r + 2, y - 2, color, scale);
            break;
        case LatheNavItem::Jog:
            line(x - r - 2, y, x + r + 2, y, color, scale);
            line(x, y - r - 2, x, y + r + 2, color, scale);
            canvas.fillTriangle(x + r + 3, y, x + r - 1, y - 3, x + r - 1, y + 3, color);
            canvas.fillTriangle(x - r - 3, y, x - r + 1, y - 3, x - r + 1, y + 3, color);
            break;
        case LatheNavItem::Probe:
            canvas.drawRect(x - 4, y - r - 3, 8, 4, color);
            line(x, y - r + 1, x, y + r - 1, color, scale);
            canvas.fillCircle(x, y + r + 1, 2, color);
            line(x - r, y + r + 4, x + r, y + r + 4, color, scale);
            break;
        case LatheNavItem::Tools:
            lathe_ui_tool_icon(LatheToolType::RightTurn, x, y, color, scale);
            break;
        case LatheNavItem::Files:
            canvas.drawRoundRect(x - r - 1, y - r + 1, r * 2 + 2, r * 2, 2, color);
            line(x - r + 1, y - r, x, y - r, color, scale);
            break;
        case LatheNavItem::Macros:
            canvas.drawCircle(x, y, r + 2, color);
            canvas.drawCircle(x, y, r + 1, color);
            canvas.fillTriangle(x - 2, y - 4, x - 2, y + 4, x + 4, y, color);
            break;
        case LatheNavItem::About:
            canvas.drawCircle(x, y, r + 2, color);
            canvas.drawCircle(x, y, r + 1, color);
            text("i", x, y + 2, color, TINY, middle_center);
            break;
    }
}

void lathe_ui_orbital_rail(int selected, int animation_phase, int direction) {
    canvas.drawArc(120, 120, 116, 113, -105, 105, UiPanelAlt);
    canvas.drawArc(120, 120, 114, 112, -105, 105, UiBlue);
    static const int offsets[7] = { -3, -2, -1, 1, 2, 3, 4 };
    static const int angles[7]  = { 105, 70, 35, 0, -35, -70, -105 };
    for (int slot = 0; slot < 7; ++slot) {
        int item = (selected + offsets[slot] + 8) % 8;
        int dx, dy;
        int animated_angle = angles[slot] + direction * animation_phase * 8;
        r_degrees_to_xy(105, animated_angle, &dx, &dy);
        int sx = 120 + dx;
        int sy = 120 - dy;
        canvas.fillCircle(sx, sy, 12, UiBg);
        canvas.drawCircle(sx, sy, 12, UiMuted);
        canvas.drawCircle(sx, sy, 11, UiMuted);
        lathe_ui_nav_icon(static_cast<LatheNavItem>(item), sx, sy, UiText);
    }
    canvas.fillRect(111, 226, 4, 3, UiCoral);
}

void lathe_ui_tool_icon(LatheToolType type, int x, int y, int color, int scale) {
    int s = scale < 1 ? 1 : scale;
    switch (type) {
        case LatheToolType::RightTurn:
            line(x - 6 * s, y + 6 * s, x + 3 * s, y - 3 * s, color, s);
            canvas.fillTriangle(x + 2 * s, y - 5 * s, x + 7 * s, y - 4 * s, x + 5 * s, y + s, color);
            break;
        case LatheToolType::LeftTurn:
            line(x + 6 * s, y + 6 * s, x - 3 * s, y - 3 * s, color, s);
            canvas.fillTriangle(x - 2 * s, y - 5 * s, x - 7 * s, y - 4 * s, x - 5 * s, y + s, color);
            break;
        case LatheToolType::DrillQuarterInch:
            line(x, y - 8 * s, x, y + 7 * s, color, s);
            line(x - 3 * s, y - 5 * s, x + 3 * s, y + 4 * s, color, s);
            canvas.fillTriangle(x - 2 * s, y - 8 * s, x + 2 * s, y - 8 * s, x, y - 11 * s, color);
            break;
        case LatheToolType::BoringBar:
            line(x - 7 * s, y + 5 * s, x + 5 * s, y + 5 * s, color, s);
            line(x + 5 * s, y + 5 * s, x + 5 * s, y - 5 * s, color, s);
            canvas.fillTriangle(x + 3 * s, y - 5 * s, x + 8 * s, y - 5 * s, x + 6 * s, y, color);
            break;
        case LatheToolType::Probe:
            line(x, y - 9 * s, x, y + 5 * s, color, s);
            canvas.drawRect(x - 4 * s, y - 10 * s, 8 * s, 4 * s, color);
            canvas.fillCircle(x, y + 8 * s, 2 * s, color);
            break;
        case LatheToolType::Thread:
            line(x - 6 * s, y + 7 * s, x + 6 * s, y - 7 * s, color, s);
            line(x - 3 * s, y + 7 * s, x + 3 * s, y + 7 * s, color, s);
            break;
        case LatheToolType::Groove:
        case LatheToolType::Parting:
            canvas.drawRect(x - 2 * s, y - 9 * s, 4 * s, 18 * s, color);
            line(x - 5 * s, y + 8 * s, x + 5 * s, y + 8 * s, color, s);
            break;
        case LatheToolType::Unset:
            canvas.drawCircle(x, y, 7 * s, color);
            text("?", x, y + 2, color, TINY, middle_center);
            break;
    }
}
