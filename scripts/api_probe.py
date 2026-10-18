#!/usr/bin/env python3
"""Exercise one bridge's native API without exposing its encryption key."""
import argparse
import asyncio
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
import yaml
from aioesphomeapi import APIClient, LightInfo, SelectInfo

async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='odelic-bridge.local')
    parser.add_argument('--secrets', type=Path, default=Path('examples/secrets.yaml'))
    args = parser.parse_args()
    private = yaml.safe_load(args.secrets.read_text())
    client = APIClient(args.host, 6053, noise_psk=private['api_encryption_key'],
                       expected_name='odelic-bridge', client_info='Odelic development probe')
    directory = Path(__file__).resolve().parents[1] / 'captures'
    directory.mkdir(exist_ok=True)
    with (directory / datetime.now().strftime('%Y%m%d-%H%M%S-api.log')).open('w', buffering=1) as log:
        def record(message):
            line = datetime.now().astimezone().isoformat(timespec='milliseconds') + ' ' + message
            print(line, flush=True)
            log.write(line + '\n')
        await client.connect(login=True)
        try:
            entities, _ = await client.list_entities_services()
            lights = [entity for entity in entities if isinstance(entity, LightInfo)]
            presets = [entity for entity in entities if isinstance(entity, SelectInfo)]
            if len(lights) != 1 or len(presets) != 1:
                raise RuntimeError('Probe requires exactly one light and one preset entity')
            record('ENCRYPTED_API_CONNECTED')
            for entity in entities:
                record(f'ENTITY {type(entity).__name__} name={entity.name} key={entity.key}')
            client.subscribe_states(lambda state: record(f'STATE {type(state).__name__} {asdict(state)}'))
            record('Commands: status, on, off, brightness PERCENT, preset NAME, quit. Commands do not run automatically.')
            while True:
                try:
                    line = await asyncio.to_thread(input)
                except EOFError:
                    break
                if line == 'quit':
                    break
                parts = line.split(maxsplit=1)
                try:
                    if line == 'status':
                        record('State events are fixture readback; no control sent.')
                    elif line in ('on', 'off'):
                        record('HOST_COMMAND ' + line)
                        client.light_command(lights[0].key, state=line == 'on', transition_length=0)
                    elif len(parts) == 2 and parts[0] == 'brightness':
                        percent = int(parts[1])
                        if not 5 <= percent <= 100 or percent % 5:
                            raise ValueError('Brightness must be 5..100 in steps of five')
                        record('HOST_COMMAND ' + line)
                        client.light_command(lights[0].key, state=True, brightness=percent / 100, transition_length=0)
                    elif len(parts) == 2 and parts[0] == 'preset' and parts[1] in presets[0].options:
                        record('HOST_COMMAND ' + line)
                        client.select_command(presets[0].key, parts[1])
                    else:
                        record('Invalid command; use an advertised preset name')
                except ValueError as error:
                    record(str(error))
        finally:
            await client.disconnect()

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
