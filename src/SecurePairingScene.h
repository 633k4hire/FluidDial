#pragma once

#if defined(USE_WIFI) && defined(ARDUINO)

#include "Scene.h"

class SecurePairingScene : public Scene {
public:
    SecurePairingScene() : Scene("Secure Pairing") {}

    void onEntry(void* arg = nullptr) override;
    void onDialButtonPress() override;
    void onRedButtonPress() override;
    void onPoll() override;
    void reDisplay() override;
};

extern SecurePairingScene securePairingScene;

#endif
