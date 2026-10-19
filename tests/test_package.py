"""Validate the real Device Builder merge without network or private credentials."""

import base64
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from esphome.config import read_config
from esphome.core import CORE

ROOT = Path(__file__).resolve().parents[1]


class DeviceBuilderPackage(unittest.TestCase):
    def load(self, yaml):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            config_path = directory / "builder.yaml"
            config_path.write_text(yaml)
            CORE.reset()
            CORE.config_path = config_path
            try:
                return read_config({})
            finally:
                CORE.reset()

    def test_builder_identity_and_secrets_survive_package_merge(self):
        key = base64.b64encode(bytes(range(32))).decode()
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            config_path = directory / "builder.yaml"
            config_path.write_text(
                '''esphome:
  name: builder-test
  friendly_name: My bridge
wifi:
  ssid: WizardNetwork
  password: WizardPassword
api:
  encryption:
    key: "'''
                + key
                + '''"
ota:
  - platform: esphome
    encryption:
      key: "'''
                + key
                + """"
"""
                + (ROOT / "examples/device-builder.yaml").read_text()
            )
            (
                directory / "secrets.yaml"
            ).write_text("""odelic_light_mac: "10:20:30:40:50:60"
odelic_lighting_id: "00010007"
""")
            CORE.reset()
            CORE.config_path = config_path
            try:
                with patch(
                    "esphome.git.clone_or_update", return_value=(ROOT, None)
                ) as clone:
                    config = read_config({})
                self.assertIsNotNone(config)
                self.assertEqual(config["esphome"]["name"], "builder-test")
                self.assertEqual(config["esphome"]["friendly_name"], "My bridge")
                self.assertEqual(config["api"]["encryption"]["key"], key)
                self.assertEqual(config["wifi"]["networks"][0]["ssid"], "WizardNetwork")
                self.assertEqual(config["ota"][0]["encryption"]["key"], key)
                self.assertNotIn("password", config["ota"][0])
                self.assertEqual(config["odelic"][0]["lighting_id"], "00000000")
                self.assertFalse(config["ble_client"][0]["auto_connect"])
                self.assertEqual(
                    config["odelic_controller"]["fixtures"][0]["lighting_id"],
                    "00010007",
                )
                self.assertEqual(
                    str(config["odelic_controller"]["fixtures"][0]["mac_address"]),
                    "10:20:30:40:50:60",
                )
                self.assertEqual(config["esp32"]["board"], "esp32-s3-devkitc-1")
                self.assertEqual(clone.call_count, 2)
                for call in clone.call_args_list:
                    self.assertIsNone(call.kwargs.get("username"))
                    self.assertIsNone(call.kwargs.get("password"))
                    self.assertEqual(call.kwargs["ref"], "main")
            finally:
                CORE.reset()

    def test_static_roster_reserves_independent_fixture_sessions(self):
        config = self.load(
            """esphome:
  name: static-multi-test
substitutions:
  odelic_light_mac: "10:20:30:40:50:60"
  odelic_lighting_id: "00010007"
packages:
  odelic: !include """
            + str(ROOT / "packages/atoms3-lite.yaml")
            + """
external_components:
  - source:
      type: local
      path: """
            + str(ROOT / "components")
            + """
    components: [odelic, odelic_controller]
odelic_controller:
  fixtures:
    - id: bedroom
      name: Bedroom
      mac_address: "10:20:30:40:50:62"
      lighting_id: "00010008"
"""
        )
        self.assertIsNotNone(config)
        self.assertEqual(len(config["odelic_controller"]["fixtures"]), 2)
        self.assertEqual(len(config["odelic_controller"]["_slots"]), 2)
        self.assertEqual(len(set(config["odelic_controller"]["_uids"])), 2)
        self.assertTrue(config["odelic_controller"]["fixtures"][0]["root_device"])
        self.assertFalse(config["odelic_controller"]["fixtures"][1]["root_device"])
        self.assertFalse(config["ble_client"][0]["auto_connect"])


if __name__ == "__main__":
    unittest.main()
