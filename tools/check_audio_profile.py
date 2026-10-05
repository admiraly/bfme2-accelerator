"""Map a verified reference into a temporary data file for the native guard test."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from accelerator_audit import BASELINE_SHA256, image_sections


def main():
    if len(sys.argv) != 3:
        raise ValueError('usage: check_audio_profile.py GAME.DAT GUARD_TEST')
    data = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(data).hexdigest() != BASELINE_SHA256:
        raise ValueError('pinned baseline required for this regression test')
    base, sections, _ = image_sections(data)
    if base != 0x400000:
        raise ValueError('unexpected image base')
    length = max(rva + len(body) for _, rva, body in sections)
    if length > 32 * 1024 * 1024:
        raise ValueError('image exceeds test bounds')
    image = bytearray(length)
    for _, rva, body in sections:
        image[rva:rva + len(body)] = body
    with tempfile.TemporaryDirectory() as tmp:
        mapped = Path(tmp) / 'mapped-image.bin'
        mapped.write_bytes(image)
        return subprocess.call([str(Path(sys.argv[2]).resolve()), str(mapped)])


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(2)
