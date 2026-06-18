// Copyright (c) 2026 Matthew Metzger
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

enum class MachineProfileKind {
    Default,
    Lathe,
};

MachineProfileKind machine_profile_kind();
bool               machine_profile_is_lathe();
void               set_machine_profile_lathe(bool enabled);

int         profile_axis_count();
int         profile_machine_axis(int display_axis);
int         profile_display_axis_for_machine(int machine_axis);
char        profile_axis_char(int display_axis);
const char* profile_axis_cstr(int display_axis);
const char* profile_axis_config_name(int display_axis);
