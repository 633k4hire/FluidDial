// Copyright (c) 2026 Matthew Metzger
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "LatheModel.h"

#include "FluidNCModel.h"
#include "HomingScene.h"
#include "MachineProfile.h"
#include "Scene.h"
#include "System.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>

static LatheStatus        s_status;
static LatheStatus        s_pending_status;
static LatheCommandResult s_last_command;
static uint32_t           s_last_status_request_ms = 0;
static bool               s_status_reply_expected  = false;
static int                s_pending_tool_change    = 0;

static const uint32_t LATHE_M6_TIMEOUT_MS              = 30000;
static const uint32_t LATHE_COMMAND_TIMEOUT_MS         = 8000;
static const uint32_t LATHE_COMMAND_REFRESH_MS         = 1000;
static const uint32_t LATHE_COMMAND_STILL_WAITING_MS   = 5000;

static void complete_command(int command, bool ok, const char* message, bool recoverable, bool timed_out, int target_tool = -1);

static std::string lower_copy(const char* value) {
    std::string s = value ? value : "";
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool parse_bool(const char* value) {
    std::string v = lower_copy(value);
    return v == "true" || v == "1" || v == "yes" || v == "on" || v == "enabled" || v == "g7" || v == "diameter";
}

static float parse_float(const char* value) {
    return value ? strtof(value, nullptr) : 0.0f;
}

static int parse_int(const char* value) {
    return value ? atoi(value) : 0;
}

static bool has_number(const char* value) {
    if (!value || !*value) {
        return false;
    }
    char* end = nullptr;
    strtof(value, &end);
    return end != value;
}

static bool status_enables_lathe(const LatheStatus& status) {
    return status.available && status.enabled;
}

static void refresh_homing_for_profile_change() {
    detect_homing_info();
    request_redisplay();
}

static void apply_status(const LatheStatus& next_status) {
    bool old_lathe_mode = machine_profile_is_lathe();
    s_status            = next_status;

    set_machine_profile_lathe(status_enables_lathe(s_status));
    if (old_lathe_mode != machine_profile_is_lathe()) {
        schedule_action(refresh_homing_for_profile_change);
    }
    if (s_last_command.pending && s_last_command.command == 6 && s_pending_tool_change > 0) {
        if (s_status.available && s_status.active_tool == s_pending_tool_change) {
            complete_command(6, true, "Tool change complete", false, false, s_pending_tool_change);
            s_pending_tool_change = 0;
        } else if (state == Alarm) {
            complete_command(6, false, "Alarm during command", true, false, s_pending_tool_change);
            s_pending_tool_change = 0;
        }
    }
    request_redisplay();
}

static std::string e4_string(e4_t value, int decimals = 4) {
    return std::string(e4_to_cstr(value, decimals));
}

static void scheduled_lathe_status_refresh() {
    request_lathe_status(true);
}

static uint32_t command_timeout_ms(int command) {
    return command == 6 ? LATHE_M6_TIMEOUT_MS : LATHE_COMMAND_TIMEOUT_MS;
}

static const char* command_response_message(int command, bool ok, const char* message) {
    if (message && *message) {
        return message;
    }
    if (command == 422) {
        return ok ? "Tool saved" : "Tool save failed";
    }
    if (command == 423) {
        return ok ? "Touch-off saved" : "Touch-off failed";
    }
    return ok ? "OK" : "Error";
}

static void begin_command(int command, const std::string& message, int target_tool = 0) {
    uint32_t now                    = millis();
    s_last_command.command         = command;
    s_last_command.known           = true;
    s_last_command.ok              = false;
    s_last_command.pending         = true;
    s_last_command.timed_out       = false;
    s_last_command.recoverable     = false;
    s_last_command.target_tool     = target_tool;
    s_last_command.message         = message;
    s_last_command.started_ms      = now;
    s_last_command.updated_ms      = now;
    s_last_command.last_refresh_ms = 0;
    request_redisplay();
}

static void complete_command(int command, bool ok, const char* message, bool recoverable, bool timed_out, int target_tool) {
    if (target_tool >= 0) {
        s_last_command.target_tool = target_tool;
    } else if (command != s_last_command.command) {
        s_last_command.target_tool = 0;
    }
    s_last_command.command     = command;
    s_last_command.known       = true;
    s_last_command.ok          = ok;
    s_last_command.pending     = false;
    s_last_command.timed_out   = timed_out;
    s_last_command.recoverable = recoverable;
    s_last_command.message     = message ? message : "";
    s_last_command.updated_ms  = millis();
    if (command != 6) {
        s_pending_tool_change = 0;
    }
    request_redisplay();
}

static void finish_command(int command, bool ok, const char* message, int target_tool = -1) {
    complete_command(command, ok, message, false, false, target_tool);
}

const LatheStatus& lathe_status() {
    return s_status;
}

const LatheCommandResult& lathe_last_command_result() {
    return s_last_command;
}

bool lathe_status_known() {
    return s_status.known;
}

bool lathe_mode_active() {
    return status_enables_lathe(s_status);
}

bool lathe_command_pending() {
    return s_last_command.pending;
}

bool lathe_command_recoverable() {
    return s_last_command.recoverable && !s_last_command.pending;
}

bool lathe_command_blocks_actions() {
    return s_last_command.pending || lathe_command_recoverable();
}

const char* lathe_command_status_text() {
    if (!s_last_command.known) {
        return "";
    }
    if (!s_last_command.message.empty()) {
        return s_last_command.message.c_str();
    }
    if (s_last_command.pending) {
        return "Waiting";
    }
    return s_last_command.ok ? "OK" : "Error";
}

LatheCommandSeverity lathe_command_severity() {
    if (!s_last_command.known) {
        return LatheCommandSeverity::None;
    }
    if (s_last_command.pending) {
        return s_last_command.message == "Still waiting" ? LatheCommandSeverity::Warning : LatheCommandSeverity::Info;
    }
    if (s_last_command.recoverable) {
        return LatheCommandSeverity::Error;
    }
    return s_last_command.ok ? LatheCommandSeverity::Success : LatheCommandSeverity::Error;
}

void request_lathe_status(bool force) {
    if (state == Disconnected) {
        return;
    }
    if (!force && s_status.known && !s_status.available) {
        return;
    }

    uint32_t now = millis();
    if (!force && s_last_status_request_ms != 0 && (uint32_t)(now - s_last_status_request_ms) < 500) {
        return;
    }
    s_last_status_request_ms = now;
    s_status_reply_expected  = true;
    send_line("[ESP421]", 500);
}

void lathe_mark_status_unavailable() {
    s_status_reply_expected = false;
    LatheStatus unavailable;
    unavailable.known      = true;
    unavailable.available  = false;
    unavailable.enabled    = false;
    unavailable.updated_ms = millis();
    apply_status(unavailable);
}

void lathe_begin_status_update() {
    s_pending_status            = LatheStatus();
    s_pending_status.known      = true;
    s_pending_status.available  = true;
    s_pending_status.updated_ms = millis();
}

void lathe_set_status_value(const char* id, const char* value) {
    if (!id) {
        return;
    }

    if (strcmp(id, "Lathe enabled") == 0) {
        s_pending_status.enabled = parse_bool(value);
    } else if (strcmp(id, "Spindle speed mode") == 0) {
        s_pending_status.spindle_speed_mode = value ? value : "";
    } else if (strcmp(id, "Diameter mode") == 0) {
        s_pending_status.diameter_mode = parse_bool(value);
    } else if (strcmp(id, "Feed mode") == 0) {
        s_pending_status.feed_mode = value ? value : "";
    } else if (strcmp(id, "Programmed S") == 0) {
        s_pending_status.programmed_s = parse_float(value);
    } else if (strcmp(id, "Effective RPM") == 0) {
        s_pending_status.effective_rpm = parse_float(value);
    } else if (strcmp(id, "CSS clamp RPM") == 0) {
        s_pending_status.css_clamp_rpm = parse_float(value);
    } else if (strcmp(id, "Minimum CSS diameter mm") == 0) {
        s_pending_status.min_css_diameter_mm = parse_float(value);
    } else if (strcmp(id, "Encoder enabled") == 0) {
        s_pending_status.encoder_enabled = parse_bool(value);
    } else if (strcmp(id, "Encoder capture active") == 0) {
        s_pending_status.encoder_capture = parse_bool(value);
    } else if (strcmp(id, "Encoder pulses/rev") == 0) {
        s_pending_status.encoder_pulses_rev = parse_int(value);
    } else if (strcmp(id, "Active lathe tool") == 0) {
        s_pending_status.active_tool = parse_int(value);
    } else if (strcmp(id, "Lathe tool X offset mm") == 0) {
        s_pending_status.tool_x_offset_mm = parse_float(value);
    } else if (strcmp(id, "Lathe tool Z offset mm") == 0) {
        s_pending_status.tool_z_offset_mm = parse_float(value);
    } else if (strcmp(id, "Tool nose radius mm") == 0) {
        s_pending_status.tool_nose_radius_mm = parse_float(value);
    } else if (strcmp(id, "Feedback measured RPM") == 0) {
        s_pending_status.feedback_rpm_known = has_number(value);
        s_pending_status.feedback_rpm       = s_pending_status.feedback_rpm_known ? parse_float(value) : 0.0f;
    } else if (strcmp(id, "Feedback index") == 0) {
        s_pending_status.feedback_index = parse_bool(value);
    } else if (strcmp(id, "Feedback angular position") == 0) {
        s_pending_status.feedback_angular_pos = parse_bool(value);
    } else if (strcmp(id, "Feedback angular rev") == 0) {
        s_pending_status.feedback_angular_known = has_number(value);
        s_pending_status.feedback_angular_rev   = s_pending_status.feedback_angular_known ? parse_float(value) : 0.0f;
    } else if (strcmp(id, "Feedback revolution count") == 0) {
        s_pending_status.feedback_rev_count = parse_int(value);
    } else if (strcmp(id, "Feedback stale") == 0) {
        s_pending_status.feedback_stale = parse_bool(value);
    } else if (strcmp(id, "Feedback fault") == 0) {
        s_pending_status.feedback_fault = parse_bool(value);
    }
}

void lathe_finish_status_update(bool ok) {
    s_status_reply_expected = false;
    if (!ok) {
        lathe_mark_status_unavailable();
        return;
    }
    apply_status(s_pending_status);
}

bool lathe_consume_status_error() {
    if (!s_status_reply_expected) {
        return false;
    }
    if ((uint32_t)(millis() - s_last_status_request_ms) > 2000) {
        s_status_reply_expected = false;
        return false;
    }
    if (s_status.known && s_status.available) {
        s_status_reply_expected = false;
        return false;
    }
    lathe_mark_status_unavailable();
    return true;
}

void lathe_handle_command_response(int command, bool ok, const char* message) {
    finish_command(command, ok, command_response_message(command, ok, message));

    if (command == 422 || command == 423) {
        schedule_action(scheduled_lathe_status_refresh);
    }
}

void lathe_fail_pending_command(const char* message) {
    if (!s_last_command.pending) {
        return;
    }
    complete_command(s_last_command.command, false, message ? message : "Command failed", true, false, s_last_command.target_tool);
    s_pending_tool_change = 0;
}

void lathe_clear_recoverable_command() {
    if (!lathe_command_recoverable()) {
        return;
    }
    s_last_command = LatheCommandResult();
    s_pending_tool_change = 0;
    request_redisplay();
}

void lathe_poll_command() {
    if (!s_last_command.pending) {
        return;
    }

    uint32_t now     = millis();
    uint32_t elapsed = (uint32_t)(now - s_last_command.started_ms);
    if (elapsed >= command_timeout_ms(s_last_command.command)) {
        complete_command(s_last_command.command, false, "Timed out", true, true, s_last_command.target_tool);
        s_pending_tool_change = 0;
        return;
    }

    if (elapsed >= LATHE_COMMAND_STILL_WAITING_MS && s_last_command.message != "Still waiting") {
        s_last_command.message    = "Still waiting";
        s_last_command.updated_ms = now;
        request_redisplay();
    }

    if (s_last_command.command == 6 &&
        (s_last_command.last_refresh_ms == 0 || (uint32_t)(now - s_last_command.last_refresh_ms) >= LATHE_COMMAND_REFRESH_MS)) {
        s_last_command.last_refresh_ms = now;
        request_lathe_status(true);
    }
}

void lathe_change_tool(int tool) {
    if (tool < 1 || tool > 5) {
        return;
    }
    if (lathe_command_blocks_actions()) {
        return;
    }
    s_pending_tool_change = tool;
    begin_command(6, "Waiting for T/M6", tool);
    send_linef("T%d", tool);
    send_line("M6");
    request_lathe_status(true);
}

void lathe_select_tool_logical(int tool) {
    if (tool < 1 || tool > 5) {
        return;
    }
    if (lathe_command_blocks_actions()) {
        return;
    }
    send_linef("M61Q%d", tool);
    finish_command(61, true, "Logical tool selected", tool);
    request_lathe_status(true);
}

void lathe_save_tool(int tool, e4_t gx, e4_t gz, e4_t wx, e4_t wz, e4_t nose_radius, int orientation) {
    if (tool < 1 || tool > 5) {
        return;
    }
    if (lathe_command_blocks_actions()) {
        return;
    }

    std::string cmd = "[ESP422]T=" + std::to_string(tool);
    cmd += " GX=" + e4_string(gx);
    cmd += " GZ=" + e4_string(gz);
    cmd += " WX=" + e4_string(wx);
    cmd += " WZ=" + e4_string(wz);
    cmd += " NR=" + e4_string(nose_radius);
    cmd += " O=" + std::to_string(orientation);
    begin_command(422, "Saving tool", tool);
    send_line(cmd.c_str(), 1000);
}

void lathe_touch_off_tool(int tool, e4_t machine_x, e4_t machine_z, e4_t reference_x, e4_t reference_z, bool diameter_mode) {
    if (tool < 1 || tool > 5) {
        return;
    }
    if (lathe_command_blocks_actions()) {
        return;
    }

    std::string cmd = "[ESP423]T=" + std::to_string(tool);
    cmd += " MX=" + e4_string(machine_x);
    cmd += " RX=" + e4_string(reference_x);
    cmd += " MODE=";
    cmd += diameter_mode ? "diameter" : "radius";
    cmd += " MZ=" + e4_string(machine_z);
    cmd += " RZ=" + e4_string(reference_z);
    begin_command(423, "Applying touch-off", tool);
    send_line(cmd.c_str(), 1000);
}
