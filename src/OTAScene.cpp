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
