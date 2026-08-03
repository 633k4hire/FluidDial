#pragma once

// Render a read-only diagnostics preview into the display framebuffer.
// The operator's active scene is restored by diagnostic_restore_screen()
// after the HTTP response has copied the framebuffer.
bool diagnostic_render_screen(const char* id);
void diagnostic_restore_screen();
