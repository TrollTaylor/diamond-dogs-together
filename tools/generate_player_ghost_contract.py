"""Map Dynamite's second-player mission-gate bypass to the installed executable.

Reads the executable only. The generated body is also executed in an isolated
fixture with substitute network services; this does not simulate a remote peer.
"""
import hashlib
import json
from pathlib import Path
from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256

root = Path(__file__).resolve().parent.parent
raw = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == GAME_SHA256
a = Analysis(raw)
f = a.function(0x9C1710)
rows = {int(i['rva'], 16): (i['mnemonic'], i['operands']) for i in f['instructions']}
checks = {
    0x9C171A: ('movzx', 'edi, dl'),
    0x9C171D: ('mov', 'rbx, rcx'),
    0x9C1720: ('call', '0x140bff050'),
    0x9C1725: ('call', '0x140bff050'),
    0x9C172A: ('mov', 'rcx, qword ptr [rax + 0x78]'),
    0x9C1731: ('call', 'qword ptr [rax + 0x40]'),
    0x9C173A: ('call', 'qword ptr [r8 + 0x18]'),
    0x9C173E: ('cmp', 'dword ptr [rbx + 0x228], 2'),
    0x9C1745: ('jne', '0x1409c1779'),
    0x9C1747: ('cmp', 'eax, 2'),
    0x9C174A: ('jne', '0x1409c1779'),
    0x9C174C: ('mov', 'eax, dword ptr [rbx + 0x224]'),
    0x9C1752: ('shr', 'eax, 8'),
    0x9C1755: ('test', 'al, 1'),
    0x9C1757: ('je', '0x1409c1779'),
    0x9C1759: ('test', 'dil, dil'),
    0x9C175C: ('jne', '0x1409c1779'),
    0x9C175E: ('mov', 'eax, 0xc382'),
    0x9C1763: ('cmp', 'word ptr [rbx + 0x4ec], ax'),
    0x9C176A: ('jne', '0x1409c1779'),
    0x9C176C: ('mov', 'al, 1'),
    0x9C1779: ('xor', 'al, al'),
}
assert all(rows.get(rva) == expected for rva, expected in checks.items())
assert f['decoded_bytes'] == 118 and f['complete_declared_range_decoded']
body = a.pe.rva_read(0x9C1710, 118)
assert body[0x5A:0x5C] == bytes.fromhex('75 0d')
report = dict(executable_sha256=GAME_SHA256, function=f, instruction_checks=len(checks),
              patch_rva='0x9c176a', expected='75 0d', replacement='66 90',
              provenance='Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e GetPatches DoesFobGhostPlayer',
              retained_checks=['player allocation count equals 2', 'native session method +18 returns 2',
                               'online initialization bit 8', 'incoming DL equals zero'],
              limitations=['Static identity is derived from matching the unique mission gate, player fields and native session calls.',
                           'Removing this gate permits the native ghost path; it does not establish a connected or controlled guest.'])
team_functions = {name: a.function(rva) for name, rva in [('defense', 0x1E03080), ('offense', 0x1E030B0)]}
team_checks = {
    0x1E03084: ('call', '0x140bff050'),
    0x1E03089: ('mov', 'rcx, qword ptr [rax + 0x78]'),
    0x1E03090: ('call', 'qword ptr [rax + 0x40]'),
    0x1E03099: ('call', 'qword ptr [rdx + 0x28]'),
    0x1E0309C: ('test', 'al, al'),
    0x1E0309E: ('sete', 'al'),
    0x1E030B4: ('call', '0x140bff050'),
    0x1E030B9: ('mov', 'rcx, qword ptr [rax + 0x78]'),
    0x1E030C0: ('call', 'qword ptr [rax + 0x40]'),
    0x1E030C9: ('add', 'rsp, 0x28'),
    0x1E030CD: ('jmp', 'qword ptr [rdx + 0x28]'),
}
team_rows = {int(i['rva'], 16): (i['mnemonic'], i['operands'])
             for fteam in team_functions.values() for i in fteam['instructions']}
assert all(team_rows.get(rva) == expected for rva, expected in team_checks.items())
assert all(fteam['complete_declared_range_decoded'] for fteam in team_functions.values())
report['team_functions'] = team_functions
report['team_instruction_checks'] = len(team_checks)
report['team_dataflow'] = 'Both use the network service session interface +28 predicate; defense negates its Boolean result and offense tail-returns it.'
report['limitations'].append('Native team identity is derived from this paired dataflow and the pinned Dynamite predicates, not a symbol-bearing retail executable.')
(root / 'docs/evidence/player-ghost-contract-2026-09-09.json').write_text(json.dumps(report, indent=2)+'\n')
lines = ['// Generated from the fingerprinted local executable by generate_player_ghost_contract.py.',
         '#pragma once', '#include <cstddef>', '#include <cstdint>',
         'namespace mgscoop::player_ghost {',
         'inline constexpr std::uintptr_t body_rva=0x9c1710;',
         'inline constexpr std::size_t branch_offset=0x5a;',
         'inline constexpr unsigned char expected_body[]={'+','.join(f'0x{x:02x}' for x in body)+'};',
         ]
for name, fteam in team_functions.items():
    rva = int(fteam['entry_rva'], 16)
    team_body = a.pe.rva_read(rva, fteam['decoded_bytes'])
    lines += [f'inline constexpr std::uintptr_t {name}_rva={hex(rva)};',
              f'inline constexpr unsigned char {name}_body[]={{'+','.join(f'0x{x:02x}' for x in team_body)+'};']
lines.append('}')
(root / 'native/player_ghost_contract.hpp').write_text('\n'.join(lines)+'\n')
print(f'{len(checks)} ghost and {len(team_checks)} team instruction checks; three complete native bodies emitted')
