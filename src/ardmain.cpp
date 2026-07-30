// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "FluidNCModel.h"  // pendant_wait_for_fluidnc_ready()
#include "LatheModel.h"
#include "Scene.h"
#include "AboutScene.h"
#if defined(USE_M5) || defined(USE_LOVYANGFX)
#    include "BrightnessScene.h"
#endif

#ifdef USE_WIFI
#    include "WiFiConnection.h"
#    ifdef ARDUINO
#        include "SecureOtaService.h"
#    endif
#    include "WiFiSetupScene.h"
#    include "PeerLink.h"
#    include "ESPNowPairingScene.h"
#    include "OTAScene.h"
#    ifdef ARDUINO
#        include "SecurePairingScene.h"
#    endif
extern Scene firstBootScene;
static bool _wifi_initialized  = false;
static bool _first_boot_active = false;
static bool _ota_boot_mode     = false;  // booted into the dedicated OTA-only loop
#endif

extern void base_display();
extern void show_logo();
extern "C" bool fnc_rx_waiting();

extern const char* git_info;

extern AboutScene aboutScene;

#ifdef USE_WIFI
extern Scene menuScene;
extern WiFiSetupScene wifiSetupScene;

void first_boot_complete() {
    _first_boot_active = false;

    TransportMode transport = wifi_get_transport();
    if (transport == TransportMode::UART) {
        fnc_realtime(StatusReport);
        activate_scene(&menuScene);
    } else if (transport == TransportMode::ESPNOW) {
        if (!_wifi_initialized) {
            _wifi_initialized = true;
            espnow_init();
        }
        activate_scene(&espnowPairingScene, &firstBootScene);
    } else {
        activate_scene(&wifiSetupScene);
    }
}
#endif

void setup() {
    init_system();

#if defined(USE_M5) || defined(USE_LOVYANGFX)
    display.setBrightness(brightnessScene.getBrightness());
#else
    display.setBrightness(aboutScene.getBrightness());
#endif

    show_logo();
    delay_ms(1000);  // view the logo and wait for the debug port to connect

    base_display();

    dbg_printf("FluidNC Pendant %s\n", git_info);

#ifdef USE_WIFI
    if (wifi_ota_boot_requested()) {
        _ota_boot_mode = true;
        dbg_println("Booting into OTA update mode");
        activate_scene(&otaScene);
        return;
    }
#endif

#ifndef USE_WIFI
    // Bounded boot probe — discards stale bootloader noise, asks FluidNC
    // for a status report, waits up to 7 s for any RX byte. If it times
    // out the runtime recovery ladder in fnc_is_connected() takes over.
    pendant_wait_for_fluidnc_ready(7000);
#else
    if (wifi_use_uart_mode()) {
        pendant_wait_for_fluidnc_ready(7000);
    }
#endif

    extern Scene* initMenus();
    Scene* menu = initMenus();

#ifdef USE_WIFI
    if (wifi_use_espnow_mode() && !_wifi_initialized) {
        _wifi_initialized = true;
        espnow_init();
    }
#endif

    // For debugging certain views without setting WiFi/UART transports
#if defined(DEV_SKIP_TO_ESPNOW_PAIRING) && defined(USE_WIFI)
    activate_scene(&espnowPairingScene);
#elif defined(DEV_SKIP_TO_SCENE)
    extern Scene DEV_SKIP_TO_SCENE;
    activate_scene(&DEV_SKIP_TO_SCENE);
#elif defined(USE_WIFI)
    // On first boot (no transport mode saved yet) show the setup wizard
    // immediately — before the main menu — so the user is never silently
    // dropped into WiFi mode.  first_boot_complete() handles the
    // transition once the user picks a transport.
    if (wifi_is_first_boot()) {
        _first_boot_active = true;
        activate_scene(&firstBootScene);
    } else if (wifi_use_espnow_mode() && !espnow_has_saved_pairing()) {
        activate_scene(&espnowPairingScene, &wifiSetupScene);
    } else if (!wifi_use_uart_mode() && !wifi_use_espnow_mode()
               && !wifi_load_config().valid) {
        // WiFi mode selected but no credentials configured yet.
        // Land in WiFiSetupScene; wifi_init() (deferred to loop) will
        // auto-start AP so the user can set credentials via browser.
        activate_scene(&wifiSetupScene);
    } else {
        activate_scene(menu);
    }
#else
    activate_scene(menu);
#endif

#if defined(USE_WIFI)
    if (wifi_use_uart_mode() && !_first_boot_active) {
        // The controller keeps its report interval when only the M5 restarts.
        // Discard reports accumulated while the display was initializing,
        // slow the persistent stream before entering loop(), then ask for one
        // fresh report that can safely be parsed now current_scene is valid.
        flush_fnc_rx(50);
        resetFlowControl();
        send_line("$RI=1000", 500);
        request_status_report();
    }
#endif
}

void loop() {
#ifdef USE_WIFI
    if (_ota_boot_mode) {
        // Drive the OTA state machine
        wifi_poll();
        dispatch_events();
        service_redisplay();
        return;
    }
    if (!_first_boot_active) {
        if (!_wifi_initialized) {
            // Defer transport init until after setup() returned + 1st render is
            // complete — ensures sprite caches allocate before WiFi/ESP-NOW consumes heap.
            _wifi_initialized = true;
            if (wifi_use_espnow_mode()) {
                espnow_init();
            } else {
                wifi_init();
            }
        }
        if (wifi_use_espnow_mode()) {
            espnow_poll();
        } else {
            wifi_poll();
        }
#ifdef ARDUINO
        if (secure_ota_pairing_pending() && current_scene != &securePairingScene) {
            // A request from the paired FluidNC workflow is enough to display
            // the comparison code. The operator authorizes it with the M5Dial
            // center button; no menu navigation is required.
            push_scene(&securePairingScene);
        }
        if (secure_ota_update_active()) {
            // OTA owns the pendant while the inactive slot is being written.
            // Do not dispatch controls or forward machine commands.
            operator_note_ota_active(true);
            service_redisplay();
            return;
        }
        operator_note_ota_active(false);
#endif
    }
#endif
#if defined(USE_WIFI) && defined(ARDUINO)
    if (wifi_use_uart_mode()) {
        static bool last_uart_connected = false;
        static uint32_t next_uart_pairing_at = 0;
        const bool uart_connected = fnc_is_connected();
        if (uart_connected && !last_uart_connected) {
            secure_ota_note_uart_link_reset();
            next_uart_pairing_at = 0;
        }
        last_uart_connected = uart_connected;
        if (uart_connected &&
            static_cast<int32_t>(millis() - next_uart_pairing_at) >= 0) {
            char command[192];
            if (secure_ota_uart_pairing_request(command, sizeof(command))) {
                send_line(command, 750);
            }
            next_uart_pairing_at = millis() + 3000U;
        }
    }
#endif
    // fnc_poll() drains ONE byte per call. The WiFi transport can refill its
    // ring buffer with hundreds of bytes per loop iteration (a kernel TCP
    // window worth — preferences.json bursts in ~5 KB). Drain a chunk per
    // tick so the ring buffer can't overflow and silently drop bytes,
    // which corrupts the streaming JSON parser mid-document.
    //
    // Drain all pending data, but stop when RX is empty to avoid 
    // unnecessary Wi-Fi polling and reduce idle-loop jitter that can make small jog movements choppy.
    for (int i = 0; i < 4096; i++) {
        fnc_poll();
        if (!fnc_rx_waiting()) {
            break;
        }
    }
    lathe_poll_status();
    dispatch_events();  // Handle dial, touch, buttons
    service_redisplay();
#if defined(USE_WIFI) && defined(ARDUINO)
    // Reaching the end of the normal application loop proves that display,
    // configuration, event dispatch, and redisplay initialization completed.
    // Secure OTA still independently requires NVS identity and HTTP service
    // health before accepting a pending image.
    secure_ota_note_application_healthy();
#endif
}
