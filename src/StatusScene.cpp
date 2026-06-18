// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "ConfirmScene.h"
#include "LatheModel.h"
#include "MachineProfile.h"

extern Scene menuScene;

class StatusScene : public Scene {
private:
    uint32_t    _last_lathe_refresh_ms = 0;

    // the fro/sro/rt rotating display state
    typedef enum {
        FRO,
        SRO,
        RT_FEED_SPEED,
    } ovrd_display_t;

    ovrd_display_t overd_display = FRO;

    void draw_info_row(int y, const char* left, const char* right, int right_color = WHITE, int left_color = DARKGREY) {
        text(left, 24, y, left_color, TINY, middle_left);
        text(right, display_short_side() - 24, y, right_color, TINY, middle_right);
    }

    void draw_status_buttons() {
        const char* grnLabel    = "";
        const char* redLabel    = "";
        const char* yellowLabel = "Back";

        switch (state) {
            case Alarm:
                if (alarm_is_critical()) {
                    redLabel = "Reset";
                } else {
                    redLabel = "Unlock";
                }
                if (alarm_is_homing()) {
                    grnLabel = "Home All";
                }
                break;
            case Homing:
                redLabel = "Reset";
                break;
            case Cycle:
                redLabel    = "E-Stop";
                grnLabel    = "Hold";
                yellowLabel = "Rst Ovr";
                break;
            case Hold:
            case DoorClosed:
                redLabel    = "Quit";
                grnLabel    = "Resume";
                yellowLabel = "Rst Ovr";
                break;
            case Jog:
                redLabel = "Jog Cancel";
                break;
            case Idle:
                break;
        }
        drawButtonLegends(redLabel, grnLabel, yellowLabel);
    }

    void draw_lathe_dashboard() {
        const LatheStatus& lathe = lathe_status();

        background();
        drawMenuTitle("Lathe");
        drawStatusTiny(24);

        DRO dro(16, 50, 210, 27);
        for (int axis = 0; axis < profile_axis_count(); ++axis) {
            dro.draw(axis, -1, true);
        }

        char tool[32];
        if (lathe.active_tool > 0) {
            snprintf(tool, sizeof(tool), "T%d%s", lathe.active_tool, lathe.active_tool == 5 ? " Probe" : "");
        } else {
            snprintf(tool, sizeof(tool), "None");
        }

        char rpm[48];
        if (lathe.feedback_rpm_known) {
            snprintf(rpm, sizeof(rpm), "Eff %.0f  Meas %.0f", lathe.effective_rpm, lathe.feedback_rpm);
        } else {
            snprintf(rpm, sizeof(rpm), "Eff %.0f  Meas N/A", lathe.effective_rpm);
        }

        char modes[48];
        snprintf(modes, sizeof(modes), "%s  %s",
                 lathe.spindle_speed_mode.empty() ? "G97" : lathe.spindle_speed_mode.c_str(),
                 lathe.feed_mode.empty() ? "G94" : lathe.feed_mode.c_str());

        const char* encoder_text  = "ENC OK";
        int         encoder_color = GREEN;
        if (!lathe.encoder_enabled) {
            encoder_text  = "ENC OFF";
            encoder_color = YELLOW;
        } else if (lathe.feedback_fault) {
            encoder_text  = "ENC FAULT";
            encoder_color = RED;
        } else if (lathe.feedback_stale) {
            encoder_text  = "ENC STALE";
            encoder_color = YELLOW;
        } else if (!lathe.encoder_capture) {
            encoder_text  = "ENC NO CAP";
            encoder_color = YELLOW;
        }

        bool threading_feedback_safe = lathe.encoder_enabled && !lathe.feedback_stale && !lathe.feedback_fault;

        draw_info_row(142, "Tool", tool, lathe.active_tool == 5 ? ORANGE : WHITE);
        draw_info_row(160, "RPM", rpm);
        draw_info_row(178, "Modes", modes, GREEN);
        draw_info_row(196,
                      threading_feedback_safe ? (lathe.diameter_mode ? "G7 Diameter" : "G8 Radius") : "Thread unsafe",
                      encoder_text,
                      encoder_color,
                      threading_feedback_safe ? DARKGREY : RED);

        draw_status_buttons();

#ifdef USE_WIFI
        if (round_display) {
            drawWiFiSignalBars(70, 20);
        }
#endif
        refreshDisplay();
    }

public:
    StatusScene() : Scene("Status") {}

    void onExit() override {}

    void onEntry(void* arg) override {
        // Returned from ConfirmScene — execute the deferred soft reset.
        if (arg && strcmp((const char*)arg, "Confirmed") == 0) {
            dbg_printf("StatusScene: sending Ctrl-X soft reset\r\n");
            fnc_realtime(Reset);
            schedule_action([]() { send_line("$X"); });
        } else {
            dbg_printf("StatusScene: onEntry arg=%s\r\n", arg ? (const char*)arg : "null");
        }
        request_lathe_status();
    }

    void onDialButtonPress() {
        if (state == Cycle || state == Hold) {
            if (overd_display == FRO)
                fnc_realtime(FeedOvrReset);
            else if (overd_display == SRO)
                fnc_realtime(SpindleOvrReset);
        } else {
            pop_scene();
        }
    }

    void onStateChange(state_t old_state) {
        if (old_state == Cycle && state == Idle && parent_scene() != &menuScene) {
            pop_scene();
        }
    }

    void onTouchClick() {
        if (touchY > 150 && (state == Cycle || state == Hold)) {
            switch (overd_display) {
                case FRO:
                    overd_display = SRO;
                    break;
                case SRO:
                    overd_display = RT_FEED_SPEED;
                    break;
                case RT_FEED_SPEED:
                    overd_display = FRO;
            }
            reDisplay();
        }
        fnc_realtime(StatusReport);  // sometimes you want an extra status
        request_lathe_status();
    }

    void onRedButtonPress() {
        switch (state) {
            case Alarm:
                if (alarm_is_critical()) {
                    // Soft reset loses work offsets — ask the user to confirm first.
                    push_scene(&confirmScene, (void*)"Soft Reset?\nOffsets will be lost");
                } else {
                    // Non-critical alarm that can be soft-cleared
                    send_line("$X");
                }
                break;
            case Cycle:
            case Homing:
            case Hold:
            case DoorClosed:
                fnc_realtime(Reset);
                break;
        }
    }

    bool alarm_is_homing() { return lastAlarm == 14 || (lastAlarm >= 6 && lastAlarm <= 9); }
    bool alarm_is_critical() {
        switch (lastAlarm) {
            case 4: case 5:                  // Probe fail
            case 6: case 7: case 8: case 9: // Homing fail
            case 14:                         // Unhomed
                return false;
            default:
                return true;
        }
    }
    void onGreenButtonPress() {
        switch (state) {
            case Cycle:
                fnc_realtime(FeedHold);
                break;
            case Hold:
            case DoorClosed:
                fnc_realtime(CycleStart);
                break;
            case Alarm:
                if (alarm_is_homing()) {
                    send_line("$H");
                }
                break;
        }
        fnc_realtime(StatusReport);
    }

    void onEncoder(int delta) {
        if (state == Cycle) {
            switch (overd_display) {
                case FRO:
                    if (delta > 0 && myFro < 200) {
                        fnc_realtime(FeedOvrFinePlus);
                    } else if (delta < 0 && myFro > 10) {
                        fnc_realtime(FeedOvrFineMinus);
                    }
                    break;
                case SRO:
                    if (delta > 0 && mySro < 200) {
                        fnc_realtime(SpindleOvrFinePlus);
                    } else if (delta < 0 && mySro > 10) {
                        fnc_realtime(SpindleOvrFineMinus);
                    }
                    break;
                case RT_FEED_SPEED:
                    overd_display = FRO;
            }

            reDisplay();
        }
    }

    void onDROChange() { reDisplay(); }
    void onLimitsChange() { reDisplay(); }

    void onPoll() override {
        if (!lathe_mode_active()) {
            return;
        }
        uint32_t now = millis();
        if (_last_lathe_refresh_ms == 0 || (uint32_t)(now - _last_lathe_refresh_ms) >= 1500) {
            _last_lathe_refresh_ms = now;
            request_lathe_status();
        }
    }

    void reDisplay() {
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
            int width  = 192;
            int height = 10;
            if (myPercent > 0) {
                drawRect(20, y, width, height, 5, LIGHTGREY);
                width = (width * myPercent) / 100;
                if (width > 0) {
                    drawRect(20, y, width, height, 5, GREEN);
                }
            }
            // Feed override
            char legend[50];
            switch (overd_display) {
                case FRO:
                    sprintf(legend, "Feed Rate Ovr:%d%%", myFro);
                    break;
                case SRO:
                    sprintf(legend, "Spindle Ovr:%d%%", mySro);
                    break;
                case RT_FEED_SPEED:
                    sprintf(legend, "Fd:%d Spd:%d", myFeed, mySpeed);
            }
            centered_text(legend, y + 23);
        } else {
            centered_text(mode_string(), y + 23, GREEN, TINY);
        }

        draw_status_buttons();

#ifdef USE_WIFI
        if (round_display) {
            drawWiFiSignalBars(70, 20);
        }
#endif
        refreshDisplay();
    }
};
StatusScene statusScene;
