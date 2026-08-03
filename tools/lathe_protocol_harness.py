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
from pathlib import Path
from typing import Any


TOOL_TYPE_DEFAULTS = ["RightTurn", "LeftTurn", "DrillQuarterInch", "BoringBar", "Probe"]


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


@dataclass
class ProbeResult:
    known: bool = False
    success: bool = False
    axes_mm: tuple[float, ...] = ()


def extract_wcs(mode_report: str) -> str:
    for token in mode_report.strip("[]").split():
        if token in {"G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"}:
            return token
    return "--"


def parse_probe_report(report: str) -> ProbeResult:
    if not report.startswith("[PRB:") or not report.endswith("]"):
        return ProbeResult()
    body = report[5:-1]
    coordinates, separator, success = body.rpartition(":")
    if not separator:
        return ProbeResult()
    try:
        axes = tuple(float(value) for value in coordinates.split(","))
    except ValueError:
        return ProbeResult()
    return ProbeResult(known=True, success=success == "1", axes_mm=axes[:6])


def carousel_select(selected: int, delta: int) -> int:
    return (selected + delta) % 8


def load_tool_types(saved: dict[int, str]) -> list[str]:
    values = TOOL_TYPE_DEFAULTS.copy()
    for station, tool_type in saved.items():
        if 1 <= station <= 5:
            values[station - 1] = tool_type
    return values


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


def assert_live_ui_contract() -> None:
    assert extract_wcs("[GC:G0 G54 G17 G21 G90]") == "G54"
    assert extract_wcs("[GC:G1 G59.3 G18 G95]") == "G59.3"
    assert extract_wcs("[GC:G0 G17 G21]") == "--"

    hit = parse_probe_report("[PRB:1.250,-2.500,0.000:1]")
    assert hit.known and hit.success and hit.axes_mm == (1.25, -2.5, 0.0)
    miss = parse_probe_report("[PRB:4.000,5.000:0]")
    assert miss.known and not miss.success and miss.axes_mm == (4.0, 5.0)
    assert not parse_probe_report("not-a-probe").known

    selected = 0
    for delta in (1, 1, -1, 8, -9):
        selected = carousel_select(selected, delta)
    assert selected == 0
    assert [carousel_select(0, delta) for delta in range(8)] == list(range(8))

    assert load_tool_types({}) == TOOL_TYPE_DEFAULTS
    assert load_tool_types({2: "Parting"}) == ["RightTurn", "Parting", "DrillQuarterInch", "BoringBar", "Probe"]
    # An upgrade re-applies defaults only to missing keys, never over a saved edit.
    assert load_tool_types({1: "Thread", 5: "Unset"})[0::4] == ["Thread", "Unset"]


def assert_maijker_build_contract() -> None:
    root = Path(__file__).resolve().parents[1]
    platformio = (root / "platformio.ini").read_text(encoding="utf-8")
    hardware = (root / "src" / "HardwareM5Dial.hpp").read_text(encoding="utf-8")
    system = (root / "src" / "SystemArduino.cpp").read_text(encoding="utf-8")
    main = (root / "src" / "ardmain.cpp").read_text(encoding="utf-8")
    jog = (root / "src" / "MultiJogScene.cpp").read_text(encoding="utf-8")
    fluidnc = (root / "src" / "FluidNCModel.cpp").read_text(encoding="utf-8")

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
    assert "#ifdef MAIJKER_XZACT_LATHE" in wifi
    assert "return TransportMode::UART;" in wifi

    menu = (root / "src" / "MenuScene.cpp").read_text(encoding="utf-8")
    lathe = (root / "src" / "LatheModel.cpp").read_text(encoding="utf-8")
    about = (root / "src" / "AboutScene.cpp").read_text(encoding="utf-8")
    status = (root / "src" / "StatusScene.cpp").read_text(encoding="utf-8")
    health = (root / "src" / "MachineHealthScene.cpp").read_text(encoding="utf-8")
    actions = (root / "src" / "MachineStateActions.cpp").read_text(encoding="utf-8")
    diagnostic_screens = (root / "src" / "DiagnosticScreens.cpp").read_text(encoding="utf-8")
    lathe_ui = (root / "src" / "LatheUi.cpp").read_text(encoding="utf-8")
    lathe_ui_model = (root / "src" / "LatheUiModel.cpp").read_text(encoding="utf-8")
    fluidnc_header = (root / "src" / "FluidNCModel.h").read_text(encoding="utf-8")
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
    assert "lathe_ui_state_pill(88, 10, shown_state)" in status
    assert 'text(inInches ? "IN" : "MM"' in status
    lathe_dashboard = status.split("void draw_lathe_dashboard()", 1)[1].split("public:", 1)[0]
    assert "axis < profile_axis_count() && axis < 3" in lathe_dashboard
    assert "current_wcs()" in lathe_dashboard
    assert "myPercent" in lathe_dashboard
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
        "home-unhomed",
        "home-homed",
        "probe-live",
        "probe-success",
        "probe-failure",
        "tools-default-types",
        "files-loading",
        "files-empty",
        "files-error",
        "macros-loading",
        "macros-empty",
        "macros-error",
    ):
        assert f'"{screen_id}"' in diagnostic_screens

    assert "bool lathe_ui_enabled()" in lathe_ui
    assert "MAIJKER_XZACT_LATHE" in lathe_ui
    assert "lathe_ui_orbital_rail" in lathe_ui
    assert "_animation_phase = 3" in menu
    assert "Menu::rotate(delta)" in menu
    assert "LINK REQUIRED" in menu
    assert "show_probe(const pos_t* axes" in fluidnc
    assert "show_probe_pin(bool on)" in fluidnc
    assert "const char* current_wcs()" in fluidnc_header
    assert "const ProbeResult& last_probe_result()" in fluidnc_header
    for expected in ("RightTurn", "LeftTurn", "DrillQuarterInch", "BoringBar", "Probe"):
        assert f"LatheToolType::{expected}" in lathe_ui_model
    assert 'nvs_get_i32(s_prefs, key, &value)' in lathe_ui_model
    assert 'nvs_set_i32(s_prefs, key, static_cast<int>(type))' in lathe_ui_model

    # The 1 Mbps link must be able to absorb a complete multi-line ESP421
    # response without a sticky parser failure after homing.
    assert "uart_driver_install(fnc_uart_port, 4096" in system
    assert "uart_set_sw_flow_ctrl(fnc_uart_port, true, 32, 96)" in system
    assert "flush_fnc_rx(50);" in main
    assert 'send_line("$RI=1000", 500);' in main
    assert "for (int i = 0; i < 4096; i++)" in main
    assert "lathe_poll_status();" in main
    assert 'send_line("$RI=1000");' in fluidnc
    assert 'send_line("$RI=200");' not in fluidnc
    assert "LATHE_STATUS_REPLY_TIMEOUT_MS     = 5000" in lathe
    assert "s_status_retry_count >= 2" in lathe
    assert "s_pending_status_saw_enabled" in lathe

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


def main() -> None:
    assert_profile_mapping()
    assert_esp421_parsing()
    assert_fallbacks()
    assert_command_results()
    assert_command_lifecycle()
    assert_live_ui_contract()
    assert_maijker_build_contract()
    print("lathe protocol harness: all checks passed")


if __name__ == "__main__":
    main()
