"""Reproduce the exact-build archive-creation and captured caller map.

Static file inspection only. Natural runtime evidence is collected separately.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from analyze_network_bindings import Analysis
from generate_registration_trace import GAME_SHA256
from generate_mount_trace import RVA, END, RESULT_RVA, BODY_SHA, PROLOGUE

HASHES = {
    RVA: BODY_SHA,
    0x49270: 'fda6045604ea555453153f6a5552bf210a404e4a10920bae8a3459b7c14d879e',
    0x4A200: 'd490d2bc64be5e1b5f70708c1890f9fc42718b82f5a811f9e7115ae562d6e017',
    0x3A960: 'a81768adc4e95b7d8d020369cb4a2b43f757e9a9f5d9f3607703766366996b01',
}
CHECKS = {
    0x48E4C: '418bf1', 0x48E4F: '498be8', 0x48E52: '4c8bfa', 0x48E55: '4c8bf1',
    0x48EA7: '33c0', 0x48EA9: 'e9bd000000', 0x48F44: '488b5870',
    0x48F4D: '48896b28', 0x48F68: '488bc3', 0x48F80: '4883c470',
    0x49286: '837b1001', 0x4928C: '488b4b18',
    0x4A206: '83791001', 0x4A20C: '488b5918',
    0x3BA9D: '83cf20', 0x3BAD6: '448bcf', 0x3BAD9: '4c8bc5',
    0x3BADC: '488bd3', 0x3BADF: '488bce', 0x3BAE2: 'e839d30000',
    0x3BAE7: '488bd8',
}


def inspect(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != GAME_SHA256:
        raise ValueError('Unsupported executable')
    analysis = Analysis(data)
    functions = {hex(r): analysis.function(r) for r in [*HASHES, 0x3BA70]}
    for r, digest in HASHES.items():
        f = functions[hex(r)]
        if not f['complete_declared_range_decoded'] or f['decoded_bytes_sha256'].lower() != digest:
            raise ValueError(f'Changed or incomplete function at {r:#x}')
    caller = functions['0x3ba70']
    if not caller['complete_declared_range_decoded']:
        raise ValueError('Incomplete captured-caller decode')
    rows = {int(i['rva'], 16): i for f in functions.values() for i in f['instructions']}
    for r, expected in CHECKS.items():
        if rows.get(r, {}).get('bytes_hex', '').replace(' ', '') != expected:
            raise ValueError(f'Changed aligned instruction at {r:#x}')
    if rows[0x3BAE2].get('direct_target_rva') != hex(RVA):
        raise ValueError('Captured caller no longer invokes mount creation')
    return dict(utc=datetime.now(timezone.utc).isoformat(), scope='static_file_only',
                executable_sha256=GAME_SHA256, functions=functions, aligned_instruction_checks=len(CHECKS),
                create_range=[hex(RVA), hex(END)], create_prologue=PROLOGUE.hex(),
                result_rva=hex(RESULT_RVA), result_stack_delta=-0x98,
                source_signature='MountPoint*(const char* name, const char* file_path, int64_t type_tag, uint32_t flags)',
                raw_handle_fields={'0x10': 'type checked against 1', '0x18': 'nullable pointer used by destruction and I/O setter',
                                   '0x28': 'incoming 64-bit tag stored when returned handle is nonnull'},
                limitations=['Natural runtime capture is separate evidence.',
                             'No new archive mounted or game code called by this tool.',
                             'Flag semantics, custom archive precedence and owned mount destruction remain unresolved.',
                             'A nonnull handle alone does not prove every asset resolves or four actors exist.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.executable)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f"Verified {result['aligned_instruction_checks']} aligned instructions across five functions; no game calls")
