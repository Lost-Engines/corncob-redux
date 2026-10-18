#!/usr/bin/env python3
"""Record original walking displacement, including both native time adjustments.

Development only: requires Unicorn and the sibling corncob-reverse workspace.
The step timer gate and alternating heading wobble are outside this fixture.
"""
import hashlib
import json
from pathlib import Path
import random
import struct
from generate_native_reference import load, run, DATA_ADDRESS, LOAD_ADDRESS, EXPECTED_SHA256
from unicorn.x86_const import UC_X86_REG_AX

ROOT = Path(__file__).resolve().parents[1]


def main():
    exe = (ROOT.parent / 'corncob-reverse/installs/deluxe_dnk/3.EXE').read_bytes()
    assert hashlib.sha256(exe).hexdigest() == EXPECTED_SHA256
    native = load(exe)
    rng = random.Random(0x57414c4b)
    headings = [0, 8192, 16384, 24576, 32768, 40960, 49152, 57344]
    headings += [rng.randrange(65536) for _ in range(24)]
    cases = []
    for heading in headings:
        for stride in (800, -800):
            for elapsed in (2, 5, 10, 12, 20, 35, 50):
                native.mem_write(DATA_ADDRESS + 0xb05, struct.pack('<3i', 0, 0, 720))
                native.mem_write(DATA_ADDRESS + 0xb11, struct.pack('<3H', heading, 0, 0))
                native.mem_write(DATA_ADDRESS + 0x1b7f, struct.pack('<h', 5))
                native.mem_write(DATA_ADDRESS + 0x1e5e, struct.pack('<h', elapsed))
                native.reg_write(UC_X86_REG_AX, stride & 65535)
                # Execute the caller's WTADJ and stride write, stopping before its STEP call.
                native.emu_start(LOAD_ADDRESS + 0x4858 - 512, LOAD_ADDRESS + 0x486b - 512, count=10000)
                run(native, 0x14eb)
                x, y, z = struct.unpack('<3i', native.mem_read(DATA_ADDRESS + 0xb05, 12))
                assert z == 720
                cases.append((heading, stride, elapsed, x, y))
    output = b'CORNWLK1' + struct.pack('<II', len(cases), 0)
    output += b''.join(struct.pack('<Hhhii', *row) for row in cases)
    destination = ROOT / 'tests/reference/walking.bin'
    destination.write_bytes(output)
    destination.with_suffix('.json').write_text(json.dumps({
        'reference_sha256': EXPECTED_SHA256,
        'fixture_sha256': hashlib.sha256(output).hexdigest(),
        'generator': 'tools/generate_walking_reference.py',
        'cases': len(cases),
        'routines': ['0x4858..0x486b caller WTADJ and stride write', '0x14eb GOODSTEP', '0x1538 MOVEOBS'],
        'stubs': [],
        'scope': 'Original forward/backward walking displacement over varied headings and time steps, including T=50; excludes step gate and camera wobble.'
    }, indent=2) + '\n')
    print(f'Recorded {len(cases)} original walking displacements: {hashlib.sha256(output).hexdigest()}')


if __name__ == '__main__':
    main()
