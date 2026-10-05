#!/usr/bin/env python3
"""Inspect or create an LAA-enabled copy of the pinned vanilla BFME II 1.06 image."""
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path

from accelerator_audit import BASELINE_SHA256, BASELINE_SIZE, image_sections

LAA = 0x20


def prepare(data):
    if len(data) != BASELINE_SIZE:
        raise ValueError('expected the pinned vanilla 1.06 file size')
    base, _sections, _imports = image_sections(data)
    if base != 0x400000:
        raise ValueError('expected vanilla 1.06 image base')
    pe, = struct.unpack_from('<I', data, 0x3c)
    offset = pe + 22
    before, = struct.unpack_from('<H', data, offset)
    normalized = bytearray(data)
    struct.pack_into('<H', normalized, offset, before & ~LAA)
    if hashlib.sha256(normalized).hexdigest() != BASELINE_SHA256:
        raise ValueError('not the pinned vanilla 1.06 image (or its LAA-only variant); no output written')
    patched = bytearray(data)
    struct.pack_into('<H', patched, offset, before | LAA)
    changes = [i for i, (a, b) in enumerate(zip(data, patched)) if a != b]
    if changes != ([] if before & LAA else [offset]):
        raise ValueError('unexpected byte changes')
    return bytes(patched), dict(
        large_address_aware_before=bool(before & LAA),
        large_address_aware_after=True,
        characteristics_before=f'0x{before:04X}',
        characteristics_after=f'0x{before | LAA:04X}',
        changed_byte_offsets=[f'0x{i:X}' for i in changes],
        input_sha256=hashlib.sha256(data).hexdigest(),
        output_sha256=hashlib.sha256(patched).hexdigest(),
        user_va_limit_before_on_64bit_windows_gib=4 if before & LAA else 2,
        user_va_limit_after_on_64bit_windows_gib=4,
        in_game_high_address_validation=False)


def inspect_or_copy(image, output=None):
    image = Path(image)
    if image.stat().st_size != BASELINE_SIZE:
        raise ValueError('expected the pinned vanilla 1.06 file size')
    original = image.read_bytes()
    patched, report = prepare(original)
    report['input'] = str(image)
    report['output_created'] = False
    if output is None:
        report['mode'] = 'inspect; proposed output only'
        return report
    output = Path(output)
    if output.resolve() == image.resolve():
        raise ValueError('output must be a separate path; original is never overwritten')
    # Exclusive creation also refuses existing files, symlinks and hard links.
    created = False
    try:
        with output.open('xb') as stream:
            created = True
            stream.write(patched)
            stream.flush()
            os.fsync(stream.fileno())
        if output.read_bytes() != patched:
            raise OSError('output verification failed')
    except (OSError, ValueError):
        if created:
            output.unlink(missing_ok=True)
        raise
    report.update(mode='patched copy', output=str(output), output_created=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path, help='original game.dat (never overwritten)')
    parser.add_argument('--output', type=Path, help='new file to create; omit for read-only inspection')
    args = parser.parse_args()
    try:
        result = inspect_or_copy(args.image, args.output)
    except (ValueError, OSError, struct.error, UnicodeError) as error:
        parser.exit(2, f'LAA patch refused: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
