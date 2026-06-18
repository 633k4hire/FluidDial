#!/usr/bin/env python3
"""Local checks for FluidDial's lathe JSON contract.

This harness mirrors the small protocol surface FluidDial consumes from FluidNC:
ESP421 status data, ESP422/ESP423 command results, and the X/Z/C operator axis
profile. It intentionally avoids firmware dependencies so it can run anywhere
Python is available.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
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


def main() -> None:
    assert_profile_mapping()
    assert_esp421_parsing()
    assert_fallbacks()
    assert_command_results()
    assert_command_lifecycle()
    print("lathe protocol harness: all checks passed")


if __name__ == "__main__":
    main()
