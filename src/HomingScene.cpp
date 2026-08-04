// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "ConfigItem.h"
#include "LatheModel.h"
#include "LatheUi.h"
#include "MachineProfile.h"

extern Scene statusScene;

#define HOMING_MACHINE_AXES 6

IntConfigItem homing_cycles[HOMING_MACHINE_AXES] = {
    { "$/axes/x/homing/cycle" },
    { "$/axes/y/homing/cycle" },
    { "$/axes/z/homing/cycle" },
    { "$/axes/a/homing/cycle" },
    { "$/axes/b/homing/cycle" },
    { "$/axes/c/homing/cycle" },
};
BoolConfigItem homing_allows[HOMING_MACHINE_AXES] = {
    { "$/axes/x/homing/allow_single_axis" },
    { "$/axes/y/homing/allow_single_axis" },
    { "$/axes/z/homing/allow_single_axis" },
    { "$/axes/a/homing/allow_single_axis" },
    { "$/axes/b/homing/allow_single_axis" },
    { "$/axes/c/homing/allow_single_axis" },
};

int  homed_axes = 0;
bool is_homed(int axis) {
    return homed_axes & (1 << axis);
}
bool is_axis_homed(int display_axis) {
    return display_axis >= 0 && display_axis < HOMING_MACHINE_AXES && is_homed(display_axis);
}
void set_axis_homed(int axis) {
    homed_axes |= 1 << axis;
    request_redisplay();
}

static bool homing_query_supported(int display_axis) {
    // This lathe's C coordinate represents the shared spindle/chuck. It has
    // no conventional FluidNC homing block until an angular encoder is
    // commissioned, so querying $/axes/c/homing/* only returns error:3.
    return !(machine_profile_is_lathe() && profile_axis_char(display_axis) == 'C');
}

void detect_homing_info() {
    clear_config_requests();
    for (int display_axis = 0; display_axis < profile_axis_count(); display_axis++) {
        if (!homing_query_supported(display_axis)) {
            continue;
        }
        int machine_axis = profile_machine_axis(display_axis);
        if (machine_axis >= 0 && machine_axis < HOMING_MACHINE_AXES) {
            homing_cycles[machine_axis].init();
            homing_allows[machine_axis].init();
        }
    }
    homed_axes = 0;
}
bool can_home(int i) {
    if (!homing_query_supported(i)) {
        return false;
    }
    int machine_axis = profile_machine_axis(i);
    if (machine_axis < 0 || machine_axis >= HOMING_MACHINE_AXES) {
        return false;
    }
    if (!homing_cycles[machine_axis].known() || !homing_allows[machine_axis].known()) {
        return false;
    }
    // Cannot home if cycle == 0 and !allow_single_axis
    return homing_cycles[machine_axis].get() != 0 || homing_allows[machine_axis].get();
}

bool have_homing_info() {
    for (int display_axis = 0; display_axis < profile_axis_count(); ++display_axis) {
        if (!homing_query_supported(display_axis)) {
            continue;
        }
        int machine_axis = profile_machine_axis(display_axis);
        if (machine_axis < 0 || machine_axis >= HOMING_MACHINE_AXES) {
            return false;
        }
        if (!homing_cycles[machine_axis].known() || !homing_allows[machine_axis].known()) {
            return false;
        }
    }
    return true;
}

class HomingScene : public Scene {
private:
    int _axis_to_home = -1;
    int _auto         = false;
    int _diagnostic_fixture = -1;
    bool _diagnostic_snapshot_active = false;
    int  _saved_axis_to_home = -1;
    int  _saved_diagnostic_fixture = -1;

    void snapshotDiagnosticState() {
        if (_diagnostic_snapshot_active) return;
        _saved_axis_to_home = _axis_to_home;
        _saved_diagnostic_fixture = _diagnostic_fixture;
        _diagnostic_snapshot_active = true;
    }

    void draw_lathe_home() {
        lathe_ui_detail_surface("HOME");
        const char* red_label = "";
        char green_label[16] = { 0 };
        const char* center_label = "Back";

        static const int row_y[3] = { 68, 104, 140 };
        for (int axis = 0; axis < 3; ++axis) {
            int y = row_y[axis];
            char label[2] = { axis == 0 ? 'X' : axis == 1 ? 'Z' : 'C', '\0' };
            text(label, 34, y, lathe_ui_text(), SMALL, middle_left);
            if (axis == 2) {
                lathe_ui_badge(128, y - 11, 70, "N/A", lathe_ui_muted());
            } else {
                bool homed = _diagnostic_fixture == 1 || (_diagnostic_fixture < 0 && is_axis_homed(axis));
                const char* value = state == Homing && is_homing(axis) ? "HOMING" : homed ? "HOMED" : "NEEDS HOME";
                int color = state == Homing && is_homing(axis) ? lathe_ui_blue() : homed ? lathe_ui_green() : lathe_ui_amber();
                lathe_ui_badge(98, y - 11, 100, value, color);
            }
        }

        const char* selected = _axis_to_home < 0 ? "ALL X/Z" : _axis_to_home == 0 ? "X AXIS" : "Z AXIS";
        lathe_ui_badge(66, 162, 108, selected, lathe_ui_blue());

        if (state == Homing) {
            red_label = "E-Stop";
        } else if (state == Idle || state == Alarm) {
            if (state == Alarm && strchr(myCtrlPins, 'D') == nullptr) red_label = "Reset";
            if (!have_homing_info()) {
                center_label = "Loading";
            } else {
                snprintf(green_label, sizeof(green_label), "Home %s", _axis_to_home < 0 ? "X/Z" : profile_axis_cstr(_axis_to_home));
            }
        } else {
            centered_text("HOME UNAVAILABLE", 189, lathe_ui_amber(), TINY);
            red_label = "E-Stop";
            if (state == Cycle) snprintf(green_label, sizeof(green_label), "Hold");
            else if (state == Hold || state == DoorClosed) snprintf(green_label, sizeof(green_label), "Resume");
        }
        lathe_ui_action_legends(red_label, green_label, center_label);
        refreshDisplay();
    }

public:
    HomingScene() : Scene("Home", 4) {}

    void diagnosticPreview(int selection) {
        snapshotDiagnosticState();
        _diagnostic_fixture = -1;
        _axis_to_home = selection - 1;
        reDisplay();
    }

    void diagnosticState(int fixture) {
        snapshotDiagnosticState();
        _diagnostic_fixture = fixture;
        _axis_to_home = -1;
        reDisplay();
    }

    void diagnosticRestore() {
        if (!_diagnostic_snapshot_active) return;
        _axis_to_home = _saved_axis_to_home;
        _diagnostic_fixture = _saved_diagnostic_fixture;
        _diagnostic_snapshot_active = false;
    }

    bool is_homing(int axis) { return can_home(axis) && (_axis_to_home == -1 || _axis_to_home == axis); }
    void onEntry(void* arg) override {
        _diagnostic_fixture = -1;
        if (state == Idle && _auto) {
            pop_scene();
        }
        const char* s = static_cast<const char*>(arg);
        _auto         = s && strcmp(s, "auto") == 0;
        if (lathe_mode_active()) {
            request_lathe_status();
        }
        if (!have_homing_info()) {
            schedule_action(detect_homing_info);
        }
    }

    void onStateChange(state_t old_state) override {
#ifdef AUTO_HOMING_RETURN
        if (old_state == Homing && state == Idle && _auto) {
            pop_scene();
        }
#endif
    }
    void onDialButtonPress() override { pop_scene(); }
    void onGreenButtonPress() override {
        if (state == Idle || state == Alarm) {
            if (_axis_to_home != -1) {
                send_linef("$H%c", profile_axis_char(_axis_to_home));
            } else {
                send_line("$H");
            }
        } else if (state == Cycle) {
            fnc_realtime(FeedHold);
        } else if (state == Hold || state == DoorClosed) {
            fnc_realtime(CycleStart);
        }
    }
    void onRedButtonPress() override {
        if (state == Homing || state == Alarm) {
            fnc_realtime(Reset);
        }
    }

    void increment_axis_to_home() {
        do {
            ++_axis_to_home;
            if (_axis_to_home >= profile_axis_count()) {
                _axis_to_home = -1;
                return;
            }
        } while (!can_home(_axis_to_home));
    }
    void onTouchClick() {
        if (state == Idle || state == Homing || state == Alarm) {
            increment_axis_to_home();
            reDisplay();
            ackBeep();
        }
    }

    void onEncoder(int delta) override {
        increment_axis_to_home();
        reDisplay();
    }
    void onDROChange() { reDisplay(); }  // also covers any status change

    void reDisplay() {
        if (lathe_ui_enabled()) {
            draw_lathe_home();
            return;
        }
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        const char* redLabel    = "";
        std::string grnLabel    = "";
        const char* orangeLabel = "";
        std::string green       = "Home ";

        if (false && state == Homing) {
            DRO dro(16, 68, 210, 32);
            for (int axis = 0; axis < profile_axis_count(); axis++) {
                dro.draw(axis, -1, true);
            }

        } else if (state == Idle || state == Homing || state == Alarm) {
            DRO dro(16, 68, 210, 32);
            for (int axis = 0; axis < profile_axis_count(); ++axis) {
                dro.drawHoming(axis, is_homing(axis), is_homed(axis));
            }

#if 0
            int x      = 50;
            int y      = 65;
            int width  = display.width() - (x * 2);
            int height = 32;

            Stripe button(x, y, width, height, SMALL);
            button.draw("Home All", _axis_to_home == -1);
            y = button.y();  // LEDs start with the Home X button
            button.draw("Home X", _axis_to_home == 0);
            button.draw("Home Y", _axis_to_home == 1);
            button.draw("Home Z", _axis_to_home == 2);
            LED led(x - 16, y + height / 2, 10, button.gap());
            led.draw(myLimitSwitches[0]);
            led.draw(myLimitSwitches[1]);
            led.draw(myLimitSwitches[2]);
#endif

            if (state == Homing) {
                redLabel = "E-Stop";
            } else {
                if (state == Alarm && (strchr(myCtrlPins, 'D') == NULL)) {  // You can reset alarms if door is not active
                    redLabel = "Reset";
                }
                if (!have_homing_info()) {
                    orangeLabel = "Loading";
                } else if (_axis_to_home == -1) {
                    for (int axis = 0; axis < profile_axis_count(); ++axis) {
                        if (can_home(axis)) {
                            if (!grnLabel.length()) {
                                grnLabel = "Home";
                            }

                            grnLabel += profile_axis_char(axis);
                        }
                    }
                } else {
                    grnLabel = "Home";
                    grnLabel += profile_axis_char(_axis_to_home);
                }
            }
        } else {
            centered_text("Invalid State", 105, WHITE, MEDIUM);
            centered_text("For Homing", 145, WHITE, MEDIUM);
            redLabel = "E-Stop";
            if (state == Cycle) {
                grnLabel = "Hold";
            } else if (state == Hold || state == DoorClosed) {
                grnLabel = "Resume";
            }
        }
        drawButtonLegends(redLabel, grnLabel.c_str(), orangeLabel[0] ? orangeLabel : "Back");

        refreshDisplay();
    }
};
HomingScene homingScene;

void diagnostic_preview_homing(int selection) {
    homingScene.diagnosticPreview(selection);
}

void diagnostic_preview_homing_state(int fixture) {
    homingScene.diagnosticState(fixture);
}

void diagnostic_restore_homing_preview() {
    homingScene.diagnosticRestore();
}
