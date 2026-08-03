#pragma once

#include "FluidNCModel.h"

struct MachineStateActionLabels {
    const char* red;
    const char* green;
};

bool machine_alarm_is_homing(int alarm);
bool machine_alarm_is_critical(int alarm);
MachineStateActionLabels machine_state_action_labels(state_t display_state, int alarm, bool lathe_axes = false);

// These handlers intentionally preserve the established StatusScene safety
// behavior and are shared by every screen that exposes machine actions.
void machine_state_red_action();
void machine_state_green_action();
bool machine_state_handle_confirmation(void* arg);
