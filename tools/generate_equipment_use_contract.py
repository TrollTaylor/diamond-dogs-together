"""Record the mapped native equipment restriction setter; never execute MGSV."""
import hashlib
import json
import struct
from pathlib import Path
from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256

root = Path(__file__).resolve().parent.parent
path = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
raw = path.read_bytes()
if hashlib.sha256(raw).hexdigest() != GAME_SHA256:
    raise SystemExit('Executable fingerprint mismatch')
a = Analysis(raw)
setter = a.function(0xFE8320)
predicate = a.function(0x8AE340)
selection_caller = a.function(0x8AF040)
if predicate['decoded_bytes'] != 212 or not predicate['complete_declared_range_decoded']:
    raise SystemExit('Item-selection predicate boundary mismatch')
if selection_caller['decoded_bytes'] != 300 or not selection_caller['complete_declared_range_decoded']:
    raise SystemExit('Item-selection caller boundary mismatch')
predicate_rows = {int(i['rva'],16): (i['mnemonic'],i['operands']) for i in predicate['instructions']}
for rva, expected in {
    0x8AE34A: ('mov','rdi, rcx'), 0x8AE354: ('mov','ebx, edx'),
    0x8AE36B: ('mov','al, 1'), 0x8AE3C0: ('mov','r8, qword ptr [rdi + 0xe0]'),
    0x8AE3C9: ('lea','rax, [r8 + 0x98]'), 0x8AE3D2: ('cmp','dword ptr [rax], ebx'),
    0x8AE3DF: ('cmp','rcx, 8'), 0x8AE3E5: ('xor','al, al'),
    0x8AE3F5: ('movzx','ecx, byte ptr [rax + r8 + 0xb8]'),
    0x8AE3FE: ('and','cl, 1'), 0x8AE401: ('jne','0x1408ae36b'),
}.items():
    if predicate_rows.get(rva) != expected:
        raise SystemExit('Item-selection dataflow mismatch')
caller_rows = {int(i['rva'],16): (i['mnemonic'],i['operands']) for i in selection_caller['instructions']}
if any(caller_rows.get(r) != v for r,v in {
    0x8AF0C9: ('mov','edx, r9d'), 0x8AF0CC: ('mov','rcx, rbx'),
    0x8AF0CF: ('call','0x1408ae340'), 0x8AF0D4: ('test','al, al'),
    0x8AF0D6: ('jne','0x1408af15c'),
    0x8AF0E3: ('mov','byte ptr [rbx + 0x253], sil'),
    0x8AF0EA: ('mov','byte ptr [rbx + 0x252], al'),
}.items()):
    raise SystemExit('Item-selection caller dataflow mismatch')
support = [a.function(r) for r in [0x8DCE10, 0x8D6A30, 0x94BC50, 0x8B21A0]]
if setter['decoded_bytes'] != 144 or not setter['complete_declared_range_decoded']:
    raise SystemExit('Setter boundary mismatch')
# Named Lua setters and native setter use the same 32-entry equipment-ID list.
# Decode the registration from its exact instruction boundary, not a raw xref.
registration = a.instructions(0x8D9D7E, 56)
if [(i.mnemonic, i.op_str) for i in registration] != [
    ('lea', 'rax, [rip + 0x197c733]'), ('mov', 'qword ptr [rbp + 0x1980], rax'),
    ('lea', 'rax, [rip + 0x307d]'), ('mov', 'qword ptr [rbp + 0x1988], rax'),
    ('lea', 'rax, [rip + 0x197c727]'), ('mov', 'qword ptr [rbp + 0x1990], rax'),
    ('lea', 'rax, [rip - 0x337f]'), ('mov', 'qword ptr [rbp + 0x1998], rax')]:
    raise SystemExit('Named registration changed')
names = {0x22564B8: 'SetNoUseEquipId', 0x22564C8: 'ResetNoUseEquipId'}
for rva, name in names.items():
    if a.pe.cstring(rva) != name:
        raise SystemExit('Named binding mismatch')
slots = [0x23AF550, 0x23AF8C0]
for slot in slots:
    if struct.unpack('<Q', a.pe.rva_read(slot, 8))[0] != a.pe.image_base + 0xFE8320:
        raise SystemExit('Equipment setter interface entry changed')
body = a.pe.rva_read(0xFE8320, 144)
header = ['// Generated from the fingerprinted executable; see equipment-use contract evidence.',
          '#pragma once', '#include <cstdint>', 'namespace mgscoop::equipment_use_contract {',
          'inline constexpr std::uintptr_t setter_rva=0xfe8320;',
          'inline constexpr unsigned char setter_body[]={' + ','.join(f'0x{x:02x}' for x in body) + '};',
          'inline constexpr std::uintptr_t slots[]={0x23af550,0x23af8c0};',
          'inline constexpr std::uintptr_t predicate_rva=0x8ae340;',
          'inline constexpr unsigned char predicate_body[]={' + ','.join(f'0x{x:02x}' for x in a.pe.rva_read(0x8AE340,212)) + '};',
          'inline constexpr std::uintptr_t selection_caller_rva=0x8af040;',
          'inline constexpr unsigned char selection_caller_body[]={' + ','.join(f'0x{x:02x}' for x in a.pe.rva_read(0x8AF040,300)) + '};', '}', '']
(root/'native/equipment_use_contract.hpp').write_text('\n'.join(header))
report = dict(executable=str(path), executable_sha256=GAME_SHA256, setter=setter,
    predicate=predicate, selection_caller=selection_caller,
    supporting_functions=support, registration_rva='0x8d9d7e', registration_bytes=a.pe.rva_read(0x8D9D7E,56).hex(),
    names={hex(r):n for r,n in names.items()}, slots=[hex(r) for r in slots],
    provenance='Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e UiControllerImplSetNoUseEquipIdHook and EquipCrossEvCallIsItemNoUseHook; GPLv3 + Commons Clause 1.0',
    mapping='Native setter receives this in RCX, enable in DL, ID in R8D. It obtains storage through this+0x48 virtual slot zero. True inserts the ID into the first zero in 32 dwords at storage+0x1a4; false clears matching IDs or the entire list for ID zero. Named Lua set/reset bindings implement the same operations. Initialization clears the list; equipment update scans it and clears selected equipment on a match.',
    predicate_mapping='RCX is equipment selector, EDX item ID. The predicate checks status masks through selector+0xe8, a selector flag at +0x2de, an ID-specific status for 0x1c3, then bit zero of the matched item flag in the eight-entry storage+0x98/+0xb8 arrays. AL true refuses selection: the complete 0x8af040 caller returns before changing selector indices +0x252/+0x253. This is a semantic current-build equivalent, not a recovered original symbol name.',
    adaptation='Forward through the original setter with ID 9999, and forward through the original selection predicate before overriding its Boolean to false, only while prepared co-op player rules are active. Both hooks must install before either overrides.',
    limitations=['Static executable inspection only; no calls, Lua execution or tests.',
                 'Returning false bypasses every refusal reason in this predicate, including status and item-specific checks; other independent equipment checks remain unchanged.',
                 'The sentinel is upstream policy, not proof of a universally unused equipment ID.',
                 'No runtime owner lifetime, item use, connected guest or gameplay behavior verified.'])
(root/'docs/evidence/equipment-selection-contract-2026-09-10.json').write_text(json.dumps(report,indent=2)+'\n')
print('Recorded equipment setter, selection predicate and complete selection-caller dataflow; no game execution.')
