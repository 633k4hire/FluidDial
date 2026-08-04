// 2026 - Figamore
// FirstBootScene.cpp — one-time setup wizard shown on first boot.
//
// Asks the user to choose a transport (WiFi/Telnet or wired UART; ESP-NOW too
// when built with -DUSE_ESPNOW). The choice is saved to NVS, then transitions
// directly to the appropriate scene.

#ifdef USE_WIFI

#    include "Scene.h"
#    include "Drawing.h"
#    include "Button.h"
#    include "WiFiConnection.h"
#    include "ESPNowPairingScene.h"
#    include "LatheUi.h"

extern void first_boot_complete();

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BTN_W = 160;
static constexpr int BTN_H = 40;
static constexpr int BTN_X = (240 - BTN_W) / 2;

static constexpr int UART_BTN_Y    = 60;
static constexpr int WIFI_BTN_Y    = 112;
static constexpr int ESPNOW_BTN_Y  = 164;

namespace {
int latheSetupChoiceCount() {
#ifdef USE_ESPNOW
    return 3;
#else
    return 2;
#endif
}

int latheSetupCardHeight() { return latheSetupChoiceCount() == 2 ? 48 : 38; }
int latheSetupCardPitch() { return latheSetupChoiceCount() == 2 ? 58 : 43; }
int latheSetupCardY(int index) { return (latheSetupChoiceCount() == 2 ? 76 : 66) + index * latheSetupCardPitch(); }

void drawSetupTransportIcon(TransportMode mode, int x, int y, int color) {
    if (mode == TransportMode::UART) {
        canvas.drawRoundRect(x - 11, y - 7, 18, 14, 3, color);
        canvas.drawRoundRect(x - 10, y - 6, 16, 12, 2, color);
        canvas.drawLine(x + 7, y - 3, x + 13, y - 3, color);
        canvas.drawLine(x + 7, y - 2, x + 13, y - 2, color);
        canvas.drawLine(x + 7, y + 3, x + 13, y + 3, color);
        canvas.drawLine(x + 7, y + 4, x + 13, y + 4, color);
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

void drawSetupCard(int index, TransportMode mode, const char* label, const char* detail,
                   bool selected, bool active) {
    constexpr int X = 36;
    constexpr int W = 168;
    int y = latheSetupCardY(index);
    int h = latheSetupCardHeight();
    int color = selected ? lathe_ui_blue() : active ? lathe_ui_green() : lathe_ui_muted();
    if (selected) canvas.fillRoundRect(X, y, W, h, 8, lathe_ui_panel_alt());
    canvas.drawRoundRect(X, y, W, h, 8, color);
    canvas.drawRoundRect(X + 1, y + 1, W - 2, h - 2, 7, color);
    drawSetupTransportIcon(mode, 57, y + h / 2, color);
    text(label, 79, y + (h == 48 ? 17 : 14), selected ? lathe_ui_text() : color, SMALL, middle_left);
    lathe_ui_fit_text(detail, 79, y + (h == 48 ? 34 : 28), 76, lathe_ui_muted(), TINY, middle_left);
    if (active) {
        canvas.fillRoundRect(157, y + 7, 39, 15, 7, lathe_ui_bg());
        canvas.drawRoundRect(157, y + 7, 39, 15, 7, lathe_ui_green());
        text("ACTIVE", 176, y + 16, lathe_ui_green(), TINY, middle_center);
    }
}
}

class FirstBootScene : public Scene {
    uint32_t _entry_ms = 0;
    int      _selected = 0;  // 0=Wired, 1=WiFi, 2=ESP-NOW
    bool     _diagnostic_snapshot_active = false;
    int      _saved_selected = 0;
    Button   wifiBtn, uartBtn, espnowBtn;

    bool selectable() { return (millis() - _entry_ms) >= 800; }

    int numChoices() {
#ifdef USE_ESPNOW
        return 3;
#else
        return 2;
#endif
    }

    void confirmSelection() {
        if (!selectable()) return;
        static const TransportMode modes[] = {
            TransportMode::UART,
            TransportMode::WIFI,
#ifdef USE_ESPNOW
            TransportMode::ESPNOW,
#endif
        };
        wifi_set_transport(modes[_selected]);
        first_boot_complete();
    }

public:
    FirstBootScene() : Scene("Setup", 4) {}

    void diagnosticPreview(int selection) {
        if (!_diagnostic_snapshot_active) {
            _saved_selected = _selected;
            _diagnostic_snapshot_active = true;
        }
        _selected = selection < 0 ? 0 : selection >= numChoices() ? numChoices() - 1 : selection;
        reDisplay();
    }

    void diagnosticRestore() {
        if (!_diagnostic_snapshot_active) return;
        _selected = _saved_selected;
        _diagnostic_snapshot_active = false;
    }

    void onEntry(void* arg = nullptr) override {
        _entry_ms = millis();
        _selected = 0;
        set_disconnected_state();  // prevent dispatch_events() redirect to menuScene
    }

    void onEncoder(int delta) override {
        int n = numChoices();
        _selected = ((_selected + delta) % n + n) % n;
        reDisplay();
    }

    void onGreenButtonPress() override { confirmSelection(); }

    bool showButtons() override { return true; }

    void onDialButtonPress() override {}

    void reDisplay() override {
        if (lathe_ui_enabled()) {
            lathe_ui_detail_surface("SETUP");
            centered_text("CONNECTION MODE", 61, lathe_ui_amber(), TINY);
            static const TransportMode modes[] = {
                TransportMode::UART,
                TransportMode::WIFI,
#ifdef USE_ESPNOW
                TransportMode::ESPNOW,
#endif
            };
            static const char* labels[] = { "WIRED", "WI-FI"
#ifdef USE_ESPNOW
                , "ESP-NOW"
#endif
            };
            static const char* details[] = { "UART CABLE", "TCP NETWORK"
#ifdef USE_ESPNOW
                , "DIRECT RADIO"
#endif
            };
            TransportMode active = wifi_get_transport();
            for (int i = 0; i < numChoices(); ++i) {
                drawSetupCard(i, modes[i], labels[i], details[i], i == _selected, modes[i] == active);
            }
            lathe_ui_action_legends("", "SELECT", "");
            refreshDisplay();
            return;
        }

        background();
        centered_text("Setup", 16);

        if (round_display) {
            drawRect(60, 28, 120, 1, 0, DARKGREY);
            centered_text("Connection Mode", 48, ORANGE, TINY);
        } else {
            drawRect(55, 22, 130, 1, 0, DARKGREY);
            centered_text("Connection Mode", 44, ORANGE, SMALL);
        }

        int uart_y   = round_display ? UART_BTN_Y   + 4  : UART_BTN_Y;
        int wifi_y   = round_display ? WIFI_BTN_Y   - 4  : WIFI_BTN_Y;
#ifdef USE_ESPNOW
        int espnow_y = round_display ? ESPNOW_BTN_Y - 12 : ESPNOW_BTN_Y;
#endif

        constexpr int SEL_EXPAND = 8;   // extra px on each side when selected
        constexpr int DOT_R      = 4;
        constexpr int DOT_OFFSET = SEL_EXPAND + DOT_R + 4;  // from selected btn left edge

        auto btnX = [&](int idx) { return _selected == idx ? BTN_X - SEL_EXPAND : BTN_X; };
        auto btnW = [&](int idx) { return _selected == idx ? BTN_W + SEL_EXPAND * 2 : BTN_W; };

        uartBtn.set(btnX(0), uart_y, btnW(0), BTN_H, "Wired",
                    0x001a4d, 0x4da6ff, 0x4da6ff,
                    [this]() { _selected = 0; confirmSelection(); });

        wifiBtn.set(btnX(1), wifi_y, btnW(1), BTN_H, "WiFi",
                    0x003300, 0x66ff66, 0x66ff66,
                    [this]() { _selected = 1; confirmSelection(); });

#ifdef USE_ESPNOW
        espnowBtn.set(btnX(2), espnow_y, btnW(2), BTN_H, "ESP-NOW",
                      0x1a0033, 0xcc66ff, 0xcc66ff,
                      [this]() { _selected = 2; confirmSelection(); });
#endif

        // Dot indicator to the left of the selected button
        int sel_ys[] = { uart_y, wifi_y
#ifdef USE_ESPNOW
            , espnow_y
#endif
        };
        int dot_x = (BTN_X - SEL_EXPAND) - DOT_OFFSET;
        int dot_y = sel_ys[_selected] + BTN_H / 2;
        canvas.fillCircle(dot_x, dot_y, DOT_R, WHITE);

        // if (!round_display) {
        //     centered_text("This can be changed later", 218, DARKGREY, TINY);
        // }
        
        drawButtonLegends("", "Select", "");
        refreshDisplay();
    }

    void onTouchClick() override {
        if (lathe_ui_enabled()) {
            for (int i = 0; i < numChoices(); ++i) {
                int y = latheSetupCardY(i);
                if (touchX >= 36 && touchX <= 204 && touchY >= y && touchY <= y + latheSetupCardHeight()) {
                    _selected = i;
                    reDisplay();
                    confirmSelection();
                    return;
                }
            }
            return;
        }
        uartBtn.handleTouch(touchX, touchY);
        wifiBtn.handleTouch(touchX, touchY);
#ifdef USE_ESPNOW
        espnowBtn.handleTouch(touchX, touchY);
#endif
    }
};

FirstBootScene firstBootScene;

void diagnostic_preview_first_boot(int selection) {
    firstBootScene.diagnosticPreview(selection);
}

void diagnostic_restore_first_boot_preview() {
    firstBootScene.diagnosticRestore();
}

#endif  // USE_WIFI
