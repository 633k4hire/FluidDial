// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <string>
#include "Scene.h"
#include "e4math.h"
#include "LatheModel.h"
#include "LatheUi.h"
#include "MachineProfile.h"

class ProbingScene : public Scene {
private:
    int  selection   = 0;
    long oldPosition = 0;
    int  _diagnostic_fixture = -1;

    // Saved to NVS
    e4_t _offset  = e4_from_int(0);
    int  _travel  = -20;
    int  _rate    = 80;
    int  _retract = 20;
    int  _axis    = 2;  // Z is default

    const char* axis_pref_name() { return machine_profile_is_lathe() ? "LatheAxis" : "Axis"; }

    const char* probe_value(int field) const {
        switch (field) {
            case 0: return e4_to_cstr(_offset, 2);
            case 1: return intToCStr(_travel);
            case 2: return intToCStr(_rate);
            case 3: return intToCStr(_retract);
            case 4: return profile_axis_cstr(_axis);
        }
        return "";
    }

    void draw_lathe_probe() {
        lathe_ui_detail_surface("PROBE");
        lathe_ui_badge(151, 44, 72, myProbeSwitch ? "CONTACT" : "OPEN", myProbeSwitch ? lathe_ui_coral() : lathe_ui_green());

        const char* green_label = "";
        const char* red_label = "";
        ProbeResult result = last_probe_result();
        if (_diagnostic_fixture == 2 || _diagnostic_fixture == 3) {
            result.known = true;
            result.success = _diagnostic_fixture == 2;
            result.axis_count = 3;
            result.axes_mm[0] = 12.345f;
            result.axes_mm[2] = -4.250f;
        }
        bool moving = _diagnostic_fixture == 1 || state == Cycle || state == Hold || state == DoorClosed;
        if (_diagnostic_fixture >= 2) {
            centered_text(result.success ? "LAST PROBE / SUCCESS" : "LAST PROBE / FAILED", 76,
                          result.success ? lathe_ui_green() : RED, TINY);
            for (int axis = 0; axis < 2; ++axis) {
                int machine_axis = profile_machine_axis(axis);
                pos_t value = machine_axis >= 0 && machine_axis < result.axis_count ? fromMm(result.axes_mm[machine_axis]) : 0;
                lathe_ui_dro_row(111 + axis * 42, profile_axis_char(axis), value, axis == _axis);
            }
        } else if (state == Idle) {
            const char* labels[5] = { "OFFSET", "TRAVEL", "FEED", "RETRACT", "AXIS" };
            for (int i = 0; i < 5; ++i) {
                int y = 74 + i * 25;
                if (selection == i) canvas.drawRoundRect(25, y - 11, 188, 22, 6, lathe_ui_blue());
                text(labels[i], 34, y, selection == i ? lathe_ui_blue() : lathe_ui_muted(), TINY, middle_left);
                text(probe_value(i), 203, y, lathe_ui_text(), SMALL_MONO, middle_right);
            }
            if (result.known) {
                text(result.success ? "LAST PROBE OK" : "LAST PROBE FAILED", 120, 202,
                     result.success ? lathe_ui_green() : RED, TINY, middle_center);
            }
            green_label = "Probe";
            red_label = "Retract";
        } else if (moving) {
            text("LIVE POSITION", 30, 70, lathe_ui_blue(), TINY, middle_left);
            for (int axis = 0; axis < 2; ++axis) {
                int machine_axis = profile_machine_axis(axis);
                pos_t value = machine_axis >= 0 && machine_axis < 6 ? myAxes[machine_axis] : 0;
                lathe_ui_dro_row(104 + axis * 42, profile_axis_char(axis), value, axis == _axis);
            }
            if (result.known) {
                text(result.success ? "CONTACT CAPTURED" : "NO CONTACT", 120, 190,
                     result.success ? lathe_ui_coral() : lathe_ui_amber(), TINY, middle_center);
            }
            if (state == Cycle) {
                red_label = "E-Stop";
                green_label = "Hold";
            } else {
                red_label = "Reset";
                green_label = "Resume";
            }
        } else {
            centered_text("PROBE UNAVAILABLE", 112, lathe_ui_amber(), SMALL);
            red_label = "Reset";
            green_label = state == Alarm ? "Unlock" : "";
        }
        drawButtonLegends(red_label, green_label, "Back");
        drawError();
        refreshDisplay();
    }

public:
    ProbingScene() : Scene("Probe") {}

    void diagnosticPreview(int item) {
        _diagnostic_fixture = -1;
        selection = item;
        reDisplay();
    }

    void diagnosticState(int fixture) {
        _diagnostic_fixture = fixture;
        reDisplay();
    }

    void onDialButtonPress() { pop_scene(); }

    void onGreenButtonPress() {
        // G38.2 G91 F80 Z-20 P8.00
        switch (state) {
            case Idle:
                send_linef("G38.2G91F%d%c%dP%s", _rate, profile_axis_char(_axis), _travel, e4_to_cstr(_offset, 2));
                break;
            case Cycle:
                fnc_realtime(FeedHold);
                break;
            case Hold:
            case DoorClosed:
                fnc_realtime(CycleStart);
                break;
            case Alarm:
                send_line("$X"); // unlock
                break;
        }
    }

    void onRedButtonPress() {
        // G38.2 G91 F80 Z-20 P8.00
        if (state == Cycle || state == Alarm) {
            fnc_realtime(Reset);            
            return;
        } else if (state == Idle) {
            int retract = _travel < 0 ? _retract : -_retract;
            send_linef("$J=G91F1000%c%d", profile_axis_char(_axis), retract);
            return;
        } else if (state == Hold || state == DoorClosed) {
            fnc_realtime(Reset);
        }
    }

    void onTouchClick() {
        // Rotate through the items to be adjusted.
        rotateNumberLoop(selection, 1, 0, 4);
        reDisplay();
        ackBeep();
    }

    void onDROChange() { reDisplay(); }

    void onEncoder(int delta) {
        if (abs(delta) > 0) {
            switch (selection) {
                case 0:
                    _offset += delta * 100;  // Increment by 0.0100
                    setPref("Offset", _offset);
                    break;
                case 1:
                    _travel += delta;
                    setPref("Travel", _travel);
                    break;
                case 2:
                    _rate += delta;
                    if (_rate < 1) {
                        _rate = 1;
                    }
                    setPref("Rate", _rate);
                    break;
                case 3:
                    _retract += delta;
                    if (_retract < 0) {
                        _retract = 0;
                    }
                    setPref("Retract", _retract);
                    break;
                case 4:
                    rotateNumberLoop(_axis, 1, 0, profile_axis_count() - 1);
                    setPref(axis_pref_name(), _axis);
            }
            reDisplay();
        }
    }
    void onEntry(void* arg) override {
        _diagnostic_fixture = -1;
        _axis = machine_profile_is_lathe() ? 1 : 2;
        if (lathe_mode_active()) {
            request_lathe_status();
        }
        if (initPrefs()) {
            static_assert(sizeof(e4_t) == sizeof(int));
            getPref("Offset", reinterpret_cast<int *>(&_offset));
            getPref("Travel", &_travel);
            getPref("Rate", &_rate);
            getPref("Retract", &_retract);
        }
        getPref(axis_pref_name(), &_axis);
        if (_axis < 0 || _axis >= profile_axis_count()) {
            _axis = machine_profile_is_lathe() ? 1 : 2;
        }
    }

    void reDisplay() {
        if (lathe_ui_enabled()) {
            draw_lathe_probe();
            return;
        }
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        const char* grnLabel = "";
        const char* redLabel = "";

        if (state == Idle) {
            int    x      = 40;
            int    y      = 62;
            int    width  = display_short_side() - (x * 2);
            int    height = 25;
            int    pitch  = 27;  // for spacing of buttons
            Stripe button(x, y, width, height, TINY);
            button.draw("Offset", e4_to_cstr(_offset, 2), selection == 0);
            button.draw("Max Travel", intToCStr(_travel), selection == 1);
            y = button.y();  // For LED
            button.draw("Feed Rate", intToCStr(_rate), selection == 2);

            button.draw("Retract", intToCStr(_retract), selection == 3);
            button.draw("Axis", profile_axis_cstr(_axis), selection == 4);

            //LED led(x - 20, y + height / 2, 10, button.gap());
            //led.draw(myProbeSwitch);

            grnLabel = "Probe";
            redLabel = "Retract";
        } else {
            if (state == Jog || state == Alarm) {  // there is no Probing state, so Cycle is a valid state on this
                //centered_text("Invalid State", 105, WHITE, MEDIUM);
                //centered_text("For Probing", 145, WHITE, MEDIUM);
                redLabel = "Reset";
                grnLabel = "Unlock";
            } else {
                int x      = 14;
                int height = 35;
                int y      = 82 - height / 2;

                LED led(120, 190, 10, 5);
                led.draw(myProbeSwitch);

                int width = display_short_side() - x * 2;
                DRO dro(x, y, width, height);
                for (int axis = 0; axis < profile_axis_count(); ++axis) {
                    dro.draw(axis, _axis == axis);
                }

                switch (state) {
                    case Cycle:
                        redLabel = "E-Stop";
                        grnLabel = "Hold";
                        break;
                    case Hold:
                    case DoorClosed:
                        redLabel = "Reset";
                        grnLabel = "Resume";
                        break;
                }
            }
        }

        drawButtonLegends(redLabel, grnLabel, "Back");
        drawError();  // only if one just happened
        refreshDisplay();
    }
};
ProbingScene probingScene;

void diagnostic_preview_probe(int selection) {
    probingScene.diagnosticPreview(selection);
}

void diagnostic_preview_probe_state(int fixture) {
    probingScene.diagnosticState(fixture);
}
