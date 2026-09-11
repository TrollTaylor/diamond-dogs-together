#!/usr/bin/env python3
"""Read-only, fingerprinted GameObject query mapping; never calls game code."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

from analyze_network_bindings import Analysis

EXPECTED_IMAGE = "085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45"
EXPECTED_BINDINGS = {
    "GetGameObjectId": 0xBF9FE0,
    "GetGameObjectIdByIndex": 0xBFA110,
    "CreateGameObjectId": 0xBF9CF0,
    "GetTypeIndex": 0xBFA1D0,
    "GetTypeIndexWithTypeName": 0xBFA240,
    "DoesGameObjectExistWithTypeName": 0xBF9DE0,
    "SendCommand": 0xBFA380,
    "SendMessage": 0xBFA510,
}


def inspect(data):
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_IMAGE:
        raise ValueError("unsupported executable fingerprint")
    analysis = Analysis(data)
    registrar = analysis.function(0xBF9E30)
    if not registrar["complete_declared_range_decoded"]:
        raise ValueError("incomplete registrar decode")
    bindings = {}
    pending = {}
    for ins in analysis.instructions(0xBF9E30, 424):
        # These exact RIP-relative LEAs are paired before each registration call.
        if ins.mnemonic == "lea" and ins.op_str.startswith(("rdx, [rip", "r8, [rip")):
            register = ins.op_str.split(",", 1)[0]
            pending[register] = ins.address + ins.size + ins.operands[1].mem.disp - analysis.pe.image_base
        if ins.mnemonic == "call":
            if ins.op_str == "0x141a15690":
                if set(pending) != {"rdx", "r8"}:
                    raise ValueError("unresolved binding arguments")
                name = analysis.pe.cstring(pending["rdx"])
                if name in bindings:
                    raise ValueError("duplicate binding")
                bindings[name] = pending["r8"]
            pending.clear()
    if bindings != EXPECTED_BINDINGS:
        raise ValueError("registration mapping differs from inspected build")
    functions = {hex(rva): analysis.function(rva) for rva in (0xBFA110, 0xBF4090, 0xBF9DE0, 0xBF3EC0)}
    checks = {
        "indexed_wrapper_calls_lookup": any(i.get("direct_target_rva") == "0xbf4090" for i in functions["0xbfa110"]["instructions"]),
        "indexed_lookup_is_jump": functions["0xbf4090"]["instructions"][0].get("direct_target_rva") == "0x7b6f4a0",
        "existence_wrapper_calls_lookup": any(i.get("direct_target_rva") == "0xbf3ec0" for i in functions["0xbf9de0"]["instructions"]),
        "existence_lookup_is_jump": functions["0xbf3ec0"]["instructions"][0].get("direct_target_rva") == "0x7b532d0",
    }
    if not all(checks.values()):
        raise ValueError("query path changed")
    return {
        "utc": datetime.now(timezone.utc).isoformat(),
        "executable_sha256": digest,
        "source": "installed executable file only",
        "runtime_calls_made": False,
        "allocation_verified": False,
        "bindings": {name: hex(rva) for name, rva in bindings.items()},
        "checks": checks,
        "registrar": registrar,
        "functions": functions,
        "limitations": [
            "Registration and wrapper dataflow are decoded, not naturally observed here.",
            "Indexed lookup target 0x7b6f4a0 contains transformed control flow; its allocation/bounds semantics remain unresolved.",
            "Type-existence lookup is also a jump into an unresolved implementation.",
            "Neither returned IDs nor type existence would alone prove four independently controlled actors.",
        ],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path("D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.game.read_bytes())
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("8 bindings and 4 query-path checks verified against exact executable; allocation unverified")
