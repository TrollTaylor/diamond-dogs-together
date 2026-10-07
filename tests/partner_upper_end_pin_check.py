"""File-only End pin regression check; no process handles, hooks or native calls.

The accepted game executable is supplied locally and is never copied. This
check is separate from portable fake-memory policy fixtures, which cannot
prove a fingerprint covers the intended retail function's complete bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXE_SHA256 = "085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45"


class Image:
    def __init__(self, data):
        self.data = data
        assert data[:2] == b"MZ", "DOS signature"
        nt = self.u32(0x3C)
        assert self.read(nt, 4) == b"PE\0\0", "PE signature"
        assert self.u16(nt + 4) == 0x8664, "AMD64 machine"
        section_count = self.u16(nt + 6)
        optional_size = self.u16(nt + 20)
        optional = nt + 24
        assert optional_size >= 240 and self.u16(optional) == 0x20B, "PE32+ optional header"
        self.image_base = self.u64(optional + 24)
        self.image_size = self.u32(optional + 56)
        assert self.u32(optional + 108) >= 6, "base relocation directory is present"
        self.relocation_rva = self.u32(optional + 112 + 5 * 8)
        self.relocation_size = self.u32(optional + 112 + 5 * 8 + 4)
        self.sections = []
        for i in range(section_count):
            at = optional + optional_size + 40 * i
            virtual_size, rva, raw_size, raw_offset = struct.unpack("<IIII", self.read(at + 8, 16))
            assert raw_offset + raw_size <= len(data), "section file bound"
            self.sections.append((rva, virtual_size, raw_offset, raw_size))

    def read(self, offset, size):
        assert offset >= 0 and size >= 0 and offset + size <= len(self.data), "file read bound"
        return self.data[offset:offset + size]

    def u16(self, at):
        return struct.unpack("<H", self.read(at, 2))[0]

    def u32(self, at):
        return struct.unpack("<I", self.read(at, 4))[0]

    def u64(self, at):
        return struct.unpack("<Q", self.read(at, 8))[0]

    def rva_read(self, at, size):
        assert 0 <= at and 0 < size and at + size <= self.image_size, "RVA image bound"
        matches = [(rva, raw) for rva, virtual, raw, count in self.sections
                   if rva <= at and at + size <= rva + count]
        assert len(matches) == 1, "RVA must map uniquely to complete file-backed bytes"
        rva, raw = matches[0]
        return self.read(raw + at - rva, size)


def check(executable, header):
    data = executable.read_bytes()
    actual_exe_hash = hashlib.sha256(data).hexdigest()
    assert actual_exe_hash == EXE_SHA256, "accepted executable fingerprint"
    image = Image(data)
    block = re.search(r"inline constexpr Pin kPins\[\] = \{(.*?)\n\};", header.read_text(encoding="utf-8"), re.S)
    assert block, "header pin list"
    records = re.findall(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f]{64})"\}', block[1])
    assert records, "nonempty complete pin list"
    leftovers = re.sub(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f]{64})"\}', "", block[1])
    assert not leftovers.strip(" \r\n\t,"), "every pin tuple parsed"
    pins = []
    for rva_text, size_text, expected in records:
        rva, size = int(rva_text, 16), int(size_text, 16)
        actual = hashlib.sha256(image.rva_read(rva, size)).hexdigest()
        assert actual == expected, f"full-span pin mismatch {rva_text}/{size_text}: expected {expected}, actual {actual}"
        pins.append({"rva": rva_text, "size": size_text, "end_rva": hex(rva + size), "sha256": actual})
    # This exact executable has no loader base relocations. Do not equate file
    # and live memory hashes on another binary without a separate proof.
    assert image.relocation_rva == 0 and image.relocation_size == 0, "accepted executable has no loader relocation bytes"
    state_update = [pin for pin in pins if pin["rva"] == "0x1c73dc0"]
    assert len(state_update) == 1 and state_update[0]["end_rva"] == "0x1c7480c", "complete proved controller-state-update span"
    old_prefix = hashlib.sha256(image.rva_read(0x1C73DC0, 0x44C)).hexdigest()
    assert old_prefix != state_update[0]["sha256"], "old 0x44c transcription must fail the full function hash"
    return {
        "executable_sha256": actual_exe_hash,
        "pins": pins,
        "base_relocation_directory": {"rva": 0, "bytes": 0},
        "old_truncated_prefix_sha256": old_prefix,
        "old_truncated_pin_rejected": True,
        "file_only": True,
        "loaded_memory_observed": False,
        "process_handles": 0,
        "native_calls": 0,
        "game_writes": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--header", type=Path, default=Path(__file__).resolve().parents[1] / "native/include/mgscoop/partner_upper_end_observer.hpp")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = check(args.executable, args.header)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"{len(result['pins'])} exact full-span End pins passed; truncated-length regression rejected; file-only, no live-memory/game actions.")


if __name__ == "__main__":
    main()
