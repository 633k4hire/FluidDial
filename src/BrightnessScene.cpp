// 2026 - Figamore

#include "BrightnessScene.h"
#include "Drawing.h"
#include "System.h"
#include "SystemScene.h"
#include "LatheUi.h"

static constexpr int MIN_BRIGHTNESS_PCT = 3;  // ~8/255

void BrightnessScene::onEntry(void* arg) {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
        _brightness = std::min(100, std::max(MIN_BRIGHTNESS_PCT, _brightness));
    }
    reDisplay();
}

void BrightnessScene::onEncoder(int delta) {
    if (delta > 0 && _brightness < 100) {
        _brightness = std::min(100, _brightness + 1);
        display.setBrightness(getBrightness());
        setPref("brightness", _brightness);
    }
    if (delta < 0 && _brightness > MIN_BRIGHTNESS_PCT) {
        _brightness = std::max(MIN_BRIGHTNESS_PCT, _brightness - 1);
        display.setBrightness(getBrightness());
        setPref("brightness", _brightness);
    }
    reDisplay();
}

void BrightnessScene::onDialButtonPress() { activate_scene(&systemScene); }
void BrightnessScene::onRedButtonPress()  { activate_scene(&systemScene); }

void BrightnessScene::reDisplay() {
    if (lathe_ui_enabled()) {
        lathe_ui_detail_surface("BRIGHTNESS");

        constexpr int CX = 120;
        constexpr int CY = 114;
        constexpr int R  = 48;
        canvas.drawCircle(CX, CY, R, lathe_ui_panel_alt());
        canvas.drawCircle(CX, CY, R - 1, lathe_ui_panel_alt());
        int sweep = 30 + (_brightness * 300) / 100;
        canvas.drawArc(CX, CY, R, R - 5, 30, sweep, lathe_ui_blue());
        canvas.drawArc(CX, CY, R - 6, R - 8, 30, sweep, lathe_ui_blue());

        // Compact sun mark keeps the center recognizable without raster assets.
        canvas.drawCircle(CX, CY - 18, 5, lathe_ui_amber());
        canvas.drawCircle(CX, CY - 18, 4, lathe_ui_amber());
        canvas.drawLine(CX, CY - 29, CX, CY - 25, lathe_ui_amber());
        canvas.drawLine(CX + 1, CY - 29, CX + 1, CY - 25, lathe_ui_amber());
        canvas.drawLine(CX, CY - 11, CX, CY - 7, lathe_ui_amber());
        canvas.drawLine(CX + 1, CY - 11, CX + 1, CY - 7, lathe_ui_amber());
        canvas.drawLine(CX - 11, CY - 18, CX - 7, CY - 18, lathe_ui_amber());
        canvas.drawLine(CX - 11, CY - 17, CX - 7, CY - 17, lathe_ui_amber());
        canvas.drawLine(CX + 7, CY - 18, CX + 11, CY - 18, lathe_ui_amber());
        canvas.drawLine(CX + 7, CY - 17, CX + 11, CY - 17, lathe_ui_amber());

        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", _brightness);
        centered_text(buf, 125, lathe_ui_text(), MEDIUM);
        centered_text("DIAL TO ADJUST", 177, lathe_ui_muted(), TINY);
        lathe_ui_action_legends("BACK", "", "");
        refreshDisplay();
        return;
    }

    background();
    centered_text("Brightness", 40, WHITE, SMALL);
    drawRect(45, 52, 150, 1, 0, DARKGREY);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _brightness);
    centered_text(buf, 100, WHITE, SMALL);

    centered_text("Turn dial to adjust", 140, LIGHTGREY, TINY);

    drawButtonLegends("Back", "", "");
    refreshDisplay();
}

int BrightnessScene::getBrightness() {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
        _brightness = std::min(100, std::max(MIN_BRIGHTNESS_PCT, _brightness));
    }
    return (_brightness * 255) / 100;
}

BrightnessScene brightnessScene;
