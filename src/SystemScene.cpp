// 2026 - Figamore
// SystemScene.cpp — "More" settings hub: display orientation, restart, sleep.

#ifdef USE_WIFI

#include "SystemScene.h"
#include "DisplaySettingsScene.h"
#include "BrightnessScene.h"
#include "WiFiSetupScene.h"
#include "WiFiConnection.h"   // wifi_request_ota_reboot()
#include "OTAScene.h"         // otaScene (simulator entry path)
#include "Drawing.h"
#include "System.h"
#include "FluidNCModel.h"
#include "LatheUi.h"

#include <algorithm>

struct SysItem {
    const char* label;
    const char* sublabel;
};

static const SysItem items[] = {
    { "Restart",    ""                   },
#ifdef USE_M5
    { "Sleep",      ""  },
    { "Brightness", ""     },
#else
    { "Orientation",    "" },
    { "Brightness", ""     },
#endif
    { "OTA Update", ""  },
};
static const int N_ITEMS = (int)(sizeof(items) / sizeof(items[0]));

static constexpr int ITEM_H_ROUND     = 38;
static constexpr int ITEM_PITCH_ROUND = 38;
static constexpr int ITEM_H_CYD       = 40;
static constexpr int ITEM_PITCH_CYD   = 42;
static constexpr int START_Y_ROUND    = 38;
static constexpr int START_Y_CYD      = 46;

namespace {
constexpr int LATHE_ITEM_X     = 28;
constexpr int LATHE_ITEM_Y     = 64;
constexpr int LATHE_ITEM_W     = 184;
constexpr int LATHE_ITEM_H     = 27;
constexpr int LATHE_ITEM_PITCH = 30;

void drawSystemIcon(int item, int x, int y, int color) {
    switch (item) {
        case 0:  // Restart
            canvas.drawArc(x, y, 10, 8, 35, 315, color);
            canvas.drawArc(x, y, 9, 7, 35, 315, color);
            canvas.fillTriangle(x + 7, y - 8, x + 13, y - 8, x + 11, y - 3, color);
            break;
        case 1:  // Sleep
            canvas.drawArc(x - 2, y, 11, 9, 55, 300, color);
            canvas.drawArc(x + 3, y, 9, 7, 100, 260, color);
            break;
        case 2:  // Brightness
            canvas.drawCircle(x, y, 6, color);
            canvas.drawCircle(x, y, 5, color);
            for (int d = -1; d <= 1; d += 2) {
                canvas.drawLine(x + d * 9, y, x + d * 13, y, color);
                canvas.drawLine(x + d * 9, y + 1, x + d * 13, y + 1, color);
                canvas.drawLine(x, y + d * 9, x, y + d * 13, color);
                canvas.drawLine(x + 1, y + d * 9, x + 1, y + d * 13, color);
                canvas.drawLine(x + d * 7, y + d * 7, x + d * 10, y + d * 10, color);
                canvas.drawLine(x + d * 7 + 1, y + d * 7, x + d * 10 + 1, y + d * 10, color);
                canvas.drawLine(x + d * 7, y - d * 7, x + d * 10, y - d * 10, color);
                canvas.drawLine(x + d * 7 + 1, y - d * 7, x + d * 10 + 1, y - d * 10, color);
            }
            break;
        default:  // OTA shield/upload
            canvas.drawLine(x - 9, y - 9, x, y - 13, color);
            canvas.drawLine(x - 8, y - 8, x, y - 12, color);
            canvas.drawLine(x, y - 13, x + 9, y - 9, color);
            canvas.drawLine(x, y - 12, x + 8, y - 8, color);
            canvas.drawLine(x - 9, y - 9, x - 7, y + 5, color);
            canvas.drawLine(x + 9, y - 9, x + 7, y + 5, color);
            canvas.drawLine(x - 7, y + 5, x, y + 11, color);
            canvas.drawLine(x + 7, y + 5, x, y + 11, color);
            canvas.drawLine(x, y + 5, x, y - 5, color);
            canvas.fillTriangle(x, y - 8, x - 4, y - 3, x + 4, y - 3, color);
            break;
    }
}
}

int SystemScene::itemCount() { return N_ITEMS; }

void SystemScene::onEntry(void* arg) {
    _selected = 0;
    reDisplay();
}

void SystemScene::onEncoder(int delta) {
    _selected += delta;
    if (_selected < 0)        _selected = N_ITEMS - 1;
    if (_selected >= N_ITEMS) _selected = 0;
    reDisplay();
}

void SystemScene::diagnosticPreview(int selection) {
    if (!_diagnostic_snapshot_active) {
        _saved_selected = _selected;
        _diagnostic_snapshot_active = true;
    }
    _selected = std::max(0, std::min(selection, N_ITEMS - 1));
    reDisplay();
}

void SystemScene::diagnosticRestore() {
    if (!_diagnostic_snapshot_active) return;
    _selected = _saved_selected;
    _diagnostic_snapshot_active = false;
}

void SystemScene::activateSelected() {
    switch (_selected) {
        case 0:
#ifdef ARDUINO
            esp_restart();
#endif
            break;
        case 1:
#ifdef USE_M5
            set_disconnected_state();
#    ifdef ARDUINO
            background();
            centered_text("Hold WAKE (M5) to wake", 118, RED, TINY);
            refreshDisplay();
            delay_ms(2000);
            deep_sleep(0);
#    endif
#else
            activate_scene(&displaySettingsScene);
#endif
            break;
        case 2:
            activate_scene(&brightnessScene);
            break;
        case 3:
            // The simulator can't reboot, so just push the scene to preview it.
#ifdef ARDUINO
            wifi_request_ota_reboot();
#else
            push_scene(&otaScene);
#endif
            break;
    }
}

void SystemScene::onDialButtonPress()  { activateSelected(); }
void SystemScene::onGreenButtonPress() { activateSelected(); }
void SystemScene::onRedButtonPress()   { activate_scene(&wifiSetupScene); }

void SystemScene::onTouchClick() {
    if (lathe_ui_enabled()) {
        for (int i = 0; i < N_ITEMS; ++i) {
            int y = LATHE_ITEM_Y + i * LATHE_ITEM_PITCH;
            if (touchX >= LATHE_ITEM_X && touchX <= LATHE_ITEM_X + LATHE_ITEM_W
                && touchY >= y && touchY <= y + LATHE_ITEM_H) {
                _selected = i;
                reDisplay();
                activateSelected();
                return;
            }
        }
        return;
    }
    int item_h    = round_display ? ITEM_H_ROUND    : ITEM_H_CYD;
    int item_pitch = round_display ? ITEM_PITCH_ROUND : ITEM_PITCH_CYD;
    int start_y   = round_display ? START_Y_ROUND   : START_Y_CYD;
    for (int i = 0; i < N_ITEMS; i++) {
        int y = start_y + i * item_pitch;
        if (touchX >= 30 && touchX <= 210 && touchY >= y - 2 && touchY < y + item_h - 4) {
            _selected = i;
            reDisplay();
            activateSelected();
            return;
        }
    }
}

void SystemScene::reDisplay() {
    if (lathe_ui_enabled()) {
        lathe_ui_detail_surface("SETTINGS");
        for (int i = 0; i < N_ITEMS; ++i) {
            int  y   = LATHE_ITEM_Y + i * LATHE_ITEM_PITCH;
            bool sel = i == _selected;
            if (sel) {
                canvas.fillRoundRect(LATHE_ITEM_X, y, LATHE_ITEM_W, LATHE_ITEM_H, 7, lathe_ui_panel_alt());
                canvas.drawRoundRect(LATHE_ITEM_X, y, LATHE_ITEM_W, LATHE_ITEM_H, 7, lathe_ui_blue());
                canvas.drawRoundRect(LATHE_ITEM_X + 1, y + 1, LATHE_ITEM_W - 2, LATHE_ITEM_H - 2, 6, lathe_ui_blue());
            }
            int color = sel ? lathe_ui_blue() : lathe_ui_muted();
            drawSystemIcon(i, 49, y + LATHE_ITEM_H / 2, color);
            text(items[i].label, 72, y + 15, sel ? lathe_ui_text() : lathe_ui_muted(), TINY, middle_left);
            canvas.drawLine(194, y + 10, 199, y + 14, color);
            canvas.drawLine(199, y + 14, 194, y + 18, color);
            canvas.drawLine(195, y + 10, 200, y + 14, color);
            canvas.drawLine(200, y + 14, 195, y + 18, color);
        }
        lathe_ui_action_legends("BACK", "SELECT", "");
        refreshDisplay();
        return;
    }

    int item_h     = round_display ? ITEM_H_ROUND    : ITEM_H_CYD;
    int item_pitch = round_display ? ITEM_PITCH_ROUND : ITEM_PITCH_CYD;
    int start_y    = round_display ? START_Y_ROUND   : START_Y_CYD;

    background();
    centered_text("Settings", 16);
    drawRect(55, 26, 130, 1, 0, DARKGREY);

    for (int i = 0; i < N_ITEMS; i++) {
        int  y   = start_y + i * item_pitch;
        bool sel = (i == _selected);

        if (sel) {
            drawOutlinedRect(30, y, 180, item_h - 4, 0x001a4d, 0x4da6ff);
        }

        bool has_sub  = items[i].sublabel[0] != '\0';
        int  label_y  = has_sub ? y + item_h / 4 : y + item_h / 2;
        int  sub_y    = y + item_h * 3 / 4;
        centered_text(items[i].label,    label_y, sel ? WHITE    : LIGHTGREY, SMALL);
        if (has_sub) {
            centered_text(items[i].sublabel, sub_y, sel ? 0x4da6ff : DARKGREY, TINY);
        }
    }

#if defined(USE_LOVYANGFX) && defined(CYD_BATTERY_ADC)
    if (!round_display) {
        int mv = battery_millivolts();
        if (mv > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d.%02d V", mv / 1000, (mv % 1000) / 10);
            centered_text(buf, 36, DARKGREY, TINY);
        }
    }
#endif

    drawButtonLegends("Back", "Select", "");
    refreshDisplay();
}

SystemScene systemScene;

void diagnostic_preview_system(int selection) {
    systemScene.diagnosticPreview(selection);
}

void diagnostic_restore_system_preview() {
    systemScene.diagnosticRestore();
}

#endif  // USE_WIFI
