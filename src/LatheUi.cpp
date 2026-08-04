#include "LatheUi.h"

#include "Drawing.h"
#include "MachineProfile.h"
#include "System.h"
#include "polar.h"

#include <cstdio>
#include <cstring>

extern const GFXfont* font[];

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

namespace RoundLayout {
constexpr int CenterX       = 120;
constexpr int CenterY       = 120;
constexpr int SurfaceRadius = 118;
constexpr int OuterRing     = 116;
constexpr int InnerRing     = 114;
constexpr int SafeRadius    = 112;

constexpr int TitleY       = 20;
constexpr int StateX       = 90;
constexpr int StateY       = 31;
constexpr int StateWidth   = 60;
constexpr int StateHeight  = 20;

constexpr int DetailLeft   = 28;
constexpr int DetailRight  = 212;
constexpr int PreviewLeft  = 40;
constexpr int PreviewRight = 174;

constexpr int FooterX      = 64;
constexpr int FooterY      = 181;
constexpr int FooterWidth  = 112;
constexpr int FooterHeight = 18;

constexpr int RailCenterY  = 112;
constexpr int RailRadius   = 94;
constexpr int RailTouch    = 20;
constexpr int RailAngles[] = { 66, 43, 21, 0, -21, -43, -66 };

int integer_sqrt(int value) {
    if (value <= 0) return 0;
    int low = 0;
    int high = SurfaceRadius;
    while (low <= high) {
        int middle = (low + high) / 2;
        int square = middle * middle;
        if (square == value) return middle;
        if (square < value) low = middle + 1;
        else high = middle - 1;
    }
    return high;
}

int half_chord_at_y(int y, int radius = SafeRadius) {
    int dy = y - CenterY;
    if (dy < 0) dy = -dy;
    if (dy >= radius) return 0;
    return integer_sqrt(radius * radius - dy * dy);
}

int safe_width_at_y(int y, int inset) {
    int half = half_chord_at_y(y) - inset;
    return half > 0 ? half * 2 : 0;
}

void rail_point(int slot, int animation_phase, int direction, int* x, int* y) {
    int dx = 0;
    int dy = 0;
    int angle = RailAngles[slot] + direction * animation_phase * 8;
    r_degrees_to_xy(RailRadius, angle, &dx, &dy);
    *x = CenterX + dx;
    *y = RailCenterY - dy;
}

int rail_item(int selected, int slot) {
    if (selected < 0) selected = 0;
    if (selected > 7) selected = 7;
    return slot < selected ? slot : slot + 1;
}
}  // namespace RoundLayout

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
    if (thickness < 2) thickness = 2;
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int first = -(thickness / 2);
    for (int i = 0; i < thickness; ++i) {
        int offset = first + i;
        if (dx >= dy) canvas.drawLine(x0, y0 + offset, x1, y1 + offset, color);
        else canvas.drawLine(x0 + offset, y0, x1 + offset, y1, color);
    }
}

void outlined_rect(int x, int y, int width, int height, int color, int thickness = 2) {
    for (int i = 0; i < thickness && width > i * 2 && height > i * 2; ++i) {
        canvas.drawRect(x + i, y + i, width - i * 2, height - i * 2, color);
    }
}

void outlined_circle(int x, int y, int radius, int color, int thickness = 2) {
    for (int i = 0; i < thickness && radius > i; ++i) {
        canvas.drawCircle(x, y, radius - i, color);
    }
}

void draw_detail_surface(const char* title, state_t shown_state) {
    drawBackground(UiBg);
    canvas.fillCircle(RoundLayout::CenterX, RoundLayout::CenterY, RoundLayout::SurfaceRadius, UiPanel);
    canvas.drawCircle(RoundLayout::CenterX, RoundLayout::CenterY, RoundLayout::OuterRing, UiMuted);
    canvas.drawCircle(RoundLayout::CenterX, RoundLayout::CenterY, RoundLayout::InnerRing, UiPanelAlt);
    int title_width = RoundLayout::safe_width_at_y(RoundLayout::TitleY, 4);
    lathe_ui_fit_text(title, RoundLayout::CenterX, RoundLayout::TitleY, title_width, UiText, TINY, middle_center);
    lathe_ui_state_pill(RoundLayout::StateX, RoundLayout::StateY, shown_state);
}

void draw_main_surface(const char* title, state_t shown_state) {
    drawBackground(UiBg);
    canvas.fillCircle(RoundLayout::CenterX, RoundLayout::CenterY, RoundLayout::SurfaceRadius, UiBg);
    canvas.drawCircle(RoundLayout::CenterX, RoundLayout::CenterY, RoundLayout::OuterRing, UiPanelAlt);
    canvas.drawCircle(RoundLayout::CenterX, RoundLayout::CenterY, RoundLayout::InnerRing, UiMuted);

    // The instrument face intentionally sits left of display center so the
    // seven destinations have a permanent, hardware-like rail on the right.
    canvas.fillCircle(98, 119, 101, UiPanel);
    canvas.drawCircle(98, 119, 101, UiMuted);
    canvas.drawCircle(98, 119, 99, UiBlue);

    int previous_x = 0;
    int previous_y = 0;
    for (int slot = 0; slot < 7; ++slot) {
        int rail_x = 0;
        int rail_y = 0;
        RoundLayout::rail_point(slot, 0, 0, &rail_x, &rail_y);
        if (slot) line(previous_x, previous_y, rail_x, rail_y, UiPanelAlt, 48);
        canvas.fillCircle(rail_x, rail_y, 24, UiPanelAlt);
        previous_x = rail_x;
        previous_y = rail_y;
    }

    // A two-pixel cyan seam separates the data face from the navigation rail.
    previous_x = 0;
    previous_y = 0;
    for (int slot = 0; slot < 7; ++slot) {
        int dx = 0;
        int dy = 0;
        r_degrees_to_xy(70, RoundLayout::RailAngles[slot], &dx, &dy);
        int seam_x = RoundLayout::CenterX + dx;
        int seam_y = RoundLayout::RailCenterY - dy;
        if (slot) line(previous_x, previous_y, seam_x, seam_y, UiBlue, 2);
        previous_x = seam_x;
        previous_y = seam_y;
    }

    lathe_ui_fit_text(title, 99, 47, 92, UiMuted, TINY, middle_center);
    lathe_ui_state_pill(48, 56, shown_state);
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
    canvas.fillRoundRect(x, y, RoundLayout::StateWidth, RoundLayout::StateHeight, RoundLayout::StateHeight / 2, UiBg);
    canvas.drawRoundRect(x, y, RoundLayout::StateWidth, RoundLayout::StateHeight, RoundLayout::StateHeight / 2, color);
    canvas.drawRoundRect(x + 1, y + 1, RoundLayout::StateWidth - 2, RoundLayout::StateHeight - 2,
                         RoundLayout::StateHeight / 2 - 1, color);
    canvas.fillCircle(x + 10, y + RoundLayout::StateHeight / 2, 3, color);
    lathe_ui_fit_text(state_label(shown_state), x + 35, y + 11, 42, color, TINY, middle_center);
}

void lathe_ui_main_surface(const char* title, state_t shown_state) {
    draw_main_surface(title, shown_state);
}

void lathe_ui_detail_surface(const char* title) {
    draw_detail_surface(title, state);
}

void lathe_ui_badge(int x, int y, int width, const char* label, int color) {
    if (width < 8) return;
    canvas.fillRoundRect(x, y, width, 22, 11, UiBg);
    canvas.drawRoundRect(x, y, width, 22, 11, color);
    canvas.drawRoundRect(x + 1, y + 1, width - 2, 20, 10, color);
    lathe_ui_fit_text(label, x + width / 2, y + 13, width - 10, color, TINY, middle_center);
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
    const char* source = value ? value : "";
    if (width <= 0) return;
    if (canvas.textWidth(source, ::font[font]) <= width) {
        text(source, x, y, color, font, datum);
        return;
    }

    char buffer[80];
    size_t keep = 0;
    while (source[keep] && keep < sizeof(buffer) - 4) {
        buffer[keep] = source[keep];
        ++keep;
    }
    buffer[keep] = '\0';

    if (canvas.textWidth("...", ::font[font]) > width) return;
    do {
        memcpy(buffer + keep, "...", 4);
        if (canvas.textWidth(buffer, ::font[font]) <= width) break;
        if (keep == 0) return;
        --keep;
    } while (true);
    text(buffer, x, y, color, font, datum);
}

void lathe_ui_dro_row(int y, char axis, pos_t value, bool focused) {
    int color = focused ? UiBlue : UiText;
    char axis_text[2] = { axis, '\0' };
    text(axis_text, RoundLayout::DetailLeft, y, color, SMALL, middle_left);
    lathe_ui_fit_text(lathe_ui_position(value, 3), RoundLayout::DetailRight, y, 146, color, SMALL_MONO, middle_right);
    canvas.fillRect(RoundLayout::DetailLeft, y + 14, RoundLayout::DetailRight - RoundLayout::DetailLeft, 2,
                    focused ? UiBlue : UiPanelAlt);
}

void lathe_ui_value_row(int y, const char* label, const char* value, int color) {
    lathe_ui_fit_text(label, RoundLayout::DetailLeft, y, 82, UiMuted, TINY, middle_left);
    lathe_ui_fit_text(value, RoundLayout::DetailRight, y, 94, color, TINY, middle_right);
}

void lathe_ui_preview_dro_row(int y, char axis, pos_t value, bool focused) {
    int color = focused ? UiBlue : UiText;
    char axis_text[2] = { axis, '\0' };
    text(axis_text, RoundLayout::PreviewLeft, y, color, SMALL, middle_left);
    lathe_ui_fit_text(lathe_ui_position(value, 3), RoundLayout::PreviewRight, y, 122, color, SMALL_MONO, middle_right);
    canvas.fillRect(RoundLayout::PreviewLeft, y + 13, RoundLayout::PreviewRight - RoundLayout::PreviewLeft, 2,
                    focused ? UiBlue : UiPanelAlt);
}

void lathe_ui_preview_value_row(int y, const char* label, const char* value, int color) {
    lathe_ui_fit_text(label, RoundLayout::PreviewLeft, y, 70, UiMuted, TINY, middle_left);
    lathe_ui_fit_text(value, RoundLayout::PreviewRight, y, 76, color, TINY, middle_right);
}

void lathe_ui_action_legends(const char* left, const char* right, const char* dial) {
    if (left && *left) {
        canvas.fillCircle(48, 204, 3, UiCoral);
        lathe_ui_fit_text(left, 54, 204, 54, UiCoral, TINY, middle_left);
    }
    if (right && *right) {
        canvas.fillCircle(192, 204, 3, UiGreen);
        lathe_ui_fit_text(right, 186, 204, 54, UiGreen, TINY, middle_right);
    }
    if (dial && *dial) {
        lathe_ui_fit_text(dial, RoundLayout::CenterX, 220, 74, UiMuted, TINY, middle_center);
    }
}

void lathe_ui_footer_banner(const char* label, int color) {
    if (!label || !*label) return;
    canvas.fillRoundRect(RoundLayout::FooterX, RoundLayout::FooterY, RoundLayout::FooterWidth, RoundLayout::FooterHeight,
                         RoundLayout::FooterHeight / 2, UiBg);
    canvas.drawRoundRect(RoundLayout::FooterX, RoundLayout::FooterY, RoundLayout::FooterWidth, RoundLayout::FooterHeight,
                         RoundLayout::FooterHeight / 2, color);
    canvas.drawRoundRect(RoundLayout::FooterX + 1, RoundLayout::FooterY + 1, RoundLayout::FooterWidth - 2,
                         RoundLayout::FooterHeight - 2, RoundLayout::FooterHeight / 2 - 1, color);
    lathe_ui_fit_text(label, RoundLayout::CenterX, RoundLayout::FooterY + 10, RoundLayout::FooterWidth - 12, color, TINY,
                      middle_center);
}

void lathe_ui_round_clip() {
    constexpr int radius = RoundLayout::SurfaceRadius;
    constexpr int width = 240;
    for (int y = 0; y < width; ++y) {
        int dy = y - RoundLayout::CenterY;
        if (dy < 0) dy = -dy;
        if (dy > radius) {
            canvas.fillRect(0, y, width, 1, UiBg);
            continue;
        }
        int half = RoundLayout::integer_sqrt(radius * radius - dy * dy);
        int left = RoundLayout::CenterX - half;
        int right = RoundLayout::CenterX + half;
        if (left > 0) canvas.fillRect(0, y, left, 1, UiBg);
        if (right + 1 < width) canvas.fillRect(right + 1, y, width - right - 1, 1, UiBg);
    }
}

void lathe_ui_nav_icon(LatheNavItem item, int x, int y, int color, int scale) {
    if (scale < 1) scale = 1;
    int r = 5 * scale;
    int stroke = scale < 2 ? 2 : scale;
    switch (item) {
        case LatheNavItem::Status:
            canvas.drawArc(x, y, r + 2, r, 210, 510, color);
            line(x, y, x + r - 1, y - r + 2, color, stroke);
            canvas.fillCircle(x, y, scale + 1, color);
            break;
        case LatheNavItem::Home:
            outlined_rect(x - r, y - 2, r * 2, r + 2, color, 2);
            line(x - r - 2, y - 2, x, y - r - 4, color, stroke);
            line(x, y - r - 4, x + r + 2, y - 2, color, stroke);
            break;
        case LatheNavItem::Jog:
            line(x - r - 2, y, x + r + 2, y, color, stroke);
            line(x, y - r - 2, x, y + r + 2, color, stroke);
            canvas.fillTriangle(x + r + 3, y, x + r - 1, y - 3, x + r - 1, y + 3, color);
            canvas.fillTriangle(x - r - 3, y, x - r + 1, y - 3, x - r + 1, y + 3, color);
            break;
        case LatheNavItem::Probe:
            outlined_rect(x - 2 * scale, y - r - 3, 4 * scale, 3 * scale, color, 2);
            line(x, y - r + 1, x, y + r - 1, color, stroke);
            canvas.fillCircle(x, y + r + 1, scale + 1, color);
            line(x - r, y + r + 4, x + r, y + r + 4, color, stroke);
            break;
        case LatheNavItem::Tools:
            lathe_ui_tool_icon(LatheToolType::RightTurn, x, y, color, scale);
            break;
        case LatheNavItem::Files:
            canvas.drawRoundRect(x - r - 1, y - r + 1, r * 2 + 2, r * 2, 2, color);
            canvas.drawRoundRect(x - r, y - r + 2, r * 2, r * 2 - 2, 2, color);
            line(x - r + 1, y - r, x, y - r, color, stroke);
            break;
        case LatheNavItem::Macros:
            outlined_circle(x, y, r + 2, color, 2);
            canvas.fillTriangle(x - 2 * scale, y - 4 * scale, x - 2 * scale, y + 4 * scale, x + 4 * scale, y, color);
            break;
        case LatheNavItem::About:
            outlined_circle(x, y, r + 2, color, 2);
            text("i", x, y + 2, color, TINY, middle_center);
            break;
    }
}

void lathe_ui_orbital_rail(int selected, int animation_phase, int direction) {
    for (int slot = 0; slot < 7; ++slot) {
        int item = RoundLayout::rail_item(selected, slot);
        int sx = 0;
        int sy = 0;
        RoundLayout::rail_point(slot, animation_phase, direction, &sx, &sy);
        lathe_ui_nav_icon(static_cast<LatheNavItem>(item), sx, sy, UiText, 2);
    }
    canvas.fillCircle(190, RoundLayout::RailCenterY, 5, UiPanelAlt);
    outlined_circle(190, RoundLayout::RailCenterY, 5, UiBlue, 2);
}

int lathe_ui_rail_item_at(int selected, int x, int y) {
    for (int slot = 0; slot < 7; ++slot) {
        int sx = 0;
        int sy = 0;
        RoundLayout::rail_point(slot, 0, 0, &sx, &sy);
        int tx = x - sx;
        int ty = y - sy;
        if (tx * tx + ty * ty <= RoundLayout::RailTouch * RoundLayout::RailTouch) {
            return RoundLayout::rail_item(selected, slot);
        }
    }
    return -1;
}

void lathe_ui_tool_icon(LatheToolType type, int x, int y, int color, int scale) {
    int s = scale < 1 ? 1 : scale;
    int stroke = s < 2 ? 2 : s;
    switch (type) {
        case LatheToolType::RightTurn:
            line(x - 5 * s, y + 4 * s, x + 1 * s, y - 2 * s, color, stroke);
            canvas.fillTriangle(x, y - 4 * s, x + 6 * s, y - 3 * s, x + 4 * s, y + 2 * s, color);
            line(x - 5 * s, y + 6 * s, x - 1 * s, y + 2 * s, color, stroke);
            break;
        case LatheToolType::LeftTurn:
            line(x + 5 * s, y + 4 * s, x - 1 * s, y - 2 * s, color, stroke);
            canvas.fillTriangle(x, y - 4 * s, x - 6 * s, y - 3 * s, x - 4 * s, y + 2 * s, color);
            line(x + 5 * s, y + 6 * s, x + 1 * s, y + 2 * s, color, stroke);
            break;
        case LatheToolType::DrillQuarterInch:
            line(x, y - 5 * s, x, y + 5 * s, color, stroke);
            line(x - 2 * s, y - 4 * s, x + 2 * s, y - 1 * s, color, stroke);
            line(x + 2 * s, y - 1 * s, x - 2 * s, y + 3 * s, color, stroke);
            canvas.fillTriangle(x - 2 * s, y - 5 * s, x + 2 * s, y - 5 * s, x, y - 7 * s, color);
            break;
        case LatheToolType::BoringBar:
            line(x - 6 * s, y + 4 * s, x + 4 * s, y + 4 * s, color, stroke);
            line(x + 4 * s, y + 4 * s, x + 4 * s, y - 3 * s, color, stroke);
            canvas.fillTriangle(x + 2 * s, y - 4 * s, x + 6 * s, y - 4 * s, x + 5 * s, y, color);
            break;
        case LatheToolType::Probe:
            outlined_rect(x - 3 * s, y - 6 * s, 6 * s, 3 * s, color, 2);
            line(x, y - 3 * s, x, y + 3 * s, color, stroke);
            canvas.fillCircle(x, y + 5 * s, s, color);
            break;
        case LatheToolType::Thread:
            line(x - 5 * s, y + 5 * s, x + 5 * s, y - 5 * s, color, stroke);
            line(x - 3 * s, y + 6 * s, x + 3 * s, y + 6 * s, color, stroke);
            break;
        case LatheToolType::Groove:
        case LatheToolType::Parting:
            outlined_rect(x - s, y - 6 * s, 2 * s + 1, 12 * s, color, 2);
            line(x - 4 * s, y + 5 * s, x + 4 * s, y + 5 * s, color, stroke);
            break;
        case LatheToolType::Unset:
            outlined_circle(x, y, 6 * s, color, 2);
            text("?", x, y + 2, color, TINY, middle_center);
            break;
    }
}
