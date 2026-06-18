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
- Confirming a tool change sends `Tn`, then `M6`.
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
converts them to millimeters, and sends the operator-entered X/Z reference
values. X touch-off mode defaults from `ESP421` diameter/radius state and can be
toggled before applying.

The existing `G38.2` probing scene remains available and is profile-aware. V1
does not automatically convert a probe move into an `ESP423` update; the
operator still confirms the manual touch-off values.

## Validation

Validated build targets for this implementation:

```sh
pio run -e m5dial
pio run -e cyddial
```

The native Windows target was attempted but could not compile in the local
environment because `g++` was not available on PATH.
