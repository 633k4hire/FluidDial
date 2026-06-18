// Copyright (c) 2026 Matthew Metzger
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "e4math.h"
#include <stdint.h>
#include <string>

struct LatheStatus {
    bool        known                 = false;
    bool        available             = false;
    bool        enabled               = false;
    std::string spindle_speed_mode;
    bool        diameter_mode         = false;
    std::string feed_mode;
    float       programmed_s          = 0.0f;
    float       effective_rpm         = 0.0f;
    float       css_clamp_rpm         = 0.0f;
    float       min_css_diameter_mm   = 0.0f;
    bool        encoder_enabled       = false;
    bool        encoder_capture       = false;
    int         encoder_pulses_rev    = 0;
    int         active_tool           = 0;
    float       tool_x_offset_mm      = 0.0f;
    float       tool_z_offset_mm      = 0.0f;
    float       tool_nose_radius_mm   = 0.0f;
    bool        feedback_rpm_known    = false;
    float       feedback_rpm          = 0.0f;
    bool        feedback_index        = false;
    bool        feedback_angular_pos  = false;
    bool        feedback_angular_known = false;
    float       feedback_angular_rev  = 0.0f;
    int         feedback_rev_count    = 0;
    bool        feedback_stale        = false;
    bool        feedback_fault        = false;
    uint32_t    updated_ms            = 0;
};

struct LatheCommandResult {
    int         command         = 0;
    bool        known           = false;
    bool        ok              = false;
    bool        pending         = false;
    bool        timed_out       = false;
    bool        recoverable     = false;
    int         target_tool     = 0;
    std::string message;
    uint32_t    started_ms      = 0;
    uint32_t    updated_ms      = 0;
    uint32_t    last_refresh_ms = 0;
};

enum class LatheCommandSeverity {
    None,
    Info,
    Success,
    Warning,
    Error,
};

const LatheStatus&        lathe_status();
const LatheCommandResult& lathe_last_command_result();

bool lathe_status_known();
bool lathe_mode_active();
bool lathe_command_pending();
bool lathe_command_recoverable();
bool lathe_command_blocks_actions();
const char* lathe_command_status_text();
LatheCommandSeverity lathe_command_severity();

void request_lathe_status(bool force = false);
void lathe_mark_status_unavailable();
bool lathe_consume_status_error();

void lathe_begin_status_update();
void lathe_set_status_value(const char* id, const char* value);
void lathe_finish_status_update(bool ok);
void lathe_handle_command_response(int command, bool ok, const char* message);
void lathe_fail_pending_command(const char* message);
void lathe_clear_recoverable_command();
void lathe_poll_command();

void lathe_change_tool(int tool);
void lathe_select_tool_logical(int tool);
void lathe_save_tool(int tool, e4_t gx, e4_t gz, e4_t wx, e4_t wz, e4_t nose_radius, int orientation);
void lathe_touch_off_tool(int tool, e4_t machine_x, e4_t machine_z, e4_t reference_x, e4_t reference_z, bool diameter_mode);
