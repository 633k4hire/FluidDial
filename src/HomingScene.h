#include <stdint.h>

extern void detect_homing_info();
extern void set_axis_homed(int axis);
extern void clear_homed_axes();
extern bool is_axis_homed(int display_axis);
extern void set_homed_machine_mask(uint8_t xzc_mask);
