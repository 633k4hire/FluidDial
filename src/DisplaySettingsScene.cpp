// 2026 - Figamore
// DisplaySettingsScene.cpp — lets the user cycle through CYD screen orientations.
//
// Turn the encoder to step through all available layouts (rotation × button position).

#ifdef USE_WIFI

#include "DisplaySettingsScene.h"
#include "Drawing.h"
#include "System.h"
#include "SystemScene.h"
#include "LatheUi.h"

static const char* layout_names[] = {
    "0 deg - Btns Bottom",   // rotation 0, buttons below
    "0 deg - Btns Top",      // rotation 0, buttons above
    "90 deg - Btns Right",    // rotation 1, buttons right
    "90 deg - Btns Left",     // rotation 1, buttons left
    "180 deg - Btns Bottom",  // rotation 2, buttons below
    "180 deg - Btns Top",     // rotation 2, buttons above
    "270 deg - Btns Left",    // rotation 3, buttons left
    "270 deg - Btns Right",   // rotation 3, buttons right
};
static const int n_layout_names = sizeof(layout_names) / sizeof(layout_names[0]);

void DisplaySettingsScene::onEntry(void* arg) {
    reDisplay();
}

void DisplaySettingsScene::onEncoder(int delta) {
    if (lathe_ui_enabled()) {
        reDisplay();
        return;
    }
    next_layout(delta);
    reDisplay();
}

void DisplaySettingsScene::onDialButtonPress() {
    activate_scene(&systemScene);
}

void DisplaySettingsScene::onRedButtonPress() {
    activate_scene(&systemScene);
}

void DisplaySettingsScene::onGreenButtonPress() {
    if (lathe_ui_enabled()) {
        reDisplay();
        return;
    }
    next_layout(1);
    reDisplay();
}

void DisplaySettingsScene::reDisplay() {
    if (lathe_ui_enabled()) {
        lathe_ui_detail_surface("DISPLAY");

        // M5Dial is mechanically fixed: portrait round panel, controls below.
        canvas.drawRoundRect(87, 70, 66, 58, 8, lathe_ui_blue());
        canvas.drawRoundRect(88, 71, 64, 56, 7, lathe_ui_blue());
        canvas.drawCircle(120, 99, 21, lathe_ui_muted());
        canvas.drawCircle(120, 99, 20, lathe_ui_muted());
        canvas.fillCircle(120, 99, 3, lathe_ui_blue());
        canvas.drawLine(108, 134, 132, 134, lathe_ui_muted());
        canvas.drawLine(108, 135, 132, 135, lathe_ui_muted());

        lathe_ui_fit_text("FIXED ORIENTATION", 120, 156, 168, lathe_ui_text(), SMALL, middle_center);
        centered_text("BUTTONS BOTTOM", 178, lathe_ui_blue(), TINY);
        lathe_ui_action_legends("BACK", "", "");
        refreshDisplay();
        return;
    }

    background();
    drawMenuTitle("Display");
    drawRect(55, 22, 130, 1, 0, DARKGREY);

    char idx_buf[12];
    snprintf(idx_buf, sizeof(idx_buf), "%d / %d", layout_num + 1, num_layouts);
    centered_text(idx_buf, 70, DARKGREY, TINY);

    // Layout name
    const char* name = (layout_num >= 0 && layout_num < n_layout_names)
                           ? layout_names[layout_num]
                           : "Unknown";
    centered_text(name, 100, WHITE, SMALL);

    centered_text("Turn dial or press green", 140, LIGHTGREY, TINY);

    drawButtonLegends("Back", "Rotate", "");
    refreshDisplay();
}

DisplaySettingsScene displaySettingsScene;

#endif  // USE_WIFI
