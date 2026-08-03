#include "Menu.h"
#include "PieMenu.h"
#include "LatheModel.h"
#include "MachineProfile.h"
#include "MachineHealthScene.h"
#include "HomingScene.h"
#include "FileParser.h"
#include "LatheUi.h"
#include "LatheUiModel.h"
#include "polar.h"
#ifdef USE_WMB_FSS
#    include "FileMenu.h"
#endif
#include "System.h"
#include <cstdio>
#include <string>
#ifdef USE_WIFI
#    include "WiFiConnection.h"
#    include "WiFiSetupScene.h"
#endif

void noop(void* arg) {}

const int buttonRadius = 30;

static const char* menu_help_text[] = { "FluidDial",
                                        "Touch icon for scene",
                                        "Touch center for help",
                                        "Flick left to go back",
                                        "Authors: @bdring,@Mitch",
                                        "Bradley,@bDuthieDev,",
                                        "@Design8Studio",
                                        "WiFi by @Figamore",
                                        NULL };

// PieMenu axisMenu("Axes", buttonRadius);

class LB : public RoundButton {
public:
    LB(const char* text, callback_t callback, color_t base_color) :
        RoundButton(text, callback, buttonRadius, base_color, GREEN, BLUE, WHITE) {}
    LB(const char* text, Scene* scene, color_t base_color) : RoundButton(text, scene, buttonRadius, base_color, GREEN, BLUE, WHITE) {}
};

constexpr int LIGHTYELLOW = 0xFFF0;
class IB : public ImageButton {
public:
    IB(const char* text, callback_t callback, const char* filename) : ImageButton(text, callback, filename, buttonRadius, WHITE) {}
    IB(const char* text, Scene* scene, const char* filename) : ImageButton(text, scene, filename, buttonRadius, WHITE) {}
};

extern Scene homingScene;
//extern Scene joggingScene;
//extern Scene joggingScene2;
extern Scene multiJogScene;
extern Scene probingScene;
extern Scene toolchangeScene;
extern Scene statusScene;
extern Scene macroMenu;

#ifdef USE_WMB_FSS
extern Scene wmbFileSelectScene;
#else
extern Scene fileSelectScene;
#endif

Scene& jogScene = multiJogScene;

extern Scene controlScene;
extern Scene aboutScene;
extern const char* git_info;

IB statusButton("Status", &statusScene, "statustp.png");
IB homingButton("Homing", &homingScene, "hometp.png");
IB jogButton("Jog", &jogScene, "jogtp.png");
IB probeButton("Probe", &probingScene, "probetp.png");
IB toolchangeButton("Tools", &toolchangeScene, "toolchangetp.png");

#ifdef USE_WMB_FSS
IB filesButton("Files", &wmbFileSelectScene, "filestp.png");
#else
IB filesButton("Files", &fileSelectScene, "filestp.png");
#endif

IB controlButton("Macros", &macroMenu, "macrostp.png");
#if defined(USE_WIFI) && !defined(MAIJKER_XZACT_LATHE)
// WiFi scene replaces About button; will reintroduce display orientation later
extern WiFiSetupScene wifiSetupScene;
IB setupButton("Settings", &wifiSetupScene, "abouttp.png");
#else
IB setupButton("About", &aboutScene, "abouttp.png");
#endif

class MenuScene : public PieMenu {
private:
    int      _animation_phase = 0;
    int      _animation_direction = 0;
    uint32_t _last_animation_ms = 0;
    bool     _files_requested = false;
    bool     _macros_requested = false;
    bool     _files_loading = false;
    bool     _macros_loading = false;
    std::string _files_error;
    std::string _macros_error;

    static const char* previewTitle(int item) {
        static const char* names[] = { "STATUS", "HOME", "JOG", "PROBE", "TOOLS", "FILES", "MACROS", "ABOUT" };
        return item >= 0 && item < 8 ? names[item] : "MAIN";
    }

    pos_t axisPosition(int display_axis) const {
        int machine_axis = profile_machine_axis(display_axis);
        return machine_axis >= 0 && machine_axis < 6 ? myAxes[machine_axis] : 0;
    }

    static const char* fileSize(int bytes) {
        static char buffer[20];
        if (bytes >= 1024 * 1024) {
            snprintf(buffer, sizeof(buffer), "%dM", bytes / (1024 * 1024));
        } else if (bytes >= 1024) {
            snprintf(buffer, sizeof(buffer), "%dK", bytes / 1024);
        } else {
            snprintf(buffer, sizeof(buffer), "%dB", bytes);
        }
        return buffer;
    }

    void requestSelectedData() {
        if (_selected == 5 && fileVector.empty() && !_files_requested && !_macros_loading) {
            _files_requested = true;
            _files_loading   = true;
            _files_error.clear();
            schedule_action(init_file_list);
        }
        if (_selected == 6 && macros.empty() && !_macros_requested && !_files_loading) {
            _macros_requested = true;
            _macros_loading   = true;
            _macros_error.clear();
            schedule_action(request_macros);
        }
    }

    void drawStatusPreview() {
        lathe_ui_badge(119, 32, 52, current_wcs(), lathe_ui_blue());
        for (int axis = 0; axis < profile_axis_count() && axis < 3; ++axis) {
            lathe_ui_dro_row(80 + axis * 31, profile_axis_char(axis), axisPosition(axis), axis == 0);
        }
        char feed[20], spindle[20];
        snprintf(feed, sizeof(feed), "%lu", static_cast<unsigned long>(myFeed));
        const LatheStatus& lathe = lathe_status();
        if (lathe.available) snprintf(spindle, sizeof(spindle), "%.0f", lathe.effective_rpm);
        else snprintf(spindle, sizeof(spindle), "%lu", static_cast<unsigned long>(mySpeed));
        lathe_ui_value_row(177, "FEED", feed, lathe_ui_text());
        lathe_ui_value_row(198, "SPINDLE", spindle, lathe_ui_text());
    }

    void drawHomePreview() {
        const char* labels[3] = { "X", "Z", "C" };
        for (int axis = 0; axis < 3; ++axis) {
            int y = 78 + axis * 39;
            text(labels[axis], 25, y, lathe_ui_text(), SMALL, middle_left);
            if (axis == 2) {
                text("N/A", 171, y, lathe_ui_muted(), TINY, middle_right);
            } else {
                bool homed = is_axis_homed(axis);
                canvas.fillCircle(106, y - 2, 4, homed ? lathe_ui_green() : lathe_ui_amber());
                text(homed ? "HOMED" : "NEEDS HOME", 171, y, homed ? lathe_ui_green() : lathe_ui_amber(), TINY, middle_right);
            }
            canvas.drawFastHLine(24, y + 15, 148, lathe_ui_panel_alt());
        }
        lathe_ui_badge(48, 194, 96, "X / Z AXES", lathe_ui_blue());
    }

    void drawJogPreview() {
        JogUiSnapshot snapshot = jog_ui_snapshot();
        lathe_ui_badge(109, 32, 62, snapshot.dynamic ? "DYNAMIC" : "PRECISE", lathe_ui_blue());
        lathe_ui_dro_row(82, 'X', axisPosition(0), snapshot.selected_mask & 1);
        lathe_ui_dro_row(119, 'Z', axisPosition(1), snapshot.selected_mask & 2);
        int axis = (snapshot.selected_mask & 2) ? 1 : 0;
        char step[24];
        snprintf(step, sizeof(step), "%s %s", lathe_ui_position(snapshot.step[axis], 3), inInches ? "IN" : "MM");
        lathe_ui_badge(43, 153, 100, step, lathe_ui_blue());
        text(snapshot.moving ? "MOVING" : "TURN DIAL IN JOG", 93, 194,
             snapshot.moving ? lathe_ui_green() : lathe_ui_muted(), TINY, middle_center);
    }

    void drawProbePreview() {
        lathe_ui_badge(99, 32, 72, myProbeSwitch ? "CONTACT" : "OPEN", myProbeSwitch ? lathe_ui_coral() : lathe_ui_green());
        const ProbeResult& result = last_probe_result();
        bool live = state == Cycle || state == Hold || state == DoorClosed;
        bool use_last = !live && result.known;
        text(live ? "LIVE" : use_last ? (result.success ? "LAST HIT" : "NO HIT") : "CURRENT", 24, 76,
             live ? lathe_ui_blue() : use_last ? (result.success ? lathe_ui_coral() : lathe_ui_amber()) : lathe_ui_muted(), TINY, middle_left);
        for (int display_axis = 0; display_axis < 2; ++display_axis) {
            int machine_axis = profile_machine_axis(display_axis);
            pos_t value = axisPosition(display_axis);
            if (use_last && machine_axis >= 0 && machine_axis < result.axis_count) {
                value = fromMm(result.axes_mm[machine_axis]);
            }
            lathe_ui_dro_row(108 + display_axis * 38, profile_axis_char(display_axis), value, display_axis == 0);
        }
        lathe_ui_value_row(193, "INPUT", myProbeSwitch ? "CLOSED" : "OPEN", myProbeSwitch ? lathe_ui_coral() : lathe_ui_green());
    }

    void drawToolsPreview() {
        const LatheStatus& status = lathe_status();
        int active = status.active_tool >= 1 && status.active_tool <= 5 ? status.active_tool : static_cast<int>(mySelectedTool);
        if (active < 1 || active > 5) active = 1;
        LatheToolType type = lathe_tool_type(active);
        lathe_ui_tool_icon(type, 64, 92, lathe_ui_blue(), 3);
        char tool[8];
        snprintf(tool, sizeof(tool), "T%d", active);
        text(tool, 108, 79, lathe_ui_blue(), SMALL, middle_left);
        lathe_ui_fit_text(lathe_tool_type_label(type), 108, 105, 64, lathe_ui_text());
        for (int station = 1; station <= 5; ++station) {
            int x = 28 + (station - 1) * 31;
            if (station == active) {
                canvas.drawRoundRect(x - 12, 137, 24, 38, 5, lathe_ui_blue());
            }
            lathe_ui_tool_icon(lathe_tool_type(station), x, 151, station == active ? lathe_ui_blue() : lathe_ui_muted());
            char number[2] = { static_cast<char>('0' + station), '\0' };
            text(number, x, 169, station == active ? lathe_ui_blue() : lathe_ui_muted(), TINY, middle_center);
        }
        lathe_ui_badge(42, 190, 104, "TURRET READY", lathe_ui_green());
    }

    void drawFilesPreview() {
        lathe_ui_badge(105, 32, 66, !_files_error.empty() ? "SD ERROR" : _files_loading ? "LOADING" : "SD READY",
                       !_files_error.empty() ? RED : _files_loading ? lathe_ui_amber() : lathe_ui_green());
        if (!_files_error.empty()) {
            lathe_ui_fit_text(_files_error.c_str(), 96, 115, 132, RED, SMALL, middle_center);
            return;
        }
        if (fileVector.empty()) {
            centered_text(_files_loading ? "READING SD" : "NO FILES", 118, lathe_ui_muted(), SMALL);
            return;
        }
        int shown = fileVector.size() < 3 ? static_cast<int>(fileVector.size()) : 3;
        for (int i = 0; i < shown; ++i) {
            int y = 80 + i * 39;
            if (i == 0) {
                canvas.drawRoundRect(18, y - 16, 154, 31, 6, lathe_ui_blue());
            }
            lathe_ui_fit_text(fileVector[i].fileName.c_str(), 27, y, 105, i == 0 ? lathe_ui_text() : lathe_ui_muted());
            const char* size = fileVector[i].isDir() ? "DIR" : fileSize(fileVector[i].fileSize);
            text(size, 165, y, i == 0 ? lathe_ui_blue() : lathe_ui_muted(), TINY, middle_right);
        }
        char count[24];
        snprintf(count, sizeof(count), "%u ITEMS", static_cast<unsigned>(fileVector.size()));
        text(count, 94, 199, lathe_ui_blue(), TINY, middle_center);
    }

    void drawMacrosPreview() {
        lathe_ui_badge(103, 32, 68, !_macros_error.empty() ? "ERROR" : _macros_loading ? "LOADING" : "SD MACROS",
                       !_macros_error.empty() ? RED : _macros_loading ? lathe_ui_amber() : lathe_ui_blue());
        if (!_macros_error.empty()) {
            lathe_ui_fit_text(_macros_error.c_str(), 96, 115, 132, RED, SMALL, middle_center);
            return;
        }
        if (macros.empty()) {
            centered_text(_macros_loading ? "READING" : "NO MACROS", 112, lathe_ui_muted(), SMALL);
            text("ADD FILES TO SD", 94, 149, lathe_ui_muted(), TINY, middle_center);
            return;
        }
        int shown = macros.size() < 4 ? static_cast<int>(macros.size()) : 4;
        for (int i = 0; i < shown; ++i) {
            int y = 75 + i * 32;
            canvas.drawRoundRect(22, y - 13, 145, 27, 5, i == 0 ? lathe_ui_blue() : lathe_ui_panel_alt());
            lathe_ui_fit_text(macros[i]->name.c_str(), 32, y, 124, i == 0 ? lathe_ui_text() : lathe_ui_muted());
        }
    }

    void drawAboutPreview() {
        text("XZACT M5DIAL", 94, 76, lathe_ui_text(), SMALL, middle_center);
        lathe_ui_fit_text(git_info ? git_info : "unknown", 94, 101, 145, lathe_ui_blue(), TINY, middle_center);
        lathe_ui_value_row(134, "UART 1M", fnc_is_connected() ? "ONLINE" : "N/C", fnc_is_connected() ? lathe_ui_green() : RED);
#ifdef USE_WIFI
        lathe_ui_value_row(158, "WIFI", wifi_signal_bars() ? "" : "OFFLINE", wifi_signal_bars() ? lathe_ui_green() : lathe_ui_muted());
        if (wifi_signal_bars()) drawWiFiSignalBars(151, 164);
#else
        lathe_ui_value_row(158, "WIFI", "N/A", lathe_ui_muted());
#endif
        lathe_ui_value_row(182, "FLUIDNC", my_state_string, state == Idle ? lathe_ui_green() : lathe_ui_amber());
        lathe_ui_badge(54, 199, 80, "SETTINGS", lathe_ui_blue());
    }

    void drawSelectedPreview() {
        lathe_ui_main_surface(previewTitle(_selected));
        switch (_selected) {
            case 0: drawStatusPreview(); break;
            case 1: drawHomePreview(); break;
            case 2: drawJogPreview(); break;
            case 3: drawProbePreview(); break;
            case 4: drawToolsPreview(); break;
            case 5: drawFilesPreview(); break;
            case 6: drawMacrosPreview(); break;
            case 7: drawAboutPreview(); break;
        }
        if (selectedItem()->disabled()) {
            lathe_ui_badge(37, 202, 112, "LINK REQUIRED", lathe_ui_amber());
        }
        lathe_ui_orbital_rail(_selected, _animation_phase, _animation_direction);
        refreshDisplay();
    }

    int touchedRailItem(int x, int y) const {
        static const int offsets[7] = { -3, -2, -1, 1, 2, 3, 4 };
        static const int angles[7]  = { 105, 70, 35, 0, -35, -70, -105 };
        for (int slot = 0; slot < 7; ++slot) {
            int dx, dy;
            r_degrees_to_xy(105, angles[slot], &dx, &dy);
            int sx = 120 + dx;
            int sy = 120 - dy;
            int tx = x - sx;
            int ty = y - sy;
            if (tx * tx + ty * ty <= 16 * 16) {
                return (_selected + offsets[slot] + 8) % 8;
            }
        }
        return -1;
    }

public:
    MenuScene() : PieMenu("Main", buttonRadius, menu_help_text) {}
    void disableMachineActions() {
        // Link loss disables machine commands, not navigation. Operators must
        // still be able to inspect status, files, and settings/recovery.
        statusButton.enable();
        homingButton.disable();
        jogButton.disable();
        probeButton.disable();
        toolchangeButton.disable();
        filesButton.enable();
        controlButton.disable();
        setupButton.enable();
    }
    void enableIcons() {
        statusButton.enable();
        homingButton.enable();
        jogButton.enable();
        probeButton.enable();
        toolchangeButton.enable();
        filesButton.enable();
        controlButton.enable();
        setupButton.enable();
    }
    void syncIconAvailability() {
        if (state == Disconnected || !operator_navigation_available()) {
            disableMachineActions();
            return;
        }
        enableIcons();
        if (machine_profile_is_lathe()) {
            if (!operator_basic_motion_actions_available()) {
                homingButton.disable();
                jogButton.disable();
                probeButton.disable();
                toolchangeButton.disable();
                controlButton.disable();
            }
        }
    }
    void reDisplay() override {
        syncIconAvailability();
        if (lathe_ui_enabled()) {
            requestSelectedData();
            drawSelectedPreview();
            return;
        }
        PieMenu::reDisplay();
    }
    void onEntry(void* arg) {
        PieMenu::onEntry(arg);
        syncIconAvailability();
        if (lathe_ui_enabled()) {
            request_lathe_status();
            requestSelectedData();
        }
    }
    void onEncoder(int delta) override {
        if (!lathe_ui_enabled()) {
            PieMenu::onEncoder(delta);
            return;
        }
        _animation_direction = delta < 0 ? -1 : 1;
        _animation_phase = 3;
        _last_animation_ms = millis();
        Menu::rotate(delta);
        requestSelectedData();
    }
    void onTouchClick() override {
        if (lathe_ui_enabled()) {
            int item = touchedRailItem(touchX, touchY);
            if (item >= 0) {
                int clockwise = (item - _selected + 8) % 8;
                _animation_direction = clockwise <= 4 ? 1 : -1;
                _animation_phase = 3;
                _last_animation_ms = millis();
                select(item);
                requestSelectedData();
                ackBeep();
                return;
            }
            if (touchIsCenter()) {
                invoke();
            }
            return;
        }
        PieMenu::onTouchClick();
    }
    void onTouchHold() override {
        if (lathe_ui_enabled() && touchIsCenter()) {
            push_scene(&machineHealthScene);
            return;
        }
        PieMenu::onTouchHold();
    }
    void onLeftFlick() override { lathe_ui_enabled() ? onEncoder(-1) : PieMenu::onLeftFlick(); }
    void onRightFlick() override { lathe_ui_enabled() ? onEncoder(1) : PieMenu::onRightFlick(); }
    void onDROChange() override { request_redisplay(); }
    void onLimitsChange() override { request_redisplay(); }
    void onFilesList() override {
        _files_loading = false;
        _macros_loading = false;
        request_redisplay();
    }
    void onError(const char* error) override {
        if (_files_loading) {
            _files_error = error ? error : "LOAD ERROR";
            _files_loading = false;
            request_redisplay();
        }
        if (_macros_loading) {
            _macros_error = error ? error : "LOAD ERROR";
            _macros_loading = false;
            request_redisplay();
        }
    }
    void onPoll() override {
        if (!lathe_ui_enabled() || _animation_phase <= 0) {
            return;
        }
        uint32_t now = millis();
        if (now - _last_animation_ms >= 30) {
            _last_animation_ms = now;
            --_animation_phase;
            request_redisplay();
        }
    }
    void onStateChange(state_t old_state) override {
        if (state != Disconnected && operator_navigation_available()) {
            syncIconAvailability();
            if (old_state == Disconnected) {
#ifdef AUTO_JOG_SCENE
                if (state == Idle) {
                    push_scene(&jogScene);
                    return;
                }
#endif
#ifdef AUTO_HOMING_SCENE
                if (state == Alarm && lastAlarm == 14) {  // Unknown or Unhomed
                    push_scene(&homingScene, (void*)"auto");
                    return;
                }
#endif
            }
        } else {
            disableMachineActions();
        }
        reDisplay();
    }
} menuScene;

Scene* initMenus() {
    menuScene.addItem(&statusButton);
    menuScene.addItem(&homingButton);
    menuScene.addItem(&jogButton);
    menuScene.addItem(&probeButton);
    menuScene.addItem(&toolchangeButton);
    menuScene.addItem(&filesButton);
    menuScene.addItem(&controlButton);
    menuScene.addItem(&setupButton);

    return &menuScene;
}

void diagnostic_preview_main_menu(int selection) {
    menuScene.select(selection);
}
