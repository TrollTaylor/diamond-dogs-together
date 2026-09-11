"""Exact-build player constructor and network allocation branch analysis.

Read-only file analysis. Source names from pinned tpp-mod and Dynamite are
explicitly separated from verified instruction dataflow and live behavior.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

from analyze_network_bindings import Analysis
from inspect_player_system import EXPECTED

RANGES = {
    'player_constructor': (0x9BA700, 0x9BABC5),
    'player_allocate_wrapper': (0x9BD7A0, 0x9BD7D4),
    'player_allocate': (0x9BD7E0, 0x9BEC45),
    'quark_system_table': (0xBFF050, 0xBFF062),
}
CHECKS = {
    0x9BA720: ('mov', 'rsi, rcx'),
    0x9BA747: ('lea', 'rdi, [rcx + 0x20]'),
    0x9BA754: ('lea', 'rax, [rip + 0x18dac75]'),
    0x9BA75B: ('mov', 'qword ptr [rsi], rax'),
    0x9BA75E: ('lea', 'rax, [rip + 0x18dad2b]'),
    0x9BA765: ('mov', 'qword ptr [rdi], rax'),
    0x9BA9CC: ('call', '0x140bff050'),
    0x9BA9D1: ('mov', 'rbx, qword ptr [rax + 0x98]'),
    0x9BAA2B: ('mov', 'rcx, qword ptr [rbx + 0xf8]'),
    0x9BAA32: ('mov', 'rax, qword ptr [rcx]'),
    0x9BAA35: ('mov', 'dl, 0x14'),
    0x9BAA37: ('call', 'qword ptr [rax]'),
    0x9BAA39: ('test', 'al, al'),
    0x9BAA3B: ('je', '0x1409baa47'),
    0x9BAA3D: ('or', 'dword ptr [rdi + 0x204], 0x100'),
    0x9BD7B1: ('call', '0x1409bd7e0'),
    0x9BD82B: ('mov', 'rsi, rcx'),
    0x9BD841: ('call', '0x140bff050'),
    0x9BD846: ('mov', 'rcx, qword ptr [rax + 0x78]'),
    0x9BD84A: ('mov', 'rax, qword ptr [rcx]'),
    0x9BD84D: ('call', 'qword ptr [rax + 0x40]'),
    0x9BD850: ('mov', 'r15, rax'),
    0x9BD853: ('mov', 'r14d, dword ptr [rsi + 0x224]'),
    0x9BD85A: ('shr', 'r14d, 8'),
    0x9BD85E: ('and', 'r14b, 1'),
    0x9BD862: ('je', '0x1409bd87a'),
    0x9BD86A: ('call', 'qword ptr [rdx + 0x20]'),
    0x9BD86D: ('cmp', 'r13d, eax'),
    0x9BD870: ('cmovb', 'eax, r13d'),
    0x9BD88A: ('mov', 'dword ptr [rsi + 0x228], r13d'),
    0x9BD8B0: ('test', 'r14b, r14b'),
    0x9BD8B3: ('je', '0x1409bd8e1'),
    0x9BD8D7: ('call', 'qword ptr [rax + 0x28]'),
    0x9BD8DA: ('mov', 'qword ptr [rsi + 0x178], rax'),
    0x9BD996: ('test', 'r14b, r14b'),
    0x9BD999: ('je', '0x1409bd9ca'),
    0x9BD9A1: ('call', 'qword ptr [rax]'),
    0x9BD9AE: ('call', 'qword ptr [rax]'),
    0x9BD9B0: ('movzx', 'edi, al'),
    0x9BD9B9: ('call', 'qword ptr [rax + 0x28]'),
    0x9BD9CD: ('mov', 'dword ptr [rsi + 0x234], edi'),
    0x9BDA96: ('test', 'r14b, r14b'),
    0x9BDA99: ('je', '0x1409bdaef'),
    0x9BDAA0: ('movabs', 'rbx, 0x6593a4717b40'),
    0x9BDAB4: ('call', '0x140bf1650'),
    0x9BDAE8: ('mov', 'qword ptr [rsi + 0xc8], rax'),
}


def inspect(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('unsupported executable')
    a = Analysis(data)
    ranges = {(b, e) for b, e, _ in a.ranges}
    bodies, decoded = {}, {}
    for name, (begin, end) in RANGES.items():
        if (begin, end) not in ranges:
            raise ValueError(f'function boundary mismatch: {name}')
        instructions = a.instructions(begin, end - begin)
        if sum(i.size for i in instructions) != end - begin:
            raise ValueError(f'incomplete body: {name}')
        rows = []
        for i in instructions:
            rva = i.address - a.pe.image_base
            decoded[rva] = (i.mnemonic, i.op_str)
            rows.append({'rva': hex(rva), 'bytes': i.bytes.hex(),
                         'mnemonic': i.mnemonic, 'operands': i.op_str})
        bodies[name] = {'begin_rva': hex(begin), 'end_rva_exclusive': hex(end),
                        'sha256': hashlib.sha256(a.pe.rva_read(begin, end - begin)).hexdigest(),
                        'complete_exception_range': True, 'instructions': rows}
    for rva, wanted in CHECKS.items():
        if decoded.get(rva) != wanted:
            raise ValueError(f'instruction mismatch {rva:#x}: {decoded.get(rva)}')
    # Constructor installs these tables. The primary allocation slot reaches
    # the inspected allocation wrapper; secondary offsets match the live reader.
    if struct.unpack('<Q', a.pe.rva_read(0x22953D0, 8))[0] != a.pe.image_base + 0x9BD7A0:
        raise ValueError('player allocation method slot mismatch')
    return {
        'utc': datetime.now(timezone.utc).isoformat(), 'executable_sha256': EXPECTED,
        'instruction_checks': len(CHECKS), 'method_table_checks': 1,
        'runtime_calls_or_writes': False, 'bodies': bodies,
        'verified_dataflow': [
            'Constructor installs primary table 0x22953d0 and secondary table 0x2295490 at enclosing+0x20.',
            'Application service at QuarkSystemTable+0x98, member+0xf8, method zero is queried with byte 0x14.',
            'A true result sets bit 8 at secondary+0x204, equivalent to enclosing+0x224.',
            'Allocation reads the same bit and obtains an interface via service+0x78 method+0x40.',
            'With bit 8 set, allocation clamps its count using interface method+0x20, performs service method+0x28 setup, reads local member/index and host-related method results, and creates a conditional component.',
            'With bit 8 clear, those branches are skipped even if the asset reserves four entries.',
        ],
        'source_interpretation': {
            'status_0x14': 'Dynamite enum names index 20 S_IS_ONLINE.',
            'service_0x78': 'tpp-mod QuarkSystemTable calls this networkSystem.',
            'application_0xf8': 'tpp-mod ApplicationSystem_tpp calls this gameStatusInterface.',
            'session_methods': 'tpp-mod SessionImpl2 interface names corresponding slots local member, original value count, and IsHost; concrete virtual targets not independently resolved here.',
        },
        'decision': 'Preserve early session/online setup as the implementation candidate. Separate early host prerequisites from later player readiness; later readiness alone cannot recreate skipped initialization.',
        'limitations': [
            'Static branches do not prove their natural execution timing or a working networked actor.',
            'Concrete service virtual targets and the flag setter are not verified by this report.',
            'No exhaustive proof excludes a supported reinitialization path; none is established.',
            'Host resource lifetime and asynchronous cleanup remain separate unresolved requirements.',
        ],
        'source_pins': ['Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e',
                        'tpp-mod 301bf797b7f8032941b21ed56a3570338de6ead4'],
    }


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    result = inspect(args.game)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'bodies'}, indent=2))
