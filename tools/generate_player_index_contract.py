"""Map the defender-index rule from the fingerprinted executable; static reads only."""
import hashlib
import json
from pathlib import Path
from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256

root = Path(__file__).resolve().parent.parent
executable = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
raw = executable.read_bytes()
assert hashlib.sha256(raw).hexdigest() == GAME_SHA256
a = Analysis(raw)
begin, end = 0x1E026B0, 0x1E02765
body = a.pe.rva_read(begin, end-begin)
instructions = a.instructions(begin, len(body))
assert sum(i.size for i in instructions) == len(body)
rows = {i.address-a.pe.image_base: (i.mnemonic, i.op_str) for i in instructions}
checks = {
    0x1E026B6: ('call', '0x140bff050'),
    0x1E026BB: ('mov', 'rcx, qword ptr [rax + 0x78]'),
    0x1E026C2: ('call', 'qword ptr [rax + 0x40]'),
    0x1E026CD: ('or', 'eax, 0xffffffff'),
    0x1E026DC: ('call', 'qword ptr [rax + 8]'),
    0x1E026F4: ('call', 'qword ptr [rax]'),
    0x1E026FC: ('movzx', 'esi, al'),
    0x1E02701: ('call', 'qword ptr [rdx + 0x20]'),
    0x1E02713: ('movzx', 'edx, dil'),
    0x1E0271A: ('call', 'qword ptr [r8 + 0x10]'),
    0x1E02729: ('call', 'qword ptr [rdx]'),
    0x1E0272B: ('cmp', 'al, sil'),
    0x1E0272E: ('jne', '0x141e02752'),
    0x1E02738: ('call', 'qword ptr [rax + 0x20]'),
    0x1E0273D: ('jl', '0x141e02710'),
    0x1E02749: ('or', 'eax, 0xffffffff'),
    0x1E0275C: ('movzx', 'eax, al'),
    0x1E02764: ('ret', ''),
}
assert all(rows.get(rva) == expected for rva, expected in checks.items())
report = {
    'executable': str(executable), 'executable_sha256': GAME_SHA256,
    'entry_rva': hex(begin), 'end_rva_exclusive': hex(end), 'body_size': len(body),
    'body_sha256': hashlib.sha256(body).hexdigest(),
    'unwind_ranges': [{'begin': hex(b), 'end': hex(e)} for b, e, _ in a.ranges if begin <= b < end],
    'instruction_checks': len(checks),
    'instructions': [{'rva': hex(i.address-a.pe.image_base), 'bytes': i.bytes.hex(),
                      'mnemonic': i.mnemonic, 'operands': i.op_str} for i in instructions],
    'provenance': 'Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e GetFobDefensePlayerInstanceIndexHook and defensePlayerID=15',
    'interface_reference': 'tpp-mod 301bf797b7f8032941b21ed56a3570338de6ead4 SessionImpl2_SessionInterface_vtbl',
    'dataflow': 'Network service +78, session getter +40, host member +8, member index +0; iterate roster via +10 and return first member index unequal to host. Missing session, host or non-host returns UINT_MAX.',
    'replacement': 'During the existing co-op scene lifetime, return upstream value 15. Otherwise forward the original function with its argument.',
    'limitations': [
        'Static mapping from dataflow and pinned sources; retail executable has no verified symbol name.',
        'Full logical body spans three unwind ranges; no native execution was performed.',
        'Value 15 is the upstream co-op policy, not the original function failure value UINT_MAX, and not evidence of an allocated player.',
        'This removes the guest defender assignment; dynamic offense targeting and shared combat remain unverified.'
    ],
}
(root/'docs/evidence/player-index-contract-2026-09-10.json').write_text(json.dumps(report, indent=2)+'\n')
(root/'native/player_index_contract.hpp').write_text('\n'.join([
    '// Generated from the fingerprinted executable by generate_player_index_contract.py.',
    '#pragma once', '#include <cstdint>', 'namespace mgscoop::player_index {',
    f'inline constexpr std::uintptr_t defense_rva={hex(begin)};',
    'inline constexpr unsigned char defense_body[]={'+','.join(f'0x{x:02x}' for x in body)+'};',
    'inline constexpr unsigned int coop_defender_index=15;', '}', ''
]))
print(f'Emitted complete {len(body)}-byte defender body and static provenance report.')
