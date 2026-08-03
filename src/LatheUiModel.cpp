#include "LatheUiModel.h"

#include "NVS.h"
#include <cstdio>

namespace {
constexpr int ToolCount = 5;

LatheToolType s_tool_types[ToolCount] = {
    LatheToolType::RightTurn,
    LatheToolType::LeftTurn,
    LatheToolType::DrillQuarterInch,
    LatheToolType::BoringBar,
    LatheToolType::Probe,
};

bool         s_loaded = false;
nvs_handle_t s_prefs {};

bool valid_type(int value) {
    return value >= static_cast<int>(LatheToolType::Unset) && value <= static_cast<int>(LatheToolType::Parting);
}

void ensure_loaded() {
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    s_prefs  = nvs_init("Tools");
    if (!s_prefs) {
        return;
    }
    for (int station = 1; station <= ToolCount; ++station) {
        char key[16];
        snprintf(key, sizeof(key), "LatheType%d", station);
        int value = static_cast<int>(s_tool_types[station - 1]);
        nvs_get_i32(s_prefs, key, &value);
        if (valid_type(value)) {
            s_tool_types[station - 1] = static_cast<LatheToolType>(value);
        }
    }
}
}

LatheToolType lathe_tool_type(int station) {
    ensure_loaded();
    if (station < 1 || station > ToolCount) {
        return LatheToolType::Unset;
    }
    return s_tool_types[station - 1];
}

void set_lathe_tool_type(int station, LatheToolType type) {
    ensure_loaded();
    if (station < 1 || station > ToolCount || !valid_type(static_cast<int>(type))) {
        return;
    }
    s_tool_types[station - 1] = type;
    if (s_prefs) {
        char key[16];
        snprintf(key, sizeof(key), "LatheType%d", station);
        nvs_set_i32(s_prefs, key, static_cast<int>(type));
    }
}

const char* lathe_tool_type_label(LatheToolType type) {
    switch (type) {
        case LatheToolType::RightTurn: return "R TURN";
        case LatheToolType::LeftTurn: return "L TURN";
        case LatheToolType::DrillQuarterInch: return "1/4 DRILL";
        case LatheToolType::BoringBar: return "BORING BAR";
        case LatheToolType::Probe: return "PROBE";
        case LatheToolType::Thread: return "THREAD";
        case LatheToolType::Groove: return "GROOVE";
        case LatheToolType::Parting: return "PARTING";
        case LatheToolType::Unset: return "UNSET";
    }
    return "UNSET";
}
