#!/usr/bin/env python3
"""Verify actual pinned-image LAA copies, rejection paths and accelerator recognition."""
import json
import struct
import sys
import tempfile
from pathlib import Path

from accelerator_audit import ROOT, audit
from bfme2_laa import inspect_or_copy, prepare


def refused(function):
    try:
        function()
    except (ValueError, OSError):
        return
    raise AssertionError('expected refusal')


def main():
    source = Path(sys.argv[1])
    original = source.read_bytes()
    patched, report = prepare(original)
    assert not report['large_address_aware_before']
    assert report['large_address_aware_after']
    assert prepare(patched)[0] == patched
    assert prepare(patched)[1]['changed_byte_offsets'] == []
    refused(lambda: prepare(b''))
    damaged = bytearray(original)
    damaged[0] ^= 1
    refused(lambda: prepare(damaged))
    damaged = bytearray(original)
    damaged[-1] ^= 1
    refused(lambda: prepare(damaged))
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        src = root / 'game.dat'
        dst = root / 'game.laa.dat'
        src.write_bytes(original)
        assert not inspect_or_copy(src)['output_created']
        refused(lambda: inspect_or_copy(src, src))
        refused(lambda: inspect_or_copy(src, root / '.' / 'game.dat'))
        existing = root / 'existing.dat'
        existing.write_bytes(b'keep this')
        refused(lambda: inspect_or_copy(src, existing))
        assert existing.read_bytes() == b'keep this'
        report = inspect_or_copy(src, dst)
        assert dst.read_bytes() == patched and src.read_bytes() == original
        refused(lambda: inspect_or_copy(src, dst))
        assert inspect_or_copy(dst)['large_address_aware_before']
        original_audit, patched_audit = audit(ROOT, src), audit(ROOT, dst)
        for key in ['text_fnv1a', 'recognized_build', 'image_base', 'imported_dlls',
                    'engine_hooks_enabled_by_build_table', 'audio_loop_exact_hits']:
            assert original_audit[key] == patched_audit[key], key
        assert original_audit['recognized_build'] == 'BFME2'
        assert not original_audit['large_address_aware'] and patched_audit['large_address_aware']
        assert patched_audit['user_va_limit_on_64bit_windows_gib'] == 4
        assert not patched_audit['repository_baseline_matches']
        # An already-patched input produces an identical separate copy.
        second = root / 'second.dat'
        assert inspect_or_copy(dst, second)['changed_byte_offsets'] == []
        assert second.read_bytes() == patched
    assert source.read_bytes() == original
    print(json.dumps(report, indent=2))
    print('LAA: one-bit change, idempotence, source preservation, refusal paths and accelerator recognition passed')


if __name__ == '__main__':
    main()
