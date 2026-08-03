// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <string>
#include "Scene.h"
#include "ConfirmScene.h"
#include "LatheModel.h"
#include "LatheUi.h"
#include "LatheUiModel.h"
#include "MachineProfile.h"
#include "e4math.h"

class ToolChangeScene : public Scene {
private:
    enum class LathePage {
        Tools,
        Setup,
        TouchOff,
    };

    enum class LathePendingAction {
        None,
        ToolChange,
        TouchOff,
    };

    int       _new_tool = 0;
    int       _lathe_tool = 1;
    LathePage _lathe_page = LathePage::Tools;
    LathePendingAction _pending_action = LathePendingAction::None;
    int       _setup_selection = 0;
    int       _touch_selection = 0;
    bool      _diagnostic_defaults = false;
    bool      _touch_diameter_mode = true;
    std::string _confirm_message;
    int       _confirm_tool = 0;
    e4_t      _confirm_machine_x = 0;
    e4_t      _confirm_machine_z = 0;
    e4_t      _confirm_reference_x = 0;
    e4_t      _confirm_reference_z = 0;
    bool      _confirm_diameter_mode = true;

    e4_t _gx[5] = { 0 };
    e4_t _gz[5] = { 0 };
    e4_t _wx[5] = { 0 };
    e4_t _wz[5] = { 0 };
    e4_t _nr[5] = { 0 };
    int  _orientation[5] = { 0 };
    e4_t _reference_x[5] = { 0 };
    e4_t _reference_z[5] = { 0 };

    int tool_index() const { return _lathe_tool - 1; }

    LatheToolType displayed_tool_type(int station) const {
        static const LatheToolType defaults[5] = { LatheToolType::RightTurn, LatheToolType::LeftTurn,
                                                   LatheToolType::DrillQuarterInch, LatheToolType::BoringBar,
                                                   LatheToolType::Probe };
        return _diagnostic_defaults && station >= 1 && station <= 5 ? defaults[station - 1] : lathe_tool_type(station);
    }

    const char* setup_value(int field, int idx) const {
        switch (field) {
            case 0: return e4_to_cstr(_gx[idx], 3);
            case 1: return e4_to_cstr(_gz[idx], 3);
            case 2: return e4_to_cstr(_wx[idx], 3);
            case 3: return e4_to_cstr(_wz[idx], 3);
            case 4: return e4_to_cstr(_nr[idx], 3);
            case 5: return intToCStr(_orientation[idx]);
            case 6: return lathe_tool_type_label(lathe_tool_type(_lathe_tool));
        }
        return "";
    }

    e4_t float_mm_to_e4(float value) {
        return (e4_t)(value * 10000.0f);
    }

    void ensure_prefs() {
        initPrefs();
    }

    void load_lathe_prefs() {
        ensure_prefs();
        static_assert(sizeof(e4_t) == sizeof(int));
        for (int tool = 0; tool < 5; ++tool) {
            getPref("LatheGX", tool, reinterpret_cast<int*>(&_gx[tool]));
            getPref("LatheGZ", tool, reinterpret_cast<int*>(&_gz[tool]));
            getPref("LatheWX", tool, reinterpret_cast<int*>(&_wx[tool]));
            getPref("LatheWZ", tool, reinterpret_cast<int*>(&_wz[tool]));
            getPref("LatheNR", tool, reinterpret_cast<int*>(&_nr[tool]));
            getPref("LatheO", tool, &_orientation[tool]);
            getPref("LatheRX", tool, reinterpret_cast<int*>(&_reference_x[tool]));
            getPref("LatheRZ", tool, reinterpret_cast<int*>(&_reference_z[tool]));
        }
    }

    void save_selected_tool_prefs() {
        ensure_prefs();
        int idx = tool_index();
        setPref("LatheGX", idx, (int)_gx[idx]);
        setPref("LatheGZ", idx, (int)_gz[idx]);
        setPref("LatheWX", idx, (int)_wx[idx]);
        setPref("LatheWZ", idx, (int)_wz[idx]);
        setPref("LatheNR", idx, (int)_nr[idx]);
        setPref("LatheO", idx, _orientation[idx]);
        setPref("LatheRX", idx, (int)_reference_x[idx]);
        setPref("LatheRZ", idx, (int)_reference_z[idx]);
    }

    void seed_active_tool_from_status() {
        const LatheStatus& status = lathe_status();
        if (!status.available || status.active_tool < 1 || status.active_tool > 5) {
            return;
        }
        _lathe_tool = status.active_tool;
        int idx = tool_index();
        _gx[idx] = float_mm_to_e4(status.tool_x_offset_mm);
        _gz[idx] = float_mm_to_e4(status.tool_z_offset_mm);
        _nr[idx] = float_mm_to_e4(status.tool_nose_radius_mm);
    }

    void selected_machine_positions_mm(e4_t& x, e4_t& z) {
        x = toMm(myAxes[profile_machine_axis(0)]);
        z = toMm(myAxes[profile_machine_axis(1)]);
    }

    int command_color(LatheCommandSeverity severity) {
        switch (severity) {
            case LatheCommandSeverity::Info:
                return YELLOW;
            case LatheCommandSeverity::Success:
                return GREEN;
            case LatheCommandSeverity::Warning:
                return ORANGE;
            case LatheCommandSeverity::Error:
                return RED;
            case LatheCommandSeverity::None:
                return WHITE;
        }
        return WHITE;
    }

    void draw_last_command_result(int y) {
        const LatheCommandResult& result = lathe_last_command_result();
        if (!result.known) {
            return;
        }
        if (!result.pending && !result.recoverable && (uint32_t)(millis() - result.updated_ms) > 6000) {
            return;
        }
        centered_text(lathe_command_status_text(), y, command_color(lathe_command_severity()), TINY);
    }

    void draw_lathe_tool_list() {
        if (lathe_ui_enabled()) {
            lathe_ui_detail_surface("TOOLS");
            const LatheStatus& status = lathe_status();
            for (int tool = 1; tool <= 5; ++tool) {
                int y = 64 + (tool - 1) * 29;
                bool selected = _lathe_tool == tool;
                if (selected) canvas.drawRoundRect(24, y - 13, 192, 26, 7, lathe_ui_blue());
                lathe_ui_tool_icon(displayed_tool_type(tool), 42, y, selected ? lathe_ui_blue() : lathe_ui_muted());
                char number[5];
                snprintf(number, sizeof(number), "T%d", tool);
                text(number, 63, y, selected ? lathe_ui_blue() : lathe_ui_text(), TINY, middle_left);
                text(lathe_tool_type_label(displayed_tool_type(tool)), 91, y, lathe_ui_text(), TINY, middle_left);
                if (status.active_tool == tool) text("ACTIVE", 205, y, lathe_ui_green(), TINY, middle_right);
            }
            draw_last_command_result(204);
            const char* red = (state == Idle && lathe_command_recoverable()) ? "Clear" : (state == Idle ? "Setup" : (state == Cycle || state == Hold || state == DoorClosed || state == Alarm ? "Reset" : ""));
            const char* green = state == Idle ? (lathe_command_pending() ? "Wait" : (lathe_command_recoverable() ? "" : "Change")) : (state == Cycle ? "Hold" : (state == Hold || state == DoorClosed ? "Resume" : ""));
            drawButtonLegends(red, green, "Back");
            refreshDisplay();
            return;
        }
        background();
        drawMenuTitle("Lathe Tools");
        drawStatusSmall(25);

        int x = 35;
        int width = display_short_side() - (x * 2);
        Stripe row(x, 58, width, 24, TINY);
        const LatheStatus& status = lathe_status();
        for (int tool = 1; tool <= 5; ++tool) {
            char label[24];
            snprintf(label, sizeof(label), "T%d%s", tool, tool == 5 ? " Probe" : "");
            char detail[20];
            if (status.active_tool == tool) {
                snprintf(detail, sizeof(detail), "Active");
            } else {
                snprintf(detail, sizeof(detail), " ");
            }
            row.draw(label, detail, _lathe_tool == tool, tool == 5 ? ORANGE : WHITE);
        }

        draw_last_command_result(190);
        const char* red = (state == Idle && lathe_command_recoverable()) ? "Clear" : (state == Idle ? "Setup" : (state == Cycle || state == Hold || state == DoorClosed || state == Alarm ? "Reset" : ""));
        const char* green = state == Idle ? (lathe_command_pending() ? "Wait" : (lathe_command_recoverable() ? "" : "Change")) : (state == Cycle ? "Hold" : (state == Hold || state == DoorClosed ? "Resume" : ""));
        drawButtonLegends(red, green, "Back");
        refreshDisplay();
    }

    void draw_lathe_setup() {
        if (lathe_ui_enabled()) {
            lathe_ui_detail_surface("TOOL SETUP");
            int idx = tool_index();
            char title[10];
            snprintf(title, sizeof(title), "T%d", _lathe_tool);
            lathe_ui_tool_icon(lathe_tool_type(_lathe_tool), 35, 52, lathe_ui_blue());
            text(title, 53, 52, lathe_ui_blue(), SMALL, middle_left);
            text(lathe_tool_type_label(lathe_tool_type(_lathe_tool)), 83, 52, lathe_ui_text(), TINY, middle_left);
            const char* labels[7] = { "GX", "GZ", "WX", "WZ", "NOSE R", "ORIENT", "TYPE" };
            for (int i = 0; i < 7; ++i) {
                int y = 76 + i * 20;
                if (_setup_selection == i) canvas.drawRoundRect(27, y - 9, 186, 18, 5, lathe_ui_blue());
                text(labels[i], 34, y, _setup_selection == i ? lathe_ui_blue() : lathe_ui_muted(), TINY, middle_left);
                lathe_ui_fit_text(setup_value(i, idx), 205, y, 116, lathe_ui_text(), TINY, middle_right);
            }
            draw_last_command_result(215);
            drawButtonLegends((state == Idle && lathe_command_recoverable()) ? "Clear" : "Back", lathe_command_blocks_actions() ? (lathe_command_pending() ? "Wait" : "") : "Save", "TouchOff");
            refreshDisplay();
            return;
        }
        background();
        drawMenuTitle("Tool Setup");
        drawStatusTiny(24);

        char title[32];
        snprintf(title, sizeof(title), "T%d%s", _lathe_tool, _lathe_tool == 5 ? " Probe" : "");
        centered_text(title, 48, _lathe_tool == 5 ? ORANGE : WHITE, SMALL);

        int x = 30;
        int width = display_short_side() - (x * 2);
        Stripe row(x, 68, width, 22, TINY);
        int idx = tool_index();
        row.draw("GX", e4_to_cstr(_gx[idx], 3), _setup_selection == 0);
        row.draw("GZ", e4_to_cstr(_gz[idx], 3), _setup_selection == 1);
        row.draw("WX", e4_to_cstr(_wx[idx], 3), _setup_selection == 2);
        row.draw("WZ", e4_to_cstr(_wz[idx], 3), _setup_selection == 3);
        row.draw("NR", e4_to_cstr(_nr[idx], 3), _setup_selection == 4);
        row.draw("O", intToCStr(_orientation[idx]), _setup_selection == 5);

        draw_last_command_result(203);
        drawButtonLegends((state == Idle && lathe_command_recoverable()) ? "Clear" : "Back", lathe_command_blocks_actions() ? (lathe_command_pending() ? "Wait" : "") : "Save", "TouchOff");
        refreshDisplay();
    }

    void draw_lathe_touch_off() {
        if (lathe_ui_enabled()) {
            lathe_ui_detail_surface("TOUCH OFF");
            int idx = tool_index();
            e4_t machine_x;
            e4_t machine_z;
            selected_machine_positions_mm(machine_x, machine_z);
            lathe_ui_dro_row(72, 'X', machine_x, _touch_selection == 0);
            lathe_ui_dro_row(105, 'Z', machine_z, _touch_selection == 1);
            lathe_ui_value_row(143, "REF X", e4_to_cstr(_reference_x[idx], 3), _touch_selection == 0 ? lathe_ui_blue() : lathe_ui_text());
            lathe_ui_value_row(166, "REF Z", e4_to_cstr(_reference_z[idx], 3), _touch_selection == 1 ? lathe_ui_blue() : lathe_ui_text());
            lathe_ui_value_row(189, "X MODE", _touch_diameter_mode ? "DIAMETER" : "RADIUS", _touch_selection == 2 ? lathe_ui_blue() : lathe_ui_text());
            draw_last_command_result(208);
            drawButtonLegends((state == Idle && lathe_command_recoverable()) ? "Clear" : "Setup", lathe_command_blocks_actions() ? (lathe_command_pending() ? "Wait" : "") : "Apply", "Tools");
            refreshDisplay();
            return;
        }
        background();
        drawMenuTitle("Touch Off");
        drawStatusTiny(24);

        char title[32];
        snprintf(title, sizeof(title), "T%d%s", _lathe_tool, _lathe_tool == 5 ? " Probe" : "");
        centered_text(title, 48, _lathe_tool == 5 ? ORANGE : WHITE, SMALL);

        e4_t machine_x;
        e4_t machine_z;
        selected_machine_positions_mm(machine_x, machine_z);

        char current[48];
        std::string machine_x_text = e4_to_cstr(machine_x, 3);
        std::string machine_z_text = e4_to_cstr(machine_z, 3);
        snprintf(current, sizeof(current), "MX %s  MZ %s", machine_x_text.c_str(), machine_z_text.c_str());
        centered_text(current, 72, DARKGREY, TINY);

        int x = 30;
        int width = display_short_side() - (x * 2);
        Stripe row(x, 96, width, 28, TINY);
        int idx = tool_index();
        row.draw("Ref X", e4_to_cstr(_reference_x[idx], 3), _touch_selection == 0);
        row.draw("Ref Z", e4_to_cstr(_reference_z[idx], 3), _touch_selection == 1);
        row.draw("X Mode", _touch_diameter_mode ? "Diameter" : "Radius", _touch_selection == 2);

        draw_last_command_result(190);
        drawButtonLegends((state == Idle && lathe_command_recoverable()) ? "Clear" : "Setup", lathe_command_blocks_actions() ? (lathe_command_pending() ? "Wait" : "") : "Apply", "Tools");
        refreshDisplay();
    }

    void draw_lathe() {
        switch (_lathe_page) {
            case LathePage::Tools:
                draw_lathe_tool_list();
                break;
            case LathePage::Setup:
                draw_lathe_setup();
                break;
            case LathePage::TouchOff:
                draw_lathe_touch_off();
                break;
        }
    }

    void confirm_lathe_tool_change() {
        _pending_action = LathePendingAction::ToolChange;
        _confirm_tool = _lathe_tool;
        const LatheStatus& status = lathe_status();
        if (status.active_tool > 0) {
            _confirm_message = "T" + std::to_string(status.active_tool) + " -> T" + std::to_string(_confirm_tool);
        } else {
            _confirm_message = "T? -> T" + std::to_string(_confirm_tool);
        }
        if (_confirm_tool == 5) {
            _confirm_message += " Probe";
        }
        _confirm_message += "?\nSends T" + std::to_string(_confirm_tool) + " + M6";
        push_scene(&confirmScene, (void*)_confirm_message.c_str());
    }

    void confirm_lathe_touch_off() {
        int idx = tool_index();
        selected_machine_positions_mm(_confirm_machine_x, _confirm_machine_z);
        _confirm_tool = _lathe_tool;
        _confirm_reference_x = _reference_x[idx];
        _confirm_reference_z = _reference_z[idx];
        _confirm_diameter_mode = _touch_diameter_mode;
        _pending_action = LathePendingAction::TouchOff;

        std::string mx = e4_to_cstr(_confirm_machine_x, 2);
        std::string mz = e4_to_cstr(_confirm_machine_z, 2);
        std::string rx = e4_to_cstr(_confirm_reference_x, 2);
        std::string rz = e4_to_cstr(_confirm_reference_z, 2);
        _confirm_message = "T" + std::to_string(_confirm_tool);
        _confirm_message += _confirm_diameter_mode ? " Dia " : " Rad ";
        _confirm_message += "MX" + mx + " MZ" + mz;
        _confirm_message += "\nRX" + rx + " RZ" + rz;
        push_scene(&confirmScene, (void*)_confirm_message.c_str());
    }

    void adjust_setup_value(int delta) {
        int idx = tool_index();
        switch (_setup_selection) {
            case 0:
                _gx[idx] += delta * 100;
                break;
            case 1:
                _gz[idx] += delta * 100;
                break;
            case 2:
                _wx[idx] += delta * 100;
                break;
            case 3:
                _wz[idx] += delta * 100;
                break;
            case 4:
                _nr[idx] += delta * 100;
                if (_nr[idx] < 0) {
                    _nr[idx] = 0;
                }
                break;
            case 5:
                rotateNumberLoop(_orientation[idx], delta, 0, 9);
                break;
            case 6: {
                int value = static_cast<int>(lathe_tool_type(_lathe_tool));
                rotateNumberLoop(value, delta, static_cast<int>(LatheToolType::Unset), static_cast<int>(LatheToolType::Parting));
                set_lathe_tool_type(_lathe_tool, static_cast<LatheToolType>(value));
                break;
            }
        }
        save_selected_tool_prefs();
    }

    void adjust_touch_value(int delta) {
        int idx = tool_index();
        switch (_touch_selection) {
            case 0:
                _reference_x[idx] += delta * 100;
                break;
            case 1:
                _reference_z[idx] += delta * 100;
                break;
            case 2:
                if (delta != 0) {
                    _touch_diameter_mode = !_touch_diameter_mode;
                }
                break;
        }
        save_selected_tool_prefs();
    }

    bool lathe_button_handled_as_state_control(bool green) {
        if (green) {
            if (state == Hold || state == DoorClosed) {
                fnc_realtime(CycleStart);
                return true;
            }
            if (state == Cycle) {
                fnc_realtime(FeedHold);
                return true;
            }
            return false;
        }

        if (state == Hold || state == DoorClosed || state == Cycle || state == Alarm) {
            fnc_realtime(Reset);
            return true;
        }
        return false;
    }

    void draw_generic() {
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        bool M6Q_button_enabled = false;

        int y = 80;

        const char* grnLabel = "";
        const char* redLabel = "";
        static char buffer[20];

        sprintf(buffer, "Current T Value: %d", mySelectedTool);
        centered_text(buffer, y, LIGHTGREY, TINY);

        switch (state) {
            case Idle:
                grnLabel = "M6";
                redLabel = "T";

                M6Q_button_enabled = true;

                sprintf(buffer, "T%d", _new_tool);
                redLabel = buffer;
                break;

            case Hold:
                grnLabel = "Resume";
                break;

            case Cycle:
                redLabel = "Reset";
                grnLabel = "Hold";
                break;

            default:
                break;
        }
        int x     = 50;
        int width = display_short_side() - (x * 2);
        if (M6Q_button_enabled) {
            Stripe button(x, 110, width, 50, SMALL);
            button.draw("M61Q", intToCStr(_new_tool), M6Q_button_enabled);
        }

        drawButtonLegends(redLabel, grnLabel, "Back");
        drawError();
        refreshDisplay();
    }

public:
    ToolChangeScene() : Scene("Tools", 4) {}

    void diagnosticPreview(int page) {
        _diagnostic_defaults = false;
        _lathe_page = page == 1 ? LathePage::Setup : page == 2 ? LathePage::TouchOff : LathePage::Tools;
        load_lathe_prefs();
        seed_active_tool_from_status();
        reDisplay();
    }

    void diagnosticDefaults() {
        _diagnostic_defaults = true;
        _lathe_page = LathePage::Tools;
        _lathe_tool = 1;
        reDisplay();
    }

    void onDialButtonPress() override {
        if (!lathe_mode_active()) {
            pop_scene();
            return;
        }
        if (_lathe_page == LathePage::Tools) {
            pop_scene();
        } else if (_lathe_page == LathePage::Setup) {
            _lathe_page = LathePage::TouchOff;
            reDisplay();
        } else {
            _lathe_page = LathePage::Tools;
            reDisplay();
        }
    }

    void onRedButtonPress() override {
        if (!lathe_mode_active()) {
            switch (state) {
                case Idle:
                    send_linef("T%d", _new_tool);
                    break;
                case Hold:
                case Cycle:
                    fnc_realtime(Reset);
                    break;
                default:
                    break;
            }
            return;
        }

        if (lathe_button_handled_as_state_control(false)) {
            return;
        }
        if (state != Idle) {
            return;
        }
        if (lathe_command_recoverable()) {
            lathe_clear_recoverable_command();
            reDisplay();
            return;
        }

        if (_lathe_page == LathePage::Tools) {
            _lathe_page = LathePage::Setup;
        } else if (_lathe_page == LathePage::Setup) {
            _lathe_page = LathePage::Tools;
        } else {
            _lathe_page = LathePage::Setup;
        }
        reDisplay();
    }

    void onGreenButtonPress() override {
        if (!lathe_mode_active()) {
            switch (state) {
                case Idle:
                    send_line("M6");
                    break;
                case Hold:
                    fnc_realtime(CycleStart);
                    break;
                case Cycle:
                    fnc_realtime(FeedHold);
                    break;
                default:
                    break;
            }
            return;
        }

        if (lathe_button_handled_as_state_control(true)) {
            return;
        }
        if (state != Idle) {
            return;
        }
        if (lathe_command_blocks_actions()) {
            return;
        }

        int idx = tool_index();
        if (_lathe_page == LathePage::Tools) {
            confirm_lathe_tool_change();
        } else if (_lathe_page == LathePage::Setup) {
            lathe_save_tool(_lathe_tool, _gx[idx], _gz[idx], _wx[idx], _wz[idx], _nr[idx], _orientation[idx]);
        } else {
            confirm_lathe_touch_off();
        }
    }

    void onStateChange(state_t old_state) override {
        if (lathe_mode_active() && state == Alarm && lathe_command_pending()) {
            lathe_fail_pending_command("Alarm during command");
        }
        reDisplay();
    }

    void onTouchClick() override {
        if (!lathe_mode_active()) {
            if (state == Idle) {
                send_linef("M61Q%d", _new_tool);
            }
            return;
        }
        if (state != Idle) {
            return;
        }

        if (_lathe_page == LathePage::Tools) {
            _lathe_page = LathePage::Setup;
        } else if (_lathe_page == LathePage::Setup) {
            rotateNumberLoop(_setup_selection, 1, 0, lathe_ui_enabled() ? 6 : 5);
        } else {
            rotateNumberLoop(_touch_selection, 1, 0, 2);
        }
        reDisplay();
        ackBeep();
    }

    void onTouchHold() override {
        if (lathe_mode_active() && state == Idle && _lathe_page == LathePage::Tools && !lathe_command_blocks_actions()) {
            lathe_select_tool_logical(_lathe_tool);
        }
    }

    void onPoll() override {
        if (lathe_mode_active()) {
            lathe_poll_command();
        }
    }

    void onEncoder(int delta) override {
        if (abs(delta) == 0) {
            return;
        }
        if (!lathe_mode_active()) {
            rotateNumberLoop(_new_tool, delta, 0, 255);
            reDisplay();
            return;
        }
        if (state != Idle) {
            return;
        }

        if (_lathe_page == LathePage::Tools) {
            rotateNumberLoop(_lathe_tool, delta, 1, 5);
        } else if (_lathe_page == LathePage::Setup) {
            adjust_setup_value(delta);
        } else {
            adjust_touch_value(delta);
        }
        reDisplay();
    }

    void onEntry(void* arg) override {
        _diagnostic_defaults = false;
        if (!lathe_mode_active()) {
            return;
        }

        load_lathe_prefs();

        if (arg && strcmp((const char*)arg, "Confirmed") == 0) {
            switch (_pending_action) {
                case LathePendingAction::ToolChange:
                    lathe_change_tool(_confirm_tool);
                    break;
                case LathePendingAction::TouchOff:
                    lathe_touch_off_tool(_confirm_tool,
                                         _confirm_machine_x,
                                         _confirm_machine_z,
                                         _confirm_reference_x,
                                         _confirm_reference_z,
                                         _confirm_diameter_mode);
                    break;
                case LathePendingAction::None:
                    break;
            }
            _pending_action = LathePendingAction::None;
            return;
        }

        seed_active_tool_from_status();
        _touch_diameter_mode = lathe_status().diameter_mode;
        request_lathe_status(true);
    }

    void reDisplay() override {
        if (lathe_mode_active()) {
            draw_lathe();
        } else {
            draw_generic();
        }
    }
};
ToolChangeScene toolchangeScene;

void diagnostic_preview_tools(int selection) {
    toolchangeScene.diagnosticPreview(selection);
}

void diagnostic_preview_tool_defaults(int) {
    toolchangeScene.diagnosticDefaults();
}
