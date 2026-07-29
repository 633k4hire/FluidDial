#!/usr/bin/env python3
"""Host-only checks for the FluidNC <-> FluidDial OTA authorization contract."""

from __future__ import annotations

import hashlib
import hmac
import pathlib
import unittest


def authorize(
    secret: bytes,
    method: str,
    path: str,
    target: str,
    nonce: str,
    counter: int,
    manifest_hash: str,
    body: bytes,
) -> str:
    body_hash = hashlib.sha256(body).hexdigest()
    canonical = "\n".join(
        (method, path, target, nonce, str(counter), manifest_hash, body_hash)
    )
    return hmac.new(secret, canonical.encode(), hashlib.sha256).hexdigest()

def authorize_response(
    secret: bytes, nonce: str, counter: int, status: int, body: bytes
) -> str:
    canonical = "\n".join(
        ("response", nonce, str(counter), str(status), hashlib.sha256(body).hexdigest())
    )
    return hmac.new(secret, canonical.encode(), hashlib.sha256).hexdigest()


class Session:
    def __init__(self, expected: int, last_counter: int = 0) -> None:
        self.expected = expected
        self.last_counter = last_counter
        self.offset = 0
        self.active_counter: int | None = None
        self.digest = hashlib.sha256()

    def begin(self, counter: int) -> None:
        if self.active_counter is not None or counter <= self.last_counter:
            raise ValueError("replayed or concurrent deployment")
        self.active_counter = counter

    def chunk(self, counter: int, offset: int, body: bytes) -> None:
        if counter != self.active_counter or offset != self.offset:
            raise ValueError("stale counter or non-sequential chunk")
        if not body or len(body) > 8192 or self.offset + len(body) > self.expected:
            raise ValueError("invalid bounded chunk")
        self.digest.update(body)
        self.offset += len(body)

    def commit(self, expected_hash: str) -> None:
        if self.offset != self.expected or self.digest.hexdigest() != expected_hash:
            raise ValueError("incomplete or corrupt image")
        assert self.active_counter is not None
        self.last_counter = self.active_counter
        self.active_counter = None


class SecureOtaContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.secret = bytes(range(32))
        self.args = dict(
            method="PUT",
            path="/api/v1/ota/chunk",
            target="fluiddial-0123456789abcdef",
            nonce="00" * 16,
            counter=7,
            manifest_hash="11" * 32,
            body=b"firmware",
        )

    def test_hmac_binds_every_authorization_field(self) -> None:
        baseline = authorize(self.secret, **self.args)
        mutations = {
            "method": "POST",
            "path": "/api/v1/ota/commit",
            "target": "fluiddial-fedcba9876543210",
            "nonce": "22" * 16,
            "counter": 8,
            "manifest_hash": "33" * 32,
            "body": b"tampered",
        }
        for field, value in mutations.items():
            changed = dict(self.args)
            changed[field] = value
            self.assertNotEqual(baseline, authorize(self.secret, **changed), field)

    def test_replay_and_concurrent_begin_are_rejected(self) -> None:
        session = Session(expected=4, last_counter=4)
        with self.assertRaisesRegex(ValueError, "replayed"):
            session.begin(4)
        session.begin(5)
        with self.assertRaisesRegex(ValueError, "concurrent"):
            session.begin(6)

    def test_response_authentication_binds_status_and_body(self) -> None:
        baseline = authorize_response(self.secret, "00" * 16, 7, 200, b'{"healthy":true}')
        self.assertNotEqual(
            baseline,
            authorize_response(self.secret, "00" * 16, 7, 200, b'{"healthy":false}'),
        )
        self.assertNotEqual(
            baseline,
            authorize_response(self.secret, "00" * 16, 7, 409, b'{"healthy":true}'),
        )

    def test_chunks_must_be_bounded_and_sequential(self) -> None:
        session = Session(expected=8)
        session.begin(1)
        session.chunk(1, 0, b"abcd")
        with self.assertRaisesRegex(ValueError, "non-sequential"):
            session.chunk(1, 0, b"efgh")
        with self.assertRaisesRegex(ValueError, "stale"):
            session.chunk(2, 4, b"efgh")
        session.chunk(1, 4, b"efgh")

    def test_commit_rechecks_complete_image_hash(self) -> None:
        image = b"abcdefgh"
        session = Session(expected=len(image))
        session.begin(1)
        session.chunk(1, 0, image[:4])
        session.chunk(1, 4, image[4:])
        with self.assertRaisesRegex(ValueError, "corrupt"):
            session.commit("00" * 32)
        session.commit(hashlib.sha256(image).hexdigest())
        self.assertEqual(1, session.last_counter)


class SecureOtaSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        root = pathlib.Path(__file__).resolve().parents[1]
        cls.service = (root / "src" / "SecureOtaService.cpp").read_text(
            encoding="utf-8"
        )
        cls.wifi = (root / "src" / "WiFiConnection.cpp").read_text(encoding="utf-8")
        cls.main = (root / "src" / "ardmain.cpp").read_text(encoding="utf-8")
        cls.pairing_scene = (root / "src" / "SecurePairingScene.cpp").read_text(
            encoding="utf-8"
        )
        cls.diagnostics = (root / "src" / "DeviceDiagnostics.cpp").read_text(
            encoding="utf-8"
        )

    def test_all_secure_endpoints_are_registered(self) -> None:
        for endpoint in (
            "/api/v1/device",
            "/api/v1/pair/start",
            "/api/v1/pair/confirm",
            "/api/v1/challenge",
            "/api/v1/ota/begin",
            "/api/v1/ota/chunk",
            "/api/v1/ota/commit",
            "/api/v1/ota/status",
            "/api/v1/ota/abort",
            "/api/v1/health",
        ):
            self.assertIn(endpoint, self.service)

    def test_normal_runtime_discovery_is_narrowly_scoped(self) -> None:
        self.assertIn('MDNS.addService("tams-fluiddial", "tcp", 80)', self.wifi)
        self.assertIn('"role", "m5dial_hmi"', self.wifi)
        self.assertIn("secure_ota_register(httpServer, false)", self.wifi)

    def test_uart_bootstrap_starts_setup_ap_without_credentials(self) -> None:
        self.assertIn("if (!cfg.valid) {", self.wifi)
        self.assertIn("if (auto_ap) {", self.wifi)
        self.assertNotIn("if (auto_ap && !_secure_ota_only) {", self.wifi)

    def test_pairing_and_pending_boot_need_physical_and_application_health(self) -> None:
        self.assertIn("physicalWindowOpen()", self.service)
        self.assertIn("secure_ota_confirm_pairing_physical", self.service)
        self.assertIn("secure_ota_confirm_recovery_physical", self.service)
        self.assertIn("recoveryConfirmedUntil", self.service)
        self.assertIn("applicationHealthy", self.service)
        self.assertIn("esp_ota_mark_app_valid_cancel_rollback", self.service)
        self.assertIn("esp_ota_mark_app_invalid_rollback_and_reboot", self.service)
        self.assertIn("secure_ota_note_application_healthy", self.main)

    def test_normal_runtime_pairing_uses_the_center_dial(self) -> None:
        self.assertIn("secure_ota_set_physical_window(true)", self.service)
        self.assertIn("secure_ota_pairing_pending()", self.main)
        self.assertIn("push_scene(&securePairingScene)", self.main)
        self.assertIn("onDialButtonPress", self.pairing_scene)
        self.assertIn("secure_ota_confirm_pairing_physical()", self.pairing_scene)
        self.assertIn("Press center dial to pair", self.pairing_scene)

    def test_legacy_upload_is_disabled_after_secure_pairing(self) -> None:
        self.assertIn("secure_ota_legacy_upload_allowed", self.wifi)
        self.assertIn("paired && TamsFirmware::trustConfigured()", self.service)

    def test_authenticated_responses_are_bound_to_nonce_counter_status_and_body(self) -> None:
        self.assertIn('"X-TAMS-Response-Auth"', self.service)
        self.assertIn('"response\\n"', self.service)

    def test_wifi_diagnostics_and_screen_capture_use_the_paired_contract(self) -> None:
        self.assertIn("/api/v1/diagnostics/link", self.service)
        self.assertIn("/api/v1/diagnostics/screen.bmp", self.service)
        self.assertIn('authenticate("GET", Path, false, false)', self.service)
        self.assertIn("attachAuthenticatedBinaryResponse(200, bodyDigest)", self.service)
        self.assertIn("sendJson(200, device_diagnostics_json())", self.service)
        self.assertIn("ScreenWidth * ScreenHeight", self.service)
        self.assertIn('\\"transport\\"', self.diagnostics)
        self.assertIn('\\"lathe_sync\\"', self.diagnostics)


if __name__ == "__main__":
    unittest.main(verbosity=2)
