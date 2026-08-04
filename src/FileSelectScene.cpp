// Copyright (c) 2023 - Barton Dring
// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "FileParser.h"
#include "LatheUi.h"
#include "polar.h"

// #define SMOOTH_SCROLL
#define WRAP_FILE_LIST

extern Scene filePreviewScene;

extern Scene& jogScene;

class FileSelectScene;
static FileSelectScene* pending_file_select_scene = nullptr;
static void request_current_file_list();

class FileSelectScene : public Scene {
private:
    int              _selected_file = 0;
    std::vector<int> prevSelect;
    std::string      dirName         = "/sd";
    int              dirLevel        = 0;
    bool             _selecting_file = false;
    bool             _reading = false;
    std::string      _error_string;
    int              _diagnostic_fixture = -1;

    const char* format_size(size_t size) {
        const int   buflen = 30;
        static char buffer[buflen];
        if (size >= 1000000) {
            int mb      = size / 1000000;
            int residue = size % 1000000;
            snprintf(buffer, buflen, "%d.%03d MB", mb, residue / 1000);
        } else if (size > 1000) {
            int kb      = size / 1000;
            int residue = size % 1000;
            snprintf(buffer, buflen, "%d.%03d KB", kb, residue);
        } else {
            snprintf(buffer, buflen, "%d bytes", size);
        }
        return buffer;
    }

public:
    FileSelectScene() : Scene("Files", 4) {}

    void diagnosticPreview(int fixture) {
        _diagnostic_fixture = fixture;
        reDisplay();
    }

    void onEntry(void* arg) {
        _diagnostic_fixture = -1;
        // a first time only thing, because files are already loaded
        if (prevSelect.size() == 0) {
            prevSelect.push_back(0);
        }
        if (fileVector.empty()) {
            _reading = true;
            _error_string.clear();
            pending_file_select_scene = this;
            schedule_action(request_current_file_list);
        }
    }

    void onDialButtonPress() { pop_scene(); }

    void onGreenButtonPress() {
        if (state != Idle) {
            return;
        }
        if (fileVector.size()) {
            fileInfo                                 = fileVector[_selected_file];
            prevSelect[(int)(prevSelect.size() - 1)] = _selected_file;
            if (fileInfo.isDir()) {
                prevSelect.push_back(0);
                dirName += "/";
                dirName += fileInfo.fileName;
                ++dirLevel;
                _reading = true;
                _error_string.clear();
                request_file_list(dirName.c_str());
            } else {
                std::string path(dirName);
                path += "/";
                path += fileInfo.fileName;
                push_scene(&filePreviewScene, (void*)path.c_str());
            }
        }
        ackBeep();
    }

    void onRedButtonPress() {
        if (state != Idle) {
            return;
        }
        if (dirLevel) {
            prevSelect.pop_back();
            auto pos = dirName.rfind('/');
            dirName  = dirName.substr(0, pos);
            --dirLevel;
            _reading = true;
            _error_string.clear();
            request_file_list(dirName.c_str());
        } else {
            prevSelect.clear();
            prevSelect.push_back(0);
            _reading = true;
            _error_string.clear();
            init_file_list();
        }
        ackBeep();
    }

    void onTouchPress() { _selecting_file = true; }
    void onTouchRelease() {
        if (_selecting_file) {
            _selecting_file = false;
            onGreenButtonPress();
        }
    }
    void onFilesList() override {
        _reading = false;
        _error_string.clear();
        _selected_file = prevSelect.back();
        reDisplay();
    }

    void onError(const char* error) override {
        if (_reading) {
            _reading = false;
            _error_string = error ? error : "SD ERROR";
            reDisplay();
        }
    }

    void onEncoder(int delta) override { scroll(delta); }

    void onMessage(char* command, char* arguments) override {
        dbg_printf("FileSelectScene::onMessage(\"%s\", \"%s\")\r\n", command, arguments);
        // now just need to know what to do with messages
    }

    void buttonLegends() {
        const char* grnLabel = "";
        const char* redLabel = "";

        if (state == Idle) {
            redLabel = dirLevel ? "Up.." : "Refresh";
            if (fileVector.size()) {
                grnLabel = fileVector[_selected_file].isDir() ? "Down.." : "Load";
            }
        }

        drawButtonLegends(redLabel, grnLabel, "Back");
    }

    // The number of filenames that can be displayed at once.
    // It should be odd.
    static const int N_DISPLAYED_FILENAMES = 3;

    // Height of the highlight box for the selected file.
    // This must be large enough for the big filename
    // font size plus the two info font sizes.
    static const int big_height = 80;

    // Y position of file type info above the big filename
    // This depends on the font sizes of the big filename and
    // the file type
    static const int type_offset = 26;

    // Y position of file size info below the big filename
    // This depends on the font sizes of the big filename and
    // the file size
    static const int size_offset = -26;

    // Separation between big filename + info and small filenames
    // This depends on big_height and the small filename font size
    int y_distance = 40;
    int y_inc      = 18;

    // Offsets the x position to account for the scroll indicator on the right
    static const int big_width = 225;  // display_short_size() - scroll_width * 2

    static const int x_offset = -7;  // - scroll_width

    struct layout {
        int _w;
    };
    // clang-format off
    // The widths change because the display is circular so you have less
    // space as the you go farther from the center.
    struct layout fnlayouts[N_DISPLAYED_FILENAMES] = {
        // _w,
        {  190 }, // file[0] fileName
        {  225 }, // file[1] fileName
        {  200 }  // file[2] fileName
    };
    // clang-format on

    void onRightFlick() { activate_scene(&jogScene); }

    void requestCurrentDirectory() {
        _reading = true;
        _error_string.clear();
        if (dirLevel == 0) {
            init_file_list();
        } else {
            request_file_list(dirName.c_str());
        }
    }

    void showFiles() {
        if (lathe_ui_enabled()) {
            lathe_ui_detail_surface("FILES");
            lathe_ui_fit_text(dirName.c_str(), 120, 50, 160, lathe_ui_blue(), TINY, middle_center);
            bool fixture_error = _diagnostic_fixture == 3;
            bool fixture_empty = _diagnostic_fixture == 2;
            bool fixture_loading = _diagnostic_fixture == 1;
            if (fixture_error || !_error_string.empty()) {
                lathe_ui_fit_text(fixture_error ? "SD CARD ERROR" : _error_string.c_str(), 120, 115, 165, RED, SMALL, middle_center);
            } else if (fixture_empty || fixture_loading || fileVector.empty()) {
                bool loading = fixture_loading || (_diagnostic_fixture < 0 && _reading);
                centered_text(loading ? "LOADING SD" : "NO FILES", 112,
                              loading ? lathe_ui_amber() : lathe_ui_muted(), SMALL);
            } else {
                int first = _selected_file - 2;
                int last = _selected_file + 2;
                for (int index = first; index <= last; ++index) {
                    if (index < 0 || index >= static_cast<int>(fileVector.size())) continue;
                    int y = 78 + (index - first) * 27;
                    bool selected = index == _selected_file;
                    if (selected) canvas.drawRoundRect(22, y - 12, 196, 24, 7, lathe_ui_blue());
                    const char* kind = fileVector[index].isDir() ? "DIR" : "FILE";
                    text(kind, 31, y, selected ? lathe_ui_blue() : lathe_ui_muted(), TINY, middle_left);
                    lathe_ui_fit_text(fileVector[index].fileName.c_str(), 68, y, 132, lathe_ui_text());
                }
                char position[24];
                snprintf(position, sizeof(position), "%d / %u", _selected_file + 1, static_cast<unsigned>(fileVector.size()));
                centered_text(position, 207, lathe_ui_muted(), TINY);
            }
            buttonLegends();
            refreshDisplay();
            return;
        }
        // canvas.createSprite(240, 240);
        // drawBackground(BLACK);
        background();
        drawMenuTitle(current_scene->name());
        std::string fName;

        int fdIter = _selected_file - 1;  // first file in display list

        for (int display_slot = 0; display_slot < N_DISPLAYED_FILENAMES; display_slot++, fdIter++) {
            auto fnlayout = fnlayouts[display_slot];

#ifdef WRAP_FILE_LIST
            if (fileVector.size() > 2) {
                if (fdIter < 0) {
                    // last file first in list
                    fdIter = fileVector.size() - 1;
                } else if (fdIter > fileVector.size() - 1) {
                    // first file last in list
                    fdIter = 0;
                }
            }
#endif
            if (fdIter < 0) {
                continue;
            }

            fName = "< no files >";
            if (fileVector.size()) {
                fName = fileVector[fdIter].fileName;
            }
            int middle_slot = (N_DISPLAYED_FILENAMES - 1) / 2;
            int offset      = middle_slot - display_slot;
            if (offset == 0) {
                int tcolor = BLACK;
                drawRect(Point(x_offset, 0), big_width, big_height, big_height * 45 / 100, LIGHTGREY);

                std::string fInfoT = "";  // file info top line
                std::string fInfoB = "";  // File info bottom line
                int         ext    = fName.rfind('.');
                if (fileVector.size()) {
                    if (fileVector[_selected_file].isDir()) {
                        fInfoB = "Folder";
                        tcolor = BLUE;
                    } else {
                        if (ext > 0) {
                            fInfoT = fName.substr(ext, fName.length());
                            fInfoT += " file";
                            fName.erase(ext);
                        }
                        fInfoB = format_size(fileVector[_selected_file].fileSize);
                    }
                }

                // Scroll indicator showing the position of the displayed files
                // in the larger list of files.
                // If there are at most three files, all are displayed, without
                // a scroll indicator.
                if (fileVector.size() > 3) {
                    int width  = 8;
                    int radius = width / 2;
                    if (round_display) {
                        for (int i = 0; i < width; i++) {
                            canvas.drawArc(120, 120, 119 - i, 115 - i, -50, 50, DARKGREY);
                        }

                        int x, y;
                        int arc_degrees = 100;
                        int divisor     = fileVector.size() - 1;
                        int increment   = arc_degrees / divisor;
                        int start_angle = (arc_degrees / 2);
                        int angle       = start_angle - (_selected_file * arc_degrees) / divisor;
                        r_degrees_to_xy(114, angle, &x, &y);
                        drawFilledCircle(Point(x - 1, y), radius + 2, LIGHTGREY);
                    } else {
                        int x            = display_short_side() - width;
                        int height       = display_short_side() - 30;
                        int inner_height = height - width;
                        int middle       = inner_height / 2;
                        int divisor      = fileVector.size() - 1;
                        int y            = width + inner_height * _selected_file / divisor;
                        drawRect(x - radius, radius, width + 2, height, radius, DARKGREY);
                        drawFilledCircle(x, y, radius + 1, LIGHTGREY);
                    }
                }

                text(fInfoT.c_str(), Point(x_offset, type_offset), BLUE, SMALL, middle_center);
                text(fInfoB.c_str(), Point(x_offset, size_offset), BLACK, TINY, middle_center);

                auto_text(fName, Point(x_offset, 0), fnlayout._w, tcolor, MEDIUM, middle_center);

#ifdef WRAP_FILE_LIST
                if (fileVector.size() >= N_DISPLAYED_FILENAMES) {
                    continue;
                }
#endif
                if (fdIter >= (int)(fileVector.size() - 1)) {
                    break;
                }
            } else {
                int y_offset = offset * y_inc;
                y_offset += (offset > 0) ? y_distance : -y_distance;
                // printf("y_offset %d\n", y_offset);
                int width = big_width;
                if (round_display) {
                    // If the display is round, we need to reduce the width available
                    // for filename display when we are off-center.
                    // This is a one-term approximation for
                    //   delta_width = 2 * half_width * (1 - cos(arcsin(y_offset/half_width)))
                    // The first term of arcsin(x) is x and the first term of cos(x) is 1-x*x/2
                    // so 2*h*(1 - cos(arcsin(y/h))
                    // ~= 2*h**(1 - (1 - y*y/2*h*h))
                    // = 2*h*(y*y/2*h*h)
                    // = y*y/h
                    int half_width  = width / 2;
                    int delta_width = y_offset * y_offset / half_width;
                    width -= delta_width;
                }
                auto_text(fName, Point(x_offset, y_offset), width, WHITE, SMALL, middle_center);
            }
        }  // for(display_slot)
        buttonLegends();
        drawStatusSmall(21);
        refreshDisplay();
    }

    void scroll(int updown) {
        int nextSelect = _selected_file + updown;
#ifdef WRAP_FILE_LIST
        if (fileVector.size() < 3) {
            if (nextSelect < 0 || nextSelect > (int)(fileVector.size() - 1)) {
                return;
            }
        } else {
            if (nextSelect < 0) {
                nextSelect = fileVector.size() - 1;
            } else if (nextSelect > (int)(fileVector.size() - 1)) {
                nextSelect = 0;
            }
        }
#else
        if (nextSelect < 0 || nextSelect > (int)(fileVector.size() - 1)) {
            return;
        }
#endif

        _selected_file = nextSelect;
        showFiles();
    }

    void reDisplay() {
        showFiles();
    }
};

static void request_current_file_list() {
    if (pending_file_select_scene) {
        pending_file_select_scene->requestCurrentDirectory();
        pending_file_select_scene = nullptr;
    }
}

FileSelectScene fileSelectScene;

void diagnostic_preview_files(int fixture) {
    fileSelectScene.diagnosticPreview(fixture);
}
