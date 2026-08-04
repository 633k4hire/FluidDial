#pragma once

#include "Scene.h"

class MachineHealthScene : public Scene {
public:
    MachineHealthScene() : Scene("Health", 4) {}

    void onEntry(void* arg = nullptr) override;
    void onEncoder(int delta) override;
    void onDialButtonPress() override;
    void onRedButtonPress() override;
    void onGreenButtonPress() override;
    void onTouchClick() override;
    void onStateChange(state_t old_state) override;
    void onDROChange() override;
    void onLimitsChange() override;
    void onPoll() override;
    void reDisplay() override;

    void diagnosticPreview(int selection);

private:
    enum class Page : uint8_t {
        Overview,
        Alarm,
        Readiness,
        Connections,
    };

    enum class Preview : uint8_t {
        Live,
        HomingAlarm,
        EncoderFault,
    };

    Page     _page = Page::Overview;
    Preview  _preview = Preview::Live;
    uint32_t _last_refresh_ms = 0;

    void nextPage(int delta);
    void drawHeader(const char* title, int title_color = WHITE);
    void drawRow(int y, const char* label, const char* value, int value_color = WHITE);
    void drawOverview();
    void drawAlarm();
    void drawReadiness();
    void drawConnections();
};

extern MachineHealthScene machineHealthScene;
