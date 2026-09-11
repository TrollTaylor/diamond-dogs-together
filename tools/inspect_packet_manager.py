#!/usr/bin/env python3
"""Reproduce the native packet-manager lifecycle map from the exact installed PE.

File inspection only: no process access, DLL loading, game calls or patches.
Requires the existing local Capstone dependency. Addresses are evidence, not hooks.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

from analyze_network_bindings import Analysis
from inspect_game import PEError
from inspect_steam_networking_calls import GAME_SHA

ENTRIES = (
    0x7E4070, 0x7ED6F0, 0x7ED8D0,
    0x7D6470, 0x7E4190, 0x7EE380, 0x7EE590, 0x312080,
    0x1A5F470, 0x1A5F5F0, 0x1A5F7C0, 0x1A5F960, 0x1A5FA10,
    0x1CEDAC0, 0x1CEDBE0, 0x1CEE1E0, 0x1CEE6D0, 0x1CEE6F0,
    0x1CEE7B0, 0x1CEE7C0, 0x1CEE890, 0x1CEE8A0, 0x1CEE8CA,
    0x1E0AE40, 0x1E0AFB0,
)
BRANCHES = {
    0x7EE3A4: 0x7E4190, 0x7EE5A6: 0x7E4190,
    0x7EE514: 0x1CEE1E0, 0x7EE6E9: 0x1CEE1E0,
    0x1CEE229: 0x1CEDAC0, 0x1CEDB27: 0x1CEDBE0,
    0x1CEE6DD: 0x1CEE6F0, 0x1CEE71B: 0x1A5F7C0,
    0x1A5F7FE: 0x1A5F470, 0x1CEE894: 0x1CEE8A0,
    0x1CEE8B5: 0x1A5FBF0, 0x1E0AE60: 0x7EE380,
    0x1E0AFC7: 0x7EE590, 0x7E4190: 0x681CB90,
    0x312080: 0x4A72F10,
}
POINTERS = {
    0x24BF440: 0x1A5FB60, 0x24BF450: 0x1A5FAC0,
    0x251FDB0: 0x1CEE6D0, 0x251FDC8: 0x1CEE7B0,
    0x251FE18: 0x1CEE890,
    0x255A528: 0x1E0AFB0, 0x255A530: 0x1E0AE40,
}
# Checks are aligned against decoded ranges, not isolated byte-search hits.
INSTRUCTIONS = {
    0x1E0AE46: '488b0db34dde00', 0x7ED711: '488d05f05da301',
    0x7ED718: '488903', 0x7ED731: '40887b48',
    0x7E40B0: 'c7471000000000',
    0x1CEE70F: '4883791800', 0x1CEE714: '7524',
    0x1CEE724: '488d4b18', 0x1CEE8AC: '488b4918',
    0x1CEE8B0: '4885c9', 0x1CEE8B3: '7405',
    0x1A5F4E9: 'bab2040000', 0x1A5F52A: 'bab3040000',
    0x7EE4F6: 'c7452810000000',
    0x7EE54A: 'ff5040', 0x7EE71F: 'ff5040',
    0x7D6470: '488b4130', 0x7D6474: '488b4028',
}


def import_slots(pe):
    """Pair the PE reader's bounded name inventory with descriptor IAT bases."""
    directory, _ = pe.directory(1)
    result = {}
    for index, descriptor in enumerate(pe.imports()):
        first_thunk = struct.unpack('<IIIII', pe.rva_read(directory+20*index, 20))[4]
        for offset, symbol in enumerate(descriptor['functions']):
            result[first_thunk+8*offset] = {'dll': descriptor['dll'], **symbol}
    return result


def inspect(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != GAME_SHA:
        raise PEError('Unsupported executable fingerprint')
    analysis = Analysis(data)
    functions = {hex(entry): analysis.function(entry) for entry in ENTRIES}
    if not all(f['complete_declared_range_decoded'] for f in functions.values()):
        raise PEError('Incomplete bounded disassembly')
    rows = {int(row['rva'], 16): row for f in functions.values() for row in f['instructions']}
    checks = []
    for rva, target in BRANCHES.items():
        row = rows.get(rva, {})
        if row.get('direct_target_rva') != hex(target):
            raise PEError(f'Unexpected aligned branch at {rva:#x}')
        checks.append({'kind': 'direct_branch', 'rva': hex(rva), 'target_rva': hex(target)})
    for rva, expected in INSTRUCTIONS.items():
        if rows.get(rva, {}).get('bytes_hex', '').replace(' ', '') != expected:
            raise PEError(f'Unexpected aligned instruction at {rva:#x}')
        checks.append({'kind': 'instruction', 'rva': hex(rva), 'bytes': expected})
    for rva, target in POINTERS.items():
        if int.from_bytes(analysis.pe.rva_read(rva, 8), 'little') != analysis.pe.image_base+target:
            raise PEError(f'Unexpected table pointer at {rva:#x}')
        checks.append({'kind': 'preferred_image_pointer', 'rva': hex(rva), 'target_rva': hex(target)})
    slots = import_slots(analysis.pe)
    for rva, name in ((0x208E0F8, 'SteamAPI_RegisterCallback'), (0x208E0F0, 'SteamAPI_UnregisterCallback')):
        if slots.get(rva, {}).get('name') != name or slots[rva]['dll'].lower() != 'steam_api64.dll':
            raise PEError(f'Unexpected Steam import identity at {rva:#x}')
        checks.append({'kind': 'import_identity', 'rva': hex(rva), **slots[rva]})
    for call in (0x1A5F4EE, 0x1A5F52F):
        raw = bytes.fromhex(rows[call]['bytes_hex'])
        if raw[:2] != b'\xff\x15' or len(raw) != 6 or call+6+struct.unpack('<i', raw[2:])[0] != 0x208E0F8:
            raise PEError('Unexpected callback registration call')
        checks.append({'kind': 'registration_import_call', 'rva': hex(call), 'slot_rva': '0x208e0f8'})
    return {
        'utc': datetime.now(timezone.utc).isoformat(), 'scope': 'static_file_only',
        'executable_sha256': GAME_SHA, 'checks': checks, 'functions': functions,
        'layout_inferences': {
            'manager_vtable_rva': '0x251fda0', 'outer_to_inner_offset': '0x10',
            'inner_to_nullable_transport_offset': '0x18', 'transport_vtable_rva': '0x24bf430',
            'setup_virtual_slot': '0x10', 'update_virtual_slot': '0x78',
            'upstream_capacity_value': 16, 'capacity_is_playable_actor_count': False,
            'callback_ids': [1202, 1203],
            'service_root_slot_rva': '0x2befc00',
            'service_pointer_offsets': ['0x30', '0x28'],
            'service_vtable_rva': '0x2223508',
            'service_state_offset': '0x10', 'service_flag_offset': '0x48',
            'service_state_zero_semantics': 'constructor_initial_value_only',
        },
        'unresolved': [
            'Activation predicate behind 0x7e4190 -> 0x681cb90',
            'Singleton constructor behind 0x312080 -> 0x4a72f10',
            'Concrete destination of upstream virtual +0x40 dispatch',
            'Caller of manager setup/update virtual slots and live object/thread ownership',
            'Names of upstream virtual wrappers; they are not proven Lua bindings',
        ],
        'limitations': [
            'Disassembly ranges can be unwind chunks rather than complete logical functions.',
            'Bounded leaf/thunk decoding stops at its first terminator; jump targets are not decoded implicitly.',
            'Static pointer edges do not demonstrate natural execution or authorize runtime calls.',
            'Absence of live getter receipts does not prove this pointer was null.',
        ],
        'runtime_calls': False, 'process_access': False, 'playable_coop_verified': False,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-directory', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.game_directory/'mgsvtpp.exe')
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f"Verified {len(report['checks'])} static checks across {len(report['functions'])} bounded ranges; no game calls")
