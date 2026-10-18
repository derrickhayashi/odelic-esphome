import asyncio
import unittest

from aioesphomeapi import (
    BinarySensorInfo,
    BinarySensorState,
    LightInfo,
    LightState,
    SelectInfo,
    SelectState,
)

from scripts.hardware_acceptance import (
    PRESETS,
    StateTracker,
    require_fixture_entities,
    run_acceptance,
)


class FakeClient:
    def __init__(self):
        self.light = LightInfo(key=1, name="Ceiling light")
        self.preset = SelectInfo(key=2, name="White preset", options=list(PRESETS))
        self.valid = BinarySensorInfo(key=3, name="Fixture state valid")
        self.on = True
        self.brightness = 0.8
        self.option = "Natural white"
        self.callback = None
        self.commands = []
        self.disconnected = False

    async def connect(self, login):
        self.commands.append(("connect", login))

    async def list_entities_services(self):
        return [self.light, self.preset, self.valid], []

    def subscribe_states(self, callback):
        self.callback = callback
        asyncio.get_running_loop().call_soon(self.publish_all)

    def publish_all(self):
        self.callback(BinarySensorState(key=3, state=True))
        self.callback(LightState(key=1, state=self.on, brightness=self.brightness))
        self.callback(SelectState(key=2, state=self.option))

    def light_command(self, key, state=None, brightness=None, **_):
        self.commands.append(("light", state, brightness))
        if state is not None:
            self.on = state
        if brightness is not None:
            self.brightness = brightness
        asyncio.get_running_loop().call_soon(self.publish_all)

    def select_command(self, key, state, **_):
        self.commands.append(("preset", state))
        self.on = True
        self.option = state
        asyncio.get_running_loop().call_soon(self.publish_all)

    async def disconnect(self):
        self.disconnected = True


class HardwareAcceptanceHelpers(unittest.IsolatedAsyncioTestCase):
    def test_requires_exactly_one_complete_fixture_entity_set(self):
        entities = [
            LightInfo(key=1, name="Ceiling light"),
            SelectInfo(key=2, name="White preset", options=list(PRESETS)),
            BinarySensorInfo(key=3, name="Fixture state valid"),
        ]
        light, preset, valid = require_fixture_entities(entities)
        self.assertEqual((light.key, preset.key, valid.key), (1, 2, 3))
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            require_fixture_entities(entities + [LightInfo(key=4, name="Other")])

    async def test_wait_requires_a_new_matching_readback(self):
        tracker = StateTracker(1, 2, 3, lambda _: None)
        tracker.update(LightState(key=1, state=True, brightness=0.8))
        tracker.update(SelectState(key=2, state="Natural white"))
        tracker.update(BinarySensorState(key=3, state=True))
        revision = tracker.revision

        async def publish():
            await asyncio.sleep(0)
            tracker.update(LightState(key=1, state=True, brightness=0.5))

        task = asyncio.create_task(publish())
        state = await tracker.wait_for(
            "50%", lambda value: value.valid and value.brightness == 0.5,
            timeout=1, after_revision=revision,
        )
        await task
        self.assertEqual(state.brightness, 0.5)

    async def test_wait_rejects_invalid_state(self):
        tracker = StateTracker(1, 2, 3, lambda _: None)
        tracker.update(LightState(key=1, state=False, brightness=0.8))
        tracker.update(SelectState(key=2, state="Natural white"))
        tracker.update(BinarySensorState(key=3, state=False))
        with self.assertRaisesRegex(RuntimeError, "Timed out"):
            await tracker.wait_for(
                "valid OFF", lambda value: value.valid and not value.on, timeout=0.01
            )

    async def test_full_sequence_restores_baseline_and_disconnects(self):
        client = FakeClient()
        baseline = await run_acceptance(client, timeout=1, record=lambda _: None)
        self.assertTrue(client.disconnected)
        self.assertTrue(client.on)
        self.assertEqual(client.option, "Natural white")
        self.assertAlmostEqual(client.brightness, 0.8)
        self.assertEqual((baseline.on, baseline.preset, baseline.brightness),
                         (True, "Natural white", 0.8))
        self.assertIn(("light", False, None), client.commands)
        for preset in PRESETS:
            self.assertIn(("preset", preset), client.commands)


if __name__ == "__main__":
    unittest.main()
