"""Exercise the runtime package through ESPHome's real configuration loader."""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from esphome.config import read_config
from esphome.core import CORE

ROOT = Path(__file__).resolve().parents[1]


class RuntimeSetupPackage(unittest.TestCase):
    def load(self, password="synthetic-setup-password", suffix=""):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "device.yaml"
            path.write_text(
                '''esphome:
  name: runtime-test
substitutions:
  odelic_setup_username: setup
  odelic_setup_password: "'''
                + password
                + """"
packages:
  odelic: !include """
                + str(ROOT / "packages/atoms3-lite-setup.yaml")
                + """
external_components:
  - source:
      type: local
      path: """
                + str(ROOT / "components")
                + """
    components: [odelic, odelic_setup]
api:
  encryption:
    key: "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
wifi:
  ssid: SyntheticNetwork
  password: SyntheticPassword
"""
                + suffix
            )
            CORE.reset()
            CORE.config_path = path
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    return read_config({})
            finally:
                CORE.reset()

    def test_public_runtime_fragment_loads_nested_package_without_git_credentials(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            path = folder / "device.yaml"
            path.write_text(
                """esphome:
  name: remote-runtime-test
api:
  encryption:
    key: "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
wifi:
  ssid: SyntheticNetwork
  password: SyntheticPassword
"""
                + (ROOT / "examples/device-builder-setup.yaml").read_text()
            )
            (folder / "secrets.yaml").write_text(
                "odelic_setup_username: setup\nodelic_setup_password: synthetic-password-only\n"
            )
            CORE.reset()
            CORE.config_path = path
            try:
                with patch(
                    "esphome.git.clone_or_update", return_value=(ROOT, None)
                ) as clone:
                    config = read_config({})
                self.assertIsNotNone(config)
                self.assertFalse(config["ble_client"][0]["auto_connect"])
                self.assertEqual(clone.call_count, 2)
                for call in clone.call_args_list:
                    self.assertIsNone(call.kwargs.get("username"))
                    self.assertIsNone(call.kwargs.get("password"))
            finally:
                CORE.reset()

    def test_runtime_package_starts_without_fixture_or_auto_connect(self):
        config = self.load()
        self.assertIsNotNone(config)
        self.assertFalse(config["ble_client"][0]["auto_connect"])
        self.assertEqual(
            str(config["ble_client"][0]["mac_address"]), "00:00:00:00:00:00"
        )
        self.assertEqual(config["odelic"][0]["lighting_id"], "00000000")
        self.assertFalse(config["esp32_ble_tracker"]["scan_parameters"]["active"])
        self.assertEqual(config["odelic_setup"]["username"], "setup")
        self.assertIn("web_server_base", config)
        self.assertEqual(config["odelic_setup"]["max_lights"], 8)
        self.assertEqual(config["esp32_ble"]["max_connections"], 8)
        self.assertEqual(len(config["odelic_controller"]["_slots"]), 8)
        self.assertEqual(config["odelic_controller"]["fixtures"], [])
        self.assertEqual(config["light"], [])

    def test_capacity_can_be_configured_beyond_two(self):
        config = self.load(
            suffix="""odelic_controller:
  max_lights: 5
odelic_setup:
  max_lights: 5
"""
        )
        self.assertIsNotNone(config)
        self.assertEqual(len(config["odelic_controller"]["_slots"]), 5)

    def test_stack_capacity_must_cover_every_reserved_fixture_session(self):
        self.assertIsNone(
            self.load(
                suffix="""esp32_ble:
  max_connections: 3
"""
            )
        )
        config = self.load(
            suffix="""esp32_ble:
  max_connections: 3
odelic_controller:
  max_lights: 3
odelic_setup:
  max_lights: 3
"""
        )
        self.assertIsNotNone(config)
        self.assertEqual(config["esp32_ble"]["max_connections"], 3)
        self.assertEqual(len(config["odelic_controller"]["_slots"]), 3)

    def test_s3_connection_limit_leaves_room_for_scan_and_advertising(self):
        self.assertIsNone(self.load(suffix="esp32_ble:\n  max_connections: 9\n"))

    def test_capacity_outside_supported_range_rejected(self):
        for capacity in (0, 9):
            self.assertIsNone(
                self.load(
                    suffix=f"odelic_controller:\n  max_lights: {capacity}\nodelic_setup:\n  max_lights: {capacity}\n"
                )
            )

    def test_short_setup_password_rejected(self):
        self.assertIsNone(self.load(password="short"))

    def test_early_auto_connect_rejected(self):
        self.assertIsNone(
            self.load(
                suffix="""ble_client:
  - id: !extend fixture_ble
    auto_connect: true
"""
            )
        )


if __name__ == "__main__":
    unittest.main()
