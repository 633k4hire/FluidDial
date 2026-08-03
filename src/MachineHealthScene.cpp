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
    centered_text("Machine Health", 14, WHITE, SMALL);
    drawRect(70, 25, 100, 1, 0, DARKGREY);
    centered_text(title, 37, title_color, TINY);

    char page[8];
    snprintf(page, sizeof(page), "%d / 4", static_cast<int>(_page) + 1);
    centered_text(page, 190, DARKGREY, TINY);
}

void MachineHealthScene::drawRow(int y, const char* label, const char* value, int value_color) {
    drawOutlinedRect(RowX, y, RowW, RowH, NAVY, DARKGREY);
    text(label, RowX + 9, y + RowH / 2 + 2, DARKGREY, TINY, middle_left);
    auto_text(std::string(value ? value : ""),
              RowX + RowW - 9,
              y + RowH / 2 + 2,
              105,
              value_color,
              SMALL,
              middle_right);
}

void MachineHealthScene::drawOverview() {
    drawHeader("Overview", state == Alarm ? RED : state == Idle ? GREEN : YELLOW);
    drawRow(RowY[0], "Controller", fnc_is_connected() ? "Online" : "N/C", fnc_is_connected() ? GREEN : RED);
    drawRow(RowY[1], "Machine", my_state_string, state == Alarm ? RED : state == Idle ? GREEN : YELLOW);

    int x_axis = displayAxis('X');
    int z_axis = displayAxis('Z');
    bool x_homed = x_axis >= 0 && is_axis_homed(x_axis);
    bool z_homed = z_axis >= 0 && is_axis_homed(z_axis);
    const char* homing = x_homed && z_homed ? "X OK  Z OK" : x_homed ? "X OK  Z HOME" : z_homed ? "X HOME  Z OK" : "X HOME  Z HOME";
    drawRow(RowY[2], "Homing", homing, x_homed && z_homed ? GREEN : YELLOW);

    const LatheStatus& lathe = lathe_status();
    char summary[48];
    const char* spindle = lathe.effective_rpm > 0.5f ? "RUN" : "STOP";
    snprintf(summary, sizeof(summary), "T%s  %s", lathe.active_tool > 0 ? intToCStr(lathe.active_tool) : "-", spindle);
    drawRow(RowY[3], "Tool / spindle", summary, lathe.effective_rpm > 0.5f ? GREEN : LIGHTGREY);
}

void MachineHealthScene::drawAlarm() {
    bool fixture = _preview == Preview::HomingAlarm;
    state_t shown_state = fixture ? Alarm : state;
    int shown_alarm = fixture ? 14 : lastAlarm;
    bool active_alarm = shown_state == Alarm;
    drawHeader("Alarms", active_alarm ? RED : GREEN);

    if (!active_alarm && !errorActive()) {
        drawRow(RowY[0], "State", "No active alarms", GREEN);
        drawRow(RowY[1], "Alarm", "None", LIGHTGREY);
        drawRow(RowY[2], "Error", "None", LIGHTGREY);
        drawRow(RowY[3], "Action", "Machine ready", GREEN);
        return;
    }

    char alarm_code[20];
    snprintf(alarm_code, sizeof(alarm_code), active_alarm ? "Alarm %d" : "None", shown_alarm);
    drawRow(RowY[0], "State", alarm_code, active_alarm ? RED : LIGHTGREY);
    drawRow(RowY[1], "Cause", active_alarm ? alarm_name_short[shown_alarm] : "None", active_alarm ? YELLOW : LIGHTGREY);
    drawRow(RowY[2], "Error", errorActive() ? decode_error_number(lastError) : "None", errorActive() ? YELLOW : LIGHTGREY);

    const char* guidance = "Reset required";
    if (machine_alarm_is_homing(shown_alarm)) {
        guidance = "Home X and Z";
    } else if (shown_alarm == 4 || shown_alarm == 5) {
        guidance = "Check probe input";
    }
    drawRow(RowY[3], "Action", guidance, YELLOW);
}

void MachineHealthScene::drawReadiness() {
    drawHeader("Readiness", operator_machine_actions_available() ? GREEN : YELLOW);
    int x_axis = displayAxis('X');
    int z_axis = displayAxis('Z');
    bool x_homed = x_axis >= 0 && is_axis_homed(x_axis);
    bool z_homed = z_axis >= 0 && is_axis_homed(z_axis);
    drawRow(RowY[0], "X axis", x_homed ? "Homed" : "Needs home", x_homed ? GREEN : YELLOW);
    drawRow(RowY[1], "Z axis", z_homed ? "Homed" : "Needs home", z_homed ? GREEN : YELLOW);

    bool limit_active = false;
    for (int display_axis_value : { x_axis, z_axis }) {
        if (display_axis_value >= 0) {
            int machine_axis = profile_machine_axis(display_axis_value);
            if (machine_axis >= 0 && machine_axis < 6 && myLimitSwitches[machine_axis]) {
                limit_active = true;
            }
        }
    }
    char inputs[36];
    snprintf(inputs, sizeof(inputs), "Limits %s  Probe %s", limit_active ? "ON" : "Clear", myProbeSwitch ? "ON" : "Clear");
    drawRow(RowY[2], "Inputs", inputs, limit_active ? RED : myProbeSwitch ? YELLOW : GREEN);

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
    drawRow(RowY[3], "Machine actions", action, action_color);
}

void MachineHealthScene::drawConnections() {
    const LatheStatus& lathe = lathe_status();
    bool fixture_fault = _preview == Preview::EncoderFault;
    drawHeader("Connections", fixture_fault || lathe.feedback_fault ? RED : GREEN);
    drawRow(RowY[0], "FluidNC UART", fnc_is_connected() ? "Online" : "N/C", fnc_is_connected() ? GREEN : RED);
    drawRow(RowY[1], "Operator sync", operatorLinkText(), operatorLinkColor());

#ifdef USE_WIFI
    const char* wifi_value = wifi_is_connected() ? wifi_local_ip() : "Offline";
    drawRow(RowY[2], "Wi-Fi diagnostics", wifi_value, wifi_is_connected() ? GREEN : LIGHTGREY);
#else
    drawRow(RowY[2], "Wi-Fi diagnostics", "Unavailable", DARKGREY);
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
    drawRow(RowY[3], "Spindle sync", capability, capability_color);
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
