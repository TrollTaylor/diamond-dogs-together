"""Reproduce the exact-build block wait analysis from the executable file only."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

from analyze_network_bindings import Analysis
from generate_block_wait_trace import BEGIN, END, BODY_SHA256, GAME_SHA256


def analyze(executable):
    data = executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != GAME_SHA256:
        raise ValueError('Unsupported executable')
    a = Analysis(data)
    body = a.pe.rva_read(BEGIN, END-BEGIN)
    if hashlib.sha256(body).hexdigest() != BODY_SHA256:
        raise ValueError('Dispatch body changed')
    if not any(begin == BEGIN and end == END for begin, end, _ in a.ranges):
        raise ValueError('Dispatch PE boundary changed')
    checks = {
        0x6D669: ('lea', 'r8, [rcx + 0x40]'),
        0x6D66D: ('mov', 'rcx, qword ptr [rcx + 0x38]'),
        0x6D671: ('call', '0x14006e4e0'),
        0x6E510: ('mov', 'rdi, rcx'),
        0x6E513: ('mov', 'eax, dword ptr [rcx + 0x84]'),
        0x6E519: ('add', 'eax, -2'),
        0x6E51C: ('cmp', 'eax, 0x10'),
        0x6E52E: ('mov', 'r9d, dword ptr [rcx + rax*4 + 0x6f1b4]'),
        0x6E536: ('add', 'r9, rcx'),
        0x6E539: ('jmp', 'r9'),
        0x6E953: ('mov', 'eax, dword ptr [rdi + 0x60]'),
        0x6E956: ('sub', 'eax, dword ptr [rdi + 0x18]'),
        0x6E959: ('add', 'eax, dword ptr [rdi + 0x40]'),
        0x6E95C: ('add', 'eax, dword ptr [rdi + 0x10]'),
        0x6E970: ('mov', 'dword ptr [rdi + 0x14c], eax'),
        0x6ED7D: ('mov', 'eax, dword ptr [rdi + 0x60]'),
        0x6ED80: ('sub', 'eax, dword ptr [rdi + 0x18]'),
        0x6ED83: ('add', 'eax, dword ptr [rdi + 0x40]'),
        0x6ED86: ('add', 'eax, dword ptr [rdi + 0x10]'),
        0x6ED89: ('cmp', 'eax, dword ptr [rdi + 0x14c]'),
        0x6ED8F: ('jbe', '0x14006edac'),
        0x6EDA7: ('jmp', '0x14006f191'),
        0x6EE13: ('mov', 'dword ptr [rdi + 0x84], 0xc'),
        0x6EE4B: ('cmp', 'eax, dword ptr [rdi + 0x148]'),
        0x6EE51: ('jbe', '0x14006ee72'),
        0x6F0A0: ('cmp', 'eax, dword ptr [rdi + 0x14c]'),
        0x6F0EB: ('cmp', 'eax, dword ptr [rdi + 0x148]'),
    }
    instructions = a.instructions(BEGIN, 0x6F1B4-BEGIN) + a.instructions(0x6D660, 0x35)
    decoded = {i.address-a.pe.image_base: i for i in instructions}
    for rva, expected in checks.items():
        i = decoded.get(rva)
        if i is None or (i.mnemonic, i.op_str) != expected:
            raise ValueError(f'Instruction check failed at {rva:#x}')
    states = struct.unpack('<17I', a.pe.rva_read(0x6F1B4, 68))
    if (states[11-2], states[12-2], states[18-2]) != (0x6ED70, 0x6EE25, 0x6F0C5):
        raise ValueError('Wait state jump table changed')
    return dict(
        utc=datetime.now(timezone.utc).isoformat(), executable_sha256=GAME_SHA256,
        body_rva=hex(BEGIN), body_end_exclusive=hex(END), body_sha256=BODY_SHA256,
        instruction_checks=len(checks), jump_table_checks=3,
        state_targets={str(n+2): hex(v) for n,v in enumerate(states)},
        counter_formula='uint32(c60 - c18 + c40 + c10)',
        wait_threshold_offsets={'11': '0x14c', '12': '0x148', '17': '0x14c', '18': '0x148'},
        checked_instructions=[dict(rva=hex(r), mnemonic=decoded[r].mnemonic,
                                   operands=decoded[r].op_str, bytes=decoded[r].bytes.hex()) for r in checks],
        runtime_calls_or_writes=False,
        source_hint='Pinned Dynamite issue_7 prompted the search; its addresses were not used as call targets.',
        limitation='Static dispatch and counter predicates only. Block names, allocation owner and a cleanup fix are not established.')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    result=analyze(args.executable)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f"{result['instruction_checks']} instruction checks; {result['jump_table_checks']} state targets")
