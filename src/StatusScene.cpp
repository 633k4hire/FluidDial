// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "LatheModel.h"
#include "LatheUi.h"
#include "MachineProfile.h"
#include "MachineStateActions.h"
#include "alarm.h"

#include <cstdio>
#include <string>

extern Scene menuScene;

class StatusScene : public Scene {
private:
    uint32_t _last_lathe_refresh_ms = 0;
    bool     _diagnostic_state = false;
    state_t  _preview_state = Idle;
    int      _preview_alarm = 0;

    enum ovrd_display_t {
        FRO,
        SRO,
        RT_FEED_SPEED,
    };

    ovrd_display_t overd_display = FRO;

    state_t displayState() const { return _diagnostic_state ? _preview_state : state; }
    int displayAlarm() const { return _diagnostic_state ? _preview_alarm : lastAlarm; }

    const char* stateName(state_t shown_state) const {
        if (!_diagnostic_state && shown_state == state) {
            return my_state_string;
        }
        switch (shown_state) {
            case Idle: return "Idle";
            case Alarm: return "Alarm";
            case CheckMode: return "Check";
            case Homing: return "Homing";
            case Cycle: return "Cycle";
            case Hold: return "Hold";
            case Jog: return "Jog";
            case DoorOpen: return "Door open";
            case DoorClosed: return "Door";
            case GrblSleep: return "Sleep";
            case ConfigAlarm: return "Config alarm";
            case Critical: return "Critical";
            case Disconnected: return "N/C";
        }
        return "Unknown";
    }

    int stateColor(state_t shown_state) const {
        if (shown_state == Alarm || shown_state == Critical || shown_state == ConfigAlarm || shown_state == Disconnected) {
            return RED;
        }
        return shown_state == Idle ? GREEN : YELLOW;
    }

    void draw_state_pill(state_t shown_state) {
        int color = stateColor(shown_state);
        drawOutlinedRect(75, 13, 90, 23, BLACK, color);
        centered_text(stateName(shown_state), 27, color, TINY);
    }

    void draw_status_buttons() {
        state_t shown_state = displayState();
        MachineStateActionLabels labels = machine_state_action_labels(shown_state, displayAlarm(), machine_profile_is_lathe());
        const char* center = (shown_state == Cycle || shown_state == Hold || shown_state == DoorClosed) ? "Rst Ovr" : "Back";
        drawButtonLegends(labels.red, labels.green, center);
    }

    void draw_legacy_lathe_dashboard(state_t shown_state, int shown_alarm, const LatheStatus& lathe) {
        background();
        draw_state_pill(shown_state);
        text(inInches ? "in" : "mm", 187, 27, DARKGREY, TINY, middle_left);
        DRO dro(23, 48, 194, 35);
        for (int axis = 0; axis < profile_axis_count(); ++axis) {
            char axis_char = profile_axis_char(axis);
            if (axis_char == 'X' || axis_char == 'Z') dro.draw(axis, -1, true);
        }
        char summary[40];
        char tool[8];
        if (lathe.active_tool > 0) snprintf(tool, sizeof(tool), "T%d", lathe.active_tool);
        else snprintf(tool, sizeof(tool), "T-");
        if (lathe.effective_rpm > 0.5f) snprintf(summary, sizeof(summary), "%s / Spindle %.0f", tool, lathe.effective_rpm);
        else snprintf(summary, sizeof(summary), "%s / Spindle STOP", tool);
        drawOutlinedRect(30, 130, 180, 30, NAVY, DARKGREY);
        auto_text(std::string(summary), 120, 147, 164, lathe.effective_rpm > 0.5f ? GREEN : LIGHTGREY, TINY, middle_center);
        char context[64] = { 0 };
        int color = LIGHTGREY;
        if (shown_state == Cycle || shown_state == Hold || shown_state == DoorClosed) {
            switch (overd_display) {
                case FRO: snprintf(context, sizeof(context), "Feed override %d%%", myFro); break;
                case SRO: snprintf(context, sizeof(context), "Spindle override %d%%", mySro); break;
                case RT_FEED_SPEED: snprintf(context, sizeof(context), "Feed %u / Speed %u", myFeed, mySpeed); break;
            }
            color = GREEN;
        } else if (shown_state == Alarm) {
            snprintf(context, sizeof(context), "Alarm %d: %s", shown_alarm, alarm_name_short[shown_alarm]);
            color = RED;
        } else if (shown_state == Disconnected) {
            snprintf(context, sizeof(context), "Controller N/C");
            color = RED;
        } else if (!_diagnostic_state && lathe_command_recoverable()) {
            snprintf(context, sizeof(context), "%s", lathe_command_status_text());
            color = YELLOW;
        }
        if (context[0]) {
            drawOutlinedRect(30, 166, 180, 28, BLACK, color);
            auto_text(std::string(context), 120, 182, 172, color, TINY, middle_center);
        }
        draw_status_buttons();
        refreshDisplay();
    }

    void draw_lathe_dashboard() {
        const LatheStatus& lathe = lathe_status();
        state_t shown_state = displayState();
        int shown_alarm = displayAlarm();

        if (!lathe_ui_enabled()) {
            draw_legacy_lathe_dashboard(shown_state, shown_alarm, lathe);
            return;
        }

        lathe_ui_detail_surface("STATUS");
        text(current_wcs(), 32, 62, lathe_ui_blue(), TINY, middle_left);
        text(inInches ? "IN" : "MM", 208, 62, lathe_ui_muted(), TINY, middle_right);

        for (int axis = 0; axis < profile_axis_count() && axis < 3; ++axis) {
            int machine_axis = profile_machine_axis(axis);
            pos_t value = machine_axis >= 0 && machine_axis < 6 ? myAxes[machine_axis] : 0;
            static const int dro_y[3] = { 82, 111, 140 };
            lathe_ui_dro_row(dro_y[axis], profile_axis_char(axis), value, axis == 0);
        }

        char machine[32];
        if (lathe.active_tool > 0) {
            if (lathe.effective_rpm > 0.5f) {
                snprintf(machine, sizeof(machine), "T%d / %.0f RPM", lathe.active_tool, lathe.effective_rpm);
            } else {
                snprintf(machine, sizeof(machine), "T%d / STOP", lathe.active_tool);
            }
        } else {
            snprintf(machine, sizeof(machine), "T- / %s", lathe.effective_rpm > 0.5f ? "RUN" : "STOP");
        }
        lathe_ui_value_row(169, "MACHINE", machine,
                           lathe.effective_rpm > 0.5f ? lathe_ui_green() :
                           lathe.active_tool == 5 ? lathe_ui_coral() : lathe_ui_text());

        const char* context = nullptr;
        int context_color = LIGHTGREY;
        char context_buffer[64];
        if (shown_state == Cycle || shown_state == Hold || shown_state == DoorClosed) {
            switch (overd_display) {
                case FRO: snprintf(context_buffer, sizeof(context_buffer), "FRO %d%% / JOB %d%%", myFro, myPercent); break;
                case SRO: snprintf(context_buffer, sizeof(context_buffer), "SRO %d%% / JOB %d%%", mySro, myPercent); break;
                case RT_FEED_SPEED: snprintf(context_buffer, sizeof(context_buffer), "F%u S%u / JOB %d%%", myFeed, mySpeed, myPercent); break;
            }
            context = context_buffer;
            context_color = lathe_ui_green();
        } else if (shown_state == Alarm) {
            snprintf(context_buffer, sizeof(context_buffer), "Alarm %d: %s", shown_alarm, alarm_name_short[shown_alarm]);
            context = context_buffer;
            context_color = RED;
        } else if (shown_state == Disconnected) {
            context = "Controller N/C";
            context_color = RED;
        } else if (!_diagnostic_state && (lathe_command_pending() || lathe_command_recoverable())) {
            context = lathe_command_status_text();
            context_color = lathe_command_pending() ? lathe_ui_blue() : lathe_ui_amber();
        }
        if (context) {
            lathe_ui_fit_text(context, 120, 188, 168, context_color, TINY, middle_center);
        } else {
            char modes[48];
            snprintf(modes, sizeof(modes), "F%u  S%u  %s", myFeed, mySpeed, mode_string());
            lathe_ui_fit_text(modes, 120, 188, 168, lathe_ui_muted(), TINY, middle_center);
        }

        MachineStateActionLabels labels = machine_state_action_labels(shown_state, shown_alarm, true);
        const char* center = (shown_state == Cycle || shown_state == Hold || shown_state == DoorClosed) ? "Rst Ovr" : "Back";
        lathe_ui_action_legends(labels.red, labels.green, center);
        refreshDisplay();
    }

public:
    StatusScene() : Scene("Status") {}

    void onEntry(void* arg) override {
        _diagnostic_state = false;
        if (!machine_state_handle_confirmation(arg)) {
            dbg_printf("StatusScene: onEntry arg=%s\r\n", arg ? (const char*)arg : "null");
        }
        request_lathe_status();
    }

    void onDialButtonPress() override {
        if (state == Cycle || state == Hold) {
            if (overd_display == FRO) {
                fnc_realtime(FeedOvrReset);
            } else if (overd_display == SRO) {
                fnc_realtime(SpindleOvrReset);
            }
        } else {
            pop_scene();
        }
    }

    void onStateChange(state_t old_state) override {
        if (old_state == Cycle && state == Idle && parent_scene() != &menuScene) {
            pop_scene();
            return;
        }
        request_redisplay();
    }

    void onTouchClick() override {
        if (touchY > 150 && (state == Cycle || state == Hold)) {
            switch (overd_display) {
                case FRO: overd_display = SRO; break;
                case SRO: overd_display = RT_FEED_SPEED; break;
                case RT_FEED_SPEED: overd_display = FRO; break;
            }
            reDisplay();
        }
        fnc_realtime(StatusReport);
        request_lathe_status();
    }

    void onRedButtonPress() override { machine_state_red_action(); }
    void onGreenButtonPress() override { machine_state_green_action(); }

    void onEncoder(int delta) override {
        if (state != Cycle) {
            return;
        }
        switch (overd_display) {
            case FRO:
                if (delta > 0 && myFro < 200) fnc_realtime(FeedOvrFinePlus);
                else if (delta < 0 && myFro > 10) fnc_realtime(FeedOvrFineMinus);
                break;
            case SRO:
                if (delta > 0 && mySro < 200) fnc_realtime(SpindleOvrFinePlus);
                else if (delta < 0 && mySro > 10) fnc_realtime(SpindleOvrFineMinus);
                break;
            case RT_FEED_SPEED:
                overd_display = FRO;
                break;
        }
        reDisplay();
    }

    void onDROChange() override { request_redisplay(); }
    void onLimitsChange() override { request_redisplay(); }

    void onPoll() override {
        if (!lathe_mode_active()) {
            return;
        }
        lathe_poll_command();
        uint32_t now = millis();
        if (_last_lathe_refresh_ms == 0 || (uint32_t)(now - _last_lathe_refresh_ms) >= 1500) {
            _last_lathe_refresh_ms = now;
            request_lathe_status();
        }
    }

    void reDisplay() override {
        if (lathe_mode_active()) {
            draw_lathe_dashboard();
            return;
        }

        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        DRO dro(16, 68, 210, 32);
        dro.draw(0, -1, true);
        dro.draw(1, -1, true);
        dro.draw(2, -1, true);

        int y = 170;
        if (state == Cycle || state == Hold) {
            int width = 192;
            int height = 10;
            if (myPercent > 0) {
                drawRect(20, y, width, height, 5, LIGHTGREY);
                width = width * myPercent / 100;
                if (width > 0) drawRect(20, y, width, height, 5, GREEN);
            }
            char legend[50];
            switch (overd_display) {
                case FRO: sprintf(legend, "Feed Rate Ovr:%d%%", myFro); break;
                case SRO: sprintf(legend, "Spindle Ovr:%d%%", mySro); break;
                case RT_FEED_SPEED: sprintf(legend, "Fd:%d Spd:%d", myFeed, mySpeed); break;
            }
            centered_text(legend, y + 23);
        } else {
            centered_text(mode_string(), y + 23, GREEN, TINY);
        }

        draw_status_buttons();
#ifdef USE_WIFI
        if (round_display) drawWiFiSignalBars(70, 20);
#endif
        refreshDisplay();
    }

    void diagnosticPreview(int fixture) {
        _diagnostic_state = true;
        _preview_alarm = 0;
        switch (fixture) {
            case 1: _preview_state = Cycle; break;
            case 2: _preview_state = Hold; break;
            case 3: _preview_state = Alarm; _preview_alarm = 14; break;
            case 4: _preview_state = Disconnected; break;
            default: _preview_state = Idle; break;
        }
        reDisplay();
    }

    void diagnosticRestore() {
        _diagnostic_state = false;
    }
};

StatusScene statusScene;

void diagnostic_preview_status(int fixture) {
    statusScene.diagnosticPreview(fixture);
}

void diagnostic_restore_status_preview() {
    statusScene.diagnosticRestore();
}
