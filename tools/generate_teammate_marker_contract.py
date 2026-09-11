"""Record the retail marker conversion path. Disassembly only, no game execution."""
import hashlib
import json
import struct
from pathlib import Path

from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256

ROOT = Path(__file__).resolve().parent.parent
EXE = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
raw = EXE.read_bytes()
if hashlib.sha256(raw).hexdigest() != GAME_SHA256:
    raise SystemExit('Executable fingerprint mismatch')
a = Analysis(raw)
begin, code_end, end = 0x935020, 0x935750, 0x935920
if (begin, end) not in [(b, e) for b, e, _ in a.ranges]:
    raise SystemExit('Converter declared boundary mismatch')
instructions = a.instructions(begin, code_end - begin)
if sum(i.size for i in instructions) != code_end - begin:
    raise SystemExit('Converter code boundary mismatch')
rows = {i.address - a.pe.image_base: (i.mnemonic, i.op_str) for i in instructions}
required = {
    0x93502F: ('movzx', 'r9d, byte ptr [rdx + 0x1e]'),
    0x935034: ('movzx', 'edi, byte ptr [rdx + 0x20]'),
    0x935038: ('movzx', 'ebx, word ptr [rdx + 0x28]'),
    0x93503C: ('movzx', 'ecx, word ptr [rdx + 0x2a]'),
    0x93504C: ('cmp', 'eax, 0x23'),
    0x93505C: ('mov', 'eax, dword ptr [r10 + rax*4 + 0x935750]'),
    0x93506E: ('test', 'ax, bx'),
    0x935073: ('mov', 'r8d, 5'),
    0x9350FA: ('bts', 'r8d, 0x15'),
    0x935112: ('mov', 'eax, 0x1c'),
    0x93511E: ('mov', 'eax, 0x1d'),
    0x9351E6: ('mov', 'word ptr [rdx + 0x28], ax'),
    0x935725: ('mov', 'eax, r8d'),
    0x935737: ('ret', ''),
    0x935747: ('mov', 'eax, r8d'),
    0x93574F: ('ret', ''),
}
if any(rows.get(r) != v for r, v in required.items()):
    raise SystemExit('Marker conversion argument/result dataflow mismatch')
# The unwind range contains switch data after its final return. Do not label
# linear decoding of those bytes as instructions or truncate the byte gate.
switches = []
for rva, count in [(0x935750, 36), (0x9357E0, 15), (0x9358F0, 12)]:
    targets = struct.unpack('<' + 'I' * count, a.pe.rva_read(rva, count * 4))
    if any(t not in rows for t in targets):
        raise SystemExit('Switch target is not a decoded instruction')
    switches.append(dict(rva=hex(rva), targets=[hex(t) for t in targets]))
main_switch = switches[0]['targets']
if main_switch[13] != '0x935069' or main_switch[18] != '0x93510f':
    raise SystemExit('Player/buddy system marker cases changed')
indices = a.pe.rva_read(0x93581C, 209)
if max(indices) >= 15:
    raise SystemExit('Vehicle switch index exceeds target table')

support = [a.function(r) for r in [0x9343A0, 0x934550, 0x936B50, 0x936C60]]
if any(not f['complete_declared_range_decoded'] for f in support):
    raise SystemExit('Incomplete support function')
ctor = {int(i['rva'], 16): (i['mnemonic'], i['operands']) for i in support[0]['instructions']}
caller = {int(i['rva'], 16): (i['mnemonic'], i['operands']) for i in support[2]['instructions']}
if ctor.get(0x934416) != ('lea', 'rax, [rip + 0x193f8c3]') or ctor.get(0x93441D) != ('mov', 'qword ptr [rbx], rax'):
    raise SystemExit('Converter owner constructor changed')
if any(caller.get(r) != v for r, v in {
    0x936BC0: ('mov', 'rdx, rax'), 0x936BC3: ('mov', 'rcx, rsi'),
    0x936BC6: ('call', 'qword ptr [r8 + 0x2b8]'), 0x936BCD: ('or', 'ebx, eax'),
}.items()):
    raise SystemExit('Converter interface caller changed')
table, slot = 0x2273CE0, 0x2273F98
if slot != table + 0x2B8 or struct.unpack('<Q', a.pe.rva_read(slot, 8))[0] != a.pe.image_base + begin:
    raise SystemExit('Converter interface entry changed')

bodies = [(begin, end - begin)] + [(int(f['entry_rva'], 16), f['decoded_bytes']) for f in support]
header = ['// Generated from the fingerprinted executable; see teammate-marker contract evidence.',
          '#pragma once', '#include <cstddef>', '#include <cstdint>',
          'namespace mgscoop::teammate_marker_contract {',
          'inline constexpr std::uintptr_t converter_rva=0x935020;',
          'inline constexpr std::uintptr_t converter_slot=0x2273f98;',
          'inline constexpr unsigned int fob_enemy=0x200005, buddy=0x1c;',
          'struct Body {std::uintptr_t rva; std::size_t size; unsigned char sha256[32];};',
          'inline constexpr Body bodies[]={']
for rva, size in bodies:
    digest = hashlib.sha256(a.pe.rva_read(rva, size)).digest()
    header.append('{' + hex(rva) + ',' + str(size) + ',{' + ','.join(f'0x{x:02x}' for x in digest) + '}},')
header.extend(['};', '}', ''])
(ROOT / 'native/teammate_marker_contract.hpp').write_text('\n'.join(header))
report = dict(
    executable=str(EXE), executable_sha256=GAME_SHA256,
    converter=dict(entry_rva=hex(begin), code_end=hex(code_end), range_end=hex(end),
        code_bytes=code_end-begin, table_and_padding_bytes=end-code_end,
        instructions=[dict(rva=hex(i.address-a.pe.image_base), bytes_hex=i.bytes.hex(' '),
                           mnemonic=i.mnemonic, operands=i.op_str) for i in instructions],
        switches=switches, vehicle_switch_indices_hex=indices.hex()),
    supporting_functions=support, interface_table=hex(table), interface_slot=hex(slot),
    body_gates=[dict(rva=hex(r), size=s, sha256=hashlib.sha256(a.pe.rva_read(r, s)).hexdigest()) for r, s in bodies],
    provenance='Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e GetUiMarkerTypeFromSystemType2Hook; GPLv3 + Commons Clause 1.0',
    license_sha256=hashlib.sha256((ROOT/'third_party/dynamite/LICENSE').read_bytes()).hexdigest(),
    mapping='RCX owner, RDX marker record, EAX UI marker type. Owner constructor installs table 0x2273ce0; two complete callers dispatch slot +0x2b8 using records from the marker system. System type 14 with flag 0x100 produces base 5; flag 0x10 or 0x20 adds bit 21. Buddy system type 19 with flag 1 selects 0x1c. Other flag modifiers and record writes remain in the original converter.',
    adaptation='Call the original converter first. Only in the active owned co-op session, replace exact result 0x200005 with 0x1c. Other results and original record updates are preserved.',
    limitations=['Static disassembly only; no executable calls, Lua execution or tests.',
                 'Semantic mapping, not a recovered original symbol.',
                 'An existing native marker is required. This hook does not create one, extend its duration or synchronize markers.',
                 'Uses the upstream D-Dog icon and exact equality; modified FOB marker values are deliberately not masked.',
                 'Owned-session scope is existing lifecycle metadata, not an atomic native lifetime guarantee.',
                 'No rendered icon, connected guest or playable movement/combat verified.'])
(ROOT/'docs/evidence/teammate-marker-contract-2026-09-10.json').write_text(json.dumps(report, indent=2)+'\n')
print('Recorded marker conversion code, switch tables, owner and interface callers; no game execution.')
