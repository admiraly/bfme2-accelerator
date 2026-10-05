#!/usr/bin/env python3
"""Read-only accelerator preflight. Static evidence never authorizes a hook."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE_SHA256 = 'f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640'
BASELINE_SIZE = 10969600


def image_sections(data):
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('truncated PE metadata')
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b'MZ':
        raise ValueError('DOS signature required')
    pe, = unpack('<I', 0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('PE signature required')
    machine, count = unpack('<HH', pe + 4)
    optional_size, = unpack('<H', pe + 20)
    optional = pe + 24
    magic, = unpack('<H', optional)
    if machine != 0x14c or magic != 0x10b or optional_size < 112:
        raise ValueError('x86 PE32 image required')
    base, = unpack('<I', optional + 28)
    table = optional + optional_size
    sections, mappings = [], []
    for index in range(count):
        at = table + index * 40
        unpack('<40s', at)
        name = data[at:at + 8].rstrip(b'\0').decode('ascii')
        virtual_size, rva, raw_size, raw_at = unpack('<IIII', at + 8)
        if raw_at + raw_size > len(data):
            raise ValueError('section extends beyond file')
        sections.append((name, rva, data[raw_at:raw_at + min(virtual_size, raw_size)]))
        mappings.append((rva, raw_size, raw_at))

    def file_at(rva, size):
        for start, length, offset in mappings:
            if start <= rva and rva + size <= start + length:
                return offset + rva - start, offset + length
        raise ValueError('import metadata is not file-backed')

    dlls = []
    directories, = unpack('<I', optional + 92)
    if directories >= 2:
        import_rva, import_size = unpack('<II', optional + 104)
        if import_rva or import_size:
            if not import_rva or import_size < 20:
                raise ValueError('invalid import directory')
            for index in range(import_size // 20 + 1):
                at, _ = file_at(import_rva + index * 20, 20)
                descriptor = unpack('<IIIII', at)
                if not any(descriptor):
                    break
                if (index + 1) * 20 > import_size:
                    raise ValueError('unterminated import descriptors')
                name_at, end = file_at(descriptor[3], 1)
                zero = data.find(b'\0', name_at, end)
                if zero < 0 or zero == name_at:
                    raise ValueError('invalid import DLL name')
                dlls.append(data[name_at:zero].decode('ascii').lower())
            else:
                raise ValueError('unterminated import directory')
    return base, sections, sorted(set(dlls))


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def build_table(source):
    table = re.search(r'static const Build kBuilds\[\]\s*=\s*\{(.*?)\n\s*\};', source, re.S)
    if not table:
        raise ValueError('accelerator build table missing or format changed')
    entries = re.findall(
        r'\{\s*(0x[\da-fA-F]+)\s*,\s*(0x[\da-fA-F]+)\s*,\s*'
        r'(0x[\da-fA-F]+)\s*,\s*(0x[\da-fA-F]+)\s*,\s*'
        r'(0x[\da-fA-F]+)\s*,\s*"([^"]+)"\s*,\s*(true|false)\s*\}', table[1])
    if not entries:
        raise ValueError('no recognized build-table rows')
    return [dict(text_hash=int(row[0], 16), name=row[5], engine_hooks=row[6] == 'true')
            for row in entries]


def audit(accelerator, image):
    data = image.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    base, sections, dlls = image_sections(data)
    text_sections = [item for item in sections if item[0] == '.text']
    if len(text_sections) != 1:
        raise ValueError('exactly one .text section required')
    _, text_rva, text = text_sections[0]
    source = (accelerator / 'src/aotr_accel.cpp').read_text()
    fingerprint = fnv1a(text)
    matches = [item for item in build_table(source) if item['text_hash'] == fingerprint]
    if len(matches) > 1:
        raise ValueError('ambiguous build hash')
    audio = (accelerator / 'src/aotr_audiolimit.inc').read_text()
    loop = re.search(r'kLoop\[54\]\s*=\s*\{(.*?)\};', audio, re.S)
    if not loop:
        raise ValueError('audio-loop guard missing or format changed')
    pattern = bytes(int(word, 16) for word in re.findall(r'0x[\da-fA-F]+', loop[1]))
    if len(pattern) != 54:
        raise ValueError('audio-loop guard length changed')
    # An exact loop hit is a structural lead only: callers, offsets and every
    # list mutation still need proof. Do not generate or enable patches here.
    hits = []
    cursor = 0
    while True:
        at = text.find(pattern, cursor)
        if at < 0:
            break
        hits.append(f'0x{base + text_rva + at:08X}')
        cursor = at + 1
    return dict(
        image_sha256=digest,
        repository_baseline_matches=digest == BASELINE_SHA256 and len(data) == BASELINE_SIZE,
        accelerator_source_sha256=hashlib.sha256(source.encode()).hexdigest(),
        image_base=f'0x{base:08X}', text_fnv1a=f'0x{fingerprint:08X}',
        recognized_build=matches[0]['name'] if matches else None,
        engine_hooks_enabled_by_build_table=matches[0]['engine_hooks'] if matches else False,
        family_static_prerequisites=base == 0x400000 and bool(text)
        and {'msvcr71.dll', 'mss32.dll'} <= set(dlls),
        imported_dlls=dlls,
        audio_loop_exact_hits=hits,
        runtime_verified=False,
        limitations='Imports do not prove DLLs are loaded. Hash recognition does not prove hook safety. '
        'Audio hits do not establish function identity or object layout. No executable is modified.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--accelerator', type=Path, default=ROOT, help='accelerator source checkout')
    parser.add_argument('--image', type=Path, required=True, help='BFME II game.dat to inspect')
    args = parser.parse_args()
    try:
        result = audit(args.accelerator, args.image)
    except (ValueError, OSError, struct.error, UnicodeError) as error:
        parser.exit(2, f'accelerator audit refused: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
