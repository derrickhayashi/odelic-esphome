#!/usr/bin/env python3
"""Run the opt-in one-fixture Odelic hardware acceptance sequence."""
import argparse
import asyncio
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable

import yaml
from aioesphomeapi import (
    APIClient,
    BinarySensorInfo,
    BinarySensorState,
    LightInfo,
    LightState,
    SelectInfo,
    SelectState,
)

PRESETS = (
    "Warm white",
    "Natural white",
    "Cool white",
    "Warm/Cool cool endpoint",
)
TEST_BRIGHTNESS = 0.5
BRIGHTNESS_TOLERANCE = 0.01


@dataclass(frozen=True)
class FixtureState:
    on: bool
    brightness: float
    preset: str
    valid: bool


class StateTracker:
    def __init__(self, light_key: int, preset_key: int, valid_key: int, record: Callable[[str], None]):
        self.light_key = light_key
        self.preset_key = preset_key
        self.valid_key = valid_key
        self.record = record
        self.light = None
        self.preset = None
        self.valid = None
        self.revision = 0
        self.changed = asyncio.Event()

    def update(self, state):
        relevant = False
        if isinstance(state, LightState) and state.key == self.light_key:
            self.light = state
            relevant = True
        elif isinstance(state, SelectState) and state.key == self.preset_key:
            self.preset = state
            relevant = True
        elif isinstance(state, BinarySensorState) and state.key == self.valid_key:
            self.valid = state
            relevant = True
        if relevant:
            self.revision += 1
            self.record(f"STATE {type(state).__name__} {asdict(state)}")
            self.changed.set()

    def snapshot(self):
        if self.light is None or self.preset is None or self.valid is None:
            return None
        if self.preset.missing_state or self.valid.missing_state:
            return None
        return FixtureState(
            on=self.light.state,
            brightness=self.light.brightness,
            preset=self.preset.state,
            valid=self.valid.state,
        )

    async def wait_for(self, description: str, predicate: Callable[[FixtureState], bool],
                       timeout: float, after_revision: int = 0):
        async def wait():
            while True:
                snapshot = self.snapshot()
                if self.revision > after_revision and snapshot is not None and predicate(snapshot):
                    self.record(f"CONFIRMED {description}: {format_state(snapshot)}")
                    return snapshot
                self.changed.clear()
                snapshot = self.snapshot()
                if self.revision > after_revision and snapshot is not None and predicate(snapshot):
                    self.record(f"CONFIRMED {description}: {format_state(snapshot)}")
                    return snapshot
                await self.changed.wait()

        try:
            return await asyncio.wait_for(wait(), timeout)
        except TimeoutError as error:
            snapshot = self.snapshot()
            observed = "no complete fixture state" if snapshot is None else format_state(snapshot)
            raise RuntimeError(f"Timed out waiting for {description}; last observed {observed}") from error


def close_to(actual: float, expected: float):
    return abs(actual - expected) <= BRIGHTNESS_TOLERANCE


def format_state(state: FixtureState):
    power = "ON" if state.on else "OFF"
    return f"{power}, {state.preset}, {round(state.brightness * 100)}%, valid={state.valid}"


def require_fixture_entities(entities):
    lights = [entity for entity in entities if isinstance(entity, LightInfo)]
    presets = [entity for entity in entities if isinstance(entity, SelectInfo)]
    valid = [
        entity for entity in entities
        if isinstance(entity, BinarySensorInfo) and entity.name == "Fixture state valid"
    ]
    if len(lights) != 1 or len(presets) != 1 or len(valid) != 1:
        raise RuntimeError(
            "Acceptance test requires exactly one light, one preset, and one "
            "Fixture state valid entity"
        )
    missing = [preset for preset in PRESETS if preset not in presets[0].options]
    if missing:
        raise RuntimeError(f"Preset entity is missing required options: {', '.join(missing)}")
    return lights[0], presets[0], valid[0]


async def command_and_wait(tracker, description, command, predicate, timeout):
    revision = tracker.revision
    tracker.record(f"HOST_COMMAND {description}")
    command()
    return await tracker.wait_for(description, predicate, timeout, revision)


async def restore_baseline(client, tracker, light, preset, baseline, timeout):
    tracker.record(f"RESTORE_BEGIN {format_state(baseline)}")
    await command_and_wait(
        tracker,
        f"restore preset {baseline.preset}",
        lambda: client.select_command(preset.key, baseline.preset, device_id=preset.device_id),
        lambda state: state.valid and state.on and state.preset == baseline.preset,
        timeout,
    )
    await command_and_wait(
        tracker,
        f"restore brightness {round(baseline.brightness * 100)}%",
        lambda: client.light_command(
            light.key, state=True, brightness=baseline.brightness,
            transition_length=0, device_id=light.device_id
        ),
        lambda state: (
            state.valid and state.on and state.preset == baseline.preset
            and close_to(state.brightness, baseline.brightness)
        ),
        timeout,
    )
    if not baseline.on:
        await command_and_wait(
            tracker,
            "restore OFF",
            lambda: client.light_command(
                light.key, state=False, transition_length=0, device_id=light.device_id
            ),
            lambda state: (
                state.valid and not state.on and state.preset == baseline.preset
                and close_to(state.brightness, baseline.brightness)
            ),
            timeout,
        )
    tracker.record(f"RESTORE_COMPLETE {format_state(tracker.snapshot())}")


async def run_acceptance(client, timeout, record):
    await client.connect(login=True)
    baseline = None
    tracker = None
    light = preset = None
    try:
        entities, _ = await client.list_entities_services()
        light, preset, valid = require_fixture_entities(entities)
        record("ENCRYPTED_API_CONNECTED")
        for entity in (valid, light, preset):
            record(
                f"ENTITY {type(entity).__name__} name={entity.name} "
                f"key={entity.key} device_id={entity.device_id}"
            )
        tracker = StateTracker(light.key, preset.key, valid.key, record)
        client.subscribe_states(tracker.update)
        baseline = await tracker.wait_for(
            "valid baseline", lambda state: state.valid and state.preset in PRESETS, timeout
        )
        record(f"BASELINE {format_state(baseline)}")

        await command_and_wait(
            tracker, "OFF",
            lambda: client.light_command(
                light.key, state=False, transition_length=0, device_id=light.device_id
            ),
            lambda state: state.valid and not state.on,
            timeout,
        )
        await command_and_wait(
            tracker, "ON",
            lambda: client.light_command(
                light.key, state=True, transition_length=0, device_id=light.device_id
            ),
            lambda state: (
                state.valid and state.on and state.preset == baseline.preset
                and close_to(state.brightness, baseline.brightness)
            ),
            timeout,
        )
        await command_and_wait(
            tracker, "brightness 50%",
            lambda: client.light_command(
                light.key, state=True, brightness=TEST_BRIGHTNESS,
                transition_length=0, device_id=light.device_id
            ),
            lambda state: (
                state.valid and state.on and state.preset == baseline.preset
                and close_to(state.brightness, TEST_BRIGHTNESS)
            ),
            timeout,
        )
        for option in PRESETS:
            await command_and_wait(
                tracker, f"preset {option}",
                lambda option=option: client.select_command(
                    preset.key, option, device_id=preset.device_id
                ),
                lambda state, option=option: (
                    state.valid and state.on and state.preset == option
                    and close_to(state.brightness, TEST_BRIGHTNESS)
                ),
                timeout,
            )
        record("ACCEPTANCE_SEQUENCE_PASSED")
    finally:
        restore_error = None
        if baseline is not None and tracker is not None:
            try:
                await restore_baseline(client, tracker, light, preset, baseline, timeout)
            except Exception as error:
                record(f"RESTORE_FAILED {error}")
                restore_error = error
        await client.disconnect()
        if restore_error is not None:
            raise restore_error
    return baseline


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="odelic-bridge.local")
    parser.add_argument("--secrets", type=Path, default=Path("examples/secrets.yaml"))
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--confirm-actuation",
        action="store_true",
        help="required acknowledgement that this test will operate a real fixture",
    )
    return parser.parse_args()


async def main():
    args = parse_args()
    if not args.confirm_actuation:
        raise SystemExit("Refusing to actuate a fixture without --confirm-actuation")
    private = yaml.safe_load(args.secrets.read_text())
    client = APIClient(
        args.host,
        6053,
        noise_psk=private["api_encryption_key"],
        expected_name="odelic-bridge",
        client_info="Odelic hardware acceptance",
    )
    directory = Path(__file__).resolve().parents[1] / "captures"
    directory.mkdir(exist_ok=True)
    path = directory / datetime.now().strftime("%Y%m%d-%H%M%S-hardware-acceptance.log")
    with path.open("w", buffering=1) as log:
        def record(message):
            line = datetime.now().astimezone().isoformat(timespec="milliseconds") + " " + message
            print(line, flush=True)
            log.write(line + "\n")

        try:
            baseline = await run_acceptance(client, args.timeout, record)
        except Exception as error:
            record(f"TEST_FAILED {error}")
            raise
        record(f"TEST_PASSED baseline restored: {format_state(baseline)}")
        record(f"EVIDENCE {path}")


if __name__ == "__main__":
    asyncio.run(main())
