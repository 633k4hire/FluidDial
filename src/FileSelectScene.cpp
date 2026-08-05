// Copyright (c) 2023 - Barton Dring
// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "FileParser.h"
#include "polar.h"

// #define SMOOTH_SCROLL
#define WRAP_FILE_LIST

extern Scene filePreviewScene;
extern Scene macroMenu;

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

    // Lathe manual helpers replace the main-menu Macros icon.  Keep custom
    // operator macros alongside SD work, where an operator naturally goes to
    // choose work to run.  This is a virtual root item; it is never sent to
    // FluidNC as a filename.
    bool macro_selected() const { return dirLevel == 0 && _selected_file == 0; }
    int  file_selection_offset() const { return dirLevel == 0 ? 1 : 0; }
    int  entry_count() const { return (int)fileVector.size() + file_selection_offset(); }
    int  selected_file_index() const { return _selected_file - file_selection_offset(); }

    bool is_macro_entry(int entry) const { return dirLevel == 0 && entry == 0; }
    int  file_index_for_entry(int entry) const { return entry - file_selection_offset(); }

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

    void onEntry(void* arg) {
        // a first time only thing, because files are already loaded
        if (prevSelect.size() == 0) {
            prevSelect.push_back(0);
        }
        if (fileVector.empty()) {
            pending_file_select_scene = this;
            schedule_action(request_current_file_list);
        }
    }

    void onDialButtonPress() { pop_scene(); }

    void onGreenButtonPress() {
        if (state != Idle) {
            return;
        }
        if (macro_selected()) {
            push_scene(&macroMenu);
            ackBeep();
            return;
        }
        int file_index = selected_file_index();
        if (file_index >= 0 && file_index < (int)fileVector.size()) {
            fileInfo                                 = fileVector[file_index];
            prevSelect[(int)(prevSelect.size() - 1)] = _selected_file;
            if (fileInfo.isDir()) {
                prevSelect.push_back(0);
                dirName += "/";
                dirName += fileInfo.fileName;
                ++dirLevel;
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
            request_file_list(dirName.c_str());
        } else {
            prevSelect.clear();
            prevSelect.push_back(0);
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
        _selected_file = prevSelect.back();
        if (_selected_file < 0 || _selected_file >= entry_count()) {
            _selected_file = 0;
        }
        reDisplay();
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
            if (macro_selected()) {
                grnLabel = "Macros";
            } else {
                int file_index = selected_file_index();
                if (file_index >= 0 && file_index < (int)fileVector.size()) {
                    grnLabel = fileVector[file_index].isDir() ? "Down.." : "Load";
                }
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
        if (dirLevel == 0) {
            init_file_list();
        } else {
            request_file_list(dirName.c_str());
        }
    }

    void showFiles() {
        // canvas.createSprite(240, 240);
        // drawBackground(BLACK);
        background();
        drawMenuTitle(current_scene->name());
        std::string fName;

        int fdIter      = _selected_file - 1;  // first entry in display list
        int entry_count = this->entry_count();

        for (int display_slot = 0; display_slot < N_DISPLAYED_FILENAMES; display_slot++, fdIter++) {
            auto fnlayout = fnlayouts[display_slot];

#ifdef WRAP_FILE_LIST
            if (entry_count > 2) {
                if (fdIter < 0) {
                    // last entry first in list
                    fdIter = entry_count - 1;
                } else if (fdIter > entry_count - 1) {
                    // first entry last in list
                    fdIter = 0;
                }
            }
#endif
            if (fdIter < 0) {
                continue;
            }

            fName = "< no files >";
            bool macro_entry = is_macro_entry(fdIter);
            int  file_index  = file_index_for_entry(fdIter);
            if (macro_entry) {
                fName = "Macros";
            } else if (file_index >= 0 && file_index < (int)fileVector.size()) {
                fName = fileVector[file_index].fileName;
            }
            int middle_slot = (N_DISPLAYED_FILENAMES - 1) / 2;
            int offset      = middle_slot - display_slot;
            if (offset == 0) {
                int tcolor = BLACK;
                drawRect(Point(x_offset, 0), big_width, big_height, big_height * 45 / 100, LIGHTGREY);

                std::string fInfoT = "";  // file info top line
                std::string fInfoB = "";  // File info bottom line
                int         ext    = fName.rfind('.');
                if (macro_entry) {
                    fInfoT = "Manual";
                    fInfoB = "Custom macros";
                    tcolor = MAROON;
                } else {
                    int selected_file_index = this->selected_file_index();
                    if (selected_file_index >= 0 && selected_file_index < (int)fileVector.size()
                        && fileVector[selected_file_index].isDir()) {
                        fInfoB = "Folder";
                        tcolor = BLUE;
                    } else if (selected_file_index >= 0 && selected_file_index < (int)fileVector.size()) {
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
                if (entry_count > 3) {
                    int width  = 8;
                    int radius = width / 2;
                    if (round_display) {
                        for (int i = 0; i < width; i++) {
                            canvas.drawArc(120, 120, 119 - i, 115 - i, -50, 50, DARKGREY);
                        }

                        int x, y;
                        int arc_degrees = 100;
                        int divisor     = entry_count - 1;
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
                        int divisor      = entry_count - 1;
                        int y            = width + inner_height * _selected_file / divisor;
                        drawRect(x - radius, radius, width + 2, height, radius, DARKGREY);
                        drawFilledCircle(x, y, radius + 1, LIGHTGREY);
                    }
                }

                text(fInfoT.c_str(), Point(x_offset, type_offset), BLUE, SMALL, middle_center);
                text(fInfoB.c_str(), Point(x_offset, size_offset), BLACK, TINY, middle_center);

                auto_text(fName, Point(x_offset, 0), fnlayout._w, tcolor, MEDIUM, middle_center);

#ifdef WRAP_FILE_LIST
                if (entry_count >= N_DISPLAYED_FILENAMES) {
                    continue;
                }
#endif
                if (fdIter >= entry_count - 1) {
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
        int entries    = entry_count();
#ifdef WRAP_FILE_LIST
        if (entries < 3) {
            if (nextSelect < 0 || nextSelect > entries - 1) {
                return;
            }
        } else {
            if (nextSelect < 0) {
                nextSelect = entries - 1;
            } else if (nextSelect > entries - 1) {
                nextSelect = 0;
            }
        }
#else
        if (nextSelect < 0 || nextSelect > entries - 1) {
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
