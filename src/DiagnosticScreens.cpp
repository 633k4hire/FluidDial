#include "DiagnosticScreens.h"

#include "Scene.h"

#include <cstring>

extern Scene menuScene;
extern Scene statusScene;
extern Scene homingScene;
extern Scene multiJogScene;
extern Scene probingScene;
extern Scene toolchangeScene;
extern Scene macroMenu;
#ifdef USE_WMB_FSS
extern Scene wmbFileSelectScene;
#else
extern Scene fileSelectScene;
#endif
extern Scene aboutScene;

void diagnostic_preview_main_menu(int selection);
void diagnostic_preview_homing(int selection);
void diagnostic_preview_jog(int selection);
void diagnostic_preview_probe(int selection);
void diagnostic_preview_tools(int selection);

#ifdef USE_WIFI
extern Scene wifiSetupScene;
extern Scene systemScene;
extern Scene brightnessScene;
extern Scene displaySettingsScene;
extern Scene transportScene;
extern Scene otaScene;
extern Scene firstBootScene;

void diagnostic_preview_system(int selection);
void diagnostic_preview_transport(int selection);
void diagnostic_preview_first_boot(int selection);
#endif

namespace {
    using Preview = void (*)(int);

    struct Screen {
        const char* id;
        Scene*      scene;
        Preview     preview;
        int         selection;
    };

    Screen screens[] = {
        { "main-status", &menuScene, diagnostic_preview_main_menu, 0 },
        { "main-homing", &menuScene, diagnostic_preview_main_menu, 1 },
        { "main-jog", &menuScene, diagnostic_preview_main_menu, 2 },
        { "main-probe", &menuScene, diagnostic_preview_main_menu, 3 },
        { "main-tools", &menuScene, diagnostic_preview_main_menu, 4 },
#ifdef USE_WMB_FSS
        { "main-files", &menuScene, diagnostic_preview_main_menu, 5 },
#else
        { "main-files", &menuScene, diagnostic_preview_main_menu, 5 },
#endif
        { "main-macros", &menuScene, diagnostic_preview_main_menu, 6 },
        { "main-about", &menuScene, diagnostic_preview_main_menu, 7 },
        { "status", &statusScene, nullptr, 0 },
        { "home-all", &homingScene, diagnostic_preview_homing, 0 },
        { "home-x", &homingScene, diagnostic_preview_homing, 1 },
        { "home-z", &homingScene, diagnostic_preview_homing, 2 },
        { "jog-x", &multiJogScene, diagnostic_preview_jog, 0 },
        { "jog-z", &multiJogScene, diagnostic_preview_jog, 1 },
        { "jog-xz", &multiJogScene, diagnostic_preview_jog, 2 },
        { "probe-offset", &probingScene, diagnostic_preview_probe, 0 },
        { "probe-travel", &probingScene, diagnostic_preview_probe, 1 },
        { "probe-feed", &probingScene, diagnostic_preview_probe, 2 },
        { "probe-retract", &probingScene, diagnostic_preview_probe, 3 },
        { "probe-axis", &probingScene, diagnostic_preview_probe, 4 },
        { "tools-list", &toolchangeScene, diagnostic_preview_tools, 0 },
        { "tools-setup", &toolchangeScene, diagnostic_preview_tools, 1 },
        { "tools-touch-off", &toolchangeScene, diagnostic_preview_tools, 2 },
#ifdef USE_WMB_FSS
        { "files", &wmbFileSelectScene, nullptr, 0 },
#else
        { "files", &fileSelectScene, nullptr, 0 },
#endif
        { "macros", &macroMenu, nullptr, 0 },
        { "about", &aboutScene, nullptr, 0 },
#ifdef USE_WIFI
        { "connection", &wifiSetupScene, nullptr, 0 },
        { "system-restart", &systemScene, diagnostic_preview_system, 0 },
        { "system-sleep", &systemScene, diagnostic_preview_system, 1 },
        { "system-brightness", &systemScene, diagnostic_preview_system, 2 },
        { "system-ota", &systemScene, diagnostic_preview_system, 3 },
        { "brightness", &brightnessScene, nullptr, 0 },
        { "display", &displaySettingsScene, nullptr, 0 },
        { "transport-wired", &transportScene, diagnostic_preview_transport, 0 },
        { "transport-wifi", &transportScene, diagnostic_preview_transport, 1 },
        { "ota", &otaScene, nullptr, 0 },
        { "first-boot-wired", &firstBootScene, diagnostic_preview_first_boot, 0 },
        { "first-boot-wifi", &firstBootScene, diagnostic_preview_first_boot, 1 },
#endif
    };

    Scene* saved_scene = nullptr;
}

bool diagnostic_render_screen(const char* id) {
    if (!id || !id[0] || saved_scene) {
        return false;
    }
    for (auto& screen : screens) {
        if (strcmp(id, screen.id) != 0) {
            continue;
        }
        saved_scene   = current_scene;
        current_scene = screen.scene;
        if (screen.preview) {
            screen.preview(screen.selection);
        } else {
            screen.scene->reDisplay();
        }
        return true;
    }
    return false;
}

void diagnostic_restore_screen() {
    if (!saved_scene) {
        return;
    }
    current_scene = saved_scene;
    saved_scene   = nullptr;
    request_redisplay();
}
