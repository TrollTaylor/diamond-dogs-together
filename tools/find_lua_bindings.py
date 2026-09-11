#!/usr/bin/env python3
"""Find unverified Lua C registration-table candidates in an offline MGSV PE.

The only target input is the installed executable. No mod source, external
address map, process memory, runtime call or executable patch is used.
"""

from __future__ import annotations

import argparse
from bisect import bisect_right
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from typing import Any

try:
    from .inspect_game import DEFAULT_GAME_DIR, PEError, PEFile
except ImportError:
    from inspect_game import DEFAULT_GAME_DIR, PEError, PEFile


ANCHORS = ("TppNetworkUtil", "IsSessionConnect", "GetSessionMemberCount")
NAME_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]{0,127}\Z")
PAIR_BYTES = 16
MIN_ADJACENT_PAIRS = 3
MAX_TABLE_PAIRS = 256
MAX_STORED_REFERENCES = 256
EXECUTABLE = 0x20000000
MAX_EXCEPTION_RANGES = 300000
MAX_LEA_REFERENCES = 256
LEA_PATTERN = re.compile(rb"[\x48-\x4f]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....", re.DOTALL)
REGISTER_NAMES = ("rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")


def section_at_rva(pe: PEFile, rva: int) -> dict[str, Any] | None:
    for section in pe.sections:
        if 0 <= rva - section["rva"] < max(section["virtual_size"], section["raw_size"]):
            return section
    return None


def address_location(pe: PEFile, va: int) -> dict[str, Any]:
    return pe.location(pe.rva_offset(va - pe.image_base))


def read_pair(pe: PEFile, offset: int) -> dict[str, Any] | None:
    """Require a mapped aligned name pointer followed by executable destination."""
    if offset < 0 or offset + PAIR_BYTES > len(pe.data):
        return None
    try:
        location = pe.location(offset)
        if location["rva"] is None:
            return None
        pair_rva = int(location["rva"], 16)
        if pair_rva % 8:
            return None
        table_section = section_at_rva(pe, pair_rva)
        if table_section is None or table_section["characteristics"] & EXECUTABLE:
            return None
        name_va, function_va = struct.unpack("<QQ", pe.rva_read(pair_rva, PAIR_BYTES))
        if not name_va or not function_va:
            return None
        name_rva, function_rva = name_va - pe.image_base, function_va - pe.image_base
        name_section = section_at_rva(pe, name_rva)
        function_section = section_at_rva(pe, function_rva)
        if name_section is None or function_section is None:
            return None
        if name_section["characteristics"] & EXECUTABLE:
            return None
        if not function_section["characteristics"] & EXECUTABLE:
            return None
        name = pe.cstring(name_rva)
        if NAME_PATTERN.fullmatch(name) is None:
            return None
        # Require at least one byte of the destination in the file; zero-filled
        # executable ranges are not sufficient evidence of a function target.
        pe.rva_read(function_rva, 1)
        destination = address_location(pe, function_va)
        destination["interpretation"] = "executable-section destination; function identity and ABI unverified"
        return {
            "entry": location,
            "name": name,
            "name_data": address_location(pe, name_va),
            "function_pointer_candidate": destination,
            "candidate_status": "unverified; not safe to call or hook",
        }
    except (PEError, struct.error):
        return None


def table_containing(pe: PEFile, offset: int) -> dict[str, Any] | None:
    current = read_pair(pe, offset)
    if current is None:
        return None
    entries = [current]
    start = offset
    while len(entries) < MAX_TABLE_PAIRS:
        previous = read_pair(pe, start - PAIR_BYTES)
        if previous is None:
            break
        start -= PAIR_BYTES
        entries.insert(0, previous)
    next_offset = offset + PAIR_BYTES
    while len(entries) < MAX_TABLE_PAIRS:
        following = read_pair(pe, next_offset)
        if following is None:
            break
        entries.append(following)
        next_offset += PAIR_BYTES
    if len(entries) < MIN_ADJACENT_PAIRS:
        return None
    end = start + len(entries) * PAIR_BYTES
    terminated = False
    try:
        end_location = pe.location(end)
        if end_location["rva"] is not None:
            terminated = pe.rva_read(int(end_location["rva"], 16), PAIR_BYTES) == bytes(PAIR_BYTES)
    except PEError:
        pass
    return {
        "candidate_status": "unverified_lua_registration_shape",
        "start": pe.location(start),
        "adjacent_pair_count": len(entries),
        "zero_pair_after_run": terminated,
        "bounded_at_pair_limit": len(entries) == MAX_TABLE_PAIRS,
        "namespace_association": "unverified; library registration code has not been identified",
        "evidence": [
            "8-byte-aligned pairs in a non-executable mapped section",
            "each name pointer resolves to a bounded ASCII identifier in non-executable data",
            "each paired pointer resolves to file-backed data in an executable section",
            f"at least {MIN_ADJACENT_PAIRS} adjacent plausible pairs",
        ],
        "matched_anchors": [],
        "entries": entries,
    }


def exception_ranges(pe: PEFile) -> tuple[list[tuple[int, int, int]], list[int], str]:
    rva, size = pe.directory(3)
    if not rva or not size:
        return [], [], "absent"
    if size % 12 or size // 12 > MAX_EXCEPTION_RANGES:
        return [], [], "unsupported_size_or_inspection_limit"
    try:
        ranges = [
            (begin, end, unwind)
            for begin, end, unwind in struct.iter_unpack("<III", pe.rva_read(rva, size))
            if 0 < begin < end <= pe.image_size
        ]
    except (PEError, struct.error):
        return [], [], "unreadable"
    ranges.sort()
    return ranges, [item[0] for item in ranges], "read_from_pe_exception_directory"


def find_lea_references(pe: PEFile, anchors: dict[str, Any]) -> dict[str, Any]:
    targets: dict[int, str] = {}
    for name, result in anchors.items():
        for occurrence in result["occurrences"]:
            rva = occurrence["string_data"]["rva"]
            if rva is not None:
                targets[int(rva, 16)] = name
    ranges, starts, range_status = exception_ranges(pe)
    references = []
    groups: dict[tuple[int, int], dict[str, Any]] = {}
    total = 0
    # This is deliberately a byte-pattern scan, not a whole instruction decoder.
    # No inference is made about an instruction boundary or callable function.
    for section in pe.sections:
        if not section["characteristics"] & EXECUTABLE:
            continue
        begin = section["raw_offset"]
        end = begin + section["raw_size"]
        for match in LEA_PATTERN.finditer(pe.data, begin, end):
            offset = match.start()
            # Reject an immediately preceding address-size prefix, which would
            # change this addressing mode. A complete decoder would be needed
            # to resolve every possible prefix or overlapping instruction.
            if offset > begin and pe.data[offset - 1] == 0x67:
                continue
            instruction_rva = section["rva"] + offset - begin
            displacement = struct.unpack_from("<i", pe.data, offset + 3)[0]
            target_rva = instruction_rva + 7 + displacement
            if target_rva not in targets:
                continue
            total += 1
            if len(references) >= MAX_LEA_REFERENCES:
                continue
            register = ((pe.data[offset + 2] >> 3) & 7) + (8 if pe.data[offset] & 4 else 0)
            location = pe.location(offset)
            location["interpretation"] = "unverified x64 LEA byte-pattern candidate; not a callable target"
            reference: dict[str, Any] = {
                "candidate_status": "unverified_instruction_byte_pattern",
                "anchor": targets[target_rva],
                "instruction": location,
                "bytes_hex": pe.data[offset:offset + 7].hex(" "),
                "destination_register_if_decoded_at_this_boundary": REGISTER_NAMES[register],
                "signed_disp32": displacement,
                "calculated_target_rva": hex(target_rva),
                "target_formula": "instruction_rva + 7 + signed_disp32",
                "target": pe.location(pe.rva_offset(target_rva)),
                "instruction_boundary_verified": False,
            }
            range_index = bisect_right(starts, instruction_rva) - 1
            if range_index >= 0 and ranges[range_index][0] <= instruction_rva < ranges[range_index][1]:
                range_begin, range_end, unwind = ranges[range_index]
                reference["covering_pe_exception_range"] = {
                    "begin_rva": hex(range_begin), "end_rva_exclusive": hex(range_end), "unwind_info_rva": hex(unwind),
                    "interpretation": "declared runtime-function range; semantic role and safe entry unverified",
                }
                key = (range_begin, range_end)
                if key not in groups:
                    groups[key] = {
                        "candidate_status": "unverified_code_region_with_anchor_references",
                        "begin_rva": hex(range_begin), "end_rva_exclusive": hex(range_end),
                        "unwind_info_rva": hex(unwind), "anchors": [],
                        "interpretation": "shared range may contain registration logic; no callable binding has been verified",
                    }
                if targets[target_rva] not in groups[key]["anchors"]:
                    groups[key]["anchors"].append(targets[target_rva])
            references.append(reference)
    return {
        "method": "executable-section REX.W LEA byte patterns with ModRM mod=00,r/m=101 and exact calculated target",
        "exception_range_source_status": range_status,
        "reference_count": total,
        "references_truncated": total > len(references),
        "references": references,
        "shared_code_region_candidates": [group for group in groups.values() if len(group["anchors"]) >= 2],
    }


def scan(data: bytes) -> dict[str, Any]:
    pe = PEFile(data)
    if pe.bits != 64 or pe.machine != 0x8664:
        raise PEError("this scanner requires an AMD64 PE with 8-byte pointer candidates")
    anchors: dict[str, Any] = {}
    tables: dict[str, dict[str, Any]] = {}
    for anchor in ANCHORS:
        occurrences = []
        pattern = anchor.encode("ascii") + b"\0"
        cursor = 0
        while True:
            offset = data.find(pattern, cursor)
            if offset < 0:
                break
            cursor = offset + len(pattern)
            if offset and 32 <= data[offset - 1] < 127:
                continue
            occurrence = {"string_data": pe.location(offset), "literal_va_reference_count": 0, "references": []}
            va_text = occurrence["string_data"]["preferred_image_va"]
            if va_text is not None:
                pointer_bytes = int(va_text, 16).to_bytes(8, "little")
                reference_cursor = 0
                while True:
                    reference_offset = data.find(pointer_bytes, reference_cursor)
                    if reference_offset < 0:
                        break
                    reference_cursor = reference_offset + 1
                    occurrence["literal_va_reference_count"] += 1
                    if len(occurrence["references"]) >= MAX_STORED_REFERENCES:
                        continue
                    table = table_containing(pe, reference_offset)
                    reference = {"location": pe.location(reference_offset), "qualifies_as_adjacent_table": table is not None}
                    if table is not None:
                        key = table["start"]["file_offset"]
                        if key not in tables:
                            tables[key] = table
                        if anchor not in tables[key]["matched_anchors"]:
                            tables[key]["matched_anchors"].append(anchor)
                        reference["candidate_table_start"] = key
                    occurrence["references"].append(reference)
            occurrence["references_truncated"] = occurrence["literal_va_reference_count"] > len(occurrence["references"])
            occurrences.append(occurrence)
        anchors[anchor] = {"string_status": "present" if occurrences else "absent", "occurrences": occurrences}
    lea = find_lea_references(pe, anchors)
    result = "unverified_table_candidates_found" if tables else "no_qualifying_candidates_found"
    if not tables and lea["reference_count"]:
        result = "no_qualifying_tables_found; unverified_instruction_candidates_only"
    return {
        "image_base": hex(pe.image_base),
        "anchors": anchors,
        "candidate_count": len(tables),
        "candidates": sorted(tables.values(), key=lambda row: int(row["start"]["file_offset"], 16)),
        "rip_relative_lea_scan": lea,
        "result": result,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-exe", type=Path, default=DEFAULT_GAME_DIR / "mgsvtpp.exe")
    parser.add_argument("--output", type=Path, default=Path("docs/native-binding-candidates.json"))
    args = parser.parse_args()
    game_exe, output = args.game_exe.resolve(), args.output.resolve()
    if game_exe == output:
        parser.error("output must not overwrite the inspected executable")
    report: dict[str, Any] = {
        "schema_version": 1,
        "inspected_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_executable": str(game_exe),
        "provenance": "original offline scanner using only this executable and generic Lua API documentation",
        "generic_shape_reference": "https://www.lua.org/manual/5.1/manual.html#luaL_Reg",
        "shape_assumption": "AMD64 adjacent {const char *name; function pointer} pairs, 16-byte stride",
        "verified_native_functions": [],
        "limitations": [
            "The shape is a heuristic; this does not establish the game's exact Lua version or internal ABI.",
            "Tables are found only through literal 64-bit preferred-image-VA references.",
            "RIP-relative LEA is scanned as a bounded seven-byte shape, without a complete x64 instruction decoder.",
            "Instruction candidates may be overlapping bytes or encoded data; exception ranges do not prove instruction boundaries.",
            "Runtime-built, encoded, relocated-at-runtime or nonstandard registration tables can be missed.",
            "Executable-section membership does not prove a function boundary or calling convention.",
            "Candidate tables are not callable targets and no hooks may be enabled based only on this report.",
            "No game process was started, read, called or modified, and no save files were accessed.",
        ],
        "bounds": {
            "anchors": list(ANCHORS),
            "minimum_adjacent_pairs": MIN_ADJACENT_PAIRS,
            "maximum_table_pairs": MAX_TABLE_PAIRS,
            "maximum_stored_references_per_string": MAX_STORED_REFERENCES,
            "maximum_stored_lea_references": MAX_LEA_REFERENCES,
            "maximum_exception_ranges": MAX_EXCEPTION_RANGES,
        },
    }
    exit_code = 0
    try:
        data = game_exe.read_bytes()
        report.update({
            "bytes": len(data),
            "sha1": hashlib.sha1(data).hexdigest().upper(),
            "sha256": hashlib.sha256(data).hexdigest().upper(),
            **scan(data),
        })
    except (OSError, PEError, struct.error) as error:
        report.update({"result": "inspection_error", "error": str(error), "candidate_count": 0, "candidates": []})
        exit_code = 1
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output}")
    lea_count = report.get("rip_relative_lea_scan", {}).get("reference_count", 0)
    print(f"Result: {report['result']}; tables: {report['candidate_count']}; LEA candidates: {lea_count}; verified functions: 0")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
