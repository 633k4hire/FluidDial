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


def main() -> None:
    assert_profile_mapping()
    assert_esp421_parsing()
    assert_fallbacks()
    assert_command_results()
    print("lathe protocol harness: all checks passed")


if __name__ == "__main__":
    main()
