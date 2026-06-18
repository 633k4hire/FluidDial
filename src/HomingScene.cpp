// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "ConfigItem.h"
#include "LatheModel.h"
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
void set_axis_homed(int axis) {
    homed_axes |= 1 << axis;
    request_redisplay();
}

void detect_homing_info() {
    clear_config_requests();
    for (int display_axis = 0; display_axis < profile_axis_count(); display_axis++) {
        int machine_axis = profile_machine_axis(display_axis);
        if (machine_axis >= 0 && machine_axis < HOMING_MACHINE_AXES) {
            homing_cycles[machine_axis].init();
            homing_allows[machine_axis].init();
        }
    }
    homed_axes = 0;
}
bool can_home(int i) {
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

public:
    HomingScene() : Scene("Home", 4) {}

    bool is_homing(int axis) { return can_home(axis) && (_axis_to_home == -1 || _axis_to_home == axis); }
    void onEntry(void* arg) override {
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
