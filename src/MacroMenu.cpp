// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Menu.h"
#include "MacroItem.h"
#include "polar.h"
#include "FileParser.h"
#include "LatheUi.h"

extern Scene statusScene;
extern Scene filePreviewScene;

void MacroItem::invoke(void* arg) {
    if (arg && strcmp((char*)arg, "Run") == 0) {
        if (_filename.rfind("cmd:", 0) == 0) {
            // Split on \n, \r, and ';' — FluidNC parses ';' as a line-comment,
            // so multi-statement macros like "G0 Z45; G0 Y166" must be sent as
            // separate lines. Trim whitespace and skip empty segments.
            const char* body = _filename.c_str() + 4;  // strip "cmd:" prefix
            std::string line;
            for (const char* p = body;; ++p) {
                char c = *p;
                if (c == '\n' || c == '\r' || c == ';' || c == '\0') {
                    size_t start = line.find_first_not_of(" \t");
                    if (start != std::string::npos) {
                        size_t end = line.find_last_not_of(" \t");
                        send_line(line.substr(start, end - start + 1).c_str());
                    }
                    line.clear();
                    if (c == '\0') {
                        break;
                    }
                } else {
                    line.push_back(c);
                }
            }
        } else {
            send_linef("$Localfs/Run=%s", _filename.c_str());
        }
    } else {
        push_scene(&filePreviewScene, (void*)_filename.c_str());
        // doFileScreen(_name);
    }
}
void MacroItem::show(const Point& where) {
    int color = WHITE;

    std::string extra(_filename);
    if (extra.rfind("/localfs", 0) == 0) {
        extra.erase(0, strlen("/localfs"));
    } else if (extra.rfind("cmd:", 0) == 0) {
        extra.erase(0, strlen("cmd:"));
    }

    if (_highlighted) {
        drawRect(where, Point { 200, 50 }, 15, color);
        text(name(), where + Point { 0, 6 }, BLACK, MEDIUM, middle_center);
        text(extra, where - Point { 0, 16 }, BLACK, TINY, middle_center);
    } else {
        text(name(), where, WHITE, SMALL, middle_center);
    }
}

class MacroMenu : public Menu {
private:
    bool        _reading = true;
    std::string _error_string;
    int         _diagnostic_fixture = -1;

public:
    MacroMenu() : Menu("Macros") {}

    void diagnosticPreview(int fixture) {
        _diagnostic_fixture = fixture;
        reDisplay();
    }

    const std::string& selected_name() { return _items[_selected]->name(); }

    void refreshMacros() {
        removeAllItems();
        _reading = true;
        request_macros();
    }

    void onRedButtonPress() { refreshMacros(); }
    void onFilesList() {
        _error_string.clear();
        _reading = false;
        if (num_items()) {
            _selected = 0;
            _items[_selected]->highlight();
        }
        reDisplay();
    }

    void onError(const char* errstr) {
        _error_string = errstr;
        _reading      = false;
        reDisplay();
    }

    void onEntry(void* arg) override {
        _diagnostic_fixture = -1;
        if (num_items() == 0) {
            refreshMacros();
        }
    }

    void onDialButtonPress() {
        if (num_items()) {
            invoke((void*)"Run");
        }
    }

    void onGreenButtonPress() {
        if (state != Idle) {
            return;
        }
        if (num_items()) {
            invoke();
        }
    }

    void onTouchClick() { onGreenButtonPress(); }

    void reDisplay() override {
        if (lathe_ui_enabled()) {
            lathe_ui_detail_surface("MACROS");
            bool fixture_loading = _diagnostic_fixture == 1;
            bool fixture_empty = _diagnostic_fixture == 2;
            bool fixture_error = _diagnostic_fixture == 3;
            if (fixture_loading || fixture_empty || fixture_error || num_items() == 0) {
                if (fixture_error || _error_string.length()) {
                    lathe_ui_fit_text(fixture_error ? "MACRO LOAD ERROR" : _error_string.c_str(), 120, 114, 170, RED, SMALL, middle_center);
                } else {
                    bool loading = fixture_loading || (_diagnostic_fixture < 0 && _reading);
                    centered_text(loading ? "LOADING MACROS" : "NO MACROS", 110,
                                  loading ? lathe_ui_amber() : lathe_ui_muted(), SMALL);
                    if (!loading) centered_text("ADD FILES TO SD", 143, lathe_ui_muted(), TINY);
                }
            } else {
                int first = _selected - 2;
                int last = _selected + 2;
                for (int index = first; index <= last; ++index) {
                    if (index < 0 || index >= num_items()) continue;
                    int y = 76 + (index - first) * 29;
                    bool selected = index == _selected;
                    if (selected) canvas.drawRoundRect(26, y - 13, 188, 26, 7, lathe_ui_blue());
                    lathe_ui_fit_text(_items[index]->name().c_str(), 38, y, 164, selected ? lathe_ui_text() : lathe_ui_muted());
                }
                char position[24];
                snprintf(position, sizeof(position), "%d / %d", _selected + 1, num_items());
                centered_text(position, 207, lathe_ui_muted(), TINY);
            }
            buttonLegends();
            refreshDisplay();
            return;
        }
        menuBackground();
        if (num_items() == 0) {
            // Point where { 0, 0 };
            // Point wh { 200, 45 };
            // drawRect(where, wh, 20, YELLOW);
            if (_error_string.length()) {
                text(_error_string, 120, 120, WHITE, SMALL, middle_center);
            } else {
                text(_reading ? "Reading Macros" : "No Macros", { 0, 0 }, WHITE, SMALL, middle_center);
            }
        } else {
            if (_selected > 1) {
                _items[_selected - 2]->show({ 0, 80 });
            }
            if (_selected > 0) {
                _items[_selected - 1]->show({ 0, 45 });
            }
            _items[_selected]->show({ 0, 0 });
            if (_selected < num_items() - 1) {
                _items[_selected + 1]->show({ 0, -45 });
            }
            if (_selected < num_items() - 2) {
                _items[_selected + 2]->show({ 0, -80 });
            }
        }
        buttonLegends();
        refreshDisplay();
    }

    void buttonLegends() {
        const char* orangeLabel = "";
        const char* grnLabel    = "";

        if (state == Idle) {
            if (num_items()) {
                orangeLabel = "Run";
                grnLabel    = "Load";
            }
        }

        drawButtonLegends(_reading ? "" : "Refresh", grnLabel, orangeLabel);
    }

    void rotate(int delta) override {
        if (_selected == 0 && delta <= 0) {
            return;
        }
        if (_selected == num_items() && delta >= 0) {
            return;
        }
        if (_selected != -1) {
            _items[_selected]->unhighlight();
        }
        _selected += delta;
        if (_selected < 0) {
            _selected = 0;
        }
        if (_selected >= num_items()) {
            _selected = num_items() - 1;
        }
        _items[_selected]->highlight();
        reDisplay();
    }

    int touchedItem(int x, int y) override { return -1; };

    void onStateChange(state_t old_state) {
        if (state == Cycle) {
            push_scene(&statusScene);
        }
    }

    void menuBackground() override {
        background();

        if (num_items()) {
            // Draw dot showing the selected file
            if (num_items() > 1) {
                int span   = 100;  // degrees
                int dtheta = span * _selected / (num_items() - 1);
                int theta  = (span / 2) - dtheta;
                int dx, dy;
                r_degrees_to_xy(110, theta, &dx, &dy);

                drawFilledCircle({ dx, dy }, 8, WHITE);
            }
        }
        if (state != Idle) {
            drawStatus();
        }

        text("Macros", { 0, 100 }, YELLOW, SMALL);
    }
} macroMenu;

void diagnostic_preview_macros(int fixture) {
    static_cast<MacroMenu&>(macroMenu).diagnosticPreview(fixture);
}
