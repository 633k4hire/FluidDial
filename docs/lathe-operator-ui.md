# FluidDial Lathe Operator UI

FluidDial has a lathe operator profile for FluidNC machines that report lathe
support through `ESP421`.

The profile is automatic. On connection, and when entering lathe-aware scenes,
FluidDial sends:

```gcode
[ESP421]
```

If FluidNC returns `cmd:"421"` with `Lathe enabled=true`, FluidDial switches the
Status, Jog, Home, Probe, and Tools scenes to the lathe profile. If `ESP421` is
absent, errors, or reports `Lathe enabled=false`, FluidDial keeps the existing
generic X/Y/Z UI active. When `ESP421` is absent, FluidDial caches that result
for the current connection and does not keep re-probing from generic scenes;
reconnect to probe again after changing controller firmware or config.

## Axis Mapping

The lathe profile presents three operator axes:

| Display slot | Operator axis | FluidNC machine axis | Config query |
| --- | --- | --- | --- |
| 0 | X | `X` / axis 0 | `$/axes/x/...` |
| 1 | Z | `Z` / axis 2 | `$/axes/z/...` |
| 2 | C | `C` / axis 5 | `$/axes/c/...` |

Jog commands, DRO values, limit indicators, probe commands, and homing commands
use the active profile mapping. Generic machines keep the original X/Y/Z mapping.

## Jog-Screen Any-Angle Mode

The lathe Jog screen has an integrated full-circle X/Z vector mode. Touch the
center of Jog to open **Jog Mode**, then use the red button as a deliberate
two-stage control:

1. First red press enters `SELECT ANGLE`.
2. Turn the M5 Dial to choose `0-359` degrees in one-degree increments.
3. The second red press arms that angle when FluidNC is Idle and X/Z are homed.
4. Press the M5 Dial center button to return to Jog with the angle active.
5. Reopen Jog Mode and press red once to disarm.

The perimeter indicator appears on both Jog Mode and Jog while active. The
convention is `0 deg = +Z/right`, `90 deg = +X/up`, `180 deg = -Z`, and
`270 deg = -X`. Green still toggles Dynamic/Precise behavior. The selected
angle and the normal per-axis distance digits are remembered in NVS, but
selecting and armed states are never persisted across a restart.

Disarmed mode is the unchanged normal Jog screen: X, Z, or a deliberate
multi-axis selection can be chosen independently. Armed mode still keeps one
visible X or Z counting/reference axis; top/bottom switches that reference and
left/right adjusts its normal distance digit. Each Dial detent is exact in the
selected axis's displayed units, while the other axis is coupled so the
physical move follows the armed angle. At a cardinal angle, a zero-motion
reference axis is skipped in favor of the axis that actually moves. C remains
unavailable while the X/Z angle is armed.

G7 converts an X count from commanded diameter to physical radius before
calculating the coupled Z component, then doubles commanded X so the physical
toolpath retains the displayed angle. Fractional X/Z residuals are carried
across finite detents to prevent rounding drift. Alarm, link loss, controller
reset, units change, G7/G8 change, leaving Jog, or a rejected command disarms
the vector. Merely selecting or arming never sends motion, and authenticated
diagnostic previews never alter the live Jog selection.

## Lathe Dashboard

The Status scene becomes a lathe dashboard when the lathe profile is active. It
shows:

- X/Z/C DRO values.
- FluidNC state.
- Active lathe tool, including `T5 Probe`.
- Spindle speed mode from FluidNC, such as `G96` or `G97`.
- Feed mode from FluidNC, such as `G93`, `G94`, or `G95`.
- Effective commanded RPM and measured RPM when feedback is available.
- Encoder state, including disabled, no-capture, stale, and fault states.
- Diameter/radius mode.

The dashboard only surfaces threading feedback health. It does not enable
threading, modify FluidNC threading settings, or override machine safety
configuration.

## ESP421 Status Model

FluidDial parses the `ESP421` JSON `data` array of `{id,value}` objects and
stores the current lathe state. The fields consumed by the UI include:

- `Lathe enabled`
- `Spindle speed mode`
- `Diameter mode`
- `Feed mode`
- `Programmed S`
- `Effective RPM`
- `CSS clamp RPM`
- `Minimum CSS diameter mm`
- `Encoder enabled`
- `Encoder capture active`
- `Encoder pulses/rev`
- `Active lathe tool`
- `Lathe tool X offset mm`
- `Lathe tool Z offset mm`
- `Tool nose radius mm`
- `Feedback measured RPM`
- `Feedback index`
- `Feedback angular position`
- `Feedback angular rev`
- `Feedback revolution count`
- `Feedback stale`
- `Feedback fault`

## Five-Tool Turret UI

The Tools scene becomes a fixed lathe tool panel with `T1` through `T5`.

- `T5` is labeled `T5 Probe`.
- The default tool action requires confirmation.
- Confirming a tool change sends `$ME`, `Tn`, and `M6`; waits for the `M6`
  acknowledgement; holds the shared DLC32 stepper enable for three additional
  seconds so the turret can seat against its ratchet; then sends `$MD`.
- After confirmation, FluidDial marks the tool change pending until a refreshed
  `ESP421` report shows the requested active tool or FluidNC enters alarm.
- While a lathe command is pending, duplicate tool, setup, touch-off, and
  logical-select actions are ignored and the button legend shows `Wait`.
- During a pending tool change, FluidDial refreshes `ESP421` periodically. It
  does not resend `Tn` or `M6` automatically.
- If a lathe command times out or FluidNC enters alarm, the Tools scene shows a
  recoverable error and exposes `Clear`. Clear only removes the pendant-side
  pending/error state; the operator must verify the physical turret and FluidNC
  state before sending another tool command.
- Holding the touch area on the tool list sends the optional logical select
  action `M61Qn`. This is intentionally not the default action.
- FluidDial refreshes `ESP421` after tool actions.

## Tool Setup

The Tool Setup page sends FluidNC lathe tool data with `ESP422`:

```gcode
[ESP422]T=1 GX=0.0000 GZ=0.0000 WX=0.0000 WZ=0.0000 NR=0.0000 O=0
```

Supported fields:

| Field | Meaning |
| --- | --- |
| `GX` | Geometry X offset in mm |
| `GZ` | Geometry Z offset in mm |
| `WX` | Wear X offset in mm |
| `WZ` | Wear Z offset in mm |
| `NR` | Tool nose radius in mm |
| `O` | Insert orientation |

FluidDial keeps local editor defaults per tool so values remain convenient while
the operator is working. FluidNC remains the authority for saved tool data.

## Manual Touch-Off

The Touch Off page sends manual tool touch-off data with `ESP423`:

```gcode
[ESP423]T=1 MX=0.0000 RX=0.0000 MODE=diameter MZ=0.0000 RZ=0.0000
```

FluidDial reads the current machine X/Z positions from the mapped lathe DRO,
converts them to millimeters, asks the operator to confirm the tool, machine
X/Z, reference X/Z, and diameter/radius mode, then sends the operator-entered
X/Z reference values. X touch-off mode defaults from `ESP421` diameter/radius
state and can be toggled before applying.

On `ESP423` success, FluidDial reports touch-off success and refreshes `ESP421`.
On error or timeout, FluidDial shows the failure and does not imply the offset
was applied.

The existing `G38.2` probing scene remains available and is profile-aware. V3
does not automatically convert a probe move into an `ESP423` update; the
operator still confirms the manual touch-off values.

## Manual Lathe Assistant

On a lathe, the main-menu Macros position becomes **Manual** and opens nine
operator helpers: Spindle, C Position, Angle, Thread Proof, Face, Turn,
Chamfer, Groove, and Peck. Generic machines retain the original Macros entry.

Custom G-code macros remain available from the **first `Macros` entry in the
root SD Files screen**. Select it with the dial or touch it, then use the green
button to open the existing macro menu. It is a virtual Files entry and is not
sent to FluidNC as a filename.

- **Angle Jog** treats the Dial as an electronic compound slide. Angles are
  measured from Z, presets include 20/35/45/80 degrees, and G7 diameter mode
  doubles the commanded X component while preserving the requested radial
  path. Arming requires confirmation. Dial detents provide precise moves and
  the red/green buttons provide hold-to-jog motion; releasing a button cancels
  its jog while leaving the compound armed.
- **Spindle** edits 0-500 RPM in 10 RPM steps, capped by the C-stepper
  spindle's independent `maximum_rpm`. Touch selects CW/CCW, green
  confirms start or apply, and red always sends M5 immediately. Opposite
  direction is blocked until FluidNC reports the spindle stopped. The panel
  identifies the `C_STEPPER` drive and distinguishes open-loop commanded speed
  from encoder-measured speed.
- **C Position** provides 1/5/15/45/90 degree detent presets. It uses the live
  C-axis positioning ceiling and the same duration-based feed policy as normal
  precise jogging, rather than a hardcoded 1 RPM. It requires a
  confirmed arm, reports the 0.225 degree microstep quantum, and carries
  fractional residuals between detents so rounding does not accumulate drift.
- **Thread Proof** uses a single temporary-modal coordinated C/Z jog. It
  computes C degrees from pitch and Z travel, caps requested RPM using the
  configured C-axis maximum rate, and requires the C-stepper spindle to be
  stopped. It does not use or enable G33/G76/G95 and is intentionally limited
  to an air, marker, indicator, or very light single-pass proof.
- **Face, Turn, and Chamfer** generate one confirmed relative helper move.
  **Groove** sequences a plunge and retract. **Peck** sequences successively
  deeper Z pecks with a full retract between pecks. Each step is an independent
  `$J` command and the next step is not sent until FluidNC returns Idle.

Every helper uses temporary `$J=G91G21...` motion so it does not change the
program's persistent units or distance mode. Alarm, link loss, scene exit,
touch cancel, or red cancel stops the active helper and prevents remaining
sequence steps from being issued.

## Validation

Run the local protocol harness for fast parser/model checks:

```sh
python tools/lathe_protocol_harness.py
```

The harness checks:

- Generic fallback when `ESP421` is unavailable, malformed, unsupported, or
  reports `Lathe enabled=false`.
- X/Z/C profile mapping to FluidNC machine axes 0/2/5 and config paths
  `$/axes/x`, `$/axes/z`, and `$/axes/c`.
- `ESP421` field parsing for status, modes, active tool, offsets, RPM feedback,
  encoder/index/stale/fault state, spindle state, shared-chuck mode, threading
  readiness, and diameter mode.
- Manual-lathe angle-vector and thread-proof calculations, including G7/G8,
  metric/TPI conversion, and legacy `ESP421` fallback behavior.
- Full-circle Jog angle vectors, G7/G8 X scaling, direction reversal,
  wraparound, and fractional residual accumulation.
- `ESP422` and `ESP423` ok/error command response handling.
- Lathe command lifecycle behavior for M6 success, M6 timeout, alarm failure,
  ESP422 success, and ESP423 timeout.

Validated build targets for this implementation:

```sh
pio run -e m5dial
pio run -e maijker_m5dial
pio run -e cyddial
```

`maijker_m5dial` is the dedicated wired M5Dial image for the Maijker XZACt
mini-lathe. It starts with the X/Z/C operator mapping, uses M5Dial Port A at
1,000,000 baud, locks the runtime transport to UART without starting the Wi-Fi
stack, and does not enable the USB-to-FluidNC debug command bridge.

The native Windows target was attempted but could not compile in the local
environment because `g++` was not available on PATH.
