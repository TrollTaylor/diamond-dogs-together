#!/usr/bin/env python3
"""Read-only, standard-library PE inventory for an independently built MGSV mod.

No target executable is loaded or run. String addresses identify file data only;
they are not validated functions, object layouts, hooks, or call targets.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from typing import Any


DEFAULT_GAME_DIR = Path(r"D:\Games\steamapps\common\MGS_TPP")
FILES = ("mgsvtpp.exe", "mgsvmgo.exe", "steam_api64.dll")
SCRIPT_ANCHORS = (
    "TppNetworkUtil",
    "IsSessionConnect",
    "GetPlayerGameObjectId",
    "CreateSession",
    "GetSessionMemberCount",
    "GetLocalPlayerId",
    "GetPlayerPosition",
    "TppMain",
    "TppMission",
    "TppPlayer",
    "GameObject",
    "SendCommand",
)
MAX_ANCHOR_MATCHES = 16
MAX_CSTRING = 4096
MAX_IMPORT_DESCRIPTORS = 8192
MAX_THUNKS = 65536
MAX_EXPORT_NAMES = 100000


class PEError(ValueError):
    """The file does not contain a supported, bounded PE structure."""


class PEFile:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if self.read(0, 2) != b"MZ":
            raise PEError("missing DOS MZ signature")
        self.pe_offset = self.u32(0x3C)
        if self.read(self.pe_offset, 4) != b"PE\0\0":
            raise PEError("missing PE signature")
        coff = self.pe_offset + 4
        self.machine, section_count = self.unpack("<HH", coff)
        self.timestamp = self.u32(coff + 4)
        optional_size = self.u16(coff + 16)
        self.characteristics = self.u16(coff + 18)
        optional = coff + 20
        self.read(optional, optional_size)
        magic = self.u16(optional)
        if magic == 0x20B:
            self.bits = 64
            self.image_base = self.u64(optional + 24)
            directory_count_offset = 108
            directory_offset = 112
        elif magic == 0x10B:
            self.bits = 32
            self.image_base = self.u32(optional + 28)
            directory_count_offset = 92
            directory_offset = 96
        else:
            raise PEError(f"unsupported optional-header magic {magic:#x}")
        if optional_size < directory_offset:
            raise PEError("truncated optional header")
        self.entrypoint_rva = self.u32(optional + 16)
        self.image_size = self.u32(optional + 56)
        self.headers_size = self.u32(optional + 60)
        directory_count = self.u32(optional + directory_count_offset)
        if directory_count > (optional_size - directory_offset) // 8:
            raise PEError("data directories extend beyond optional header")
        self.directories = [
            self.unpack("<II", optional + directory_offset + index * 8)
            for index in range(min(directory_count, 16))
        ]
        if section_count > 1024:
            raise PEError("unreasonable section count")
        self.sections: list[dict[str, Any]] = []
        for index in range(section_count):
            offset = optional + optional_size + index * 40
            header = self.read(offset, 40)
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", header, 8)
            if raw_size:
                self.read(raw_offset, raw_size)
            self.sections.append({
                "name": header[:8].split(b"\0", 1)[0].decode("ascii", errors="replace"),
                "rva": rva,
                "virtual_size": virtual_size,
                "raw_offset": raw_offset,
                "raw_size": raw_size,
                "characteristics": struct.unpack_from("<I", header, 36)[0],
            })

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise PEError(f"out-of-file read at {offset:#x}, size {size}")
        return self.data[offset:offset + size]

    def unpack(self, fmt: str, offset: int) -> tuple[Any, ...]:
        return struct.unpack(fmt, self.read(offset, struct.calcsize(fmt)))

    def u16(self, offset: int) -> int:
        return self.unpack("<H", offset)[0]

    def u32(self, offset: int) -> int:
        return self.unpack("<I", offset)[0]

    def u64(self, offset: int) -> int:
        return self.unpack("<Q", offset)[0]

    def directory(self, index: int) -> tuple[int, int]:
        return self.directories[index] if index < len(self.directories) else (0, 0)

    def rva_offset(self, rva: int) -> int:
        if 0 <= rva < min(self.headers_size, len(self.data)):
            return rva
        for section in self.sections:
            relative = rva - section["rva"]
            if 0 <= relative < max(section["virtual_size"], section["raw_size"]):
                if relative >= section["raw_size"]:
                    raise PEError(f"RVA {rva:#x} points to zero-filled memory, not file data")
                return section["raw_offset"] + relative
        raise PEError(f"unmapped RVA {rva:#x}")

    def rva_read(self, rva: int, size: int) -> bytes:
        offset = self.rva_offset(rva)
        if size and self.rva_offset(rva + size - 1) != offset + size - 1:
            raise PEError(f"RVA read crosses non-contiguous file ranges at {rva:#x}")
        return self.read(offset, size)

    def cstring(self, rva: int) -> str:
        offset = self.rva_offset(rva)
        end = self.data.find(b"\0", offset, min(len(self.data), offset + MAX_CSTRING))
        if end < 0:
            raise PEError(f"unterminated or oversized string at RVA {rva:#x}")
        self.rva_read(rva, end - offset + 1)
        return self.data[offset:end].decode("ascii", errors="replace")

    def location(self, offset: int) -> dict[str, Any]:
        rva = None
        name = None
        if offset < self.headers_size:
            rva, name = offset, "headers"
        else:
            for section in self.sections:
                relative = offset - section["raw_offset"]
                if 0 <= relative < section["raw_size"]:
                    rva, name = section["rva"] + relative, section["name"]
                    break
        return {
            "file_offset": hex(offset),
            "rva": hex(rva) if rva is not None else None,
            "preferred_image_va": hex(self.image_base + rva) if rva is not None else None,
            "section": name,
            "interpretation": "data occurrence only; not a verified callable address",
        }

    def imports(self) -> list[dict[str, Any]]:
        rva, size = self.directory(1)
        if not rva:
            return []
        count_limit = min(size // 20, MAX_IMPORT_DESCRIPTORS)
        imports = []
        for index in range(count_limit):
            values = struct.unpack("<IIIII", self.rva_read(rva + index * 20, 20))
            if not any(values):
                return imports
            original_thunk, _, _, name_rva, first_thunk = values
            symbols = []
            thunk_rva = original_thunk or first_thunk
            if not thunk_rva:
                raise PEError("import descriptor has no thunk table")
            width = self.bits // 8
            ordinal_mask = 1 << (self.bits - 1)
            for thunk_index in range(MAX_THUNKS):
                entry = int.from_bytes(self.rva_read(thunk_rva + thunk_index * width, width), "little")
                if not entry:
                    break
                if entry & ordinal_mask:
                    symbols.append({"ordinal": entry & 0xFFFF})
                else:
                    hint = struct.unpack("<H", self.rva_read(entry, 2))[0]
                    symbols.append({"name": self.cstring(entry + 2), "hint": hint})
            else:
                raise PEError("import thunk table exceeds bounded inspection limit")
            imports.append({"dll": self.cstring(name_rva), "functions": symbols})
        raise PEError("import descriptors have no terminator within directory bounds")

    def exports(self) -> list[dict[str, Any]]:
        rva, size = self.directory(0)
        if not rva:
            return []
        fields = struct.unpack("<IIHHIIIIIII", self.rva_read(rva, 40))
        ordinal_base, function_count, name_count, functions_rva, names_rva, ordinals_rva = fields[5:]
        if name_count > MAX_EXPORT_NAMES or function_count > MAX_EXPORT_NAMES:
            raise PEError("export table exceeds bounded inspection limit")
        result = []
        named_indices = set()
        for index in range(name_count):
            name_rva = struct.unpack("<I", self.rva_read(names_rva + index * 4, 4))[0]
            function_index = struct.unpack("<H", self.rva_read(ordinals_rva + index * 2, 2))[0]
            if function_index >= function_count:
                raise PEError("export ordinal index exceeds function table")
            named_indices.add(function_index)
            address_rva = struct.unpack("<I", self.rva_read(functions_rva + function_index * 4, 4))[0]
            entry: dict[str, Any] = {
                "name": self.cstring(name_rva),
                "ordinal": ordinal_base + function_index,
                "rva": hex(address_rva),
            }
            if rva <= address_rva < rva + size:
                entry["forwarder"] = self.cstring(address_rva)
            result.append(entry)
        for index in range(function_count):
            if index in named_indices:
                continue
            address_rva = struct.unpack("<I", self.rva_read(functions_rva + index * 4, 4))[0]
            if address_rva:
                entry = {"name": None, "ordinal": ordinal_base + index, "rva": hex(address_rva)}
                if rva <= address_rva < rva + size:
                    entry["forwarder"] = self.cstring(address_rva)
                result.append(entry)
        return result

    def anchors(self) -> dict[str, Any]:
        found: dict[str, Any] = {}
        for anchor in SCRIPT_ANCHORS:
            pattern = anchor.encode("ascii") + b"\0"
            matches = []
            total = 0
            start = 0
            while True:
                offset = self.data.find(pattern, start)
                if offset < 0:
                    break
                start = offset + len(pattern)
                # Avoid reporting a suffix of a longer printable string as an
                # exact script identifier. This deliberately undercounts substrings.
                if offset and 32 <= self.data[offset - 1] < 127:
                    continue
                total += 1
                if len(matches) < MAX_ANCHOR_MATCHES:
                    matches.append(self.location(offset))
            found[anchor] = {
                "status": "present" if total else "absent",
                "matching": "exact ASCII NUL-terminated identifier",
                "occurrence_count": total,
                "truncated": total > len(matches),
                "occurrences": matches,
            }
        return found

    def steam_interface_strings(self) -> list[dict[str, Any]]:
        pattern = re.compile(rb"(?:Steam|STEAM)[A-Za-z0-9_]{3,100}[0-9]{3}\x00")
        result = []
        seen = set()
        for match in pattern.finditer(self.data):
            if match.start() and 32 <= self.data[match.start() - 1] < 127:
                continue
            value = match.group()[:-1].decode("ascii")
            if value in seen:
                continue
            seen.add(value)
            result.append({"value": value, **self.location(match.start())})
            if len(result) >= 256:
                break
        return result


def inspect(path: Path) -> dict[str, Any]:
    result: dict[str, Any] = {"path": str(path), "name": path.name}
    if not path.is_file():
        return {**result, "status": "missing"}
    data = path.read_bytes()
    result.update({
        "status": "inspected",
        "bytes": len(data),
        "sha1": hashlib.sha1(data).hexdigest().upper(),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
    })
    pe = PEFile(data)
    result["pe"] = {
        "machine": hex(pe.machine),
        "architecture": {0x8664: "AMD64", 0x14C: "I386", 0xAA64: "ARM64"}.get(pe.machine, "unknown"),
        "bits": pe.bits,
        "image_base": hex(pe.image_base),
        "image_size": pe.image_size,
        "entrypoint_rva": hex(pe.entrypoint_rva),
        "coff_timestamp_raw": pe.timestamp,
        "sections": pe.sections,
        "imports": pe.imports(),
    }
    result["script_string_anchors"] = pe.anchors()
    if path.name.lower() == "steam_api64.dll":
        result["exports"] = pe.exports()
        result["steam_interface_strings"] = pe.steam_interface_strings()
        result["interface_string_note"] = (
            "Bounded string inventory only. Strings and exports do not establish ABI "
            "compatibility, initialized interfaces, authentication or network connectivity."
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=DEFAULT_GAME_DIR)
    parser.add_argument("--output", type=Path, default=Path("docs/installed-build.json"))
    args = parser.parse_args()
    game_dir = args.game_dir.resolve()
    output = args.output.resolve()
    inputs = [(game_dir / name).resolve() for name in FILES]
    if output in inputs:
        parser.error("output must not overwrite an inspected game binary")
    report: dict[str, Any] = {
        "schema_version": 1,
        "inspected_at_utc": datetime.now(timezone.utc).isoformat(),
        "method": "offline standard-library PE parsing; no executable loading or execution",
        "game_directory": str(game_dir),
        "limitations": [
            "String locations are file data, never independently verified function addresses.",
            "Preferred image virtual addresses do not account for runtime ASLR.",
            "Only the ordinary PE import directory is inventoried; delay imports are not parsed.",
            "Absence means no exact ASCII NUL-terminated match in the selected files.",
            "No game archives, save files, running process memory or network services are inspected.",
            "A four-player engine session and a complete co-op campaign remain unverified.",
        ],
        "files": [],
    }
    failed = False
    for path in inputs:
        try:
            entry = inspect(path)
        except (OSError, PEError, struct.error) as error:
            entry = {"name": path.name, "path": str(path), "status": "error", "error": str(error)}
        report["files"].append(entry)
        if entry["status"] != "inspected":
            failed = True
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output}")
    for entry in report["files"]:
        if entry["status"] == "inspected":
            present = [name for name, value in entry["script_string_anchors"].items() if value["status"] == "present"]
            print(f"{entry['name']}: {entry['pe']['architecture']}, {entry['bytes']} bytes, {len(present)} exact script anchors")
        else:
            print(f"{entry['name']}: {entry['status']} {entry.get('error', '')}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
