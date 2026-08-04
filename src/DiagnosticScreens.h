#pragma once

#include <cstdint>

// Render a read-only diagnostics preview into the display framebuffer.
// The operator's active scene is restored by diagnostic_restore_screen()
// after the HTTP response has copied the framebuffer.
bool diagnostic_render_screen(const char* id);
void diagnostic_restore_screen();

// Monotonically increases only after a requested fixture has completed its
// synchronous reDisplay()/refreshDisplay path.  The screen endpoint exposes
// this as an acknowledgement so capture tooling never guesses with sleeps.
uint32_t diagnostic_screen_revision();
