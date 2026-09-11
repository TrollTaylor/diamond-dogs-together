#!/usr/bin/env python3
"""Original bounded static check of the installed Steam packet export wrappers.

Uses PE metadata and exact independently decoded bytes, without loading a DLL,
invoking exports, or accessing process memory. Does not grant a runtime lease.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from inspect_game import PEFile, PEError

EXPECTED_SIZE = 211368
EXPECTED_SHA256 = "08bd0152356e5619ee56a6714ac951b6800dbb21821693a4efaf85278442c60a"
WRAPPERS = (
    ("SteamAPI_ISteamNetworking_SendP2PPacket", 0x1D10,
     "4883ec388b4424684c8b11894424288b4424608944242041ff124883c438c3",
     "bool(intptr_t instance, uint64 SteamID bits, const void*, uint32 size, int32 mode, int32 channel)"),
    ("SteamAPI_ISteamNetworking_IsP2PPacketAvailable", 0x1950,
     "488b0148ff6008", "bool(intptr_t instance, uint32* size, int32 channel)"),
    ("SteamAPI_ISteamNetworking_ReadP2PPacket", 0x21A0,
     "4c8b1149ff6210", "bool(intptr_t instance, void*, uint32 capacity, uint32* size, uint64* SteamID bits, int32 channel)"),
)

def inspect(path: Path) -> dict:
    report = {"utc": datetime.now(timezone.utc).isoformat(), "file": str(path.resolve()),
              "scope": "static_file_only", "dll_loaded": False, "exports_called": False,
              "runtime_access_authorized": False, "status": "refused"}
    try:
        # Hold the opened file while reading and checking its actual length.
        with path.open("rb") as file:
            data = file.read(EXPECTED_SIZE + 1)
        if len(data) != EXPECTED_SIZE:
            raise PEError("unsupported DLL size")
        report["sha256"] = hashlib.sha256(data).hexdigest()
        if report["sha256"] != EXPECTED_SHA256:
            raise PEError("unsupported DLL fingerprint")
        pe = PEFile(data)
        if pe.bits != 64 or pe.machine != 0x8664:
            raise PEError("AMD64 DLL required")
        exports = pe.exports()
        records = []
        for name, rva, expected_hex, abi in WRAPPERS:
            found = [item for item in exports if item["name"] == name]
            if len(found) != 1 or int(found[0]["rva"], 16) != rva or found[0].get("forwarder"):
                raise PEError(f"unexpected export identity: {name}")
            expected = bytes.fromhex(expected_hex)
            if pe.rva_read(rva, len(expected)) != expected:
                raise PEError(f"unexpected wrapper bytes: {name}")
            records.append({"export": name, "rva": hex(rva), "bytes": expected_hex,
                            "windows_x64_machine_boundary": abi})
        report.update(status="static_wrappers_matched", wrappers=records,
                      limitation="No live instance, lifecycle, channel ownership, authentication or delivery verified")
    except (OSError, PEError, ValueError) as error:
        report["reason"] = str(error)
    return report

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, default=Path("D:/Games/steamapps/common/MGS_TPP/steam_api64.dll"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = inspect(args.dll)
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0 if result["status"] == "static_wrappers_matched" else 1

if __name__ == "__main__":
    raise SystemExit(main())
