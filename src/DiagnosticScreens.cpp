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
extern Scene machineHealthScene;

void diagnostic_preview_main_menu(int selection);
void diagnostic_preview_main_menu_state(int encoded);
void diagnostic_step_main_menu(int delta);
void diagnostic_clear_main_menu_state();
void diagnostic_preview_homing(int selection);
void diagnostic_restore_homing_preview();
void diagnostic_preview_jog(int selection);
void diagnostic_restore_jog_preview();
void diagnostic_preview_probe(int selection);
void diagnostic_restore_probe_preview();
void diagnostic_preview_tools(int selection);
void diagnostic_restore_tools_preview();
void diagnostic_preview_about(int page);
void diagnostic_restore_about_preview();
void diagnostic_preview_machine_health(int selection);
void diagnostic_restore_machine_health_preview();
void diagnostic_preview_status(int fixture);
void diagnostic_restore_status_preview();
void diagnostic_preview_homing_state(int fixture);
void diagnostic_preview_probe_state(int fixture);
void diagnostic_preview_tool_defaults(int fixture);
void diagnostic_preview_files(int fixture);
void diagnostic_restore_files_preview();
void diagnostic_preview_macros(int fixture);
void diagnostic_restore_macros_preview();

#ifdef USE_WIFI
extern Scene wifiSetupScene;
extern Scene systemScene;
extern Scene brightnessScene;
extern Scene displaySettingsScene;
extern Scene transportScene;
extern Scene otaScene;
extern Scene firstBootScene;

void diagnostic_preview_system(int selection);
void diagnostic_restore_system_preview();
void diagnostic_preview_transport(int selection);
void diagnostic_restore_transport_preview();
void diagnostic_preview_first_boot(int selection);
void diagnostic_restore_first_boot_preview();
#endif

namespace {
    using Preview = void (*)(int);
    using Cleanup = void (*)();

    struct Screen {
        const char* id;
        Scene*      scene;
        Preview     preview;
        int         selection;
        Cleanup     cleanup;

        Screen(const char* screen_id,
               Scene*      screen_scene,
               Preview     screen_preview,
               int         screen_selection,
               Cleanup     screen_cleanup = nullptr) :
            id(screen_id),
            scene(screen_scene),
            preview(screen_preview),
            selection(screen_selection),
            cleanup(screen_cleanup) {}
    };

    Screen screens[] = {
        { "main-status", &menuScene, diagnostic_preview_main_menu, 0, diagnostic_clear_main_menu_state },
        { "main-homing", &menuScene, diagnostic_preview_main_menu, 1, diagnostic_clear_main_menu_state },
        { "main-jog", &menuScene, diagnostic_preview_main_menu, 2, diagnostic_clear_main_menu_state },
        { "main-probe", &menuScene, diagnostic_preview_main_menu, 3, diagnostic_clear_main_menu_state },
        { "main-tools", &menuScene, diagnostic_preview_main_menu, 4, diagnostic_clear_main_menu_state },
#ifdef USE_WMB_FSS
        { "main-files", &menuScene, diagnostic_preview_main_menu, 5, diagnostic_clear_main_menu_state },
#else
        { "main-files", &menuScene, diagnostic_preview_main_menu, 5, diagnostic_clear_main_menu_state },
#endif
        { "main-macros", &menuScene, diagnostic_preview_main_menu, 6, diagnostic_clear_main_menu_state },
        { "main-about", &menuScene, diagnostic_preview_main_menu, 7, diagnostic_clear_main_menu_state },
        { "main-carousel-next", &menuScene, diagnostic_step_main_menu, 1, diagnostic_clear_main_menu_state },
        { "main-connected-status", &menuScene, diagnostic_preview_main_menu_state, 0, diagnostic_clear_main_menu_state },
        { "main-connected-homing", &menuScene, diagnostic_preview_main_menu_state, 1, diagnostic_clear_main_menu_state },
        { "main-connected-jog", &menuScene, diagnostic_preview_main_menu_state, 2, diagnostic_clear_main_menu_state },
        { "main-connected-probe", &menuScene, diagnostic_preview_main_menu_state, 3, diagnostic_clear_main_menu_state },
        { "main-connected-tools", &menuScene, diagnostic_preview_main_menu_state, 4, diagnostic_clear_main_menu_state },
        { "main-connected-files", &menuScene, diagnostic_preview_main_menu_state, 5, diagnostic_clear_main_menu_state },
        { "main-connected-macros", &menuScene, diagnostic_preview_main_menu_state, 6, diagnostic_clear_main_menu_state },
        { "main-connected-about", &menuScene, diagnostic_preview_main_menu_state, 7, diagnostic_clear_main_menu_state },
        { "main-disconnected-status", &menuScene, diagnostic_preview_main_menu_state, 8, diagnostic_clear_main_menu_state },
        { "main-disconnected-homing", &menuScene, diagnostic_preview_main_menu_state, 9, diagnostic_clear_main_menu_state },
        { "main-disconnected-jog", &menuScene, diagnostic_preview_main_menu_state, 10, diagnostic_clear_main_menu_state },
        { "main-disconnected-probe", &menuScene, diagnostic_preview_main_menu_state, 11, diagnostic_clear_main_menu_state },
        { "main-disconnected-tools", &menuScene, diagnostic_preview_main_menu_state, 12, diagnostic_clear_main_menu_state },
        { "main-disconnected-files", &menuScene, diagnostic_preview_main_menu_state, 13, diagnostic_clear_main_menu_state },
        { "main-disconnected-macros", &menuScene, diagnostic_preview_main_menu_state, 14, diagnostic_clear_main_menu_state },
        { "main-disconnected-about", &menuScene, diagnostic_preview_main_menu_state, 15, diagnostic_clear_main_menu_state },
        { "status", &statusScene, nullptr, 0 },
        { "status-idle", &statusScene, diagnostic_preview_status, 0, diagnostic_restore_status_preview },
        { "status-cycle", &statusScene, diagnostic_preview_status, 1, diagnostic_restore_status_preview },
        { "status-hold", &statusScene, diagnostic_preview_status, 2, diagnostic_restore_status_preview },
        { "status-alarm", &statusScene, diagnostic_preview_status, 3, diagnostic_restore_status_preview },
        { "status-disconnected", &statusScene, diagnostic_preview_status, 4, diagnostic_restore_status_preview },
        { "health-overview", &machineHealthScene, diagnostic_preview_machine_health, 0, diagnostic_restore_machine_health_preview },
        { "health-alarm", &machineHealthScene, diagnostic_preview_machine_health, 1, diagnostic_restore_machine_health_preview },
        { "health-readiness", &machineHealthScene, diagnostic_preview_machine_health, 2, diagnostic_restore_machine_health_preview },
        { "health-connections", &machineHealthScene, diagnostic_preview_machine_health, 3, diagnostic_restore_machine_health_preview },
        { "health-alarm-preview", &machineHealthScene, diagnostic_preview_machine_health, 4, diagnostic_restore_machine_health_preview },
        { "health-encoder-fault", &machineHealthScene, diagnostic_preview_machine_health, 5, diagnostic_restore_machine_health_preview },
        { "home-all", &homingScene, diagnostic_preview_homing, 0, diagnostic_restore_homing_preview },
        { "home-x", &homingScene, diagnostic_preview_homing, 1, diagnostic_restore_homing_preview },
        { "home-z", &homingScene, diagnostic_preview_homing, 2, diagnostic_restore_homing_preview },
        { "home-unhomed", &homingScene, diagnostic_preview_homing_state, 0, diagnostic_restore_homing_preview },
        { "home-homed", &homingScene, diagnostic_preview_homing_state, 1, diagnostic_restore_homing_preview },
        { "jog-x", &multiJogScene, diagnostic_preview_jog, 0, diagnostic_restore_jog_preview },
        { "jog-z", &multiJogScene, diagnostic_preview_jog, 1, diagnostic_restore_jog_preview },
        { "jog-xz", &multiJogScene, diagnostic_preview_jog, 2, diagnostic_restore_jog_preview },
        { "probe-offset", &probingScene, diagnostic_preview_probe, 0, diagnostic_restore_probe_preview },
        { "probe-travel", &probingScene, diagnostic_preview_probe, 1, diagnostic_restore_probe_preview },
        { "probe-feed", &probingScene, diagnostic_preview_probe, 2, diagnostic_restore_probe_preview },
        { "probe-retract", &probingScene, diagnostic_preview_probe, 3, diagnostic_restore_probe_preview },
        { "probe-axis", &probingScene, diagnostic_preview_probe, 4, diagnostic_restore_probe_preview },
        { "probe-live", &probingScene, diagnostic_preview_probe_state, 1, diagnostic_restore_probe_preview },
        { "probe-success", &probingScene, diagnostic_preview_probe_state, 2, diagnostic_restore_probe_preview },
        { "probe-failure", &probingScene, diagnostic_preview_probe_state, 3, diagnostic_restore_probe_preview },
        { "tools-list", &toolchangeScene, diagnostic_preview_tools, 0, diagnostic_restore_tools_preview },
        { "tools-setup", &toolchangeScene, diagnostic_preview_tools, 1, diagnostic_restore_tools_preview },
        { "tools-touch-off", &toolchangeScene, diagnostic_preview_tools, 2, diagnostic_restore_tools_preview },
        { "tools-default-types", &toolchangeScene, diagnostic_preview_tool_defaults, 0, diagnostic_restore_tools_preview },
#ifdef USE_WMB_FSS
        { "files", &wmbFileSelectScene, nullptr, 0 },
        { "files-loading", &wmbFileSelectScene, diagnostic_preview_files, 1, diagnostic_restore_files_preview },
        { "files-empty", &wmbFileSelectScene, diagnostic_preview_files, 2, diagnostic_restore_files_preview },
        { "files-error", &wmbFileSelectScene, diagnostic_preview_files, 3, diagnostic_restore_files_preview },
#else
        { "files", &fileSelectScene, nullptr, 0 },
        { "files-loading", &fileSelectScene, diagnostic_preview_files, 1, diagnostic_restore_files_preview },
        { "files-empty", &fileSelectScene, diagnostic_preview_files, 2, diagnostic_restore_files_preview },
        { "files-error", &fileSelectScene, diagnostic_preview_files, 3, diagnostic_restore_files_preview },
#endif
        { "macros", &macroMenu, nullptr, 0 },
        { "macros-loading", &macroMenu, diagnostic_preview_macros, 1, diagnostic_restore_macros_preview },
        { "macros-empty", &macroMenu, diagnostic_preview_macros, 2, diagnostic_restore_macros_preview },
        { "macros-error", &macroMenu, diagnostic_preview_macros, 3, diagnostic_restore_macros_preview },
        { "about", &aboutScene, nullptr, 0 },
        { "about-device", &aboutScene, diagnostic_preview_about, 0, diagnostic_restore_about_preview },
        { "about-controls", &aboutScene, diagnostic_preview_about, 1, diagnostic_restore_about_preview },
#ifdef USE_WIFI
        { "connection", &wifiSetupScene, nullptr, 0 },
        { "system-restart", &systemScene, diagnostic_preview_system, 0, diagnostic_restore_system_preview },
        { "system-sleep", &systemScene, diagnostic_preview_system, 1, diagnostic_restore_system_preview },
        { "system-brightness", &systemScene, diagnostic_preview_system, 2, diagnostic_restore_system_preview },
        { "system-ota", &systemScene, diagnostic_preview_system, 3, diagnostic_restore_system_preview },
        { "brightness", &brightnessScene, nullptr, 0 },
        { "display", &displaySettingsScene, nullptr, 0 },
        { "transport-wired", &transportScene, diagnostic_preview_transport, 0, diagnostic_restore_transport_preview },
        { "transport-wifi", &transportScene, diagnostic_preview_transport, 1, diagnostic_restore_transport_preview },
        { "ota", &otaScene, nullptr, 0 },
        { "first-boot-wired", &firstBootScene, diagnostic_preview_first_boot, 0, diagnostic_restore_first_boot_preview },
        { "first-boot-wifi", &firstBootScene, diagnostic_preview_first_boot, 1, diagnostic_restore_first_boot_preview },
#endif
    };

    Scene*  saved_scene     = nullptr;
    Screen* rendered_screen = nullptr;
    uint32_t screen_revision = 0;
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
        rendered_screen = &screen;
        ++screen_revision;
        return true;
    }
    return false;
}

void diagnostic_restore_screen() {
    if (!saved_scene) {
        return;
    }
    if (rendered_screen && rendered_screen->cleanup) {
        rendered_screen->cleanup();
    }
    current_scene = saved_scene;
    saved_scene   = nullptr;
    rendered_screen = nullptr;
    request_redisplay();
}

uint32_t diagnostic_screen_revision() {
    return screen_revision;
}
