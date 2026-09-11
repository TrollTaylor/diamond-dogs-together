"""Reproduce static derivation of the read-only player index/allocation observer."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

from analyze_network_bindings import Analysis
from inspect_player_system import CODE_CHECKS, EXPECTED


def inspect(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError("unsupported executable")
    analysis = Analysis(data)
    spans = []
    for begin, end in [(0x9C39F0, 0x9C3A28), (0xBF4270, 0xBF42DD),
                       (0x9C39B0, 0x9C39EE), (0x9C3BB0, 0x9C3BBE),
                       (0xBF5250, 0xBF5276), (0xBF56E0, 0xBF5740)]:
        decoded = analysis.instructions(begin, end - begin)
        if sum(i.size for i in decoded) != end - begin:
            raise ValueError("incomplete span")
        spans.append({"begin_rva": hex(begin), "end_rva_exclusive": hex(end),
                      "sha256": hashlib.sha256(analysis.pe.rva_read(begin, end - begin)).hexdigest(),
                      "instructions": [{"rva": hex(i.address - analysis.pe.image_base), "bytes": i.bytes.hex(),
                                        "mnemonic": i.mnemonic, "operands": i.op_str} for i in decoded]})
    boundaries = {int(i["rva"], 16) for span in spans for i in span["instructions"]}
    for rva, expected in CODE_CHECKS.items():
        expected = bytes.fromhex(expected)
        if rva not in boundaries or analysis.pe.rva_read(rva, len(expected)) != expected:
            raise ValueError(f"allocation check mismatch {rva:#x}")
    # Live tables match this file; explicitly bind the descriptor and flag slots.
    slots = {0x2295490 + 2 * 8: 0x9C39F0, 0x22953D0 + 8 * 8: 0x9C39B0,
             0x23063F8 + 8: 0xBF5250, 0x2295490 + 11 * 8: 0x9C3BB0}
    for rva, target in slots.items():
        if struct.unpack("<Q", analysis.pe.rva_read(rva, 8))[0] != analysis.pe.image_base + target:
            raise ValueError("method table mismatch")
    return {"utc": datetime.now(timezone.utc).isoformat(), "executable_sha256": EXPECTED,
            "runtime_calls": False, "code_checks": len(CODE_CHECKS), "method_slot_checks": len(slots),
            "spans": spans,
            "derivation": [
                "Secondary player method subtracts 0x20 to obtain the enclosing GameObject before generic indexed lookup.",
                "Generic lookup calls enclosing slot 0x40 and accepts an unsigned index only below descriptor DWORD +4.",
                "The live descriptor slot reads enclosing +0x224 bit 0, +0x228 limit, and +0x230 secondary value.",
                "Accepted generic lookup resolves enclosing +8 then internal +0x28 to the indexed executor.",
                "Executor flag getter reads its +0x28 bitset at index/8, bit index%8; null bitset returns true.",
                "Executor flag setter bounds its index using executor DWORD +0x0c.",
                "Secondary slot 11 returns pointer array at secondary +0x430 indexed by unsigned EDX.",
            ],
            "limitations": ["The bitset's broader gameplay meaning remains unverified.",
                            "Indexed storage presence does not prove fully initialized, visible or independently controlled actors.",
                            "No engine entry point is called or patched by this inspection."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path("D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.game)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("6 code spans/checks and 4 method slots matched; observation only")
