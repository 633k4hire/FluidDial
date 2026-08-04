// 2026 - Figamore

#ifdef USE_WIFI

#include "TransportScene.h"
#include "WiFiConnection.h"
#include "Drawing.h"
#include "System.h"
#include "LatheUi.h"

#include <algorithm>

struct TransportItem {
    const char*   label;
    const char*   sublabel;
    TransportMode mode;
    int           fill;
    int           outline;
};

#ifdef USE_ESPNOW
static constexpr int N_TRANSPORT = 3;
#else
static constexpr int N_TRANSPORT = 2;
#endif

static const TransportItem kItems[N_TRANSPORT] = {
    { "Wired",    "UART serial cable",  TransportMode::UART,   0x001a4d, 0x4da6ff },
    { "WiFi",     "TCP",     TransportMode::WIFI,   0x003300, 0x66ff66 },
#ifdef USE_ESPNOW
    { "ESP-NOW",  "No router needed",   TransportMode::ESPNOW, 0x1a001a, 0xcc66ff },
#endif
};

static constexpr int ITEM_H       = 46;
static constexpr int ITEM_PITCH   = 54;
static constexpr int START_Y_ROUND = 46;
static constexpr int START_Y_CYD   = 42;

namespace {
int latheTransportHeight() { return N_TRANSPORT == 2 ? 48 : 38; }
int latheTransportPitch() { return N_TRANSPORT == 2 ? 58 : 43; }
int latheTransportY(int index) { return (N_TRANSPORT == 2 ? 70 : 62) + index * latheTransportPitch(); }

void drawTransportIcon(TransportMode mode, int x, int y, int color) {
    if (mode == TransportMode::UART) {
        canvas.drawRoundRect(x - 11, y - 7, 18, 14, 3, color);
        canvas.drawRoundRect(x - 10, y - 6, 16, 12, 2, color);
        canvas.drawLine(x + 7, y - 3, x + 13, y - 3, color);
        canvas.drawLine(x + 7, y - 2, x + 13, y - 2, color);
        canvas.drawLine(x + 7, y + 3, x + 13, y + 3, color);
        canvas.drawLine(x + 7, y + 4, x + 13, y + 4, color);
        canvas.drawLine(x - 5, y - 10, x - 5, y - 7, color);
        canvas.drawLine(x - 4, y - 10, x - 4, y - 7, color);
        canvas.drawLine(x + 1, y - 10, x + 1, y - 7, color);
        canvas.drawLine(x + 2, y - 10, x + 2, y - 7, color);
    } else if (mode == TransportMode::WIFI) {
        canvas.drawArc(x, y + 5, 14, 12, 205, 335, color);
        canvas.drawArc(x, y + 5, 9, 7, 210, 330, color);
        canvas.fillCircle(x, y + 5, 3, color);
    } else {
        canvas.drawCircle(x - 7, y, 4, color);
        canvas.drawCircle(x - 7, y, 3, color);
        canvas.drawCircle(x + 7, y, 4, color);
        canvas.drawCircle(x + 7, y, 3, color);
        canvas.drawLine(x - 3, y - 2, x + 3, y - 2, color);
        canvas.drawLine(x - 3, y - 1, x + 3, y - 1, color);
        canvas.drawLine(x - 3, y + 2, x + 3, y + 2, color);
        canvas.drawLine(x - 3, y + 3, x + 3, y + 3, color);
        canvas.drawLine(x, y - 9, x, y + 9, color);
        canvas.drawLine(x + 1, y - 9, x + 1, y + 9, color);
    }
}

void drawTransportCard(int index, bool selected, bool active) {
    constexpr int X = 36;
    constexpr int W = 168;
    int y = latheTransportY(index);
    int h = latheTransportHeight();
    int icon_color = selected ? lathe_ui_blue() : active ? lathe_ui_green() : lathe_ui_muted();
    if (selected) {
        canvas.fillRoundRect(X, y, W, h, 8, lathe_ui_panel_alt());
        canvas.drawRoundRect(X, y, W, h, 8, lathe_ui_blue());
        canvas.drawRoundRect(X + 1, y + 1, W - 2, h - 2, 7, lathe_ui_blue());
    } else {
        canvas.drawRoundRect(X, y, W, h, 8, active ? lathe_ui_green() : lathe_ui_muted());
        canvas.drawRoundRect(X + 1, y + 1, W - 2, h - 2, 7, active ? lathe_ui_green() : lathe_ui_muted());
    }

    drawTransportIcon(kItems[index].mode, 57, y + h / 2, icon_color);
    int label_y = N_TRANSPORT == 2 ? y + 17 : y + 14;
    int sub_y   = N_TRANSPORT == 2 ? y + 34 : y + 28;
    text(kItems[index].label, 79, label_y, selected ? lathe_ui_text() : icon_color, SMALL, middle_left);
    lathe_ui_fit_text(kItems[index].sublabel, 79, sub_y, 76, lathe_ui_muted(), TINY, middle_left);
    if (active) {
        canvas.fillRoundRect(157, y + 7, 39, 15, 7, lathe_ui_bg());
        canvas.drawRoundRect(157, y + 7, 39, 15, 7, lathe_ui_green());
        text("ACTIVE", 176, y + 16, lathe_ui_green(), TINY, middle_center);
    }
}
}


static int modeIndex(TransportMode m) {
    for (int i = 0; i < N_TRANSPORT; i++) {
        if (kItems[i].mode == m) return i;
    }
    return 0;
}


void TransportScene::onEntry(void* /*arg*/) {
    _selected = modeIndex(wifi_get_transport());
    reDisplay();
}

void TransportScene::onEncoder(int delta) {
    _selected = (_selected + delta + N_TRANSPORT) % N_TRANSPORT;
    reDisplay();
}

void TransportScene::diagnosticPreview(int selection) {
    if (!_diagnostic_snapshot_active) {
        _saved_selected = _selected;
        _diagnostic_snapshot_active = true;
    }
    _selected = std::max(0, std::min(selection, N_TRANSPORT - 1));
    reDisplay();
}

void TransportScene::diagnosticRestore() {
    if (!_diagnostic_snapshot_active) return;
    _selected = _saved_selected;
    _diagnostic_snapshot_active = false;
}

void TransportScene::confirmSelection() {
    TransportMode chosen = kItems[_selected].mode;
    if (chosen != wifi_get_transport()) {
        // Gracefully close any live WiFi/Telnet connection (FIN) before the reboot
        wifi_shutdown();
        wifi_set_transport(chosen);
#ifdef ARDUINO
        ESP.restart();
#endif
    } else {
        pop_scene();
    }
}

void TransportScene::onDialButtonPress()  { confirmSelection(); }
void TransportScene::onGreenButtonPress() { confirmSelection(); }
void TransportScene::onRedButtonPress()   { pop_scene(); }

void TransportScene::onTouchClick() {
    if (lathe_ui_enabled()) {
        for (int i = 0; i < N_TRANSPORT; ++i) {
            int y = latheTransportY(i);
            if (touchX >= 36 && touchX <= 204 && touchY >= y && touchY <= y + latheTransportHeight()) {
                _selected = i;
                reDisplay();
                confirmSelection();
                return;
            }
        }
        return;
    }
    int start_y = round_display ? START_Y_ROUND : START_Y_CYD;
    int bx      = round_display ? 28 : 18;
    int bw      = round_display ? 184 : 284;
    for (int i = 0; i < N_TRANSPORT; i++) {
        int y = start_y + i * ITEM_PITCH;
        if (touchX >= bx && touchX <= bx + bw
            && touchY >= y - 2 && touchY < y + ITEM_H + 2) {
            _selected = i;
            reDisplay();
            confirmSelection();
            return;
        }
    }
}

void TransportScene::reDisplay() {
    if (lathe_ui_enabled()) {
        lathe_ui_detail_surface("TRANSPORT");
        TransportMode current = wifi_get_transport();
        for (int i = 0; i < N_TRANSPORT; ++i) {
            drawTransportCard(i, i == _selected, kItems[i].mode == current);
        }
        lathe_ui_action_legends("CANCEL", "SELECT", "");
        refreshDisplay();
        return;
    }

    background();

    if (round_display) {
        centered_text("Transport", 18);
        drawRect(70, 28, 100, 1, 0, DARKGREY);
    } else {
        centered_text("Transport", 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    TransportMode cur     = wifi_get_transport();
    int           start_y = round_display ? START_Y_ROUND : START_Y_CYD;
    int           bx      = round_display ? 28 : 18;
    int           bw      = round_display ? 184 : 204;

    for (int i = 0; i < N_TRANSPORT; i++) {
        int  y      = start_y + i * ITEM_PITCH;
        bool sel    = (i == _selected);
        bool active = (kItems[i].mode == cur);

        if (sel) {
            drawOutlinedRect(bx, y - 5, bw, ITEM_H, kItems[i].fill, kItems[i].outline);
        }

        if (active) {
            drawFilledCircle(bx + 8, y + ITEM_H / 2 - 1, 3, kItems[i].outline);
        }

        int label_color = sel ? WHITE : (active ? kItems[i].outline : LIGHTGREY);
        int sub_color   = sel ? kItems[i].outline : DARKGREY;

        centered_text(kItems[i].label,    y + 12, label_color, SMALL);
        centered_text(kItems[i].sublabel, y + 30, sub_color,   TINY);
    }

    drawButtonLegends("Cancel", "Select", "");
    refreshDisplay();
}

TransportScene transportScene;

void diagnostic_preview_transport(int selection) {
    transportScene.diagnosticPreview(selection);
}

void diagnostic_restore_transport_preview() {
    transportScene.diagnosticRestore();
}

#endif
