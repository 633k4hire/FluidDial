// 2026 - Figamore
// OTAScene.cpp — firmware update over WiFi.
//
// No credentials: spawns AP, browser enters WiFi creds, device restarts.
// Has credentials: connects STA, browser opens fluiddial.local for OTA.

#ifdef USE_WIFI

#include "OTAScene.h"
#include "WiFiConnection.h"
#ifdef ARDUINO
#    include "SecureOtaService.h"
#endif
#include "Drawing.h"
#include "System.h"
#include "LatheUi.h"

namespace {
void drawOtaShield(int y, int color, bool upload, bool error) {
    constexpr int x = 120;
    canvas.drawLine(x - 20, y - 17, x, y - 25, color);
    canvas.drawLine(x - 19, y - 16, x, y - 24, color);
    canvas.drawLine(x, y - 25, x + 20, y - 17, color);
    canvas.drawLine(x, y - 24, x + 19, y - 16, color);
    canvas.drawLine(x - 20, y - 17, x - 16, y + 12, color);
    canvas.drawLine(x - 19, y - 16, x - 15, y + 11, color);
    canvas.drawLine(x + 20, y - 17, x + 16, y + 12, color);
    canvas.drawLine(x + 19, y - 16, x + 15, y + 11, color);
    canvas.drawLine(x - 16, y + 12, x, y + 24, color);
    canvas.drawLine(x - 15, y + 11, x, y + 23, color);
    canvas.drawLine(x + 16, y + 12, x, y + 24, color);
    canvas.drawLine(x + 15, y + 11, x, y + 23, color);
    if (error) {
        canvas.drawLine(x - 8, y - 7, x + 8, y + 9, color);
        canvas.drawLine(x - 7, y - 7, x + 9, y + 9, color);
        canvas.drawLine(x + 8, y - 7, x - 8, y + 9, color);
        canvas.drawLine(x + 9, y - 7, x - 7, y + 9, color);
    } else if (upload) {
        canvas.drawLine(x, y + 11, x, y - 9, color);
        canvas.drawLine(x + 1, y + 11, x + 1, y - 9, color);
        canvas.fillTriangle(x, y - 14, x - 6, y - 6, x + 6, y - 6, color);
    } else {
        canvas.drawLine(x - 8, y, x - 2, y + 7, color);
        canvas.drawLine(x - 7, y, x - 1, y + 7, color);
        canvas.drawLine(x - 2, y + 7, x + 10, y - 8, color);
        canvas.drawLine(x - 1, y + 7, x + 11, y - 8, color);
    }
}

void drawOtaProgress(int pct) {
    constexpr int X = 36;
    constexpr int Y = 145;
    constexpr int W = 168;
    constexpr int H = 14;
    canvas.fillRoundRect(X, Y, W, H, 7, lathe_ui_bg());
    canvas.drawRoundRect(X, Y, W, H, 7, lathe_ui_muted());
    canvas.drawRoundRect(X + 1, Y + 1, W - 2, H - 2, 6, lathe_ui_muted());
    int fill = (W - 6) * pct / 100;
    if (fill > 0) canvas.fillRoundRect(X + 3, Y + 3, fill, H - 6, 4, lathe_ui_green());
    char value[8];
    snprintf(value, sizeof(value), "%d%%", pct);
    centered_text(value, 177, lathe_ui_text(), TINY);
}
}

void OTAScene::onEntry(void* arg) {
    wifi_start_ota_server();
    reDisplay();
}

void OTAScene::onRedButtonPress() {
#ifdef ARDUINO
    secure_ota_cancel_pairing();
#endif
    wifi_stop_ota_server();
 
#ifdef ARDUINO
    esp_restart();
#else
    pop_scene();
#endif
}

void OTAScene::onGreenButtonPress() {
#ifdef ARDUINO
    if (secure_ota_recovery_pending()) {
        secure_ota_confirm_recovery_physical();
        reDisplay();
        return;
    }
    if (secure_ota_pairing_pending()) {
        secure_ota_confirm_pairing_physical();
        reDisplay();
        return;
    }
#endif
    if (!wifi_ota_ap_mode() && wifi_ota_error()) {
        wifi_ota_force_ap_setup();
        reDisplay();
    }
}

void OTAScene::reDisplay() {
    if (lathe_ui_enabled()) {
        lathe_ui_detail_surface("SECURE OTA");
        int pct = wifi_ota_progress();
#ifdef ARDUINO
        if (secure_ota_recovery_pending()) {
            drawOtaShield(88, lathe_ui_amber(), false, false);
            lathe_ui_fit_text("RECOVERY DOWNGRADE", 120, 126, 168, lathe_ui_amber(), SMALL, middle_center);
            centered_text("SIGNED RECOVERY KEY", 150, lathe_ui_blue(), TINY);
            centered_text("CONFIRM ONLY IF INTENDED", 172, lathe_ui_muted(), TINY);
            lathe_ui_action_legends("CANCEL", "AUTHORIZE", "");
            refreshDisplay();
            return;
        }
        if (secure_ota_pairing_pending()) {
            drawOtaShield(84, lathe_ui_blue(), false, false);
            centered_text("COMPARE PAIRING CODE", 122, lathe_ui_muted(), TINY);
            lathe_ui_fit_text(secure_ota_pairing_code(), 120, 146, 168, lathe_ui_blue(), SMALL, middle_center);
            centered_text("MATCH FLUIDNC.LOCAL", 170, lathe_ui_muted(), TINY);
            lathe_ui_action_legends("CANCEL", "CONFIRM", "");
            refreshDisplay();
            return;
        }
#endif
        if (pct == -1) {
            drawOtaShield(90, RED, false, true);
            centered_text("UPLOAD FAILED", 132, RED, SMALL);
            centered_text("CHECK BROWSER", 157, lathe_ui_muted(), TINY);
            lathe_ui_action_legends("CANCEL", "", "");
            refreshDisplay();
            return;
        }
        if (pct >= 1) {
            drawOtaShield(88, pct == 100 ? lathe_ui_green() : lathe_ui_blue(), true, false);
            lathe_ui_fit_text(pct == 100 ? "UPLOAD COMPLETE" : "UPLOADING FIRMWARE", 120, 128, 168,
                              pct == 100 ? lathe_ui_green() : lathe_ui_amber(), SMALL, middle_center);
            drawOtaProgress(pct);
            lathe_ui_action_legends("CANCEL", "", "");
            refreshDisplay();
            return;
        }

        if (wifi_ota_ap_mode()) {
            drawOtaShield(82, lathe_ui_blue(), true, false);
            centered_text("CONNECT TO WI-FI", 119, lathe_ui_muted(), TINY);
            lathe_ui_fit_text(wifi_ap_ssid(), 120, 140, 168, lathe_ui_blue(), SMALL, middle_center);
            centered_text("OPEN 192.168.4.1", 166, lathe_ui_green(), TINY);
            lathe_ui_action_legends("CANCEL", "", "");
            refreshDisplay();
            return;
        }

        const char* error = wifi_ota_error();
        if (wifi_ota_sta_connected()) {
            drawOtaShield(82, lathe_ui_green(), true, false);
            centered_text("READY FOR UPLOAD", 121, lathe_ui_green(), SMALL);
            centered_text("FLUIDDIAL.LOCAL", 146, lathe_ui_text(), TINY);
            lathe_ui_fit_text(wifi_ota_ip(), 120, 168, 168, lathe_ui_muted(), TINY, middle_center);
            lathe_ui_action_legends("CANCEL", "", "");
        } else if (error) {
            drawOtaShield(82, RED, false, true);
            centered_text("WI-FI FAILED", 121, RED, SMALL);
            lathe_ui_fit_text(error, 120, 146, 168, lathe_ui_amber(), TINY, middle_center);
            centered_text("RE-ENTER WI-FI", 168, lathe_ui_muted(), TINY);
            lathe_ui_action_legends("CANCEL", "SETUP", "");
        } else {
            drawOtaShield(86, lathe_ui_blue(), true, false);
            lathe_ui_fit_text("CONNECTING TO WI-FI", 120, 130, 168, lathe_ui_amber(), SMALL, middle_center);
            centered_text("STAND BY", 157, lathe_ui_muted(), TINY);
            lathe_ui_action_legends("CANCEL", "", "");
        }
        refreshDisplay();
        return;
    }

    background();

    if (round_display) {
        centered_text("Secure OTA", 24);
        drawRect(70, 34, 100, 1, 0, DARKGREY);
    } else {
        centered_text("Secure OTA", 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    int pct = wifi_ota_progress();
#ifdef ARDUINO
    if (secure_ota_recovery_pending()) {
        centered_text("Recovery downgrade", 66, YELLOW, TINY);
        centered_text("Signed recovery key", 96, CYAN, SMALL);
        centered_text("Confirm only if intended", 124, DARKGREY, TINY);
        drawButtonLegends("Cancel", "Authorize", "");
        refreshDisplay();
        return;
    }
    if (secure_ota_pairing_pending()) {
        centered_text("Compare pairing code", 66, LIGHTGREY, TINY);
        centered_text(secure_ota_pairing_code(), 96, CYAN, SMALL);
        centered_text("Match FluidNC.local", 124, DARKGREY, TINY);
        centered_text("then confirm", 144, DARKGREY, TINY);
        drawButtonLegends("Cancel", "Confirm", "");
        refreshDisplay();
        return;
    }
#endif
    if (pct == -1) {
        centered_text("Upload Failed",  100, RED,      SMALL);
        centered_text("Check browser",  124, DARKGREY, TINY);
        drawButtonLegends("Cancel", "", "");
        refreshDisplay();
        return;
    }
    if (pct >= 1) {
        const char* lbl = (pct == 100) ? "Upload Done!" : "Uploading...";
        centered_text(lbl, 76, (pct == 100) ? GREEN : YELLOW, SMALL);
        static constexpr int BX = 30, BW = 180, BH = 16;
        int by = 100;
        drawOutlinedRect(BX, by, BW, BH, 0x1a1a1a, DARKGREY);
        int fill = (BW - 4) * pct / 100;
        if (fill > 0) drawRect(BX + 2, by + 2, fill, BH - 4, 0, GREEN);
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
        centered_text(buf, 128, WHITE, TINY);
        drawButtonLegends("Cancel", "", "");
        refreshDisplay();
        return;
    }

    // -- AP Mode: No credentials (show instructions to enter WiFi) --
    if (wifi_ota_ap_mode()) {
        int y = round_display ? 52 : 46;
        const int LINE = 22;
        centered_text("Connect to WiFi:", y, LIGHTGREY, TINY);  y += LINE;
        centered_text(wifi_ap_ssid(),     y, CYAN,      SMALL); y += LINE;
        drawRect(40, y - 2, 160, 1, 0, DARKGREY);               y += LINE - 4;
        centered_text("Open browser to:", y, LIGHTGREY, TINY);  y += LINE;
        centered_text("192.168.4.1",      y, GREEN,     SMALL); y += LINE + 4;
        drawButtonLegends("Cancel", "", "");
        refreshDisplay();
        return;
    }

    // --- STA Mode ---
    int y = round_display ? 50 : 44;
    const int LINE = 22;

    const char* err = wifi_ota_error();

    if (wifi_ota_sta_connected()) {
        centered_text("Open browser to:",  y, LIGHTGREY, TINY);   y += LINE;
        centered_text("fluiddial.local",   y, GREEN,     SMALL);  y += LINE + 4;
        const char* ip = wifi_ota_ip();
        if (ip && ip[0]) {
            char buf[48]; snprintf(buf, sizeof(buf), "or %s", ip);
            centered_text(buf, y, DARKGREY, TINY);
        }
        drawButtonLegends("Cancel", "", "");
    } else if (err) {
        centered_text("WiFi Failed",  y, RED, SMALL);          y += LINE + 2;
        centered_text(err,            y, YELLOW, TINY);        y += LINE;
        centered_text("Re-enter WiFi to retry.", y, DARKGREY, TINY);
        drawButtonLegends("Cancel", "Re-enter WiFi", "");
    } else {
        centered_text("Connecting to WiFi...", y, LIGHTGREY, TINY); y += LINE + 4;
        centered_text("Stand by.",             y, DARKGREY,  TINY);
        drawButtonLegends("Cancel", "", "");
    }

    refreshDisplay();
}

OTAScene otaScene;

#endif
