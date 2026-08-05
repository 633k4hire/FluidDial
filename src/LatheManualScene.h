// Copyright (c) 2026 Matthew Metzger
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Scene.h"

extern Scene& latheManualMenuScene;
extern Scene& latheAngleJogScene;
extern Scene& latheSpindleScene;
extern Scene& latheCPositionScene;
extern Scene& latheThreadProofScene;
extern Scene& latheFaceScene;
extern Scene& latheTurnScene;
extern Scene& latheChamferScene;
extern Scene& latheGrooveScene;
extern Scene& lathePeckScene;

void diagnostic_preview_lathe_manual(int selection);

struct LatheVectorMove {
    float x_command_mm = 0.0f;
    float z_mm         = 0.0f;
};

LatheVectorMove lathe_angle_vector(float path_mm, float angle_degrees, bool positive_slope, bool diameter_mode);
float           lathe_thread_pitch_mm(bool tpi_mode, float pitch_or_tpi);
float           lathe_thread_c_degrees(float z_travel_mm, float pitch_mm);
float           lathe_thread_planner_feed(float pitch_mm, float c_rpm);
