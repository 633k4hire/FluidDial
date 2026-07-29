#if defined(USE_WIFI) && defined(ARDUINO)

#include "SecurePairingScene.h"

#include "Drawing.h"
#include "SecureOtaService.h"
#include "System.h"

void SecurePairingScene::onEntry(void*) {
    reDisplay();
}

void SecurePairingScene::onDialButtonPress() {
    secure_ota_confirm_pairing_physical();
    reDisplay();
}

void SecurePairingScene::onRedButtonPress() {
    secure_ota_cancel_pairing();
    if (parent_scene()) pop_scene();
}

void SecurePairingScene::onPoll() {
    if (!secure_ota_pairing_pending()) {
        secure_ota_set_physical_window(false);
        if (parent_scene()) pop_scene();
    }
}

void SecurePairingScene::reDisplay() {
    background();
    centered_text("Pair FluidNC", round_display ? 28 : 18, WHITE, SMALL);
    centered_text("Compare this code", round_display ? 66 : 58, LIGHTGREY, TINY);
    centered_text(secure_ota_pairing_code(), round_display ? 98 : 92, CYAN, LARGE);
    centered_text("Match fluidnc.local", round_display ? 132 : 128, DARKGREY, TINY);
    centered_text("Press center dial to pair", round_display ? 154 : 154, GREEN, TINY);
    drawButtonLegends("Cancel", "", "Confirm");
    refreshDisplay();
}

SecurePairingScene securePairingScene;

#endif
