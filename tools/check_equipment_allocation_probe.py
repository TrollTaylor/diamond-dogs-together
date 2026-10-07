"""Verify public equipment allocation source/ABI pins; never run game code.

Reads the installed executable and the existing extracted retail Lua files.
Outputs a small JSON verification record on stdout. No process is opened and
no game, save, executable or evidence file is written by this checker.
"""
import argparse
import hashlib
import json
from pathlib import Path

from analyze_network_bindings import Analysis

ROOT = Path(__file__).resolve().parent.parent
GAME_SHA256 = "085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45"
BODIES = (
    ("TppEquip registration", 0xA03910, 254,
     "29F99895F31B4DAAAE3EEF74DA6914F23D5BB27884446D74096A1D5BA8EAA3B6"),
    ("CreateEquipGhostBlockGroups", 0xA02670, 1490,
     "30DDE9DF7824538392FA46B82632D3A64315D928A9D9725162D686085F3BCF3F"),
    ("CreateEquipMissionBlockGroup", 0xA02C50, 351,
     "A152303D6FBCF1C2554DB287D7B4F28CE5A528EAABC2F216E6D5BF2B30524888"),
    ("StartLoadingToEquipMissionBlock", 0xA03F70, 112,
     "BB3E77EB9FB4F9FDB3C0F1B85CC29CDBAFFAF92C1175E13C9DB8EB240545CED8"),
)
SOURCES = (
    ("retail TppMain", "build/player-allocation-lifecycle-20260909/package-00.lua",
     "6542D997C3F29742FF7EA0587F7F307794586A9B7A8EE3114B58E79E7F0B6103"),
    ("retail TppPlayer", "build/targeting-scripts-20260910/package-01.lua",
     "7407BD41DD82913EDFE9EF7FBB344F443ADC7399D42F86C7798BA6B02C94E5B9"),
)


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe"))
    parser.add_argument("--source-root", type=Path, default=ROOT)
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    require(hashlib.sha256(raw).hexdigest().upper() == GAME_SHA256, "Executable fingerprint mismatch")
    analysis = Analysis(raw)
    body_records = []
    for name, rva, size, digest in BODIES:
        body = analysis.function(rva)
        require(body["decoded_bytes"] == size and body["complete_declared_range_decoded"], name + " boundary mismatch")
        require(body["decoded_bytes_sha256"] == digest, name + " body mismatch")
        body_records.append({"name": name, "rva": hex(rva), "bytes": size, "sha256": digest})
    # Full registration decoding proves the stack pairs and named module at
    # real instruction boundaries; a nearby string/table candidate is not used.
    rows = {int(row["rva"], 16): (row["mnemonic"], row["operands"])
            for row in analysis.function(0xA03910)["instructions"]}
    pairs = (
        (0xA0396B, 0xA03972, 0xA03976, 0xA0397D, 0x22A1FC8, "CreateEquipMissionBlockGroup", 0xA02C50, -0x78),
        (0xA03981, 0xA03988, 0xA0398C, 0xA03993, 0x22A1FE8, "CreateEquipGhostBlockGroups", 0xA02670, -0x68),
        (0xA039AD, 0xA039B4, 0xA039B8, 0xA039BF, 0x22A2028, "StartLoadingToEquipMissionBlock", 0xA03F70, -0x48),
    )
    require(analysis.pe.cstring(0x22A0F50) == "TppEquip", "Equipment module name mismatch")
    require(rows[0xA03939] == ("lea", "rdx, [rip + 0x189d610]"), "Equipment module registration mismatch")
    require(rows[0xA039E3] == ("call", "0x14006b8c0"), "Equipment registration dispatch mismatch")
    for name_lea, name_store, function_lea, function_store, string_rva, name, function_rva, offset in pairs:
        require(analysis.pe.cstring(string_rva) == name, name + " registered name mismatch")
        name_instruction = analysis.instructions(name_lea, 7)[0]
        function_instruction = analysis.instructions(function_lea, 7)[0]
        require(rows[name_lea][0] == "lea" and name_instruction.address + 7 + name_instruction.operands[1].mem.disp
                == analysis.pe.image_base + string_rva, name + " name-pointer mismatch")
        require(rows[function_lea][0] == "lea" and function_instruction.address + 7 + function_instruction.operands[1].mem.disp
                == analysis.pe.image_base + function_rva, name + " function-pointer mismatch")
        require(rows[name_store] == ("mov", f"qword ptr [r11 - {abs(offset):#x}], rax")
                and rows[function_store] == ("mov", f"qword ptr [r11 - {abs(offset + 8):#x}], rax"),
                name + " adjacent stack-pair mismatch")
    source_records = []
    for name, relative, digest in SOURCES:
        source = (args.source_root / relative).read_bytes()
        require(hashlib.sha256(source).hexdigest().upper() == digest, name + " source mismatch")
        source_records.append({"name": name, "path": relative, "sha256": digest})
    print(json.dumps({"executable_sha256": GAME_SHA256, "public_bindings": body_records,
                      "retail_sources": source_records, "native_calls": 0, "process_access": False,
                      "game_writes": False, "runtime_allocation_verified": False}, indent=2))


if __name__ == "__main__":
    main()
