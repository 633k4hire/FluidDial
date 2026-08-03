// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "FileParser.h"
#include "AboutScene.h"
#include "BootLog.h"
#include "MachineProfile.h"
#include "LatheUi.h"
#ifdef USE_WIFI
#    include "SystemScene.h"
#    include "WiFiConnection.h"
#endif

#include <cctype>

extern Scene menuScene;

extern const char* git_info;  // auto generated version.cpp

static const int MIN_BRIGHTNESS = 8;

namespace {
    bool useRoundLatheAbout() {
        return round_display && machine_profile_is_lathe();
    }

    const std::string& firmwareVersion() {
        static const std::string version = [] {
            std::string raw = git_info ? git_info : "unknown";
            size_t length = 0;
            while (length < raw.size() && (isdigit(static_cast<unsigned char>(raw[length])) || raw[length] == '.')) ++length;
            return length ? raw.substr(0, length) : raw;
        }();
        return version;
    }

    const std::string& firmwareBuild() {
        static const std::string build = [] {
            std::string raw = git_info ? git_info : "unknown";
            bool dirty = raw.size() >= 6 && raw.rfind("-dirty") == raw.size() - 6;
            if (dirty) raw.resize(raw.size() - 6);
            size_t dash = raw.rfind('-');
            std::string value = dash == std::string::npos ? raw : raw.substr(dash + 1);
            if (value.size() > 8) value.resize(8);
            return dirty ? value + " dev" : value;
        }();
        return build;
    }

    void drawAboutLine(int y, const char* value, int color = WHITE) {
        if (lathe_ui_enabled()) {
            lathe_ui_badge(30, y, 180, value, color);
            return;
        }
        drawOutlinedRect(30, y, 180, 26, NAVY, DARKGREY);
        auto_text(std::string(value ? value : ""), 120, y + 15, 164, color, TINY, middle_center);
    }
}

void AboutScene::onEntry(void* arg) {
    getBrightness();
    _round_page = 0;

    if (state != Disconnected) {
        send_line("$G");
        send_line("$I");
    }
}

void AboutScene::onDialButtonPress() {
    if (useRoundLatheAbout()) {
        _round_page = (_round_page + 1) % 2;
        reDisplay();
        return;
    }
    activate_scene(&menuScene);
}
void AboutScene::onGreenButtonPress() {
#ifdef USE_WIFI
    if (useRoundLatheAbout()) {
        activate_scene(&systemScene);
        return;
    }
#endif
#ifdef ARDUINO
    esp_restart();
#endif
}
void AboutScene::onRedButtonPress() {
    if (useRoundLatheAbout()) {
        activate_scene(&menuScene);
        return;
    }
#ifdef USE_M5
    set_disconnected_state();
#    ifdef ARDUINO
    background();
    centered_text("Press WAKE (M5) to wakeup", 118, RED, TINY);
    refreshDisplay();
    delay_ms(2000);

    deep_sleep(0);
#    else
    dbg_println("Sleep");
#    endif
#else
    next_layout(1);
    reDisplay();
#endif
}

void AboutScene::onTouchClick() {
    if (useRoundLatheAbout()) {
        _round_page = (_round_page + 1) % 2;
        reDisplay();
        return;
    }
    fnc_realtime(StatusReport);
    if (state == Idle) {
        send_line("$G");
        send_line("$I");
    }
}

void AboutScene::onEncoder(int delta) {
    if (useRoundLatheAbout()) {
        if (delta) {
            _round_page = (_round_page + (delta < 0 ? -1 : 1) + 2) % 2;
            reDisplay();
        }
        return;
    }
    if (delta > 0 && _brightness < 255) {
        display.setBrightness(++_brightness);
        setPref("brightness", _brightness);
    }
    if (delta < 0 && _brightness > MIN_BRIGHTNESS) {
        display.setBrightness(--_brightness);
        setPref("brightness", _brightness);
    }
    reDisplay();
}
void AboutScene::onStateChange(state_t old_state) {
    reDisplay();
}
void AboutScene::reDisplay() {
    if (useRoundLatheAbout()) {
        if (lathe_ui_enabled()) {
            lathe_ui_detail_surface(_round_page == 0 ? "DEVICE" : "CONTROLS");
        } else {
            background();
            centered_text("About", 15, WHITE, SMALL);
            drawRect(70, 26, 100, 1, 0, DARKGREY);
        }

        if (_round_page == 0) {
            centered_text("XZACT M5DIAL", 48, lathe_ui_enabled() ? lathe_ui_blue() : GREEN, SMALL);
            char version[48];
            char build[48];
            snprintf(version, sizeof(version), "Firmware %s", firmwareVersion().c_str());
            snprintf(build, sizeof(build), "Build %s", firmwareBuild().c_str());
            centered_text(version, 73, WHITE, TINY);
            centered_text(build, 91, DARKGREY, TINY);

            drawAboutLine(108, fnc_is_connected() ? "UART 1M / ONLINE" : "UART 1M / N/C",
                          fnc_is_connected() ? (lathe_ui_enabled() ? lathe_ui_green() : GREEN) : RED);
#ifdef USE_WIFI
            char wifi[48];
            snprintf(wifi, sizeof(wifi), wifi_is_connected() ? "IP %s" : "Wi-Fi Offline", wifi_local_ip());
            drawAboutLine(139, wifi, wifi_is_connected() ? (lathe_ui_enabled() ? lathe_ui_green() : GREEN)
                                                                 : (lathe_ui_enabled() ? lathe_ui_muted() : LIGHTGREY));
#else
            drawAboutLine(139, "Wi-Fi unavailable", DARKGREY);
#endif
            char machine[48];
            snprintf(machine, sizeof(machine), "Machine / %s", my_state_string);
            drawAboutLine(170, machine, state == Alarm ? RED : state == Idle ? (lathe_ui_enabled() ? lathe_ui_green() : GREEN)
                                                                                     : (lathe_ui_enabled() ? lathe_ui_amber() : YELLOW));
        } else {
            centered_text("OPERATOR MAP", 47, lathe_ui_enabled() ? lathe_ui_blue() : GREEN, SMALL);
            drawAboutLine(70, "Dial: select/change");
            drawAboutLine(104, "Touch: open/next");
            drawAboutLine(138, "Red/green: context");
            centered_text("FluidDial contributors", 181, DARKGREY, TINY);
        }

        drawButtonLegends("Back", "Settings", "Next");
        refreshDisplay();
        return;
    }

    background();
    drawStatus();

    const int key_x     = 118;
    const int val_x     = 122;
    const int y_spacing = 20;
    int       y         = 80;

    std::string version_str = "Ver ";
    version_str += git_info;
    centered_text(version_str.c_str(), y, LIGHTGREY, TINY);
    refreshDisplay();
    y += 10;
#ifdef MAIJKER_XZACT_LATHE
    centered_text("Maijker XZACt UART HMI", y += y_spacing, GREEN, TINY);
#endif
#ifdef FNC_BAUD  // FNC_BAUD might not be defined for Windows
    text("FNC baud:", key_x, y += y_spacing, LIGHTGREY, TINY, bottom_right);
    text(intToCStr(FNC_BAUD), val_x, y, GREEN, TINY, bottom_left);
#endif

#ifndef DEBUG_TO_USB  // backlight shares a pin with this.
    text("Brightness:", key_x, y += y_spacing, LIGHTGREY, TINY, bottom_right);
    text(intToCStr(_brightness), val_x, y, GREEN, TINY, bottom_left);
#endif

    if (wifi_ssid.length()) {
        std::string wifi_str = wifi_mode;
        if (wifi_mode == "No Wifi") {
            centered_text(wifi_str.c_str(), y += y_spacing, LIGHTGREY, TINY);
        } else {
            wifi_str += " ";
            wifi_str += wifi_ssid;
            centered_text(wifi_str.c_str(), y += y_spacing, LIGHTGREY, TINY);
            if (wifi_mode == "STA" && wifi_connected == "Not connected") {
                centered_text(wifi_connected.c_str(), y += y_spacing, RED, TINY);
            } else {
                wifi_str = "IP ";
                wifi_str += wifi_ip;
                centered_text(wifi_str.c_str(), y += y_spacing, LIGHTGREY, TINY);
            }
        }
    }

    // When the link is down, surface the last few boot/handshake log lines
    // here. UART0 is shared with the USB-serial bridge on CYD, so this is
    // the only way to see what is happening without unplugging.
    if (state == Disconnected) {
        const int log_line_spacing = 10;
        int       log_y            = y + 14;
        centered_text("Boot log (newest first):", log_y, YELLOW, TINY);
        const int max_lines = 6;
        int       avail     = bootlog_count();
        int       shown     = avail < max_lines ? avail : max_lines;
        for (int i = 0; i < shown; i++) {
            const char* line = bootlog_line(i);
            if (line) {
                log_y += log_line_spacing;
                centered_text(line, log_y, LIGHTGREY, TINY);
            }
        }
    }

#ifdef ARDUINO
    const char* greenLegend = "Restart";
#else
    const char* greenLegend = "";
#endif

    //drawOptionButton("Tool Menu", enable_tool_menu, 40, 135, 160);

    drawMenuTitle(current_scene->name());

#ifdef USE_M5
    drawButtonLegends("Sleep", greenLegend, "Menu");
#else
    drawButtonLegends("Layout", greenLegend, "Menu");
#endif
    drawError();  // if there is one
    refreshDisplay();
}

int AboutScene::getBrightness() {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
    }
    return _brightness;
}

AboutScene aboutScene;

void AboutScene::diagnosticPreview(int page) {
    _round_page = page <= 0 ? 0 : 1;
    reDisplay();
}

void diagnostic_preview_about(int page) {
    aboutScene.diagnosticPreview(page);
}
