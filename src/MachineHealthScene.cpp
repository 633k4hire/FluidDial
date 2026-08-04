#include "MachineHealthScene.h"

#include "Drawing.h"
#include "FluidNCModel.h"
#include "HomingScene.h"
#include "LatheModel.h"
#include "MachineProfile.h"
#include "MachineStateActions.h"
#include "System.h"
#ifdef USE_WIFI
#    include "WiFiConnection.h"
#endif
#include "alarm.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
    constexpr int RowX = 28;
    constexpr int RowW = 184;
    constexpr int RowH = 27;
    constexpr int RowY[] = { 52, 83, 114, 145 };

    const char* operatorLinkText() {
        switch (operator_link_state()) {
            case OperatorLinkState::Disconnected: return "Disconnected";
            case OperatorLinkState::Synchronizing: return "Synchronizing";
            case OperatorLinkState::Ready: return "Ready";
            case OperatorLinkState::CommandPending: return "Command pending";
            case OperatorLinkState::Recoverable: return "Needs attention";
            case OperatorLinkState::Updating: return "Updating";
        }
        return "Unknown";
    }

    int operatorLinkColor() {
        switch (operator_link_state()) {
            case OperatorLinkState::Ready: return GREEN;
            case OperatorLinkState::Disconnected: return RED;
            case OperatorLinkState::Recoverable: return YELLOW;
            default: return LIGHTGREY;
        }
    }

    bool errorActive() {
        return lastError && (milliseconds() - errorExpire) < 0;
    }

    int displayAxis(char axis_char) {
        for (int axis = 0; axis < profile_axis_count(); ++axis) {
            if (profile_axis_char(axis) == axis_char) {
                return axis;
            }
        }
        return -1;
    }

    bool axisLimitActive(int display_axis) {
        if (display_axis < 0) {
            return false;
        }
        int machine_axis = profile_machine_axis(display_axis);
        return machine_axis >= 0 && machine_axis < 6 && myLimitSwitches[machine_axis];
    }
}

void MachineHealthScene::onEntry(void* arg) {
    bool confirmed = machine_state_handle_confirmation(arg);
    if (!confirmed) {
        _page = Page::Overview;
    }
    _preview = Preview::Live;
    request_lathe_status();
}

void MachineHealthScene::nextPage(int delta) {
    int page = static_cast<int>(_page);
    page = (page + (delta < 0 ? -1 : 1) + 4) % 4;
    _page = static_cast<Page>(page);
    _preview = Preview::Live;
    reDisplay();
}

void MachineHealthScene::onEncoder(int delta) {
    if (delta) {
        nextPage(delta);
    }
}

void MachineHealthScene::onDialButtonPress() { pop_scene(); }
void MachineHealthScene::onRedButtonPress() { machine_state_red_action(); }
void MachineHealthScene::onGreenButtonPress() { machine_state_green_action(); }
void MachineHealthScene::onTouchClick() { nextPage(1); }
void MachineHealthScene::onStateChange(state_t) { request_redisplay(); }
void MachineHealthScene::onDROChange() { request_redisplay(); }
void MachineHealthScene::onLimitsChange() { request_redisplay(); }

void MachineHealthScene::onPoll() {
    lathe_poll_command();
    uint32_t now = millis();
    if (_last_refresh_ms == 0 || (uint32_t)(now - _last_refresh_ms) >= 1500) {
        _last_refresh_ms = now;
        request_lathe_status();
    }
}

void MachineHealthScene::drawHeader(const char* title, int title_color) {
    background();
    centered_text("HEALTH", 16, WHITE, TINY);
    drawRect(70, 25, 100, 1, 0, DARKGREY);
    centered_text(title, 37, title_color, TINY);

    char page[8];
    snprintf(page, sizeof(page), "%d / 4", static_cast<int>(_page) + 1);
    centered_text(page, 190, DARKGREY, TINY);
}

void MachineHealthScene::drawRow(int y, const char* label, const char* value, int value_color) {
    drawOutlinedRect(RowX, y, RowW, RowH, NAVY, DARKGREY);
    int mid = y + RowH / 2 + 2;
    auto_text(std::string(label ? label : ""), RowX + 9, mid, 52, DARKGREY, TINY, middle_left);
    drawRect(RowX + 68, y + 5, 1, RowH - 10, 0, DARKGREY);
    auto_text(std::string(value ? value : ""),
              RowX + RowW - 9,
              mid,
              103,
              value_color,
              TINY,
              middle_right);
}

static void drawFullRow(int y, const char* value, int value_color) {
    drawOutlinedRect(RowX, y, RowW, RowH, NAVY, DARKGREY);
    auto_text(std::string(value ? value : ""), 120, y + RowH / 2 + 2, RowW - 18,
              value_color, TINY, middle_center);
}

void MachineHealthScene::drawOverview() {
    drawHeader("Overview", state == Alarm ? RED : state == Idle ? GREEN : YELLOW);
    drawRow(RowY[0], "Link", fnc_is_connected() ? "Online" : "N/C", fnc_is_connected() ? GREEN : RED);
    drawRow(RowY[1], "State", my_state_string, state == Alarm ? RED : state == Idle ? GREEN : YELLOW);

    int x_axis = displayAxis('X');
    int z_axis = displayAxis('Z');
    bool x_homed = x_axis >= 0 && is_axis_homed(x_axis);
    bool z_homed = z_axis >= 0 && is_axis_homed(z_axis);
    const char* homing = x_homed && z_homed ? "X/Z ready" : x_homed ? "X ok Z home" : z_homed ? "X home Z ok" : "X/Z needed";
    drawRow(RowY[2], "Home", homing, x_homed && z_homed ? GREEN : YELLOW);

    const LatheStatus& lathe = lathe_status();
    char summary[48];
    const char* spindle = lathe.effective_rpm > 0.5f ? "RUN" : "STOP";
    snprintf(summary, sizeof(summary), "T%s / %s", lathe.active_tool > 0 ? intToCStr(lathe.active_tool) : "-", spindle);
    drawRow(RowY[3], "Tool", summary, lathe.effective_rpm > 0.5f ? GREEN : LIGHTGREY);
}

void MachineHealthScene::drawAlarm() {
    bool fixture = _preview == Preview::HomingAlarm;
    state_t shown_state = fixture ? Alarm : state;
    int shown_alarm = fixture ? 14 : lastAlarm;
    bool active_alarm = shown_state == Alarm;
    drawHeader("Alarms", active_alarm ? RED : GREEN);

    if (!active_alarm && !errorActive()) {
        drawRow(RowY[0], "State", "No alarms", GREEN);
        drawRow(RowY[1], "Alarm", "None", LIGHTGREY);
        drawRow(RowY[2], "Error", "None", LIGHTGREY);
        drawRow(RowY[3], "Do", "Ready", GREEN);
        return;
    }

    char alarm_code[20];
    snprintf(alarm_code, sizeof(alarm_code), active_alarm ? "Alarm %d" : "None", shown_alarm);
    drawRow(RowY[0], "State", alarm_code, active_alarm ? RED : LIGHTGREY);
    drawRow(RowY[1], "Why", active_alarm ? alarm_name_short[shown_alarm] : "None", active_alarm ? YELLOW : LIGHTGREY);
    drawRow(RowY[2], "Error", errorActive() ? decode_error_number(lastError) : "None", errorActive() ? YELLOW : LIGHTGREY);

    const char* guidance = "Reset required";
    if (machine_alarm_is_homing(shown_alarm)) {
        guidance = "Home X/Z";
    } else if (shown_alarm == 4 || shown_alarm == 5) {
        guidance = "Check probe input";
    }
    drawRow(RowY[3], "Do", guidance, YELLOW);
}

void MachineHealthScene::drawReadiness() {
    drawHeader("Readiness", operator_machine_actions_available() ? GREEN : YELLOW);
    int x_axis = displayAxis('X');
    int z_axis = displayAxis('Z');
    bool x_homed = x_axis >= 0 && is_axis_homed(x_axis);
    bool z_homed = z_axis >= 0 && is_axis_homed(z_axis);
    bool x_limit = axisLimitActive(x_axis);
    bool z_limit = axisLimitActive(z_axis);
    char x_status[24];
    char z_status[24];
    snprintf(x_status, sizeof(x_status), "%s / %s", x_homed ? "HOME" : "NEED", x_limit ? "LIM ON" : "CLR");
    snprintf(z_status, sizeof(z_status), "%s / %s", z_homed ? "HOME" : "NEED", z_limit ? "LIM ON" : "CLR");
    drawRow(RowY[0], "X", x_status, x_limit ? RED : x_homed ? GREEN : YELLOW);
    drawRow(RowY[1], "Z", z_status, z_limit ? RED : z_homed ? GREEN : YELLOW);
    drawRow(RowY[2], "Probe", myProbeSwitch ? "Active" : "Clear", myProbeSwitch ? YELLOW : GREEN);

    const char* action = "Ready";
    int action_color = GREEN;
    if (!fnc_is_connected()) {
        action = "Link unavailable";
        action_color = RED;
    } else if (state == Alarm) {
        action = "Alarm active";
        action_color = RED;
    } else if (lathe_command_blocks_actions()) {
        action = lathe_command_status_text();
        action_color = YELLOW;
    } else if (!operator_machine_actions_available()) {
        action = "Synchronizing";
        action_color = YELLOW;
    }
    drawRow(RowY[3], "Act", action, action_color);
}

void MachineHealthScene::drawConnections() {
    const LatheStatus& lathe = lathe_status();
    bool fixture_fault = _preview == Preview::EncoderFault;
    drawHeader("Connections", fixture_fault || lathe.feedback_fault ? RED : GREEN);
    char uart_line[28];
    snprintf(uart_line, sizeof(uart_line), "UART / %s", fnc_is_connected() ? "Online" : "N/C");
    drawFullRow(RowY[0], uart_line, fnc_is_connected() ? GREEN : RED);
    char sync_line[40];
    snprintf(sync_line, sizeof(sync_line), "Sync / %s", operatorLinkText());
    drawFullRow(RowY[1], sync_line, operatorLinkColor());

#ifdef USE_WIFI
    const char* wifi_value = wifi_is_connected() ? wifi_local_ip() : "Offline";
    char wifi_line[32];
    snprintf(wifi_line, sizeof(wifi_line), wifi_is_connected() ? "IP %s" : "Wi-Fi / %s", wifi_value);
    drawFullRow(RowY[2], wifi_line, wifi_is_connected() ? GREEN : LIGHTGREY);
#else
    drawFullRow(RowY[2], "Wi-Fi / Unavailable", DARKGREY);
#endif

    const char* capability = "Not installed";
    int capability_color = DARKGREY;
    if (fixture_fault || lathe.feedback_fault) {
        capability = "Encoder fault";
        capability_color = RED;
    } else if (lathe.encoder_enabled && lathe.feedback_stale) {
        capability = "Encoder stale";
        capability_color = YELLOW;
    } else if (lathe.encoder_enabled && !lathe.encoder_capture) {
        capability = "No capture";
        capability_color = YELLOW;
    } else if (lathe.encoder_enabled && lathe.encoder_capture) {
        capability = "Threading ready";
        capability_color = GREEN;
    }
    char encoder_line[40];
    snprintf(encoder_line, sizeof(encoder_line), "ENC / %s", capability);
    drawFullRow(RowY[3], encoder_line, capability_color);
}

void MachineHealthScene::reDisplay() {
    switch (_page) {
        case Page::Overview: drawOverview(); break;
        case Page::Alarm: drawAlarm(); break;
        case Page::Readiness: drawReadiness(); break;
        case Page::Connections: drawConnections(); break;
    }

    state_t shown_state = _preview == Preview::HomingAlarm ? Alarm : state;
    int shown_alarm = _preview == Preview::HomingAlarm ? 14 : lastAlarm;
    MachineStateActionLabels labels = machine_state_action_labels(shown_state, shown_alarm, true);
    drawButtonLegends(labels.red, labels.green, "Back");
    refreshDisplay();
}

void MachineHealthScene::diagnosticPreview(int selection) {
    _preview = Preview::Live;
    if (selection == 4) {
        _page = Page::Alarm;
        _preview = Preview::HomingAlarm;
    } else if (selection == 5) {
        _page = Page::Connections;
        _preview = Preview::EncoderFault;
    } else {
        int page = selection < 0 ? 0 : selection > 3 ? 3 : selection;
        _page = static_cast<Page>(page);
    }
    reDisplay();
}

MachineHealthScene machineHealthScene;

void diagnostic_preview_machine_health(int selection) {
    machineHealthScene.diagnosticPreview(selection);
}
