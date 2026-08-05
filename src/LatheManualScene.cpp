// Copyright (c) 2026 Matthew Metzger
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "LatheManualScene.h"

#include "ConfigItem.h"
#include "Drawing.h"
#include "FluidNCModel.h"
#include "HomingScene.h"
#include "LatheModel.h"
#include "MachineProfile.h"
#include "Menu.h"
#include "PieMenu.h"
#include "System.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern Scene confirmScene;

namespace {
constexpr float Pi = 3.14159265358979323846f;

float clampf(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

const char* bool_sign(bool positive) {
    return positive ? "+" : "-";
}

bool status_is(const std::string& value, const char* expected) {
    return value == expected;
}

bool spindle_stopped() {
    const LatheStatus& lathe = lathe_status();
    if (!lathe.spindle_state.empty()) {
        return status_is(lathe.spindle_state, "STOPPED");
    }
    return lathe.effective_rpm <= 0.5f;
}

bool spindle_running_cw() {
    return status_is(lathe_status().spindle_state, "CLOCKWISE");
}

bool spindle_running_ccw() {
    return status_is(lathe_status().spindle_state, "COUNTERCLOCKWISE");
}

bool shared_chuck_is_spindle() {
    return status_is(lathe_status().shared_chuck_mode, "SPINDLE") ||
           status_is(lathe_status().shared_chuck_mode, "spindle");
}

bool manual_link_ready() {
    return lathe_mode_active() && operator_basic_motion_actions_available() && fnc_is_connected();
}

bool x_homed() {
    return is_axis_homed(0);
}

bool z_homed() {
    return is_axis_homed(1);
}

void draw_value_row(int y, const char* label, const char* value, bool selected, int value_color = WHITE) {
    drawOutlinedRect(28, y - 12, 184, 24, selected ? NAVY : BLACK, selected ? CYAN : DARKGREY);
    text(label, 36, y + 1, selected ? CYAN : LIGHTGREY, TINY, middle_left);
    text(value, 204, y + 1, value_color, TINY, middle_right);
}

void poll_lathe_status(uint32_t& last_request_ms) {
    const uint32_t now = millis();
    if ((uint32_t)(now - last_request_ms) >= 1000) {
        request_lathe_status();
        last_request_ms = now;
    }
}

std::string jog_command(float feed, float x, float z, float c = 0.0f) {
    char command[128];
    int  used = snprintf(command, sizeof(command), "$J=G91G21F%.3f", feed);
    if (std::fabs(x) >= 0.00005f) {
        used += snprintf(command + used, sizeof(command) - used, "X%.4f", x);
    }
    if (std::fabs(z) >= 0.00005f) {
        used += snprintf(command + used, sizeof(command) - used, "Z%.4f", z);
    }
    if (std::fabs(c) >= 0.00005f) {
        snprintf(command + used, sizeof(command) - used, "C%.4f", c);
    }
    return command;
}

class JogSequenceScene : public Scene {
protected:
    struct JogMove {
        float x;
        float z;
        float c;
        float feed;

        JogMove(float x_value = 0.0f, float z_value = 0.0f, float c_value = 0.0f, float feed_value = 60.0f) :
            x(x_value), z(z_value), c(c_value), feed(feed_value) {}
    };

    std::vector<JogMove> _moves;
    size_t               _move_index = 0;
    bool                 _running    = false;
    bool                 _seen_jog   = false;
    bool                 _cancelled  = false;
    uint32_t             _sent_ms    = 0;
    uint32_t             _expected_done_ms = 0;
    uint32_t             _last_motion_probe_ms = 0;
    std::string          _result;

    JogSequenceScene(const char* name) : Scene(name) {}

    virtual bool preflight(std::string& reason) const {
        if (!manual_link_ready()) {
            reason = "Controller not ready";
            return false;
        }
        if (state != Idle) {
            reason = "Machine must be Idle";
            return false;
        }
        return true;
    }

    void start_moves() {
        std::string reason;
        if (!preflight(reason) || _moves.empty()) {
            _result = reason.empty() ? "No motion generated" : reason;
            _running = false;
            request_redisplay();
            return;
        }
        _move_index = 0;
        _running    = true;
        _seen_jog   = false;
        _cancelled  = false;
        _result     = "Running";
        send_current_move();
    }

    void send_current_move() {
        if (!_running || _move_index >= _moves.size()) {
            return;
        }
        const JogMove& move = _moves[_move_index];
        const std::string command = jog_command(move.feed, move.x, move.z, move.c);
        dbg_printf("ManualLathe: %s\r\n", command.c_str());
        _sent_ms = millis();
        const float planner_distance = std::sqrt(move.x * move.x + move.z * move.z + move.c * move.c);
        const uint32_t nominal_ms = move.feed > 0.0f ? (uint32_t)(planner_distance * 60000.0f / move.feed) : 0;
        // Add a full status interval plus acceleration margin. Very short
        // moves can start and finish between reports; the fallback in onPoll
        // may only advance after this conservative window and a reported Idle.
        _expected_done_ms = _sent_ms + std::max<uint32_t>(nominal_ms, 100) + 1250;
        _last_motion_probe_ms = 0;
        send_jog_line(command.c_str());
    }

    void complete_current_move() {
        if (!_running) return;
        ++_move_index;
        _seen_jog = false;
        if (_move_index >= _moves.size()) {
            _running = false;
            _result  = "Completed";
            request_lathe_status(true);
        } else {
            send_current_move();
        }
    }

    void cancel_sequence(const char* result = "Canceled") {
        if (_running || state == Jog) {
            send_jog_cancel();
        }
        _running   = false;
        _seen_jog  = false;
        _cancelled = true;
        _result    = result;
        request_redisplay();
    }

public:
    void onRedButtonPress() override { cancel_sequence(); }

    void onError(const char* errstr) override {
        _running  = false;
        _seen_jog = false;
        _result   = std::string("Rejected: ") + (errstr ? errstr : "controller error");
        request_redisplay();
    }

    void onTouchPress() override {
        if (_running || state == Jog) {
            cancel_sequence();
        }
    }

    void onStateChange(state_t old_state) override {
        (void)old_state;
        if (!_running) {
            request_redisplay();
            return;
        }
        if (state == Jog) {
            _seen_jog = true;
        } else if (state == Alarm || state == Critical || state == ConfigAlarm || state == Disconnected) {
            cancel_sequence("Interrupted");
        } else if (_seen_jog && state == Idle) {
            complete_current_move();
        }
        request_redisplay();
    }

    void onExit() override {
        if (_running || state == Jog) {
            cancel_sequence("Scene exited");
        }
    }

    void onPoll() override {
        if (!_running) return;
        const uint32_t now = millis();
        if (!_last_motion_probe_ms || (uint32_t)(now - _last_motion_probe_ms) >= 200) {
            request_status_report();
            _last_motion_probe_ms = now;
        }
        if (state == Idle && static_cast<int32_t>(now - _expected_done_ms) >= 0) {
            complete_current_move();
        } else if (state != Idle && static_cast<int32_t>(now - (_expected_done_ms + 5000)) >= 0) {
            cancel_sequence("Motion completion timeout");
        }
    }
};
}  // namespace

LatheVectorMove lathe_angle_vector(float path_mm, float angle_degrees, bool positive_slope, bool diameter_mode) {
    const float radians = clampf(angle_degrees, 0.0f, 90.0f) * Pi / 180.0f;
    const float radial  = path_mm * std::sin(radians) * (positive_slope ? 1.0f : -1.0f);
    LatheVectorMove move;
    move.x_command_mm = radial * (diameter_mode ? 2.0f : 1.0f);
    move.z_mm         = path_mm * std::cos(radians);
    if (std::fabs(move.x_command_mm) < 0.0000001f) move.x_command_mm = 0.0f;
    if (std::fabs(move.z_mm) < 0.0000001f) move.z_mm = 0.0f;
    return move;
}

float lathe_thread_pitch_mm(bool tpi_mode, float pitch_or_tpi) {
    if (pitch_or_tpi <= 0.0f) return 0.0f;
    return tpi_mode ? 25.4f / pitch_or_tpi : pitch_or_tpi;
}

float lathe_thread_c_degrees(float z_travel_mm, float pitch_mm) {
    return pitch_mm > 0.0f ? std::fabs(z_travel_mm) * 360.0f / pitch_mm : 0.0f;
}

float lathe_thread_planner_feed(float pitch_mm, float c_rpm) {
    const float z_rate = pitch_mm * c_rpm;
    const float c_rate = 360.0f * c_rpm;
    return std::sqrt(z_rate * z_rate + c_rate * c_rate);
}

namespace {
class AngleJogScene : public Scene {
    int      _angle             = 45;
    int      _step_index        = 1;
    bool     _positive_slope    = true;
    bool     _armed             = false;
    bool     _confirming_arm    = false;
    bool     _continuous        = false;
    bool     _armed_diameter    = true;
    double   _ideal_path_mm     = 0.0;
    double   _sent_x_mm         = 0.0;
    double   _sent_z_mm         = 0.0;
    int      _preset_index      = 2;
    uint32_t _last_status_ms    = 0;
    std::string _confirm_message;

    static constexpr float Steps[3] = { 0.01f, 0.1f, 1.0f };
    static constexpr int Presets[4] = { 20, 35, 45, 80 };

    void reset_residuals() {
        _ideal_path_mm = 0.0;
        _sent_x_mm = 0.0;
        _sent_z_mm = 0.0;
    }

    bool can_arm(std::string& reason) const {
        if (!manual_link_ready() || state != Idle) {
            reason = "Controller must be Idle";
            return false;
        }
        if (!x_homed() || !z_homed()) {
            reason = "Home X and Z first";
            return false;
        }
        return true;
    }

    void disarm(const char* reason = nullptr) {
        if (_continuous || state == Jog) send_jog_cancel();
        _continuous = false;
        _armed      = false;
        reset_residuals();
        if (reason) _confirm_message = reason;
        request_redisplay();
    }

    void precise_move(int delta) {
        if (!_armed || delta == 0) return;
        if (lathe_status().diameter_mode != _armed_diameter) {
            disarm("G7/G8 changed; re-arm");
            return;
        }
        const double previous_path = _ideal_path_mm;
        _ideal_path_mm += (double)delta * Steps[_step_index];
        LatheVectorMove ideal = lathe_angle_vector((float)_ideal_path_mm, (float)_angle, _positive_slope, _armed_diameter);
        float dx = ideal.x_command_mm - (float)_sent_x_mm;
        float dz = ideal.z_mm - (float)_sent_z_mm;
        _sent_x_mm = ideal.x_command_mm;
        _sent_z_mm = ideal.z_mm;
        float path = std::fabs((float)(_ideal_path_mm - previous_path));
        float feed = clampf(path * 600.0f, 2.0f, 600.0f);
        send_jog_line(jog_command(feed, dx, dz).c_str());
    }

    void start_continuous(bool positive) {
        if (!_armed || state != Idle) return;
        const float path = positive ? 5000.0f : -5000.0f;
        LatheVectorMove move = lathe_angle_vector(path, (float)_angle, _positive_slope, _armed_diameter);
        send_jog_line(jog_command(300.0f, move.x_command_mm, move.z_mm).c_str());
        _continuous = true;
    }

    void stop_continuous() {
        if (!_continuous) return;
        send_jog_cancel();
        _continuous = false;
        reset_residuals();
        request_redisplay();
    }

public:
    AngleJogScene() : Scene("Angle Jog") {}

    void onEntry(void* arg) override {
        if (arg && strcmp((const char*)arg, "Confirmed") == 0 && _confirming_arm) {
            std::string reason;
            if (can_arm(reason)) {
                _armed          = true;
                _armed_diameter = lathe_status().diameter_mode;
                reset_residuals();
            } else {
                _confirm_message = reason;
            }
        }
        _confirming_arm = false;
        request_lathe_status(true);
        if (initPrefs()) {
            getPref("Angle", &_angle);
            getPref("Step", &_step_index);
            int slope = 1;
            getPref("Slope", &slope);
            _positive_slope = slope != 0;
        }
        _angle = std::max(0, std::min(_angle, 90));
        _step_index = std::max(0, std::min(_step_index, 2));
    }

    void onEncoder(int delta) override {
        if (_armed) precise_move(delta);
        else {
            _angle = std::max(0, std::min(90, _angle + delta));
            setPref("Angle", _angle);
            reDisplay();
        }
    }

    void onDialButtonPress() override {
        if (_armed) {
            disarm();
            return;
        }
        std::string reason;
        if (!can_arm(reason)) {
            _confirm_message = reason;
            reDisplay();
            return;
        }
        char msg[80];
        snprintf(msg, sizeof(msg), "Arm %d deg %s?\nDial and +/- will move", _angle, _positive_slope ? "/" : "\\");
        _confirm_message = msg;
        _confirming_arm  = true;
        push_scene(&confirmScene, (void*)_confirm_message.c_str());
    }

    void onGreenButtonPress() override { start_continuous(true); }
    void onRedButtonPress() override { start_continuous(false); }
    void onGreenButtonRelease() override { stop_continuous(); }
    void onRedButtonRelease() override { stop_continuous(); }

    void onTouchClick() override {
        if (_armed || state == Jog) {
            disarm();
            return;
        }
        if (touchX < 80) {
            _positive_slope = !_positive_slope;
            setPref("Slope", _positive_slope ? 1 : 0);
        } else if (touchX > 160) {
            _step_index = (_step_index + 1) % 3;
            setPref("Step", _step_index);
        } else {
            _preset_index = (_preset_index + 1) % 4;
            _angle = Presets[_preset_index];
            setPref("Angle", _angle);
        }
        reDisplay();
    }

    void onStateChange(state_t old_state) override {
        (void)old_state;
        if (_armed && state != Idle && state != Jog) disarm("Controller state changed");
        request_redisplay();
    }

    void onError(const char* errstr) override {
        std::string message = std::string("Rejected: ") + (errstr ? errstr : "controller error");
        disarm(message.c_str());
    }

    void onPoll() override {
        poll_lathe_status(_last_status_ms);
        if (_armed && (!manual_link_ready() || lathe_status().diameter_mode != _armed_diameter)) {
            disarm("Status changed; re-arm");
        }
    }

    void onExit() override { if (_armed || state == Jog) disarm("Scene exited"); }

    void reDisplay() override {
        background();
        drawMenuTitle("Angle Jog");
        char title[40];
        snprintf(title, sizeof(title), "%d deg  %s", _angle, _positive_slope ? "/" : "\\");
        centered_text(title, 52, _armed ? GREEN : CYAN, MEDIUM);

        const float step = Steps[_step_index];
        LatheVectorMove move = lathe_angle_vector(step, (float)_angle, _positive_slope, lathe_status().diameter_mode);
        char step_text[24], x_text[24], z_text[24];
        snprintf(step_text, sizeof(step_text), "%.2f mm path", step);
        snprintf(x_text, sizeof(x_text), "Xcmd %+.4f", move.x_command_mm);
        snprintf(z_text, sizeof(z_text), "Z %+.4f", move.z_mm);
        centered_text(step_text, 84, WHITE, SMALL);
        centered_text(x_text, 110, LIGHTGREY, TINY);
        centered_text(z_text, 130, LIGHTGREY, TINY);
        centered_text(lathe_status().diameter_mode ? "G7 diameter" : "G8 radius", 151, YELLOW, TINY);

        int x0 = 120, y0 = 176;
        float radians = _angle * Pi / 180.0f;
        int dx = (int)(42.0f * std::sin(radians) * (_positive_slope ? 1.0f : -1.0f));
        int dy = (int)(42.0f * std::cos(radians));
        canvas.drawLine(x0 - dx, y0 + dy, x0 + dx, y0 - dy, _armed ? GREEN : CYAN);
        canvas.fillCircle(x0 + dx, y0 - dy, 3, _armed ? GREEN : CYAN);

        if (!_confirm_message.empty() && !_armed) centered_text(_confirm_message.c_str(), 207, ORANGE, TINY);
        drawButtonLegends(_armed ? "Jog-" : "Slope", _armed ? "Jog+" : "Preset", _armed ? "Disarm" : "Arm");
        refreshDisplay();
    }
} angleJogScene;

constexpr float AngleJogScene::Steps[3];
constexpr int AngleJogScene::Presets[4];

class SpindleScene : public Scene {
    int         _target_rpm      = 50;
    bool        _cw              = true;
    bool        _confirming      = false;
    int         _confirm_rpm     = 0;
    bool        _confirm_cw      = true;
    int         _preset_index    = 1;
    uint32_t    _last_status_ms  = 0;
    std::string _message;
    static constexpr int Presets[5] = { 50, 100, 200, 300, 500 };

    int max_rpm() const {
        const float reported = lathe_status().spindle_maximum_rpm;
        return std::max(10, (int)std::floor(reported > 0.0f ? reported : 500.0f));
    }

public:
    SpindleScene() : Scene("Spindle") {}

    void onEntry(void* arg) override {
        if (arg && strcmp((const char*)arg, "Confirmed") == 0 && _confirming) {
            send_linef("M%dS%d", _confirm_cw ? 3 : 4, _confirm_rpm);
            _message = "Command sent";
            lathe_schedule_status_refresh(true);
        }
        _confirming = false;
        request_lathe_status(true);
        if (initPrefs()) {
            getPref("StepperRPMV2", &_target_rpm);
            int cw = 1;
            getPref("CW", &cw);
            _cw = cw != 0;
        }
        _target_rpm = std::max(0, std::min(_target_rpm, max_rpm()));
    }

    void onEncoder(int delta) override {
        _target_rpm = std::max(0, std::min(max_rpm(), _target_rpm + delta * 10));
        setPref("StepperRPMV2", _target_rpm);
        reDisplay();
    }

    void onTouchClick() override {
        if (touchY < 75) {
            _preset_index = (_preset_index + 1) % 5;
            _target_rpm = std::min(Presets[_preset_index], max_rpm());
            setPref("StepperRPMV2", _target_rpm);
        } else {
            _cw = touchX >= 120;
            setPref("CW", _cw ? 1 : 0);
        }
        reDisplay();
    }

    void onGreenButtonPress() override {
        if (!manual_link_ready() || state != Idle || _target_rpm <= 0) {
            _message = "Controller/RPM not ready";
            reDisplay();
            return;
        }
        const bool opposite = (_cw && spindle_running_ccw()) || (!_cw && spindle_running_cw());
        if (opposite) {
            _message = "Press STOP before reversing";
            reDisplay();
            return;
        }
        _confirm_rpm = _target_rpm;
        _confirm_cw  = _cw;
        char msg[64];
        snprintf(msg, sizeof(msg), "Start %s at %d RPM?\nRed remains immediate STOP", _cw ? "CW" : "CCW", _target_rpm);
        _message = msg;
        _confirming = true;
        push_scene(&confirmScene, (void*)_message.c_str());
    }

    void onRedButtonPress() override {
        send_line("M5");
        _message = "STOP sent";
        lathe_schedule_status_refresh(true);
        reDisplay();
    }

    void onDialButtonPress() override { pop_scene(); }

    void onPoll() override { poll_lathe_status(_last_status_ms); }
    void onStateChange(state_t old_state) override { (void)old_state; request_redisplay(); }

    void onError(const char* errstr) override {
        _message = std::string("Rejected: ") + (errstr ? errstr : "controller error");
        request_redisplay();
    }

    void reDisplay() override {
        background();
        drawMenuTitle("Spindle");
        char rpm[32];
        snprintf(rpm, sizeof(rpm), "%d RPM", _target_rpm);
        centered_text(rpm, 58, GREEN, MEDIUM);
        centered_text(_cw ? "CW selected" : "CCW selected", 88, CYAN, SMALL);

        const LatheStatus& lathe = lathe_status();
        const char* actual = lathe.spindle_state.empty() ? "State unknown" : lathe.spindle_state.c_str();
        centered_text(actual, 120, spindle_stopped() ? LIGHTGREY : GREEN, SMALL);
        char measured[48];
        if (lathe.feedback_rpm_known) snprintf(measured, sizeof(measured), "Measured %.1f / Cmd %.1f", lathe.feedback_rpm, lathe.spindle_commanded_rpm);
        else snprintf(measured, sizeof(measured), "Open-loop %.1f / Cmd %.1f", lathe.spindle_open_loop_rpm, lathe.spindle_commanded_rpm);
        centered_text(measured, 147, LIGHTGREY, TINY);
        char ownership[64];
        snprintf(ownership, sizeof(ownership), "%s  %s", lathe.spindle_drive.empty() ? "Drive unknown" : lathe.spindle_drive.c_str(),
                 lathe.shared_chuck_mode.empty() ? "mode unknown" : lathe.shared_chuck_mode.c_str());
        centered_text(ownership, 169, YELLOW, TINY);
        if (!_message.empty()) centered_text(_message.c_str(), 194, ORANGE, TINY);
        drawButtonLegends("STOP", spindle_stopped() ? "Start" : "Apply", "Back");
        refreshDisplay();
    }
} spindleScene;

constexpr int SpindleScene::Presets[5];

StringConfigItem c_axis_max_rate("$/axes/c/max_rate_mm_per_min");

class CPositionScene : public Scene {
    int         _preset_index   = 2;
    bool        _armed          = false;
    bool        _confirming     = false;
    float       _residual_deg   = 0.0f;
    uint32_t    _last_status_ms = 0;
    std::string _message;
    static constexpr float Presets[5] = { 1.0f, 5.0f, 15.0f, 45.0f, 90.0f };

    float quantum_deg() const {
        const int steps = lathe_status().spindle_steps_rev;
        return steps > 0 ? 360.0f / steps : 0.225f;
    }

    float positioning_feed(float degrees) const {
        const float configured = c_axis_max_rate.known() ? strtof(c_axis_max_rate.get().c_str(), nullptr) : 2000.0f;
        const float maximum    = configured > 0.0f ? configured : 2000.0f;
        // Match the proven normal precise-jog behavior: target roughly 100 ms
        // for a small detent, while respecting the configured C positioning
        // ceiling. The planner remains responsible for acceleration.
        return std::min(maximum, std::max(1.0f, std::fabs(degrees) * 600.0f));
    }

    bool ready(std::string& reason) const {
        if (!manual_link_ready() || state != Idle) { reason = "Controller must be Idle"; return false; }
        if (!spindle_stopped() || shared_chuck_is_spindle()) { reason = "Stop spindle first"; return false; }
        if (!lathe_status().c_position_dead_reckoned || lathe_status().spindle_steps_rev <= 0) {
            reason = "C-stepper status unavailable";
            return false;
        }
        return true;
    }

    void disarm(bool cancel) {
        if (cancel && state == Jog) send_jog_cancel();
        _armed = false;
        _residual_deg = 0.0f;
        _message = "Disarmed";
    }

public:
    CPositionScene() : Scene("C Position") {}

    void onEntry(void* arg) override {
        if (arg && strcmp((const char*)arg, "Confirmed") == 0 && _confirming) {
            std::string reason;
            if (ready(reason)) {
                _armed = true;
                _message = "Armed: turn Dial";
            } else {
                _message = reason;
            }
        }
        _confirming = false;
        request_lathe_status(true);
    }

    void onEncoder(int delta) override {
        if (!_armed || delta == 0) return;
        std::string reason;
        if (!ready(reason)) {
            disarm(state == Jog);
            _message = reason;
            reDisplay();
            return;
        }

        const float desired = delta * Presets[_preset_index] + _residual_deg;
        const float quantum = quantum_deg();
        const int pulses = (int)std::lround(desired / quantum);
        if (pulses == 0) {
            _residual_deg = desired;
            return;
        }
        const float commanded = pulses * quantum;
        _residual_deg = desired - commanded;
        char command[64];
        snprintf(command, sizeof(command), "$J=G91G21F%.3fC%.4f", positioning_feed(commanded), commanded);
        send_jog_line(command);
        char msg[48];
        snprintf(msg, sizeof(msg), "Command %.3f deg (%d pulses)", commanded, std::abs(pulses));
        _message = msg;
        reDisplay();
    }

    void onTouchClick() override {
        if (_armed) {
            disarm(true);
        } else {
            _preset_index = (_preset_index + 1) % 5;
            _message.clear();
        }
        reDisplay();
    }

    void onGreenButtonPress() override {
        if (_armed) return;
        std::string reason;
        if (!ready(reason)) {
            _message = reason;
            reDisplay();
            return;
        }
        char msg[80];
        snprintf(msg, sizeof(msg), "Arm C positioning?\nDial step %.1f deg", Presets[_preset_index]);
        _message = msg;
        _confirming = true;
        push_scene(&confirmScene, (void*)_message.c_str());
    }

    void onRedButtonPress() override {
        if (_armed) disarm(true);
        reDisplay();
    }

    void onDialButtonPress() override {
        if (_armed) {
            disarm(true);
            reDisplay();
        } else {
            pop_scene();
        }
    }

    void onPoll() override { poll_lathe_status(_last_status_ms); }
    void onStateChange(state_t old_state) override {
        (void)old_state;
        if (_armed && state != Idle && state != Jog) disarm(true);
        request_redisplay();
    }
    void onAlarm() { disarm(true); request_redisplay(); }
    void onExit() override { if (_armed) disarm(true); }

    void reDisplay() override {
        background();
        drawMenuTitle("C Position");
        char step[36], quantum[42], ownership[52];
        snprintf(step, sizeof(step), "%.1f deg/detent", Presets[_preset_index]);
        snprintf(quantum, sizeof(quantum), "Microstep %.3f deg", quantum_deg());
        snprintf(ownership, sizeof(ownership), "%s  %s", _armed ? "ARMED" : "Disarmed",
                 lathe_status().shared_chuck_mode.empty() ? "mode unknown" : lathe_status().shared_chuck_mode.c_str());
        centered_text(step, 66, _armed ? GREEN : CYAN, SMALL);
        centered_text(quantum, 103, LIGHTGREY, TINY);
        centered_text("Open-loop position", 126, YELLOW, TINY);
        centered_text("Dead-reckoned", 143, YELLOW, TINY);
        centered_text(ownership, 166, _armed ? GREEN : LIGHTGREY, SMALL);
        if (!_message.empty()) centered_text(_message.c_str(), 188, ORANGE, TINY);
        drawButtonLegends(_armed ? "Cancel" : "", _armed ? "" : "Arm", _armed ? "Disarm" : "Back");
        refreshDisplay();
    }
} cPositionScene;

constexpr float CPositionScene::Presets[5];

class ThreadProofScene : public JogSequenceScene {
    int         _selection       = 0;
    bool        _tpi_mode       = false;
    int         _pitch_microns  = 1000;
    int         _tpi            = 20;
    int         _travel_tenths  = 20;
    int         _rpm_tenths     = 10;
    bool        _z_positive     = false;
    bool        _c_positive     = true;
    bool        _confirming     = false;
    uint32_t    _last_status_ms = 0;
    std::string _confirm_message;

    float c_max_rpm() const {
        if (!c_axis_max_rate.known()) return 5.0f;
        const float rate = strtof(c_axis_max_rate.get().c_str(), nullptr);
        return rate > 0.0f ? rate / 360.0f : 5.0f;
    }

    float pitch_mm() const { return lathe_thread_pitch_mm(_tpi_mode, _tpi_mode ? (float)_tpi : _pitch_microns / 1000.0f); }
    float travel_mm() const { return _travel_tenths / 10.0f; }
    float rpm() const { return std::min(_rpm_tenths / 10.0f, c_max_rpm()); }

    bool preflight(std::string& reason) const override {
        if (!JogSequenceScene::preflight(reason)) return false;
        if (!z_homed()) { reason = "Home Z first"; return false; }
        if (!spindle_stopped() || shared_chuck_is_spindle()) { reason = "Stop spindle first"; return false; }
        if (pitch_mm() <= 0.0f || travel_mm() <= 0.0f || rpm() <= 0.0f) { reason = "Pitch/travel/RPM invalid"; return false; }
        return true;
    }

    void build_move() {
        _moves.clear();
        const float pitch = pitch_mm();
        const float z = travel_mm() * (_z_positive ? 1.0f : -1.0f);
        const float c = lathe_thread_c_degrees(z, pitch) * (_c_positive ? 1.0f : -1.0f);
        _moves.push_back({ 0.0f, z, c, lathe_thread_planner_feed(pitch, rpm()) });
    }

public:
    ThreadProofScene() : JogSequenceScene("Thread Proof") {}

    void onEntry(void* arg) override {
        if (arg && strcmp((const char*)arg, "Confirmed") == 0 && _confirming) {
            build_move();
            start_moves();
        }
        _confirming = false;
        request_lathe_status(true);
        c_axis_max_rate.init();
        if (initPrefs()) {
            int tpi_mode = 0;
            getPref("TPI mode", &tpi_mode);
            _tpi_mode = tpi_mode != 0;
            getPref("Pitch um", &_pitch_microns);
            getPref("TPI", &_tpi);
            getPref("Travel 0.1", &_travel_tenths);
            getPref("RPM 0.1", &_rpm_tenths);
            int zp = 0, cp = 1;
            getPref("Z positive", &zp);
            getPref("C positive", &cp);
            _z_positive = zp != 0;
            _c_positive = cp != 0;
        }
    }

    void onEncoder(int delta) override {
        if (_running || delta == 0) return;
        switch (_selection) {
            case 0:
                if (_tpi_mode) _tpi = std::max(1, std::min(200, _tpi + delta));
                else _pitch_microns = std::max(50, std::min(10000, _pitch_microns + delta * 50));
                break;
            case 1: _travel_tenths = std::max(1, std::min(900, _travel_tenths + delta)); break;
            case 2: _rpm_tenths = std::max(1, std::min((int)std::floor(c_max_rpm() * 10.0f), _rpm_tenths + delta)); break;
            case 3: if (delta) _z_positive = !_z_positive; break;
            case 4: if (delta) _c_positive = !_c_positive; break;
        }
        setPref("Pitch um", _pitch_microns);
        setPref("TPI", _tpi);
        setPref("Travel 0.1", _travel_tenths);
        setPref("RPM 0.1", _rpm_tenths);
        setPref("Z positive", _z_positive ? 1 : 0);
        setPref("C positive", _c_positive ? 1 : 0);
        reDisplay();
    }

    void onTouchClick() override {
        if (_running) { cancel_sequence(); return; }
        if (touchIsCenter()) {
            _tpi_mode = !_tpi_mode;
            setPref("TPI mode", _tpi_mode ? 1 : 0);
        } else {
            _selection = (_selection + 1) % 5;
        }
        reDisplay();
    }

    void onGreenButtonPress() override {
        if (_running) return;
        std::string reason;
        if (!preflight(reason)) { _result = reason; reDisplay(); return; }
        const float revs = travel_mm() / pitch_mm();
        const float seconds = revs / rpm() * 60.0f;
        char msg[96];
        snprintf(msg, sizeof(msg), "C/Z proof %.3fmm x %.1fmm?\n%.2f rev, %.1fs, no encoder", pitch_mm(), travel_mm(), revs, seconds);
        _confirm_message = msg;
        _confirming = true;
        push_scene(&confirmScene, (void*)_confirm_message.c_str());
    }

    void onDialButtonPress() override {
        if (_running || state == Jog) cancel_sequence();
        else pop_scene();
    }

    void onRedButtonPress() override {
        if (_running || state == Jog) JogSequenceScene::onRedButtonPress();
        else if (!spindle_stopped()) {
            send_line("M5");
            _result = "STOP sent; wait for stopped";
            lathe_schedule_status_refresh(true);
            reDisplay();
        }
    }

    void onPoll() override {
        JogSequenceScene::onPoll();
        poll_lathe_status(_last_status_ms);
    }

    void reDisplay() override {
        background();
        drawMenuTitle("Thread Proof");
        char pitch[32], travel[32], speed[32], zdir[12], cdir[12];
        if (_tpi_mode) snprintf(pitch, sizeof(pitch), "%d TPI / %.3f mm", _tpi, pitch_mm());
        else snprintf(pitch, sizeof(pitch), "%.3f mm", pitch_mm());
        snprintf(travel, sizeof(travel), "%s%.1f mm", bool_sign(_z_positive), travel_mm());
        snprintf(speed, sizeof(speed), "%.1f / %.2f max RPM", rpm(), c_max_rpm());
        snprintf(zdir, sizeof(zdir), "Z%s", bool_sign(_z_positive));
        snprintf(cdir, sizeof(cdir), "C%s", bool_sign(_c_positive));
        draw_value_row(50, _tpi_mode ? "Pitch (TPI)" : "Pitch (mm)", pitch, _selection == 0);
        draw_value_row(76, "Z travel", travel, _selection == 1);
        draw_value_row(102, "C speed", speed, _selection == 2);
        draw_value_row(128, "Z direction", zdir, _selection == 3);
        draw_value_row(154, "C direction", cdir, _selection == 4);
        const float revs = pitch_mm() > 0 ? travel_mm() / pitch_mm() : 0;
        char preview[64];
        snprintf(preview, sizeof(preview), "%.2f rev / %.1f deg / %.1fs", revs, revs * 360.0f, rpm() > 0 ? revs / rpm() * 60.0f : 0);
        centered_text(preview, 183, CYAN, TINY);
        const char* gate = !spindle_stopped() ? "Press red: STOP SPINDLE" : _running ? "RUNNING - red/touch cancels" : _result.c_str();
        centered_text(gate, 205, !spindle_stopped() ? RED : _running ? GREEN : ORANGE, TINY);
        drawButtonLegends(_running ? "Cancel" : (!spindle_stopped() ? "STOP" : ""), "Preview/Run", "Back");
        refreshDisplay();
    }
} threadProofScene;

enum class ManualCycleKind { Face, Turn, Chamfer, Groove, Peck };

class ManualCycleScene : public JogSequenceScene {
    ManualCycleKind _kind;
    int             _selection       = 0;
    int             _distance_hundredths = 100;
    int             _feed            = 80;
    bool            _positive        = false;
    int             _angle           = 45;
    bool            _positive_slope  = true;
    int             _peck_hundredths = 100;
    bool            _confirming      = false;
    std::string     _confirm_message;

    const char* kind_name() const {
        switch (_kind) {
            case ManualCycleKind::Face: return "Face";
            case ManualCycleKind::Turn: return "Turn";
            case ManualCycleKind::Chamfer: return "Chamfer";
            case ManualCycleKind::Groove: return "Groove";
            case ManualCycleKind::Peck: return "Peck";
        }
        return "Manual";
    }

    bool needs_x() const { return _kind == ManualCycleKind::Face || _kind == ManualCycleKind::Chamfer || _kind == ManualCycleKind::Groove; }
    bool needs_z() const { return _kind == ManualCycleKind::Turn || _kind == ManualCycleKind::Chamfer || _kind == ManualCycleKind::Peck; }
    float distance_mm() const { return _distance_hundredths / 100.0f; }
    float x_command(float radial) const { return radial * (lathe_status().diameter_mode ? 2.0f : 1.0f); }

    bool preflight(std::string& reason) const override {
        if (!JogSequenceScene::preflight(reason)) return false;
        if ((needs_x() && !x_homed()) || (needs_z() && !z_homed())) { reason = "Home required axes first"; return false; }
        if (distance_mm() <= 0 || _feed <= 0) { reason = "Distance/feed invalid"; return false; }
        return true;
    }

    void build_moves() {
        _moves.clear();
        const float sign = _positive ? 1.0f : -1.0f;
        const float d = distance_mm();
        switch (_kind) {
            case ManualCycleKind::Face:
                _moves.push_back({ x_command(sign * d), 0, 0, (float)_feed });
                break;
            case ManualCycleKind::Turn:
                _moves.push_back({ 0, sign * d, 0, (float)_feed });
                break;
            case ManualCycleKind::Chamfer: {
                LatheVectorMove move = lathe_angle_vector(sign * d, (float)_angle, _positive_slope, lathe_status().diameter_mode);
                _moves.push_back({ move.x_command_mm, move.z_mm, 0, (float)_feed });
                break;
            }
            case ManualCycleKind::Groove: {
                const float x = x_command(sign * d);
                _moves.push_back({ x, 0, 0, (float)_feed });
                _moves.push_back({ -x, 0, 0, (float)_feed });
                break;
            }
            case ManualCycleKind::Peck: {
                const float q = std::max(0.01f, _peck_hundredths / 100.0f);
                float depth = q;
                int guard = 0;
                while (depth < d - 0.0001f && guard++ < 50) {
                    _moves.push_back({ 0, sign * depth, 0, (float)_feed });
                    _moves.push_back({ 0, -sign * depth, 0, (float)_feed });
                    depth += q;
                }
                _moves.push_back({ 0, sign * d, 0, (float)_feed });
                _moves.push_back({ 0, -sign * d, 0, (float)_feed });
                break;
            }
        }
    }

    int field_count() const {
        if (_kind == ManualCycleKind::Chamfer) return 5;
        if (_kind == ManualCycleKind::Peck) return 4;
        return 3;
    }

public:
    ManualCycleScene(const char* name, ManualCycleKind kind) : JogSequenceScene(name), _kind(kind) {}

    void onEntry(void* arg) override {
        if (arg && strcmp((const char*)arg, "Confirmed") == 0 && _confirming) {
            build_moves();
            start_moves();
        }
        _confirming = false;
        request_lathe_status(true);
    }

    void onEncoder(int delta) override {
        if (_running || delta == 0) return;
        if (_selection == 0) _distance_hundredths = std::max(1, std::min(9000, _distance_hundredths + delta));
        else if (_selection == 1) _feed = std::max(1, std::min(600, _feed + delta * 5));
        else if (_selection == 2) {
            if (_kind == ManualCycleKind::Chamfer) _angle = std::max(0, std::min(90, _angle + delta));
            else _positive = !_positive;
        } else if (_selection == 3) {
            if (_kind == ManualCycleKind::Chamfer) _positive_slope = !_positive_slope;
            else _peck_hundredths = std::max(1, std::min(_distance_hundredths, _peck_hundredths + delta));
        } else if (_selection == 4) {
            _positive = !_positive;
        }
        reDisplay();
    }

    void onTouchClick() override {
        if (_running) { cancel_sequence(); return; }
        _selection = (_selection + 1) % field_count();
        reDisplay();
    }

    void onGreenButtonPress() override {
        if (_running) return;
        std::string reason;
        if (!preflight(reason)) { _result = reason; reDisplay(); return; }
        char msg[96];
        snprintf(msg, sizeof(msg), "%s %s%.2fmm at %d?\nTemporary jog; red cancels", kind_name(), bool_sign(_positive), distance_mm(), _feed);
        _confirm_message = msg;
        _confirming = true;
        push_scene(&confirmScene, (void*)_confirm_message.c_str());
    }

    void onDialButtonPress() override {
        if (_running || state == Jog) cancel_sequence();
        else pop_scene();
    }

    void reDisplay() override {
        background();
        drawMenuTitle(kind_name());
        char distance[24], feed[24], option[32], extra[32];
        snprintf(distance, sizeof(distance), "%s%.2f mm", bool_sign(_positive), distance_mm());
        snprintf(feed, sizeof(feed), "%d mm/min", _feed);
        if (_kind == ManualCycleKind::Chamfer) snprintf(option, sizeof(option), "%d deg from Z", _angle);
        else snprintf(option, sizeof(option), "%s direction", _positive ? "+" : "-");
        if (_kind == ManualCycleKind::Chamfer) snprintf(extra, sizeof(extra), "%s slope", _positive_slope ? "/" : "\\");
        else snprintf(extra, sizeof(extra), "%.2f mm peck", _peck_hundredths / 100.0f);
        draw_value_row(50, "Travel", distance, _selection == 0);
        draw_value_row(76, "Feed", feed, _selection == 1);
        draw_value_row(102, _kind == ManualCycleKind::Chamfer ? "Angle" : "Direction", option, _selection == 2);
        if (field_count() >= 4) draw_value_row(128, _kind == ManualCycleKind::Chamfer ? "Slope" : "Peck", extra, _selection == 3);
        if (_kind == ManualCycleKind::Chamfer) draw_value_row(154, "Direction", _positive ? "+ path" : "- path", _selection == 4);
        const char* description = _kind == ManualCycleKind::Groove ? "Plunge then retract" : _kind == ManualCycleKind::Peck ? "Full retract each peck" : "Single relative helper move";
        centered_text(description, 182, CYAN, TINY);
        if (!_result.empty()) centered_text(_result.c_str(), 204, _running ? GREEN : ORANGE, TINY);
        drawButtonLegends(_running ? "Cancel" : "", "Preview/Run", "Back");
        refreshDisplay();
    }
};

ManualCycleScene faceScene("Face", ManualCycleKind::Face);
ManualCycleScene turnScene("Turn", ManualCycleKind::Turn);
ManualCycleScene chamferScene("Chamfer", ManualCycleKind::Chamfer);
ManualCycleScene grooveScene("Groove", ManualCycleKind::Groove);
ManualCycleScene peckScene("Peck", ManualCycleKind::Peck);

class ManualButton : public RoundButton {
public:
    ManualButton(const char* label, Scene* scene, color_t color) : RoundButton(label, scene, 29, color, GREEN, CYAN, WHITE) {}
};

ManualButton spindleButton("Spindle", &spindleScene, MAROON);
ManualButton cPositionButton("C Pos", &cPositionScene, BROWN);
ManualButton angleButton("Angle", &angleJogScene, NAVY);
ManualButton threadButton("Thread", &threadProofScene, BROWN);
ManualButton faceButton("Face", &faceScene, BLUE);
ManualButton turnButton("Turn", &turnScene, BLUE);
ManualButton chamferButton("Chamfer", &chamferScene, NAVY);
ManualButton grooveButton("Groove", &grooveScene, BROWN);
ManualButton peckButton("Peck", &peckScene, MAROON);

class LatheManualMenu : public PieMenu {
    void ensure_items() {
        if (num_items() != 0) return;
        addItem(&spindleButton);
        addItem(&cPositionButton);
        addItem(&angleButton);
        addItem(&threadButton);
        addItem(&faceButton);
        addItem(&turnButton);
        addItem(&chamferButton);
        addItem(&grooveButton);
        addItem(&peckButton);
    }

public:
    LatheManualMenu() : PieMenu("Manual Lathe", 29) {}

    void onEntry(void* arg) override {
        (void)arg;
        ensure_items();
        request_lathe_status(true);
        PieMenu::onEntry(arg);
    }

    void diagnostic_preview(int selection) {
        ensure_items();
        select(selection);
    }

    void reDisplay() override {
        if (!lathe_mode_active()) {
            background();
            centered_text("Lathe status unavailable", 110, RED, SMALL);
            centered_text("Reconnect or update controller", 136, YELLOW, TINY);
            drawButtonLegends("", "", "Back");
            refreshDisplay();
            return;
        }
        PieMenu::reDisplay();
    }
} latheManualMenu;
}  // namespace

Scene& latheManualMenuScene = latheManualMenu;
Scene& latheAngleJogScene = angleJogScene;
Scene& latheSpindleScene = spindleScene;
Scene& latheCPositionScene = cPositionScene;
Scene& latheThreadProofScene = threadProofScene;
Scene& latheFaceScene = faceScene;
Scene& latheTurnScene = turnScene;
Scene& latheChamferScene = chamferScene;
Scene& latheGrooveScene = grooveScene;
Scene& lathePeckScene = peckScene;

void diagnostic_preview_lathe_manual(int selection) {
    latheManualMenu.diagnostic_preview(selection);
}
