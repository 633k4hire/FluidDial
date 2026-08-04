// 2026 - Figamore

#ifdef USE_WIFI

#include "WiFiSetupScene.h"
#include "WiFiConnection.h"
#include "PeerLink.h"
#include "ESPNowPairingScene.h"
#include "ESPNowMachineScene.h"
#include "TransportScene.h"
#include "Drawing.h"
#include "Menu.h"
#include "System.h"
#include "Button.h"
#include "SystemScene.h"
#include "LatheUi.h"

extern Scene       menuScene;
extern const char* git_info;

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BX = 20, BY = 28, BW = 200, BH = 34;       // status badge
static constexpr int CX = 15, CW = 210, CH = 28, CI = 8;        // info cards
static constexpr int SBX = 12, SBY = 160, SBW = 216, SBH = 36;  // switch button

static constexpr int CARD_Y0    = 68;
static constexpr int CARD_PITCH = CH + 4;  // 32 px per card row

namespace {
void drawConnectionHero(TransportMode mode, int y, int color) {
    constexpr int x = 120;
    if (mode == TransportMode::UART) {
        canvas.drawRoundRect(x - 19, y - 12, 30, 24, 5, color);
        canvas.drawRoundRect(x - 18, y - 11, 28, 22, 4, color);
        canvas.drawLine(x + 11, y - 6, x + 23, y - 6, color);
        canvas.drawLine(x + 11, y - 5, x + 23, y - 5, color);
        canvas.drawLine(x + 11, y + 5, x + 23, y + 5, color);
        canvas.drawLine(x + 11, y + 6, x + 23, y + 6, color);
        canvas.drawLine(x - 10, y - 18, x - 10, y - 12, color);
        canvas.drawLine(x - 9, y - 18, x - 9, y - 12, color);
        canvas.drawLine(x, y - 18, x, y - 12, color);
        canvas.drawLine(x + 1, y - 18, x + 1, y - 12, color);
    } else if (mode == TransportMode::WIFI) {
        canvas.drawArc(x, y + 8, 23, 20, 205, 335, color);
        canvas.drawArc(x, y + 8, 16, 13, 208, 332, color);
        canvas.drawArc(x, y + 8, 9, 6, 213, 327, color);
        canvas.fillCircle(x, y + 8, 3, color);
    } else {
        canvas.drawCircle(x - 13, y, 7, color);
        canvas.drawCircle(x - 13, y, 6, color);
        canvas.drawCircle(x + 13, y, 7, color);
        canvas.drawCircle(x + 13, y, 6, color);
        canvas.drawLine(x - 6, y - 4, x + 6, y - 4, color);
        canvas.drawLine(x - 6, y - 3, x + 6, y - 3, color);
        canvas.drawLine(x - 6, y + 4, x + 6, y + 4, color);
        canvas.drawLine(x - 6, y + 5, x + 6, y + 5, color);
        canvas.drawLine(x, y - 17, x, y + 17, color);
        canvas.drawLine(x + 1, y - 17, x + 1, y + 17, color);
    }
}

void drawConnectionCard(int y, const char* label, const char* detail, int color) {
    canvas.fillRoundRect(36, y, 168, 38, 8, lathe_ui_panel_alt());
    canvas.drawRoundRect(36, y, 168, 38, 8, color);
    canvas.drawRoundRect(37, y + 1, 166, 36, 7, color);
    lathe_ui_fit_text(label, 120, y + 14, 148, color, TINY, middle_center);
    lathe_ui_fit_text(detail, 120, y + 29, 148, lathe_ui_text(), TINY, middle_center);
}
}

// ─── Card drawing helpers ──────────────────────────────────────────────────────

static void drawCard(int y, const char* label, const char* value, int val_color = WHITE) {
    drawOutlinedRect(CX, y, CW, CH, NAVY, WHITE);
    int mid = y + CH / 2 + 2;
    text(label, CX + CI, mid, DARKGREY, TINY, middle_left);
    text(value, CX + CW - CI, mid, val_color, SMALL, middle_right);
}

static void drawCardAuto(int y, const char* label, const char* value, int val_color = WHITE) {
    drawOutlinedRect(CX, y, CW, CH, NAVY, WHITE);
    int mid = y + CH / 2 + 2;
    text(label, CX + CI, mid, DARKGREY, TINY, middle_left);
    static constexpr int VALUE_W = 130;
    auto_text(std::string(value), CX + CW - CI, mid, VALUE_W, val_color, SMALL, middle_right);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

static const char* signal_str(int bars) {
    switch (bars) {
        case 4:  return "Excellent";
        case 3:  return "Good";
        case 2:  return "Fair";
        case 1:  return "Weak";
        default: return "None";
    }
}

// ─── Event handlers ───────────────────────────────────────────────────────────

void WiFiSetupScene::onEntry(void* arg)    { reDisplay(); }
void WiFiSetupScene::onStateChange(state_t){ reDisplay(); }

void WiFiSetupScene::onModeSwitchButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap_and_restart();
    } else {
        push_scene(&transportScene);
    }
}

void WiFiSetupScene::onRedButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap();
        reDisplay();
    } else {
        activate_scene(&menuScene);
    }
}

void WiFiSetupScene::onGreenButtonPress() {
    if (wifi_in_ap_mode()) {
#ifdef ARDUINO
        ESP.restart();
#endif
    } else if (wifi_use_espnow_mode()) {
        if (espnow_profile_count() > 0) {
            push_scene(&espnowMachineScene);
        } else {
            push_scene(&espnowPairingScene);
        }
    } else if (!wifi_use_uart_mode()) {
        wifi_start_ap_setup();
        reDisplay();
    } else {
#ifdef ARDUINO
        ESP.restart();
#endif
    }
}

void WiFiSetupScene::onDialButtonPress() {
    if (!wifi_in_ap_mode()) {
        activate_scene(&systemScene);
    }
}

void WiFiSetupScene::onTouchClick() {
    if (lathe_ui_enabled()) {
        if (!wifi_in_ap_mode() && touchX >= 36 && touchX <= 204 && touchY >= 156 && touchY <= 186) {
            onModeSwitchButtonPress();
        }
        return;
    }
    modeSwitchBtn.handleTouch(touchX, touchY);
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

void WiFiSetupScene::drawApView() {
    if (lathe_ui_enabled()) {
        modeSwitchBtn.w = 0;
        modeSwitchBtn.h = 0;
        modeSwitchBtn.onPress = nullptr;
        drawConnectionHero(TransportMode::WIFI, 82, lathe_ui_amber());
        drawConnectionCard(109, "SETUP ACCESS POINT", wifi_ap_ssid(), lathe_ui_amber());
        drawConnectionCard(151, "OPEN IN BROWSER", "192.168.4.1", lathe_ui_green());
        lathe_ui_action_legends("EXIT", "RESTART", "");
        return;
    }
    // ── Status badge ──────────────────────────────────────────────────────────
    int by = round_display ? BY + 5 : BY - 3;
    int bx = round_display ? 55 : BX;
    int bw = round_display ? 130 : BW;
    int bh = round_display ? 24 : BH;
    int ty = round_display ? BY + bh / 2 + 10 : BY + bh / 2 + 3;
    drawOutlinedRect(bx - 5, by, bw + 10, bh + 6, 0x8400, 0x8400);  // dark orange
    centered_text("AP Mode", ty, WHITE, SMALL);

    // ── AP Info ────────────────────────────────────────────────────────────────
    int y = CARD_Y0 + 14;
    int line_height = 24;

    // SSID section
    centered_text("Connect to SSID:", y, LIGHTGREY, TINY);
    y += line_height;
    centered_text(wifi_ap_ssid(), y, CYAN, SMALL);

    y += line_height;
    drawRect(40, y - 2, 160, 1, 0, DARKGREY);  // divider

    // IP section
    y += line_height + 4;
    centered_text("Open Browser To:", y, LIGHTGREY, TINY);
    y += line_height;
    centered_text("192.168.4.1", y, GREEN, SMALL);

    // ── Button legends ────────────────────────────────────────────────────────
    drawButtonLegends("Exit", "Restart", "");
}

void WiFiSetupScene::drawSettingsView() {
    TransportMode transport  = wifi_get_transport();
    bool          uart_mode  = (transport == TransportMode::UART);
    bool          espnow_mode = (transport == TransportMode::ESPNOW);
    WiFiConfig    cfg        = wifi_active_config();
    bool          ws_ok      = websocket_is_connected();
    bool          wf_ok      = wifi_is_connected();

    if (lathe_ui_enabled()) {
        const char* mode_label = "WI-FI";
        const char* detail     = "CONNECTING";
        int         color      = lathe_ui_amber();

        if (uart_mode) {
            mode_label = "WIRED UART";
            detail     = "1 MBAUD / LOCAL";
            color      = lathe_ui_blue();
        } else if (espnow_mode) {
            color = lathe_ui_blue();
            if (espnow_is_connected()) {
                mode_label = "ESP-NOW READY";
                detail     = espnow_status_str();
                color      = lathe_ui_green();
            } else if (espnow_is_reconnecting()) {
                mode_label = "ESP-NOW SEARCHING";
                detail     = "SCANNING FOR MACHINE";
            } else if (espnow_is_paired()) {
                mode_label = "ESP-NOW PAIRED";
                detail     = "WAITING FOR MACHINE";
            } else {
                mode_label = "ESP-NOW";
                detail     = "NOT PAIRED";
            }
        } else if (!cfg.valid) {
            mode_label = "WI-FI NOT CONFIGURED";
            detail     = "PRESS SETUP";
            color      = RED;
        } else if (ws_ok) {
            mode_label = "FLUIDNC CONNECTED";
            detail     = cfg.ssid;
            color      = lathe_ui_green();
        } else if (wf_ok) {
            mode_label = "FLUIDNC CONNECTING";
            detail     = cfg.fluidnc_ip;
            color      = lathe_ui_amber();
        } else if (wifi_last_error()) {
            mode_label = "WI-FI ERROR";
            detail     = wifi_last_error();
            color      = RED;
        } else {
            mode_label = "WI-FI CONNECTING";
            detail     = cfg.ssid;
            color      = lathe_ui_amber();
        }

        drawConnectionHero(transport, 82, color);
        drawConnectionCard(109, mode_label, detail, color);
        modeSwitchBtn.w = 0;
        modeSwitchBtn.h = 0;
        modeSwitchBtn.onPress = nullptr;
        canvas.fillRoundRect(36, 156, 168, 30, 8, lathe_ui_panel_alt());
        canvas.drawRoundRect(36, 156, 168, 30, 8, lathe_ui_blue());
        canvas.drawRoundRect(37, 157, 166, 28, 7, lathe_ui_blue());
        lathe_ui_fit_text("CHANGE TRANSPORT", 120, 173, 150, lathe_ui_blue(), TINY, middle_center);

        const char* green_label;
        if (uart_mode) green_label = "RESTART";
        else if (espnow_mode) green_label = espnow_profile_count() > 0 ? "MACHINES" : "PAIR";
        else green_label = "SETUP";
        lathe_ui_action_legends("BACK", green_label, "MORE");
        return;
    }

    // ── Status badge ──────────────────────────────────────────────────────────
    int         badge_fill;
    int         badge_outline;
    int         badge_text;
    const char* badge_label;

    if (uart_mode) {
        badge_fill    = 0x001a4d;
        badge_outline = 0x4da6ff;
        badge_label   = "UART Mode";
        badge_text    = 0x4da6ff;
    } else if (espnow_mode) {
        if (espnow_is_connected()) {
            badge_fill    = 0x003300;
            badge_outline = 0xcc66ff;
            badge_label   = "Using ESP-NOW";
            badge_text    = 0xcc66ff;
        } else if (espnow_is_reconnecting()) {
            static const char* rc_frames[] = {"Searching", "Searching.", "Searching..", "Searching..."};
            badge_fill    = 0x1a0033;
            badge_outline = 0xcc66ff;
            badge_label   = rc_frames[(millis() / 400) % 4];
            badge_text    = 0xcc66ff;
        } else if (espnow_is_paired()) {
            badge_fill    = 0x1a001a;
            badge_outline = 0xcc66ff;
            badge_label   = "ESP-NOW Paired";
            badge_text    = 0xcc66ff;
        } else {
            badge_fill    = 0x1a001a;
            badge_outline = 0xcc66ff;
            badge_label   = "ESP-NOW";
            badge_text    = 0xcc66ff;
        }
    } else if (!cfg.valid) {
        badge_fill    = 0x4d0000;
        badge_outline = 0xe02b2b;
        badge_label   = "Not Configured";
        badge_text    = 0xe02b2b;
    } else if (ws_ok) {
        badge_fill    = 0x003300;
        badge_outline = 0x66ff66;
        badge_label   = "Ready";
        badge_text    = 0x66ff66;
    } else if (wf_ok) {
        static const char* nc_frames[] = {"FluidNC", "FluidNC.", "FluidNC..", "FluidNC..."};
        badge_fill    = 0x332200;
        badge_outline = YELLOW;
        badge_label   = nc_frames[(millis() / 400) % 4];
        badge_text    = YELLOW;
    } else if (wifi_last_error()) {
        badge_fill    = 0x4d0000;
        badge_outline = RED;
        badge_label   = "WiFi Error";
        badge_text    = WHITE;
    } else {
        static const char* wifi_frames[] = {"WiFi", "WiFi.", "WiFi..", "WiFi..."};
        badge_fill    = 0x2a0000;
        badge_outline = 0xe02b2b;
        badge_label   = wifi_frames[(millis() / 400) % 4];
        badge_text    = WHITE;
    }

    int bx = round_display ? 55 : BX;
    int by = round_display ? BY + 6 : BY - 3;
    int bw = round_display ? 130 : BW;
    int bh = round_display ? 24 : BH;
    int ty = round_display ? BY + bh / 2 + 9 : BY + bh / 2 + 3;
    drawOutlinedRect(bx - 5, by, bw + 10, bh + 6, badge_fill, badge_outline);
    centered_text(badge_label, ty, badge_text, round_display ? TINY : SMALL);

    // ── Info section ──────────────────────────────────────────────────────────
    int y = CARD_Y0;

    if (uart_mode) {
        y += 28;
        centered_text("1 Mbaud", y, 0xe02b2b, SMALL);
        y += 20;
        centered_text("Wired UART", y, WHITE, TINY);
    } else if (espnow_mode) {
        y += (round_display ? 8 : 14);
        if (!espnow_is_paired()) {
            centered_text("Not yet paired", y, DARKGREY, TINY);
            y += 18;
            centered_text("Press green to pair", y, 0xcc66ff, TINY);
        } else if (espnow_is_reconnecting()) {
            centered_text("Connection lost", y, YELLOW, TINY);
            y += 18;
            centered_text("Scanning...", y, 0xcc66ff, TINY);
        } else {
            y += 18;
            ESPNowProfileInfo profile;
            int active_profile = espnow_active_profile_index();
            if (active_profile >= 0 && espnow_get_profile((size_t)active_profile, profile)) {
                const char* name = profile.hostname[0] ? profile.hostname : "Selected Machine";
                auto_text(std::string(name), display.width() / 2, y, round_display ? 150 : 190, WHITE, SMALL);
                y += (round_display ? 18 : 22);
            } else {
                y += (round_display ? 14 : 20);
            }
            y += 14;
            if (espnow_is_connected()) {
                int8_t rssi = espnow_rssi();
                if (rssi != 0) {
                    char rssi_buf[16];
                    snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", rssi);
                    centered_text(rssi_buf, y, 0xcc66ff, SMALL);
                } else {
                    // centered_text(espnow_status_str(), y, 0xcc66ff, SMALL);
                }
            } else {
                centered_text(espnow_status_str(), y, DARKGREY, SMALL);
            }
        }
    } else if (!cfg.valid) {
        y += 14;
        centered_text("Press green button", y, 0xe02b2b, TINY);
        y += 18;
        centered_text("to setup WiFi", y, 0xe02b2b, TINY);
    } else {
        y += (round_display ? 8 : 10);
        centered_text("Network", y, DARKGREY, TINY);
        y += 20;
        centered_text(cfg.ssid, y, WHITE, SMALL);
        y += (round_display ? 14 : 20);
        drawRect(40, y, 160, 1, 0, DARKGREY);
        y += 14;

        if (wifi_last_error()) {
            centered_text("WiFi Error", y, DARKGREY, TINY);
            y += 20;
            centered_text(wifi_last_error(), y, RED, SMALL);
            y += 20;
            centered_text("Retrying...", y, 0x888888, TINY);
        } else {
            centered_text("FluidNC Address", y, DARKGREY, TINY);
            y += 20;
            int ip_color = ws_ok ? GREEN : wf_ok ? YELLOW : LIGHTGREY;
            centered_text(cfg.fluidnc_ip, y, ip_color, SMALL);
            y += 22;
        }
    }

    {
        int sbx = round_display ? 40 : SBX;
        int sbw = round_display ? 160 : SBW;
        int sby = round_display ? SBY : SBY + 6;
        modeSwitchBtn.font = round_display ? TINY : SMALL;
        modeSwitchBtn.set(sbx, sby, sbw, SBH, "Switch Mode",
                          0x001a4d, 0x4da6ff, 0x4da6ff,
                          [this]() { onModeSwitchButtonPress(); });
    }

    // ── Button legends ────────────────────────────────────────────────────────
    const char* green_label;
    if (uart_mode)   green_label = "Restart";
    else if (espnow_mode) green_label = espnow_profile_count() > 0 ? "Machines" : "Pair";
    else             green_label = "Setup";
    drawButtonLegends("Back", green_label, "More");
}

void WiFiSetupScene::reDisplay() {
    if (lathe_ui_enabled()) {
        lathe_ui_detail_surface("CONNECTION");
        if (wifi_in_ap_mode()) {
            drawApView();
        } else {
            drawSettingsView();
        }
        drawError();
        refreshDisplay();
        return;
    }

    background();

    const char* title;
    if (wifi_use_espnow_mode()) {
        title = round_display ? "ESP-NOW" : "Connection Settings";
    } else if (round_display) {
        if (wifi_in_ap_mode() || wifi_use_uart_mode() || !wifi_active_config().valid) {
            title = "WiFi Setup";
        } else if (websocket_is_connected()) {
            title = "Connected";
        } else if (wifi_is_connected()) {
            title = "Connecting";
        } else {
            title = "WiFi Setup";
        }
    } else {
        if (wifi_in_ap_mode() || wifi_use_uart_mode() || !wifi_active_config().valid) {
            title = "Connection Settings";
        } else if (websocket_is_connected()) {
            title = " Connected to FluidNC";
        } else if (wifi_is_connected()) {
            title = " Connecting to FluidNC";
        } else {
            title = "Connecting to WiFi";
        }
    }
    if (round_display) {
        centered_text(title, 18);
        drawRect(70, 28, 100, 1, 0, DARKGREY);
    } else {
        centered_text(title, 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    if (wifi_in_ap_mode()) {
        drawApView();
    } else {
        drawSettingsView();
    }

    drawError();
    refreshDisplay();
}

WiFiSetupScene wifiSetupScene;

#endif  // USE_WIFI
