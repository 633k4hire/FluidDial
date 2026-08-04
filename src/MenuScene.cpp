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
#include <cctype>
#include <cstdio>
#include <cstring>
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
    bool     _diagnostic_visual_forced = false;
    bool     _diagnostic_blocked = false;
    state_t  _diagnostic_shown_state = Idle;
    bool     _diagnostic_snapshot_valid = false;
    int      _diagnostic_saved_selection = 0;
    int      _diagnostic_saved_animation_phase = 0;
    int      _diagnostic_saved_animation_direction = 0;
    uint32_t _diagnostic_saved_animation_ms = 0;
    bool     _diagnostic_saved_visual_forced = false;
    bool     _diagnostic_saved_blocked = false;
    state_t  _diagnostic_saved_shown_state = Idle;
    bool     _diagnostic_cursor_valid = false;
    int      _diagnostic_cursor = 0;

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

    static bool isHex(char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
    }

    static void firmwareSummary(char* output, size_t output_size) {
        if (!output || output_size == 0) return;
        const char* raw = git_info ? git_info : "unknown";
        const char* version_start = raw;
        if (*version_start == 'v' || *version_start == 'V') ++version_start;

        char version[18] = "unknown";
        size_t version_length = 0;
        while ((version_start[version_length] == '.' ||
                (version_start[version_length] >= '0' && version_start[version_length] <= '9')) &&
               version_length < sizeof(version) - 1) {
            version[version_length] = version_start[version_length];
            ++version_length;
        }
        if (version_length) version[version_length] = '\0';

        size_t raw_length = strlen(raw);
        if (raw_length > 6 && strcmp(raw + raw_length - 6, "-dirty") == 0) raw_length -= 6;
        const char* hash = nullptr;
        for (size_t index = raw_length; index > 0; --index) {
            if (raw[index - 1] == '-') {
                const char* candidate = raw + index;
                size_t candidate_length = raw_length - index;
                bool valid = candidate_length >= 7 && candidate_length <= 12;
                for (size_t digit = 0; valid && digit < candidate_length; ++digit) valid = isHex(candidate[digit]);
                if (valid) hash = candidate;
                break;
            }
        }

        if (hash) {
            int hash_length = static_cast<int>(raw_length - static_cast<size_t>(hash - raw));
            if (hash_length > 8) hash_length = 8;
            snprintf(output, output_size, "FW %s / %.*s", version, hash_length, hash);
        }
        else snprintf(output, output_size, "FW %s", version);
    }

    bool previewBlocked() {
        return _diagnostic_visual_forced ? _diagnostic_blocked : selectedItem()->disabled();
    }

    state_t previewState() const {
        return _diagnostic_visual_forced ? _diagnostic_shown_state : state;
    }

    static void paintPreviewFooter(const char* label, int color) {
        if (!label || !*label) return;
        if (strcmp(label, "LINK REQUIRED") == 0) {
            // The full uppercase phrase is wider than the lower-left chord in
            // FreeSansBold9. Stack it so it remains literal and legible while
            // staying clear of the rail glyph centered near (158, 198).
            constexpr int link_x = 40;
            constexpr int link_y = 174;
            constexpr int link_width = 100;
            constexpr int link_height = 33;
            canvas.fillRoundRect(link_x, link_y, link_width, link_height, 10, lathe_ui_bg());
            canvas.drawRoundRect(link_x, link_y, link_width, link_height, 10, color);
            canvas.drawRoundRect(link_x + 1, link_y + 1, link_width - 2, link_height - 2, 9, color);
            lathe_ui_fit_text("LINK", link_x + link_width / 2, 183, link_width - 8, color, TINY, middle_center);
            lathe_ui_fit_text("REQUIRED", link_x + link_width / 2, 198, link_width - 4, color, TINY, middle_center);
            return;
        }
        // End before the bottom rail glyph while using the wider lower-left
        // chord for the ordinary measured footer labels.
        constexpr int x = 28;
        constexpr int y = 181;
        constexpr int width = 112;
        constexpr int height = 18;
        canvas.fillRoundRect(x, y, width, height, height / 2, lathe_ui_bg());
        canvas.drawRoundRect(x, y, width, height, height / 2, color);
        canvas.drawRoundRect(x + 1, y + 1, width - 2, height - 2, height / 2 - 1, color);
        lathe_ui_fit_text(label, x + width / 2, y + 10, width - 10, color, TINY, middle_center);
    }

    void drawPreviewFooter(const char* label, int color) {
        if (!previewBlocked()) paintPreviewFooter(label, color);
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
        lathe_ui_badge(116, 55, 50, current_wcs(), lathe_ui_blue());
        for (int axis = 0; axis < profile_axis_count() && axis < 3; ++axis) {
            lathe_ui_preview_dro_row(92 + axis * 30, profile_axis_char(axis), axisPosition(axis), axis == 0);
        }
        char feed[20], spindle[20];
        snprintf(feed, sizeof(feed), "%lu", static_cast<unsigned long>(myFeed));
        const LatheStatus& lathe = lathe_status();
        if (lathe.available) snprintf(spindle, sizeof(spindle), "%.0f", lathe.effective_rpm);
        else snprintf(spindle, sizeof(spindle), "%lu", static_cast<unsigned long>(mySpeed));
        char footer[48];
        snprintf(footer, sizeof(footer), "F %s  S %s", feed, spindle);
        drawPreviewFooter(footer, lathe_ui_text());
    }

    void drawHomePreview() {
        const char* labels[3] = { "X", "Z", "C" };
        for (int axis = 0; axis < 3; ++axis) {
            int y = 92 + axis * 34;
            text(labels[axis], 40, y, lathe_ui_text(), SMALL, middle_left);
            if (axis == 2) {
                lathe_ui_fit_text("N/A", 174, y, 88, lathe_ui_muted(), TINY, middle_right);
            } else {
                bool homed = is_axis_homed(axis);
                int color = homed ? lathe_ui_green() : lathe_ui_amber();
                canvas.fillCircle(68, y - 1, 3, color);
                lathe_ui_fit_text(homed ? "HOMED" : "NEEDS HOME", 174, y, 92, color, TINY, middle_right);
            }
            canvas.fillRect(40, y + 14, 134, 2, lathe_ui_panel_alt());
        }
        drawPreviewFooter("X / Z AXES", lathe_ui_blue());
    }

    void drawJogPreview() {
        JogUiSnapshot snapshot = jog_ui_snapshot();
        lathe_ui_badge(114, 55, 54, snapshot.dynamic ? "DYNAMIC" : "PRECISE", lathe_ui_blue());
        lathe_ui_preview_dro_row(94, 'X', axisPosition(0), snapshot.selected_mask & 1);
        lathe_ui_preview_dro_row(128, 'Z', axisPosition(1), snapshot.selected_mask & 2);
        int axis = (snapshot.selected_mask & 2) ? 1 : 0;
        char axis_step[32];
        const char* active_axis = (snapshot.selected_mask & 1) && (snapshot.selected_mask & 2) ? "X+Z" :
                                  ((snapshot.selected_mask & 2) ? "Z" : "X");
        snprintf(axis_step, sizeof(axis_step), "%s / %s %s", active_axis, lathe_ui_position(snapshot.step[axis], 3),
                 inInches ? "IN" : "MM");
        lathe_ui_preview_value_row(161, "AXIS / STEP", axis_step, lathe_ui_blue());
        drawPreviewFooter(snapshot.moving ? "MOVING" : "TURN DIAL", snapshot.moving ? lathe_ui_green() : lathe_ui_muted());
    }

    void drawProbePreview() {
        lathe_ui_badge(48, 82, 92, myProbeSwitch ? "CONTACT" : "OPEN", myProbeSwitch ? lathe_ui_coral() : lathe_ui_green());
        const ProbeResult& result = last_probe_result();
        bool live = state == Cycle || state == Hold || state == DoorClosed;
        bool use_last = !live && result.known;
        text(live ? "LIVE POSITION" : use_last ? (result.success ? "LAST HIT" : "LAST / NO HIT") : "CURRENT POSITION", 106, 113,
             live ? lathe_ui_blue() : use_last ? (result.success ? lathe_ui_coral() : lathe_ui_amber()) : lathe_ui_muted(), TINY,
             middle_center);
        for (int display_axis = 0; display_axis < 2; ++display_axis) {
            int machine_axis = profile_machine_axis(display_axis);
            pos_t value = axisPosition(display_axis);
            if (use_last && machine_axis >= 0 && machine_axis < result.axis_count) {
                value = fromMm(result.axes_mm[machine_axis]);
            }
            lathe_ui_preview_dro_row(137 + display_axis * 29, profile_axis_char(display_axis), value, display_axis == 0);
        }
        drawPreviewFooter(myProbeSwitch ? "INPUT CLOSED" : "INPUT OPEN", myProbeSwitch ? lathe_ui_coral() : lathe_ui_green());
    }

    void drawToolsPreview() {
        const LatheStatus& status = lathe_status();
        int active = status.active_tool >= 1 && status.active_tool <= 5 ? status.active_tool : static_cast<int>(mySelectedTool);
        if (active < 1 || active > 5) active = 1;
        LatheToolType type = lathe_tool_type(active);
        lathe_ui_tool_icon(type, 68, 98, lathe_ui_blue(), 3);
        char tool[8];
        snprintf(tool, sizeof(tool), "T%d", active);
        text(tool, 103, 88, lathe_ui_blue(), SMALL, middle_left);
        lathe_ui_fit_text(lathe_tool_type_label(type), 103, 112, 70, lathe_ui_text());
        for (int station = 1; station <= 5; ++station) {
            int x = 48 + (station - 1) * 30;
            if (station == active) {
                canvas.drawRoundRect(x - 13, 128, 26, 35, 5, lathe_ui_blue());
                canvas.drawRoundRect(x - 12, 129, 24, 33, 4, lathe_ui_blue());
            }
            lathe_ui_tool_icon(lathe_tool_type(station), x, 145, station == active ? lathe_ui_blue() : lathe_ui_muted(), 2);
            char number[2] = { static_cast<char>('0' + station), '\0' };
            text(number, x, 169, station == active ? lathe_ui_blue() : lathe_ui_muted(), TINY, middle_center);
        }
        drawPreviewFooter("TURRET READY", lathe_ui_green());
    }

    void drawFilesPreview() {
        const char* sd_label = !_files_error.empty() ? "SD ERROR" : _files_loading ? "LOADING" : "SD READY";
        int sd_color = !_files_error.empty() ? RED : _files_loading ? lathe_ui_amber() : lathe_ui_green();
        lathe_ui_badge(114, 55, 54, sd_label, sd_color);
        if (!_files_error.empty()) {
            lathe_ui_nav_icon(LatheNavItem::Files, 106, 105, RED, 4);
            lathe_ui_fit_text(_files_error.c_str(), 106, 145, 128, RED, TINY, middle_center);
            drawPreviewFooter("SD ERROR", RED);
            return;
        }
        if (fileVector.empty()) {
            lathe_ui_nav_icon(LatheNavItem::Files, 106, 105, _files_loading ? lathe_ui_amber() : lathe_ui_blue(), 4);
            const bool confirmed_empty = _files_requested && !_files_loading;
            lathe_ui_fit_text(_files_loading ? "READING SD" : confirmed_empty ? "NO FILES" : "WAITING FOR SD", 106, 145, 128,
                              _files_loading ? lathe_ui_amber() : lathe_ui_muted(), TINY, middle_center);
            drawPreviewFooter(_files_loading ? "LOADING" : confirmed_empty ? "ADD FILES TO SD" : "SD QUEUED",
                              _files_loading ? lathe_ui_amber() : confirmed_empty ? lathe_ui_muted() : lathe_ui_blue());
            return;
        }
        int shown = fileVector.size() < 3 ? static_cast<int>(fileVector.size()) : 3;
        for (int i = 0; i < shown; ++i) {
            int y = 91 + i * 31;
            if (i == 0) {
                canvas.drawRoundRect(36, y - 14, 140, 28, 6, lathe_ui_blue());
                canvas.drawRoundRect(37, y - 13, 138, 26, 5, lathe_ui_blue());
            }
            lathe_ui_nav_icon(LatheNavItem::Files, 47, y, i == 0 ? lathe_ui_text() : lathe_ui_muted(), 1);
            lathe_ui_fit_text(fileVector[i].fileName.c_str(), 60, y, 78, i == 0 ? lathe_ui_text() : lathe_ui_muted());
            const char* size = fileVector[i].isDir() ? "DIR" : fileSize(fileVector[i].fileSize);
            lathe_ui_fit_text(size, 170, y, 34, i == 0 ? lathe_ui_blue() : lathe_ui_muted(), TINY, middle_right);
        }
        char count[24];
        snprintf(count, sizeof(count), "%u ITEMS", static_cast<unsigned>(fileVector.size()));
        drawPreviewFooter(count, lathe_ui_blue());
    }

    void drawMacrosPreview() {
        if (!_macros_error.empty()) {
            lathe_ui_nav_icon(LatheNavItem::Macros, 106, 105, RED, 4);
            lathe_ui_fit_text(_macros_error.c_str(), 106, 145, 128, RED, TINY, middle_center);
            drawPreviewFooter("MACRO ERROR", RED);
            return;
        }
        if (macros.empty()) {
            lathe_ui_nav_icon(LatheNavItem::Macros, 106, 105, _macros_loading ? lathe_ui_amber() : lathe_ui_blue(), 4);
            const bool confirmed_empty = _macros_requested && !_macros_loading;
            lathe_ui_fit_text(_macros_loading ? "READING MACROS" : confirmed_empty ? "NO MACROS" : "WAITING FOR SD", 106, 145,
                              128, _macros_loading ? lathe_ui_amber() : lathe_ui_muted(), TINY, middle_center);
            drawPreviewFooter(_macros_loading ? "LOADING" : confirmed_empty ? "ADD FILES TO SD" : "SD QUEUED",
                              _macros_loading ? lathe_ui_amber() : confirmed_empty ? lathe_ui_muted() : lathe_ui_blue());
            return;
        }
        int shown = macros.size() < 3 ? static_cast<int>(macros.size()) : 3;
        for (int i = 0; i < shown; ++i) {
            int y = 91 + i * 31;
            canvas.drawRoundRect(36, y - 14, 140, 28, 6, i == 0 ? lathe_ui_blue() : lathe_ui_panel_alt());
            if (i == 0) canvas.drawRoundRect(37, y - 13, 138, 26, 5, lathe_ui_blue());
            lathe_ui_fit_text(macros[i]->name.c_str(), 48, y, 116, i == 0 ? lathe_ui_text() : lathe_ui_muted());
        }
        char count[24];
        snprintf(count, sizeof(count), "%u MACROS", static_cast<unsigned>(macros.size()));
        drawPreviewFooter(count, lathe_ui_blue());
    }

    void drawAboutPreview() {
        char firmware[48];
        firmwareSummary(firmware, sizeof(firmware));
        text("XZACT M5DIAL", 106, 84, lathe_ui_text(), SMALL, middle_center);
        lathe_ui_fit_text(firmware, 106, 105, 132, lathe_ui_blue(), TINY, middle_center);
        lathe_ui_preview_value_row(128, "UART 1M", fnc_is_connected() ? "ONLINE" : "N/C", fnc_is_connected() ? lathe_ui_green() : RED);
#ifdef USE_WIFI
        lathe_ui_preview_value_row(148, "WIFI", wifi_signal_bars() ? "" : "OFFLINE", wifi_signal_bars() ? lathe_ui_green() : lathe_ui_muted());
        if (wifi_signal_bars()) drawWiFiSignalBars(153, 152);
#else
        lathe_ui_preview_value_row(148, "WIFI", "N/A", lathe_ui_muted());
#endif
        lathe_ui_preview_value_row(168, "FLUIDNC", my_state_string, state == Idle ? lathe_ui_green() : lathe_ui_amber());
        drawPreviewFooter("SETTINGS", lathe_ui_blue());
    }

    void drawSelectedPreview() {
        lathe_ui_main_surface(previewTitle(_selected), previewState());
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
        if (previewBlocked()) paintPreviewFooter("LINK REQUIRED", lathe_ui_amber());
        lathe_ui_orbital_rail(_selected, _animation_phase, _animation_direction);
        lathe_ui_round_clip();
        refreshDisplay();
    }

    int touchedRailItem(int x, int y) const {
        return lathe_ui_rail_item_at(_selected, x, y);
    }

    void setSelectionSilently(int selection) {
        if (selection < -1 || selection >= num_items()) return;
        if (selection >= 0 && _items[selection]->hidden()) return;
        if (_selected >= 0 && _selected < num_items()) _items[_selected]->unhighlight();
        if (selection == -1) {
            _selected = -1;
            return;
        }
        _selected = selection;
        _items[_selected]->highlight();
    }

    void snapshotDiagnosticState() {
        if (_diagnostic_snapshot_valid) return;
        _diagnostic_saved_selection = _selected;
        _diagnostic_saved_animation_phase = _animation_phase;
        _diagnostic_saved_animation_direction = _animation_direction;
        _diagnostic_saved_animation_ms = _last_animation_ms;
        _diagnostic_saved_visual_forced = _diagnostic_visual_forced;
        _diagnostic_saved_blocked = _diagnostic_blocked;
        _diagnostic_saved_shown_state = _diagnostic_shown_state;
        _diagnostic_snapshot_valid = true;
    }

    void forceDiagnosticVisual(bool blocked) {
        _diagnostic_visual_forced = true;
        _diagnostic_blocked = blocked;
        _diagnostic_shown_state = blocked ? Disconnected : Idle;
    }

public:
    MenuScene() : PieMenu("Main", buttonRadius, menu_help_text) {}
    void previewDiagnosticSelection(int encoded, bool force_state) {
        snapshotDiagnosticState();
        if (encoded < 0) encoded = 0;
        if (encoded > 15) encoded = 15;
        if (force_state) forceDiagnosticVisual(encoded >= 8);
        else {
            _diagnostic_visual_forced = false;
            _diagnostic_blocked = false;
            _diagnostic_shown_state = Idle;
        }
        _animation_phase = 0;
        _animation_direction = 0;
        setSelectionSilently(encoded % 8);
        reDisplay();
    }
    void stepDiagnosticSelection(int delta) {
        snapshotDiagnosticState();
        if (!_diagnostic_cursor_valid) {
            _diagnostic_cursor = _diagnostic_saved_selection;
            if (_diagnostic_cursor < 0 || _diagnostic_cursor >= num_items()) _diagnostic_cursor = 0;
            _diagnostic_cursor_valid = true;
        }
        setSelectionSilently(_diagnostic_cursor);
        forceDiagnosticVisual(false);
        onEncoder(delta);
        _diagnostic_cursor = _selected;
    }
    void setDiagnosticVisual(int encoded) {
        previewDiagnosticSelection(encoded, true);
    }
    void clearDiagnosticVisual() {
        if (!_diagnostic_snapshot_valid) {
            _diagnostic_visual_forced = false;
            _diagnostic_blocked = false;
            _diagnostic_shown_state = Idle;
            return;
        }
        setSelectionSilently(_diagnostic_saved_selection);
        _animation_phase = _diagnostic_saved_animation_phase;
        _animation_direction = _diagnostic_saved_animation_direction;
        _last_animation_ms = _diagnostic_saved_animation_ms;
        _diagnostic_visual_forced = _diagnostic_saved_visual_forced;
        _diagnostic_blocked = _diagnostic_saved_blocked;
        _diagnostic_shown_state = _diagnostic_saved_shown_state;
        _diagnostic_snapshot_valid = false;
    }
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
    menuScene.previewDiagnosticSelection(selection, false);
}

void diagnostic_preview_main_menu_state(int encoded) {
    menuScene.setDiagnosticVisual(encoded);
}

void diagnostic_step_main_menu(int delta) {
    menuScene.stepDiagnosticSelection(delta);
}

void diagnostic_clear_main_menu_state() {
    menuScene.clearDiagnosticVisual();
}
