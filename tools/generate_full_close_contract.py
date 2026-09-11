"""Map DYNAMITE's mission branch adaptation without executing MGSV."""
import hashlib
import json
from pathlib import Path
from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256

root = Path(__file__).resolve().parent.parent
path = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
raw = path.read_bytes()
assert hashlib.sha256(raw).hexdigest() == GAME_SHA256
a = Analysis(raw)
begin, end, displacement = 0xA1A710, 0xA1A781, 0xA1A75A
instructions = a.instructions(begin, end - begin)
assert sum(i.size for i in instructions) == end - begin
rows = {i.address - a.pe.image_base: (i.mnemonic, i.op_str) for i in instructions}
checks = {
    0xA1A716: ('call', '0x140313780'),
    0xA1A721: ('je', '0x140a1a779'),
    0xA1A749: ('cmp', 'word ptr [rbx + 0x12], ax'),
    0xA1A74D: ('mov', 'rcx, qword ptr [rcx + 0x190]'),
    0xA1A759: ('jne', '0x140a1a76e'),
    0xA1A75B: ('test', 'rcx, rcx'),
    0xA1A75E: ('je', '0x140a1a76e'),
    0xA1A763: ('call', 'qword ptr [rax + 0x18]'),
    0xA1A76D: ('ret', ''),
    0xA1A776: ('call', 'qword ptr [rax + 0x60]'),
}
assert all(rows.get(rva) == expected for rva, expected in checks.items())
body = a.pe.rva_read(begin, end - begin)
assert body[displacement - begin - 1:displacement - begin + 1] == b'\x75\x13'
adapted = bytearray(body)
adapted[displacement - begin] = 0
decoded = list(a.disassembler.disasm(bytes(adapted), a.pe.image_base + begin))
assert [(i.address, i.size) for i in decoded] == [(i.address, i.size) for i in instructions]
branch = next(i for i in decoded if i.address == a.pe.image_base + displacement - 1)
assert (branch.mnemonic, branch.op_str) == ('jne', '0x140a1a75b')
support = [a.function(rva) for rva in [0xBFF050, 0xBFEFA0]]
assert all(f['complete_declared_range_decoded'] for f in support)
lines = ['// Generated from the exact MGSV executable; no runtime evidence implied.',
         '#pragma once', '#include <cstddef>', '#include <cstdint>',
         'namespace mgscoop::full_close_contract {',
         'struct Body {std::uintptr_t rva; std::size_t size; const unsigned char* bytes;};',
         f'constexpr std::uintptr_t wrapper_rva={hex(begin)};',
         f'constexpr std::size_t displacement_offset={hex(displacement-begin)};']
bodies = [(begin, body)] + [(int(f['entry_rva'], 16), a.pe.rva_read(int(f['entry_rva'], 16), f['decoded_bytes'])) for f in support]
for i, (rva, data) in enumerate(bodies):
    lines.append(f'constexpr unsigned char bytes_{i}[]={{' + ','.join(f'0x{x:02x}' for x in data) + '};')
lines.append('constexpr Body bodies[]={')
for i, (rva, data) in enumerate(bodies):
    lines.append('{' + hex(rva) + f',sizeof(bytes_{i}),bytes_{i}' + '},')
lines.extend(['};', '}', ''])
(root / 'native/full_close_contract.hpp').write_text('\n'.join(lines))
report = {
    'executable': str(path), 'executable_sha256': GAME_SHA256,
    'upstream': 'Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e dynamitePatches.cpp full CloseSession',
    'wrapper': {'begin': hex(begin), 'end': hex(end), 'sha256': hashlib.sha256(body).hexdigest(),
                'instructions': [{'rva': hex(i.address-a.pe.image_base), 'op': i.mnemonic+' '+i.op_str} for i in instructions]},
    'support': support,
    'adaptation': {'rva': hex(displacement), 'original_byte': '13', 'replacement_byte': '00',
                   'effect': 'JNE +0x13 becomes JNE +0: either condition reaches the original service-null check. Instruction sizes, flags and all native calls remain unchanged.'},
    'limitations': [
        'Static code inspection only; not a runtime test.',
        'This adapts the verified wrapper branch, not the concrete FOB parameter-service implementation, which remains unmapped.',
        'The original game retains service dispatch, null fallback and asynchronous lifetime management.',
        'A one-byte change is process-wide while armed; concurrent callers during that short interval are not excluded by a source-thread gate.',
        'Returning from CloseSession does not prove service cleanup or safe reconnect.',
    ],
}
(root / 'docs/evidence/full-close-contract-2026-09-10.json').write_text(json.dumps(report, indent=2)+'\n')
print('Mapped the full close wrapper, two context accessors and same-size branch adaptation; no game execution.')
