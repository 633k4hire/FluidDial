#pragma once

#include "e4math.h"
#include <stdint.h>

enum class LatheToolType : uint8_t {
    Unset = 0,
    RightTurn,
    LeftTurn,
    DrillQuarterInch,
    BoringBar,
    Probe,
    Thread,
    Groove,
    Parting,
};

struct JogUiSnapshot {
    uint8_t selected_mask = 1;
    e4_t    step[3]       = {};
    bool    dynamic       = true;
    bool    moving        = false;
};

JogUiSnapshot jog_ui_snapshot();

LatheToolType lathe_tool_type(int station);
void          set_lathe_tool_type(int station, LatheToolType type);
const char*   lathe_tool_type_label(LatheToolType type);

