// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
// changed for lathe XZC control by Matthew Metzger . github @633k4hire 
#include "Scene.h"
#include "ConfigItem.h"

extern Scene statusScene;

#define HOMING_N_AXIS 3

// These configuration items now refer only to the X, Z, and C axes.
IntConfigItem homing_cycles[HOMING_N_AXIS] = {
    { "$/axes/x/homing/cycle" },
    { "$/axes/z/homing/cycle" },
    { "$/axes/c/homing/cycle" },
};
BoolConfigItem homing_allows[HOMING_N_AXIS] = {
    { "$/axes/x/homing/allow_single_axis" },
    { "$/axes/z/homing/allow_single_axis" },
    { "$/axes/c/homing/allow_single_axis" },
};

int homed_axes = 0;
bool is_homed(int axis) {
    return homed_axes & (1 << axis);
}
void set_axis_homed(int axis) {
    homed_axes |= 1 << axis;
    current_scene->reDisplay();
}

void detect_homing_info() {
    for (int i = 0; i < HOMING_N_AXIS; i++) {
        homing_cycles[i].init();
        homing_allows[i].init();
    }
    homed_axes = 0;
}
bool can_home(int i) {
    // Cannot home if cycle == 0 and single axis homing is not allowed.
    return homing_cycles[i].get() != 0 || homing_allows[i].get();
}

bool have_homing_info() {
    return homing_allows[HOMING_N_AXIS - 1].known();
}

class HomingScene : public Scene {
private:
    // _axis_to_home holds the selected axis: 0 for X, 1 for Z, 2 for C, or -1 for "all axes".
    int _axis_to_home = -1;
    int _auto         = false;

    bool _allows[HOMING_N_AXIS];

public:
    HomingScene() : Scene("Home", 4) {}

    // Check if the given axis is eligible for homing.
    bool is_homing(int axis) {
        return can_home(axis) && (_axis_to_home == -1 || _axis_to_home == axis);
    }
    void onEntry(void* arg) override {
        if (state == Idle && _auto) {
            pop_scene();
        }
        const char* s = static_cast<const char*>(arg);
        _auto = s && strcmp(s, "auto") == 0;
    }

    void onStateChange(state_t old_state) override {
#ifdef AUTO_HOMING_RETURN
        if (old_state == Homing && state == Idle && _auto) {
            pop_scene();
        }
#endif
    }
    void onDialButtonPress() override {
        pop_scene();
    }
    void onGreenButtonPress() override {
        if (state == Idle || state == Alarm) {
            if (_axis_to_home != -1) {
                // Send the homing command for the specific axis using the updated mapping.
                send_linef("$H%c", axisNumToChar(_axis_to_home));
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

    // Cycle through the available axes. If we exceed the number of axes, reset to -1 (meaning "home all").
    void increment_axis_to_home() {
        do {
            _axis_to_home++;
            if (_axis_to_home >= HOMING_N_AXIS) {
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

        const char* redLabel = "";
        std::string grnLabel = "";
        // We don’t need an orange label change for this scene.
        if (state == Idle || state == Homing || state == Alarm) {
            DRO dro(16, 68, 210, 32);
            // Display the homing DRO for axes 0 (X), 1 (Z), and 2 (C)
            for (int axis = 0; axis < HOMING_N_AXIS; ++axis) {
                dro.drawHoming(axis, is_homing(axis), is_homed(axis));
            }

            if (state == Homing) {
                redLabel = "E-Stop";
            } else {
                if (state == Alarm && (strchr(myCtrlPins, 'D') == NULL)) {  
                    // Allow alarm reset only if door is inactive.
                    redLabel = "Reset";
                }
                if (_axis_to_home == -1) {
                    // If no specific axis is selected, show the command for all homable axes.
                    for (int axis = 0; axis < HOMING_N_AXIS; ++axis) {
                        if (can_home(axis)) {
                            if (!grnLabel.length()) {
                                grnLabel = "Home";
                            }
                            grnLabel += axisNumToChar(axis);
                        }
                    }
                } else {
                    grnLabel = "Home";
                    grnLabel += axisNumToChar(_axis_to_home);
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
        drawButtonLegends(redLabel, grnLabel.c_str(), "Back");

        refreshDisplay();
    }
};

HomingScene homingScene;
