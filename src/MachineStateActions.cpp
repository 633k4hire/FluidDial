#include "MachineStateActions.h"

#include "ConfirmScene.h"
#include "Scene.h"

#include <cstring>

bool machine_alarm_is_homing(int alarm) {
    return alarm == 14 || (alarm >= 6 && alarm <= 9);
}

bool machine_alarm_is_critical(int alarm) {
    switch (alarm) {
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 14:
            return false;
        default:
            return true;
    }
}

MachineStateActionLabels machine_state_action_labels(state_t display_state, int alarm, bool lathe_axes) {
    switch (display_state) {
        case Alarm:
            return { machine_alarm_is_critical(alarm) ? "Reset" : "Unlock",
                     machine_alarm_is_homing(alarm) ? (lathe_axes ? "Home XZ" : "Home All") : "" };
        case Homing:
            return { "Reset", "" };
        case Cycle:
            return { "E-Stop", "Hold" };
        case Hold:
        case DoorClosed:
            return { "Quit", "Resume" };
        case Jog:
            return { "Jog Cancel", "" };
        default:
            return { "", "" };
    }
}

void machine_state_red_action() {
    switch (state) {
        case Alarm:
            if (machine_alarm_is_critical(lastAlarm)) {
                push_scene(&confirmScene, (void*)"Soft Reset?\nOffsets will be lost");
            } else {
                send_line("$X");
            }
            break;
        case Cycle:
        case Homing:
        case Hold:
        case DoorClosed:
            fnc_realtime(Reset);
            break;
        case Jog:
            send_jog_cancel();
            break;
        default:
            break;
    }
}

void machine_state_green_action() {
    switch (state) {
        case Cycle:
            fnc_realtime(FeedHold);
            break;
        case Hold:
        case DoorClosed:
            fnc_realtime(CycleStart);
            break;
        case Alarm:
            if (machine_alarm_is_homing(lastAlarm)) {
                send_line("$H");
            }
            break;
        default:
            break;
    }
    fnc_realtime(StatusReport);
}

bool machine_state_handle_confirmation(void* arg) {
    if (!arg || strcmp(static_cast<const char*>(arg), "Confirmed") != 0) {
        return false;
    }
    fnc_realtime(Reset);
    schedule_action([]() { send_line("$X"); });
    return true;
}
