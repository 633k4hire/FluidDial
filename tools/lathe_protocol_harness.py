#!/usr/bin/env python3
"""Local checks for FluidDial's lathe JSON contract.

This harness mirrors the small protocol surface FluidDial consumes from FluidNC:
ESP421 status data, ESP422/ESP423 command results, and the X/Z/C operator axis
profile. It intentionally avoids firmware dependencies so it can run anywhere
Python is available.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_PROFILE = [
    {"slot": 0, "letter": "X", "machine_axis": 0, "config_path": "$/axes/x"},
    {"slot": 1, "letter": "Y", "machine_axis": 1, "config_path": "$/axes/y"},
    {"slot": 2, "letter": "Z", "machine_axis": 2, "config_path": "$/axes/z"},
]

LATHE_PROFILE = [
    {"slot": 0, "letter": "X", "machine_axis": 0, "config_path": "$/axes/x"},
    {"slot": 1, "letter": "Z", "machine_axis": 2, "config_path": "$/axes/z"},
    {"slot": 2, "letter": "C", "machine_axis": 5, "config_path": "$/axes/c"},
]


@dataclass
class LatheStatus:
    known: bool = False
    available: bool = False
    enabled: bool = False
    spindle_state: str = ""
    shared_chuck_mode: str = ""
    spindle_drive: str = ""
    spindle_commanded_rpm: float = 0.0
    spindle_open_loop_rpm: float = 0.0
    spindle_maximum_rpm: float = 0.0
    spindle_steps_rev: int = 0
    c_position_dead_reckoned: bool = False
    threading_enabled: bool = False
    threading_feedback_ready: bool = False
    spindle_speed_mode: str = ""
    diameter_mode: bool = False
    feed_mode: str = ""
    programmed_s: float = 0.0
    effective_rpm: float = 0.0
    css_clamp_rpm: float = 0.0
    min_css_diameter_mm: float = 0.0
    encoder_enabled: bool = False
    encoder_capture: bool = False
    encoder_pulses_rev: int = 0
    active_tool: int = 0
    tool_x_offset_mm: float = 0.0
    tool_z_offset_mm: float = 0.0
    tool_nose_radius_mm: float = 0.0
    feedback_rpm_known: bool = False
    feedback_rpm: float = 0.0
    feedback_index: bool = False
    feedback_angular_pos: bool = False
    feedback_angular_known: bool = False
    feedback_angular_rev: float = 0.0
    feedback_rev_count: int = 0
    feedback_stale: bool = False
    feedback_fault: bool = False


@dataclass
class CommandResult:
    command: int
    known: bool
    ok: bool
    message: str


@dataclass
class CommandState:
    command: int = 0
    known: bool = False
    ok: bool = False
    pending: bool = False
    timed_out: bool = False
    recoverable: bool = False
    target_tool: int = 0
    message: str = ""
    started_ms: int = 0
    updated_ms: int = 0
    last_refresh_ms: int = 0


M6_TIMEOUT_MS = 30_000
COMMAND_TIMEOUT_MS = 8_000
STILL_WAITING_MS = 5_000
REFRESH_MS = 1_000


def parse_bool(value: Any) -> bool:
    return str(value).lower() in {"true", "1", "yes", "on", "enabled", "g7", "diameter"}


def parse_float(value: Any) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def parse_int(value: Any) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def has_number(value: Any) -> bool:
    try:
        float(value)
        return True
    except (TypeError, ValueError):
        return False


def parse_esp421(payload: str) -> LatheStatus:
    try:
        document = json.loads(payload)
    except json.JSONDecodeError:
        return LatheStatus(known=True, available=False)

    if str(document.get("cmd")) != "421" or not isinstance(document.get("data"), list):
        return LatheStatus(known=True, available=False)

    status = LatheStatus(known=True, available=True)
    for item in document["data"]:
        if not isinstance(item, dict):
            continue
        key = item.get("id")
        value = item.get("value", "")

        if key == "Lathe enabled":
            status.enabled = parse_bool(value)
        elif key == "Spindle state":
            status.spindle_state = str(value)
        elif key == "Shared chuck mode":
            status.shared_chuck_mode = str(value)
        elif key == "Spindle drive":
            status.spindle_drive = str(value)
        elif key == "Spindle commanded RPM":
            status.spindle_commanded_rpm = parse_float(value)
        elif key == "Spindle open-loop RPM":
            status.spindle_open_loop_rpm = parse_float(value)
        elif key == "Spindle maximum RPM":
            status.spindle_maximum_rpm = parse_float(value)
        elif key == "Spindle steps/rev":
            status.spindle_steps_rev = parse_int(value)
        elif key == "C position dead reckoned":
            status.c_position_dead_reckoned = parse_bool(value)
        elif key == "Threading enabled":
            status.threading_enabled = parse_bool(value)
        elif key == "Threading feedback ready":
            status.threading_feedback_ready = parse_bool(value)
        elif key == "Spindle speed mode":
            status.spindle_speed_mode = str(value)
        elif key == "Diameter mode":
            status.diameter_mode = parse_bool(value)
        elif key == "Feed mode":
            status.feed_mode = str(value)
        elif key == "Programmed S":
            status.programmed_s = parse_float(value)
        elif key == "Effective RPM":
            status.effective_rpm = parse_float(value)
        elif key == "CSS clamp RPM":
            status.css_clamp_rpm = parse_float(value)
        elif key == "Minimum CSS diameter mm":
            status.min_css_diameter_mm = parse_float(value)
        elif key == "Encoder enabled":
            status.encoder_enabled = parse_bool(value)
        elif key == "Encoder capture active":
            status.encoder_capture = parse_bool(value)
        elif key == "Encoder pulses/rev":
            status.encoder_pulses_rev = parse_int(value)
        elif key == "Active lathe tool":
            status.active_tool = parse_int(value)
        elif key == "Lathe tool X offset mm":
            status.tool_x_offset_mm = parse_float(value)
        elif key == "Lathe tool Z offset mm":
            status.tool_z_offset_mm = parse_float(value)
        elif key == "Tool nose radius mm":
            status.tool_nose_radius_mm = parse_float(value)
        elif key == "Feedback measured RPM":
            status.feedback_rpm_known = has_number(value)
            status.feedback_rpm = parse_float(value) if status.feedback_rpm_known else 0.0
        elif key == "Feedback index":
            status.feedback_index = parse_bool(value)
        elif key == "Feedback angular position":
            status.feedback_angular_pos = parse_bool(value)
        elif key == "Feedback angular rev":
            status.feedback_angular_known = has_number(value)
            status.feedback_angular_rev = parse_float(value) if status.feedback_angular_known else 0.0
        elif key == "Feedback revolution count":
            status.feedback_rev_count = parse_int(value)
        elif key == "Feedback stale":
            status.feedback_stale = parse_bool(value)
        elif key == "Feedback fault":
            status.feedback_fault = parse_bool(value)

    return status


def active_profile(status: LatheStatus) -> list[dict[str, Any]]:
    return LATHE_PROFILE if status.available and status.enabled else DEFAULT_PROFILE


def parse_command_response(payload: str) -> CommandResult:
    document = json.loads(payload)
    command = int(document["cmd"])
    return CommandResult(
        command=command,
        known=command in {422, 423},
        ok=document.get("status", "ok") == "ok",
        message=str(document.get("data", "")),
    )


def command_timeout_ms(command: int) -> int:
    return M6_TIMEOUT_MS if command == 6 else COMMAND_TIMEOUT_MS


def begin_command(command: int, target_tool: int, message: str, now_ms: int = 0) -> CommandState:
    return CommandState(
        command=command,
        known=True,
        pending=True,
        target_tool=target_tool,
        message=message,
        started_ms=now_ms,
        updated_ms=now_ms,
    )


def complete_command(
    state: CommandState,
    ok: bool,
    message: str,
    now_ms: int,
    *,
    recoverable: bool = False,
    timed_out: bool = False,
) -> CommandState:
    state.known = True
    state.ok = ok
    state.pending = False
    state.recoverable = recoverable
    state.timed_out = timed_out
    state.message = message
    state.updated_ms = now_ms
    return state


def apply_status_to_command(state: CommandState, status: LatheStatus, machine_state: str, now_ms: int) -> CommandState:
    if state.pending and state.command == 6 and state.target_tool > 0:
        if status.available and status.active_tool == state.target_tool:
            return complete_command(state, True, "Tool change complete", now_ms)
        if machine_state == "Alarm":
            return complete_command(state, False, "Alarm during command", now_ms, recoverable=True)
    return state


def poll_command(state: CommandState, now_ms: int) -> CommandState:
    if not state.pending:
        return state

    elapsed = now_ms - state.started_ms
    if elapsed >= command_timeout_ms(state.command):
        return complete_command(state, False, "Timed out", now_ms, recoverable=True, timed_out=True)

    if elapsed >= STILL_WAITING_MS:
        state.message = "Still waiting"
        state.updated_ms = now_ms

    if state.command == 6 and (state.last_refresh_ms == 0 or now_ms - state.last_refresh_ms >= REFRESH_MS):
        state.last_refresh_ms = now_ms

    return state


def assert_profile_mapping() -> None:
    assert DEFAULT_PROFILE[0]["letter"] == "X"
    assert DEFAULT_PROFILE[1]["letter"] == "Y"
    assert DEFAULT_PROFILE[2]["letter"] == "Z"
    assert [axis["machine_axis"] for axis in DEFAULT_PROFILE] == [0, 1, 2]

    assert LATHE_PROFILE[0]["letter"] == "X"
    assert LATHE_PROFILE[1]["letter"] == "Z"
    assert LATHE_PROFILE[2]["letter"] == "C"
    assert [axis["machine_axis"] for axis in LATHE_PROFILE] == [0, 2, 5]
    assert [axis["config_path"] for axis in LATHE_PROFILE] == ["$/axes/x", "$/axes/z", "$/axes/c"]


def assert_esp421_parsing() -> None:
    payload = json.dumps(
        {
            "cmd": "421",
            "status": "ok",
            "data": [
                {"id": "Lathe enabled", "value": "true"},
                {"id": "Spindle state", "value": "CLOCKWISE"},
                {"id": "Shared chuck mode", "value": "SPINDLE"},
                {"id": "Spindle drive", "value": "C_STEPPER"},
                {"id": "Spindle commanded RPM", "value": "5.0"},
                {"id": "Spindle open-loop RPM", "value": "4.75"},
                {"id": "Spindle maximum RPM", "value": "500.0"},
                {"id": "Spindle steps/rev", "value": "1600"},
                {"id": "C position dead reckoned", "value": "true"},
                {"id": "Threading enabled", "value": "false"},
                {"id": "Threading feedback ready", "value": "true"},
                {"id": "Spindle speed mode", "value": "G96"},
                {"id": "Diameter mode", "value": "G7"},
                {"id": "Feed mode", "value": "G95"},
                {"id": "Programmed S", "value": "400"},
                {"id": "Effective RPM", "value": "325.5"},
                {"id": "CSS clamp RPM", "value": "1200"},
                {"id": "Minimum CSS diameter mm", "value": "2.5"},
                {"id": "Encoder enabled", "value": "false"},
                {"id": "Encoder capture active", "value": "false"},
                {"id": "Encoder pulses/rev", "value": "1024"},
                {"id": "Active lathe tool", "value": "5"},
                {"id": "Lathe tool X offset mm", "value": "-1.25"},
                {"id": "Lathe tool Z offset mm", "value": "3.5"},
                {"id": "Tool nose radius mm", "value": "0.4"},
                {"id": "Feedback measured RPM", "value": "318.2"},
                {"id": "Feedback index", "value": "true"},
                {"id": "Feedback angular position", "value": "true"},
                {"id": "Feedback angular rev", "value": "0.125"},
                {"id": "Feedback revolution count", "value": "42"},
                {"id": "Feedback stale", "value": "true"},
                {"id": "Feedback fault", "value": "true"},
            ],
        }
    )

    status = parse_esp421(payload)
    assert status.known and status.available and status.enabled
    assert status.spindle_state == "CLOCKWISE"
    assert status.shared_chuck_mode == "SPINDLE"
    assert status.spindle_drive == "C_STEPPER"
    assert status.spindle_commanded_rpm == 5.0
    assert status.spindle_open_loop_rpm == 4.75
    assert math.isclose(status.spindle_maximum_rpm, 500.0)
    assert status.spindle_steps_rev == 1600
    assert status.c_position_dead_reckoned is True
    assert status.threading_enabled is False
    assert status.threading_feedback_ready is True
    assert status.spindle_speed_mode == "G96"
    assert status.diameter_mode is True
    assert status.feed_mode == "G95"
    assert status.programmed_s == 400.0
    assert status.effective_rpm == 325.5
    assert status.css_clamp_rpm == 1200.0
    assert status.min_css_diameter_mm == 2.5
    assert status.encoder_enabled is False
    assert status.encoder_capture is False
    assert status.encoder_pulses_rev == 1024
    assert status.active_tool == 5
    assert status.tool_x_offset_mm == -1.25
    assert status.tool_z_offset_mm == 3.5
    assert status.tool_nose_radius_mm == 0.4
    assert status.feedback_rpm_known and status.feedback_rpm == 318.2
    assert status.feedback_index is True
    assert status.feedback_angular_pos is True
    assert status.feedback_angular_known and status.feedback_angular_rev == 0.125
    assert status.feedback_rev_count == 42
    assert status.feedback_stale is True
    assert status.feedback_fault is True
    assert active_profile(status) == LATHE_PROFILE


def assert_fallbacks() -> None:
    disabled = parse_esp421(
        json.dumps(
            {
                "cmd": "421",
                "status": "ok",
                "data": [{"id": "Lathe enabled", "value": "false"}],
            }
        )
    )
    assert disabled.known and disabled.available and not disabled.enabled
    assert active_profile(disabled) == DEFAULT_PROFILE

    malformed = parse_esp421('{"cmd":"421","data":')
    assert malformed.known and not malformed.available
    assert active_profile(malformed) == DEFAULT_PROFILE

    unsupported = parse_esp421(json.dumps({"cmd": "999", "status": "ok"}))
    assert unsupported.known and not unsupported.available
    assert active_profile(unsupported) == DEFAULT_PROFILE

    legacy = parse_esp421(
        json.dumps({"cmd": "421", "data": [{"id": "Lathe enabled", "value": "true"}]})
    )
    assert legacy.enabled
    assert legacy.spindle_state == ""
    assert legacy.shared_chuck_mode == ""
    assert legacy.spindle_drive == ""
    assert legacy.spindle_steps_rev == 0
    assert legacy.c_position_dead_reckoned is False
    assert legacy.threading_enabled is False
    assert legacy.threading_feedback_ready is False


def assert_manual_lathe_math() -> None:
    def vector(path: float, angle: float, positive_slope: bool, diameter: bool) -> tuple[float, float]:
        radians = math.radians(angle)
        radial = path * math.sin(radians) * (1 if positive_slope else -1)
        return radial * (2 if diameter else 1), path * math.cos(radians)

    for angle in (0, 20, 35, 45, 80, 90):
        x_radius, z_radius = vector(0.1, angle, True, False)
        x_diameter, z_diameter = vector(0.1, angle, True, True)
        assert math.isclose(x_diameter, x_radius * 2, abs_tol=1e-9)
        assert math.isclose(z_diameter, z_radius, abs_tol=1e-9)
        x_negative, z_negative = vector(0.1, angle, False, False)
        assert math.isclose(x_negative, -x_radius, abs_tol=1e-9)
        assert math.isclose(z_negative, z_radius, abs_tol=1e-9)

    x45, z45 = vector(0.1, 45, True, True)
    assert math.isclose(x45, 0.141421356, rel_tol=1e-6)
    assert math.isclose(z45, 0.070710678, rel_tol=1e-6)

    pitch = 25.4 / 20
    revolutions = 10 / pitch
    c_degrees = revolutions * 360
    rpm = 1.0
    planner_feed = math.hypot(pitch * rpm, 360 * rpm)
    assert math.isclose(pitch, 1.27)
    assert math.isclose(c_degrees, 2834.645669291339, rel_tol=1e-9)
    assert planner_feed > 360 and planner_feed < 360.01

    steps_per_rev = round(4.444444 * 360)
    assert steps_per_rev == 1600
    assert math.isclose(2000 / 360, 5.5555555556, rel_tol=1e-9)
    assert math.isclose(0.5 * steps_per_rev / 60, 13.3333333333, rel_tol=1e-9)
    assert math.isclose(1.0 * steps_per_rev / 60, 26.6666666667, rel_tol=1e-9)
    assert math.isclose(5.0 * steps_per_rev / 60, 133.3333333333, rel_tol=1e-9)

    quantum = 360 / steps_per_rev
    residual = 0.0
    commanded = 0.0
    for _ in range(9):
        desired = 1.0 + residual
        pulses = round(desired / quantum)
        move = pulses * quantum
        residual = desired - move
        commanded += move
    assert math.isclose(commanded + residual, 9.0, abs_tol=1e-9)


def assert_jog_angle_math() -> None:
    def cpp_round(value: float) -> int:
        return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)

    def components(path_e4: float, angle: int, diameter: bool) -> tuple[int, int]:
        radians = math.radians(angle % 360)
        x = cpp_round(path_e4 * math.sin(radians) * (2 if diameter else 1))
        z = cpp_round(path_e4 * math.cos(radians))
        return x, z

    def path_per_count(count_e4: int, angle: int, reference_axis: int, diameter: bool) -> float | None:
        radians = math.radians(angle % 360)
        component = abs(math.sin(radians) if reference_axis == 0 else math.cos(radians))
        command_scale = 2 if reference_axis == 0 and diameter else 1
        return None if component <= 0.0001 else count_e4 / (component * command_scale)

    expected = {
        0: (0, 10_000),
        90: (10_000, 0),
        180: (0, -10_000),
        270: (-10_000, 0),
    }
    for angle, target in expected.items():
        assert components(10_000, angle, False) == target

    assert path_per_count(1000, 0, 0, False) is None
    assert path_per_count(1000, 90, 1, False) is None
    assert path_per_count(1000, 180, 0, False) is None
    assert path_per_count(1000, 270, 1, False) is None

    # X or Z is the counting reference.  Its commanded displacement is exact,
    # while the other component is coupled to preserve the physical angle.
    for angle in (0, 1, 20, 35, 45, 80, 90, 135, 180, 225, 270, 315, 359):
        for reference_axis in (0, 1):
            for diameter in (False, True):
                path = path_per_count(1000, angle, reference_axis, diameter)
                if path is None:
                    continue
                x, z = components(path, angle, diameter)
                reference_move = x if reference_axis == 0 else z
                assert abs(abs(reference_move) - 1000) <= 1
                physical_x = x / 2 if diameter else x
                physical_angle = math.degrees(math.atan2(physical_x, z)) % 360
                assert math.isclose(physical_angle, angle, abs_tol=0.05)
                reverse_x, reverse_z = components(-path, angle, diameter)
                assert reverse_x == -x
                assert reverse_z == -z

    assert components(10_000, 360, False) == components(10_000, 0, False)
    assert components(10_000, -1, False) == components(10_000, 359, False)

    # Cumulative ideal-path accounting carries fractional X/Z residues across
    # detents.  The selected reference remains exact after 1,000 clicks.
    angle = 17
    path_step = path_per_count(1, angle, 0, False)
    assert path_step is not None
    total_path = 0.0
    sent_x = 0
    sent_z = 0
    for _ in range(1000):
        total_path += path_step
        ideal_x, ideal_z = components(total_path, angle, False)
        dx = ideal_x - sent_x
        dz = ideal_z - sent_z
        sent_x += dx
        sent_z += dz
    assert (sent_x, sent_z) == components(total_path, angle, False)
    assert sent_x == 1000


def assert_command_results() -> None:
    tool_save_ok = parse_command_response(json.dumps({"cmd": "422", "status": "ok", "data": "tool saved"}))
    assert tool_save_ok.known and tool_save_ok.command == 422 and tool_save_ok.ok
    assert tool_save_ok.message == "tool saved"

    tool_save_error = parse_command_response(json.dumps({"cmd": "422", "status": "error", "data": "bad tool"}))
    assert tool_save_error.known and not tool_save_error.ok
    assert tool_save_error.message == "bad tool"

    touch_off_ok = parse_command_response(json.dumps({"cmd": "423", "status": "ok", "data": "touch-off applied"}))
    assert touch_off_ok.known and touch_off_ok.command == 423 and touch_off_ok.ok

    touch_off_error = parse_command_response(json.dumps({"cmd": "423", "status": "error", "data": "probe missing"}))
    assert touch_off_error.known and not touch_off_error.ok
    assert touch_off_error.message == "probe missing"


def assert_command_lifecycle() -> None:
    m6 = begin_command(6, 3, "Waiting for T/M6")
    poll_command(m6, 1_000)
    assert m6.pending and m6.last_refresh_ms == 1_000
    poll_command(m6, STILL_WAITING_MS)
    assert m6.pending and m6.message == "Still waiting"
    status = LatheStatus(known=True, available=True, enabled=True, active_tool=3)
    apply_status_to_command(m6, status, "Idle", 6_000)
    assert m6.known and m6.ok and not m6.pending
    assert m6.message == "Tool change complete"

    timeout = begin_command(6, 4, "Waiting for T/M6")
    poll_command(timeout, M6_TIMEOUT_MS)
    assert timeout.known and not timeout.ok and not timeout.pending
    assert timeout.timed_out and timeout.recoverable
    assert timeout.target_tool == 4 and timeout.message == "Timed out"

    alarm = begin_command(6, 2, "Waiting for T/M6")
    apply_status_to_command(alarm, LatheStatus(known=True, available=True, enabled=True, active_tool=1), "Alarm", 2_000)
    assert alarm.known and not alarm.ok and not alarm.pending
    assert alarm.recoverable and alarm.message == "Alarm during command"

    save = begin_command(422, 1, "Saving tool")
    response = parse_command_response(json.dumps({"cmd": "422", "status": "ok", "data": "tool saved"}))
    complete_command(save, response.ok, response.message, 250)
    assert save.ok and not save.pending and save.message == "tool saved"

    touch = begin_command(423, 5, "Applying touch-off")
    poll_command(touch, COMMAND_TIMEOUT_MS)
    assert touch.timed_out and touch.recoverable and touch.message == "Timed out"


def assert_maijker_build_contract() -> None:
    root = Path(__file__).resolve().parents[1]
    platformio = (root / "platformio.ini").read_text(encoding="utf-8")
    hardware = (root / "src" / "HardwareM5Dial.hpp").read_text(encoding="utf-8")
    system = (root / "src" / "SystemArduino.cpp").read_text(encoding="utf-8")
    main = (root / "src" / "ardmain.cpp").read_text(encoding="utf-8")
    jog = (root / "src" / "MultiJogScene.cpp").read_text(encoding="utf-8")
    fluidnc = (root / "src" / "FluidNCModel.cpp").read_text(encoding="utf-8")
    file_parser = (root / "src" / "FileParser.cpp").read_text(encoding="utf-8")
    manual = (root / "src" / "LatheManualScene.cpp").read_text(encoding="utf-8")

    section = platformio.split("[env:maijker_m5dial]", 1)[1].split("[env:", 1)[0]
    assert "-DMAIJKER_XZACT_LATHE" in section
    assert "-DFNC_BAUD=1000000" in section
    assert "-DUSE_WIFI" in section
    assert "-DDEBUG_TO_USB" not in section

    assert "PND_RX_FNC_TX_PIN = GPIO_NUM_15" in hardware
    assert "PND_TX_FNC_RX_PIN = GPIO_NUM_13" in hardware
    assert "RED_BUTTON_PIN    = GPIO_NUM_1" in hardware
    assert "GREEN_BUTTON_PIN  = GPIO_NUM_2" in hardware
    assert "fnc_putchar(c);  // So you can type commands to FluidNC" in system
    assert "#ifdef DEBUG_TO_USB" in system
    poll_extra = system.split('extern "C" void poll_extra()', 1)[1].split(
        "#ifdef DEBUG_TO_USB", 1
    )[0]
    assert "else if (!wifi_use_uart_mode())" in poll_extra
    assert "update_events();" not in poll_extra

    wifi = (root / "src" / "WiFiConnection.cpp").read_text(encoding="utf-8")
    device_diagnostics = (root / "src" / "DeviceDiagnostics.cpp").read_text(encoding="utf-8")
    assert "#ifdef MAIJKER_XZACT_LATHE" in wifi
    assert "return TransportMode::UART;" in wifi
    wifi_startup = wifi.split("void wifi_init(bool auto_ap)", 1)[1].split("void wifi_poll()", 1)[0]
    assert "WifiReconnectPhase::DriverOff" in wifi_startup
    assert "millis() + WIFI_DRIVER_OFF_MS" in wifi_startup
    assert "delay(100)" not in wifi_startup
    assert "if (_wifi_ignore_disconnect_events)" in wifi
    assert "_secure_ota_only || (_wifi_reconnect_attempts % 2) == 1" in wifi
    assert "++_wifi_association_attempts;" in wifi
    for field in (
        '"wifi"',
        '"association_attempts"',
        '"driver_resets"',
        '"soft_reconnects"',
        '"ignored_internal_disconnects"',
    ):
        assert field.replace('"', '\\"') in device_diagnostics

    menu = (root / "src" / "MenuScene.cpp").read_text(encoding="utf-8")
    lathe = (root / "src" / "LatheModel.cpp").read_text(encoding="utf-8")
    about = (root / "src" / "AboutScene.cpp").read_text(encoding="utf-8")
    status = (root / "src" / "StatusScene.cpp").read_text(encoding="utf-8")
    health = (root / "src" / "MachineHealthScene.cpp").read_text(encoding="utf-8")
    actions = (root / "src" / "MachineStateActions.cpp").read_text(encoding="utf-8")
    diagnostic_screens = (root / "src" / "DiagnosticScreens.cpp").read_text(encoding="utf-8")
    assert "void reDisplay() override" in menu
    assert "syncIconAvailability();" in menu
    assert "operator_basic_motion_actions_available()" in menu
    assert "s_status.available && s_status.enabled" in lathe
    assert "void lathe_schedule_status_refresh(bool immediate)" in lathe
    assert "void lathe_poll_status()" in lathe
    assert "request_lathe_status(true);" in lathe
    assert "json_reset_depth();" in lathe

    # The round display has separate operator, health, and device-information
    # surfaces. Safety actions remain centralized so labels and behavior agree.
    assert "push_scene(&machineHealthScene)" in menu
    assert 'drawButtonLegends("Back", "Settings", "Next")' in about
    assert "draw_state_pill(shown_state)" in status
    assert 'text(inInches ? "in" : "mm"' in status
    lathe_dashboard = status.split("void draw_lathe_dashboard()", 1)[1].split("public:", 1)[0]
    assert "axis_char == 'X' || axis_char == 'Z'" in lathe_dashboard
    assert "Thread unsafe" not in status
    assert "ENC OFF" not in status
    for page in ("drawOverview", "drawAlarm", "drawReadiness", "drawConnections"):
        assert page in health
    assert "machine_state_red_action" in status and "machine_state_red_action" in health
    assert 'send_line("$H")' in actions
    for screen_id in (
        "about-device",
        "about-controls",
        "health-overview",
        "health-alarm-preview",
        "health-encoder-fault",
        "status-disconnected",
        "manual-menu",
        "manual-angle",
        "manual-spindle",
        "manual-c-position",
        "manual-thread-proof",
        "manual-face",
        "manual-turn",
        "manual-chamfer",
        "manual-groove",
        "manual-peck",
    ):
        assert f'"{screen_id}"' in diagnostic_screens

    # The 1 Mbps link must absorb a complete multi-line ESP421 response without
    # entering FluidNC's synchronous XON/XOFF path.  A controller reset must
    # invalidate both representations of state so the first report reconnects.
    assert "uart_driver_install(fnc_uart_port, 4096" in system
    assert "uart_set_sw_flow_ctrl(fnc_uart_port, false, 0, 0)" in system
    assert "fnc_putchar(0x11)" not in fluidnc
    rst_handler = file_parser.split('if (strcmp(command, "RST") == 0)', 1)[1].split(
        'if (strcmp(command, "Files changed") == 0)', 1
    )[0]
    assert "set_disconnected_state();" in rst_handler
    assert "state = Disconnected;" not in rst_handler
    assert "flush_fnc_rx(50);" in main
    assert 'send_line("$RI=1000", 500);' in main
    assert "for (int i = 0; i < 4096; i++)" in main
    assert "lathe_poll_status();" in main
    assert 'send_line("$RI=1000");' in fluidnc
    assert 'send_line("$RI=200");' not in fluidnc
    assert "LATHE_STATUS_REPLY_TIMEOUT_MS     = 5000" in lathe
    assert "s_status_retry_count >= 2" in lathe
    assert "s_pending_status_saw_enabled" in lathe

    # Manual-lathe helpers use temporary-modal jogs, never silently enable
    # encoder threading, and expose a common cancel path.
    assert 'PieMenu("Manual Lathe"' in manual
    assert '"Angle Jog"' in manual
    assert '"Spindle"' in manual
    assert '"Thread Proof"' in manual
    for helper in ('"Face"', '"Turn"', '"Chamfer"', '"Groove"', '"Peck"'):
        assert helper in manual
    assert '"$J=G91G21F%.3f"' in manual
    assert "send_jog_cancel();" in manual
    assert "_expected_done_ms" in manual
    assert "request_status_report();" in manual
    assert "complete_current_move();" in manual
    assert 'send_line("M5")' in manual
    assert 'send_linef("M%dS%d"' in manual
    assert '"$J=G91G21F%.3fC%.4f"' in manual
    assert "StepperRPMV2" in manual
    assert "delta * 10" in manual
    assert "{ 50, 100, 200, 300, 500 }" in manual
    assert "positioning_feed(commanded)" in manual
    assert "G33" not in manual and "G76" not in manual and "G95" not in manual
    assert "lathe_angle_vector" in manual
    assert "lathe_thread_planner_feed" in manual

    # The DLC32 turret shares the controller stepper enable. A confirmed M6
    # must enable before motion, wait for the M6 acknowledgement, hold for
    # seating, and release only afterward.
    tool_change = lathe.split("void lathe_change_tool(int tool)", 1)[1].split(
        "void lathe_select_tool_logical", 1
    )[0]
    expected_turret_sequence = [
        'send_line("$ME");',
        'send_linef("T%d", tool);',
        'send_line("M6", LATHE_M6_TIMEOUT_MS);',
        'send_line("$ME", LATHE_M6_TIMEOUT_MS);',
        "hold_turret_enable();",
        'send_line("$MD");',
        "request_lathe_status(true);",
    ]
    positions = [tool_change.index(statement) for statement in expected_turret_sequence]
    assert positions == sorted(positions)
    assert "LATHE_TURRET_ENABLE_HOLD_MS      = 3000" in lathe
    assert "fnc_poll();" in lathe.split("static void hold_turret_enable()", 1)[1].split("}", 1)[0]

    wifi = (root / "src" / "WiFiConnection.cpp").read_text(encoding="utf-8")
    handshake = wifi.split("WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT", 1)[1].split(
        "WIFI_REASON_NO_AP_FOUND", 1
    )[0]
    assert "_wifi_retry_at" in handshake
    assert "MSG_CHECK_PASS" not in handshake

    # WiFi recovery must not starve the wired HMI while the AP is slow or the
    # ESP32 station driver needs to be restarted.
    assert "WIFI_CONNECT_TIMEOUT_MS   30000" in wifi
    assert "enum class WifiReconnectPhase" in wifi
    assert "service_wifi_reconnect(now);" in wifi
    assert "WiFi.disconnect(restart_driver, false);" in wifi
    assert "if (!_secure_ota_only) {\n            set_disconnected_state();" in wifi
    retry = wifi.split("// Start recovery without blocking", 1)[1].split(
        "// Detect WiFi reconnects", 1
    )[0]
    assert "delay(" not in retry

    # The M5 commissioning profile is intentionally gentle for the small lathe.
    assert "static const int DEFAULT_DIST_INDEX = 1;" in jog
    assert 'getPref("GentleJogV1", &gentle_jog_profile)' in jog
    assert 'setPref("GentleJogV1", 1)' in jog
    assert "e4_from_int(inInches ? 24 : 600)" in jog
    assert "e4_from_int(inInches ? 2 : 60)" in jog
    assert "static const uint32_t PRECISE_MOVE_MS = 100;" in jog
    assert "e4_t precise_jog_feed(e4_t move)" in jog
    assert "e4_t     feed = precise_jog_feed(move);" in jog

    # Jog angle selection is full-circle and never persists its armed state.
    # X or Z remains the counting reference while all armed motion is emitted
    # as a coupled X/Z vector through the existing bounded jog transport.
    assert "normalized_angle(_angle_candidate_degrees - delta)" in jog
    assert 'setPref("AngleDeg", _angle_degrees);' in jog
    assert 'setPref("AngleArmed"' not in jog
    assert "drawBackground(BLACK);" in jog
    assert "canvas.fillCircle(120, 120, 119, BROWN);" in jog
    assert "state == Idle || _diagnostic_angle_armed" in jog
    assert "angle_path_per_count()" in jog
    assert "_selected_mask        = 1 << _angle_reference_axis;" in jog
    assert "_diagnostic_selection_mask = -1;" in jog
    assert "never change the operator's live axis selection" in jog
    assert "angle_increment_components(delta, x_command, z_move);" in jog
    assert "append_angle_axes(cmd, x_command, z_move);" in jog
    assert "_angle_armed_diameter ? 2.0 : 1.0" in jog
    assert 'drawButtonLegends(jog_angle_available()' in jog
    for screen_id in (
        "jog-angle-off",
        "jog-angle-selecting",
        "jog-angle-armed",
        "jog-angle-active",
    ):
        assert f'"{screen_id}"' in diagnostic_screens


def main() -> None:
    assert_profile_mapping()
    assert_esp421_parsing()
    assert_fallbacks()
    assert_command_results()
    assert_command_lifecycle()
    assert_manual_lathe_math()
    assert_jog_angle_math()
    assert_maijker_build_contract()
    print("lathe protocol harness: all checks passed")


if __name__ == "__main__":
    main()
