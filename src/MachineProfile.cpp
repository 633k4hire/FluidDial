// Copyright (c) 2026 Matthew Metzger
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineProfile.h"

static MachineProfileKind s_profile = MachineProfileKind::Default;

static const int   default_axes[]       = { 0, 1, 2 };
static const char  default_axis_chars[] = { 'X', 'Y', 'Z' };
static const char* default_axis_names[] = { "x", "y", "z" };

static const int   lathe_axes[]       = { 0, 2, 5 };
static const char  lathe_axis_chars[] = { 'X', 'Z', 'C' };
static const char* lathe_axis_names[] = { "x", "z", "c" };

MachineProfileKind machine_profile_kind() {
    return s_profile;
}

bool machine_profile_is_lathe() {
    return s_profile == MachineProfileKind::Lathe;
}

void set_machine_profile_lathe(bool enabled) {
    s_profile = enabled ? MachineProfileKind::Lathe : MachineProfileKind::Default;
}

int profile_axis_count() {
    return 3;
}

static bool valid_display_axis(int display_axis) {
    return display_axis >= 0 && display_axis < profile_axis_count();
}

int profile_machine_axis(int display_axis) {
    if (!valid_display_axis(display_axis)) {
        return 0;
    }
    return machine_profile_is_lathe() ? lathe_axes[display_axis] : default_axes[display_axis];
}

int profile_display_axis_for_machine(int machine_axis) {
    for (int display_axis = 0; display_axis < profile_axis_count(); ++display_axis) {
        if (profile_machine_axis(display_axis) == machine_axis) {
            return display_axis;
        }
    }
    return -1;
}

char profile_axis_char(int display_axis) {
    if (!valid_display_axis(display_axis)) {
        return '?';
    }
    return machine_profile_is_lathe() ? lathe_axis_chars[display_axis] : default_axis_chars[display_axis];
}

const char* profile_axis_cstr(int display_axis) {
    static char ret[2] = { '\0', '\0' };
    ret[0]             = profile_axis_char(display_axis);
    return ret;
}

const char* profile_axis_config_name(int display_axis) {
    if (!valid_display_axis(display_axis)) {
        return "x";
    }
    return machine_profile_is_lathe() ? lathe_axis_names[display_axis] : default_axis_names[display_axis];
}
