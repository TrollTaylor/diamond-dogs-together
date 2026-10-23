"""Record the retail Quiet/Ocelot player-type reset branch. Disassembly only, no game execution.

Reads the fingerprinted installed executable (read-only) and writes
native/player_type_contract.hpp plus a JSON evidence report. Every fact the
native patch depends on is re-checked here and the generator refuses on any
difference, so a regenerated header always matches a re-verified body.
"""
import hashlib
import json
import re
import struct
from bisect import bisect_right
from pathlib import Path

from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256

ROOT = Path(__file__).resolve().parent.parent
EXE = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
raw = EXE.read_bytes()
if hashlib.sha256(raw).hexdigest() != GAME_SHA256:
    raise SystemExit('Executable fingerprint mismatch')
a = Analysis(raw)
pe = a.pe
IB = pe.image_base

# --- Declared function extent: primary unwind range plus five chained ranges.
begin, code_end, end = 0x9CD030, 0x9CE8E4, 0x9CE924
expected_ranges = [(0x9CD030, 0x9CD0A4), (0x9CD0A4, 0x9CD0CD), (0x9CD0CD, 0x9CE8BB),
                   (0x9CE8BB, 0x9CE8C3), (0x9CE8C3, 0x9CE8E4), (0x9CE8E4, 0x9CE924)]
declared = [(b, e, u) for b, e, u in a.ranges if begin <= b < end]
if [(b, e) for b, e, _ in declared] != expected_ranges:
    raise SystemExit('UpdatePartsStatus unwind ranges changed')
chain = []
for b, e, unwind in declared:
    header = pe.rva_read(unwind, 4)
    flags, codes = header[0] >> 3, header[2]
    parent = None
    if flags & 4:  # UNW_FLAG_CHAININFO: parent RUNTIME_FUNCTION follows the codes
        offset = unwind + 4 + ((codes + 1) & ~1) * 2
        parent = struct.unpack('<III', pe.rva_read(offset, 12))
        if not begin <= parent[0] < end:
            raise SystemExit('Chained unwind range leaves the function')
    elif b != begin:
        raise SystemExit('Secondary range is not chained to the function')
    chain.append(dict(range=[hex(b), hex(e)], unwind=hex(unwind), flags=flags,
                      parent=None if parent is None else [hex(parent[0]), hex(parent[1])]))
# The equality above also proves no other function starts inside the extent.

# --- Complete linear decode of the code part; the tail holds switch tables.
instructions = a.instructions(begin, code_end - begin)
if sum(i.size for i in instructions) != code_end - begin:
    raise SystemExit('UpdatePartsStatus code boundary mismatch')
rows = {i.address - IB: (i.mnemonic, i.op_str) for i in instructions}
required = {
    # Entry: enclosing player object in RCX/RSI, parts-info object Q in R14.
    0x9CD05F: ('mov', 'r14, qword ptr [rcx + 0x80]'),
    0x9CD066: ('mov', 'rsi, rcx'),
    0x9CD072: ('call', '0x140bff050'),
    0x9CD077: ('mov', 'rbx, qword ptr [rax + 0x98]'),
    0x9CD07E: ('mov', 'r13, qword ptr [rbx + 0x10]'),
    # Per-instance loop bounded by the instance count.
    0x9CD0C0: ('cmp', 'dword ptr [rsi + 0x228], r10d'),
    0x9CE896: ('cmp', 'r12d, dword ptr [rsi + 0x228]'),
    0x9CE89D: ('jb', '0x1409cd0e5'),
    # Local instance only, outside parts-loading states 1/2.
    0x9CD2CF: ('jbe', '0x1409ce768'),
    0x9CD2D5: ('cmp', 'r12d, dword ptr [rsi + 0x234]'),
    0x9CD2DC: ('jne', '0x1409ce768'),
    # vars.playerType (+0xfb) / vars.playerCamoType (+0xf9) are read.
    0x9CD303: ('movzx', 'eax, byte ptr [r13 + 0xfb]'),
    0x9CD30B: ('movzx', 'ebx, byte ptr [r13 + 0xf9]'),
    0x9CD313: ('mov', 'byte ptr [rsp + 0x45], al'),
    # type in {5,6} (OCELOT, QUIET) selects the guarded reset.
    0x9CD353: ('movzx', 'eax, byte ptr [rsp + 0x45]'),
    0x9CD358: ('lea', 'ecx, [rax - 5]'),
    0x9CD35B: ('cmp', 'cl, 1'),
    0x9CD35E: ('ja', '0x1409cdb31'),
    # Guard 1: enclosing+0x224 bit 18 (set in allocation for FOB missions).
    0x9CD364: ('mov', 'ecx, dword ptr [rsi + 0x224]'),
    0x9CD36A: ('shr', 'ecx, 0x12'),
    0x9CD36D: ('test', 'cl, 1'),
    0x9CD370: ('jne', '0x1409cdb12'),
    # Guard 2: enclosing+0x284 bit 2.
    0x9CD376: ('test', 'byte ptr [rsi + 0x284], 4'),
    0x9CD37D: ('jne', '0x1409cdb12'),
    # Reset: vars.playerType = SNAKE (0) and a notification on the player.
    0x9CD383: ('xor', 'al, al'),
    0x9CD385: ('mov', 'byte ptr [rsp + 0x45], al'),
    0x9CD389: ('mov', 'byte ptr [r13 + 0xfb], al'),
    0x9CD3A2: ('call', 'qword ptr [rax + 0x2a8]'),
    0x9CD3AD: ('jmp', '0x1409cdb35'),
    # Keep path: OCELOT -> parts 0x1a camo 0x73, QUIET -> parts 0x1b camo 0x74.
    0x9CDB12: ('cmp', 'al, 5'),
    0x9CDB16: ('mov', 'dil, 0x1a'),
    0x9CDB19: ('mov', 'bl, 0x73'),
    0x9CDB1D: ('cmp', 'al, 6'),
    0x9CDB21: ('mov', 'dil, 0x1b'),
    0x9CDB24: ('mov', 'bl, 0x74'),
    0x9CDB2A: ('mov', 'byte ptr [r13 + 0xf9], bl'),
    0x9CDBDC: ('mov', 'byte ptr [r13 + 0xf8], dil'),
    0x9CE8E0: ('ret', ''),
}
mismatch = {hex(r): (rows.get(r), v) for r, v in required.items() if rows.get(r) != v}
if mismatch:
    raise SystemExit('Player-type reset dataflow mismatch: ' + repr(mismatch))

# --- The patched branch and its replacement encoding.
site = 0x9CD370
original = bytes.fromhex('0f859c070000')
replacement = bytes.fromhex('48e99c070000')
if pe.rva_read(site, 6) != original:
    raise SystemExit('Branch bytes changed')
if site % 2 or (site - begin) % 2:
    raise SystemExit('Branch is not word aligned for the interlocked exchange')
if site % 64 + len(original) > 64 or IB % 64:
    raise SystemExit('Branch instruction crosses a cache line')
if original[2:] != replacement[2:]:
    raise SystemExit('Replacement must differ only in its first two bytes')
decoded = list(a.disassembler.disasm(replacement, IB + site))
if len(decoded) != 1 or decoded[0].size != 6 or decoded[0].mnemonic != 'jmp' or decoded[0].op_str != '0x1409cdb12':
    raise SystemExit('Replacement does not decode as one unconditional jump to the keep path')
code_section = pe.rva_read(0x1000, 0x208C000)
occurrences = [m.start() + 0x1000 for m in re.finditer(re.escape(original), code_section)]
if occurrences != [site]:
    raise SystemExit('Branch byte pattern is not unique in the code section')

# --- Switch tables after the final return (data inside the declared range).
switches = []
for rva, count in [(0x9CE8E4, 2), (0x9CE908, 7)]:
    targets = struct.unpack('<' + 'I' * count, pe.rva_read(rva, count * 4))
    if any(t not in rows for t in targets):
        raise SystemExit('Switch target is not a decoded instruction')
    switches.append(dict(rva=hex(rva), targets=[hex(t) for t in targets]))
index_table = pe.rva_read(0x9CE8EC, 0x9CE906 - 0x9CE8EC)
if max(index_table) >= 2:
    raise SystemExit('Switch index table exceeds its target table')
for r, v in {0x9CDCB7: ('movzx', 'eax, byte ptr [r8 + rax + 0x9ce8ec]'),
             0x9CDCC0: ('mov', 'ecx, dword ptr [r8 + rax*4 + 0x9ce8e4]'),
             0x9CDCDB: ('mov', 'ecx, dword ptr [r8 + rax*4 + 0x9ce908]')}.items():
    if rows.get(r) != v:
        raise SystemExit('Switch table reference changed')

# --- No base relocations can alter the gated bytes.
if pe.directory(5)[0] != 0:
    raise SystemExit('Executable carries base relocations; body hash would be load dependent')

# --- Direct callers (player update function), recorded as evidence only.
callers = []
for m in re.finditer(rb'\xe8', code_section):
    i = m.start()
    if i + 5 > len(code_section):
        break
    if 0x1000 + i + 5 + struct.unpack_from('<i', code_section, i + 1)[0] == begin:
        callers.append(0x1000 + i)
if callers != [0x9C2A25, 0x9C2D37]:
    raise SystemExit('Direct caller set changed')
caller_range = a.ranges[bisect_right(a.starts, 0x9C2A25) - 1]
if caller_range[:2] != (0x9C1F00, 0x9C2E8B):
    raise SystemExit('Caller function range changed')

# --- FOB flag producer for guard 1 (allocation).
fob = {i.address - IB: (i.mnemonic, i.op_str) for i in a.instructions(0x9BEB31, 0x23)}
if fob.get(0x9BEB31) != ('call', '0x140a1f970') or fob.get(0x9BEB36) != ('test', 'al, 1') or \
   fob.get(0x9BEB3A) != ('or', 'dword ptr [r12 + 0x204], 0x40200') or \
   fob.get(0x9BEB48) != ('and', 'dword ptr [r12 + 0x204], 0xfffbfdff'):
    raise SystemExit('FOB flag producer changed')
getter = a.instructions(0xA1F970, 7)
setter = {i.address - IB: (i.mnemonic, i.op_str) for i in a.instructions(0xA200F0, 0x20)}
if (getter[0].mnemonic, getter[0].op_str) != ('mov', 'eax, dword ptr [rip + 0x22003ca]') or \
   setter.get(0xA20103) != ('mov', 'dword ptr [rip + 0x21ffc37], eax'):
    raise SystemExit('Mission flag global accessors changed')
mission_flags_global = 0xA1F976 + 0x22003CA
if mission_flags_global != 0xA20109 + 0x21FFC37:
    raise SystemExit('Mission flag accessors disagree')

# --- Lua enum registration (Player library) for the values used above.
def decode_enums(lo, hi):
    regs, enum, table = {}, None, {}
    def put(r, v):
        regs[r] = v
        if r in ('r8d', 'r9d'):
            regs[r[:-1]] = v
    for x in a.instructions(lo, hi - lo):
        op = x.op_str
        m = re.match(r'(\w+), \[rip ([+-]) (0x[0-9a-f]+)\]$', op)
        if x.mnemonic == 'lea' and m:
            t = x.address - IB + x.size + int(m.group(3), 16) * (1 if m.group(2) == '+' else -1)
            try:
                put(m.group(1), pe.cstring(t))
            except Exception:
                put(m.group(1), None)
            continue
        m = re.match(r'(\w+), \[(\w+) \+ (0x[0-9a-f]+|\d+)\]$', op)
        if x.mnemonic == 'lea' and m and isinstance(regs.get(m.group(2)), int):
            put(m.group(1), regs[m.group(2)] + int(m.group(3), 0))
            continue
        m = re.match(r'(\w+), (0x[0-9a-f]+|\d+)$', op)
        if x.mnemonic == 'mov' and m:
            put(m.group(1), int(m.group(2), 0))
            continue
        m = re.match(r'(\w+), (\w+)$', op)
        if x.mnemonic == 'mov' and m and m.group(2) in regs:
            put(m.group(1), regs[m.group(2)])
            continue
        if x.mnemonic == 'xor' and len(set(op.split(', '))) == 1:
            put(op.split(', ')[0], 0)
            continue
        if x.mnemonic == 'call':
            if op == '0x141a158e0':
                enum = regs.get('rdx')
                table.setdefault(enum, {})
            elif op == '0x141a156e0' and enum:
                table[enum][regs.get('rdx')] = regs.get('r8d')
            regs = {}
    return table

enums = decode_enums(0x9D3630, 0x9D5CCA)
expected_enums = {
    'PlayerType': {'SNAKE': 0, 'DD_MALE': 1, 'DD_FEMALE': 2, 'AVATAR': 3, 'LIQUID': 4, 'OCELOT': 5, 'QUIET': 6},
    'PlayerPartsType': {'NORMAL': 0, 'NORMAL_SCARF': 1, 'OCELOT': 26, 'QUIET': 27},
    'PlayerCamoType': {'OLIVEDRAB': 0, 'OCELOT': 115, 'QUIET': 116},
    'PlayerHandType': {'NONE': 0, 'NORMAL': 1},
}
for name, values in expected_enums.items():
    got = enums.get(name, {})
    if any(got.get(k) != v for k, v in values.items()):
        raise SystemExit(f'{name} registration changed')
if len(enums['PlayerType']) != 7:
    raise SystemExit('PlayerType gained or lost entries')

# --- Player.GetPlayerType readback (0..4, OCELOT/QUIET and out-of-range read as 0).
if a.ranges[bisect_right(a.starts, 0x9D30B0) - 1][:2] != (0x9D30B0, 0x9D318B):
    raise SystemExit('GetPlayerType range changed')
readback = {i.address - IB: (i.mnemonic, i.op_str) for i in a.instructions(0x9D30B0, 0x9D318B - 0x9D30B0)}
if readback.get(0x9D3119) != ('cmp', 'esi, dword ptr [r14 + 0x208]') or \
   readback.get(0x9D312D) != ('mov', 'rax, qword ptr [rax + 0x40]') or \
   readback.get(0x9D3149) != ('lea', 'edi, [rdx + 4]') or \
   pe.cstring(0x9D30E3 + 0x18C4F15) != 'instanceIndex':
    raise SystemExit('GetPlayerType readback changed')

# --- Lab readback guard (native PLAYERS receipt). GetPlayerType loads
# P = *(root + 8), then [P+0x208], Q = [P+0x60] and [Q+0x40] with no null
# check, so a call while P is null (before Mission.Start, at OnInitialize entry,
# archive/research-2026-10-23:docs/host-readiness-timing.md) faults. The guard samples the same chain
# through ReadProcessMemory first and gates on these exact bytes.
getter_rva, getter_bytes = 0xAEC8D0, pe.rva_read(0xAEC8D0, 8)
decoded_getter = [(x.mnemonic, x.op_str) for x in a.disassembler.disasm(getter_bytes, IB + getter_rva)]
if decoded_getter != [('lea', 'rax, [rip + 0x1f96351]'), ('ret', '')]:
    raise SystemExit('Player root getter changed')
root_rva = getter_rva + 7 + 0x1F96351
player_slot_rva = root_rva + 8
if root_rva != 0x2A82C28 or player_slot_rva != 0x2A82C30:
    raise SystemExit('Player root moved')
slot_section = next(s for s in pe.sections if 0 <= player_slot_rva - s['rva'] < s['virtual_size'])
if not slot_section['characteristics'] & 0x80000000:
    raise SystemExit('Player slot is not in a writable data section')
for r, v in {0x9D30CF: ('call', '0x140aec8d0'),
             0x9D30D4: ('mov', 'r14, qword ptr [rax + 8]'),
             0x9D3119: ('cmp', 'esi, dword ptr [r14 + 0x208]'),
             0x9D3120: ('jb', '0x1409d3127'),
             0x9D3127: ('mov', 'rax, qword ptr [r14 + 0x60]'),
             0x9D312B: ('mov', 'ecx, esi'),
             0x9D312D: ('mov', 'rax, qword ptr [rax + 0x40]'),
             0x9D3131: ('movzx', 'edx, byte ptr [rax + rcx]')}.items():
    if readback.get(r) != v:
        raise SystemExit('GetPlayerType pointer chain changed')
if any(m in ('test', 'je', 'jne') and r < 0x9D3127 and 'r14' in o for r, (m, o) in readback.items()):
    raise SystemExit('GetPlayerType gained a null check; re-evaluate the guard')
get_player_type_bytes = pe.rva_read(0x9D30B0, 0x9D318B - 0x9D30B0)
# Player object type: the vtable host_session_trial.cpp PlayerReadiness and
# archive/research-2026-10-23:tools/inspect_player_system.py (archived) require live (slots 2 and 11 observed live).
player_table_rva = 0x2295490
table_section = next(s for s in pe.sections if 0 <= player_table_rva - s['rva'] < s['virtual_size'])
if table_section['characteristics'] & 0x80000000 or \
   struct.unpack('<Q', pe.rva_read(player_table_rva + 16, 8))[0] != IB + 0x9C39F0 or \
   struct.unpack('<Q', pe.rva_read(player_table_rva + 88, 8))[0] != IB + 0x9C3BB0:
    raise SystemExit('Player vtable changed')

digest = hashlib.sha256(pe.rva_read(begin, end - begin)).digest()
header = [
    '// Generated by tools/generate_player_type_contract.py from the fingerprinted',
    '// executable (SHA-256 085c2f82...bb45). Do not edit by hand.',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    'namespace mgscoop::player_type_contract {',
    '// tpp::gm::player::impl::Player2GameObjectImpl::UpdatePartsStatus (Dynamite naming),',
    '// complete declared extent [0x9cd030, 0x9ce924): code to 0x9ce8e4, then switch tables.',
    f'inline constexpr std::uintptr_t body_rva=0x{begin:x};',
    f'inline constexpr std::size_t body_size={end - begin};',
    f'inline constexpr std::size_t site_offset=0x{site - begin:x}; // 0x{site:x}',
    'inline constexpr std::uintptr_t keep_path_rva=0x9cdb12;',
    'inline constexpr unsigned char original_bytes[6]={' + ','.join(f'0x{b:02x}' for b in original) + '}; // jne keep_path',
    'inline constexpr unsigned char replacement_bytes[6]={' + ','.join(f'0x{b:02x}' for b in replacement) + '}; // jmp keep_path (REX.W ignored)',
    '// SHA-256 of the complete extent with the original branch bytes in place.',
    'inline constexpr unsigned char body_sha256[32]={' + ','.join(f'0x{b:02x}' for b in digest) + '};',
    '// Lab readback guard: Player.GetPlayerType reads P=*(player_slot), [P+limit_offset],',
    '// Q=[P+parts_info_offset], [Q+type_array_offset][idx] without null checks.',
    f'inline constexpr std::uintptr_t player_slot_rva=0x{player_slot_rva:x}; // *(getter 0x{getter_rva:x} result + 8)',
    f'inline constexpr std::uintptr_t player_table_rva=0x{player_table_rva:x};',
    'inline constexpr std::size_t limit_offset=0x208;',
    'inline constexpr std::size_t parts_info_offset=0x60;',
    'inline constexpr std::size_t type_array_offset=0x40;',
    f'inline constexpr std::uintptr_t root_getter_rva=0x{getter_rva:x};',
    'inline constexpr unsigned char root_getter_bytes[8]={' + ','.join(f'0x{b:02x}' for b in getter_bytes) + '}; // lea rax,[root]; ret',
    'inline constexpr std::uintptr_t get_player_type_rva=0x9d30b0;',
    f'inline constexpr unsigned char get_player_type_bytes[{len(get_player_type_bytes)}]={{',
    *['    ' + ','.join(f'0x{b:02x}' for b in get_player_type_bytes[i:i + 24]) + ','
      for i in range(0, len(get_player_type_bytes), 24)],
    '};',
    '}',
    '',
]
(ROOT / 'native/player_type_contract.hpp').write_text('\n'.join(header))

report = dict(
    executable=str(EXE), executable_sha256=GAME_SHA256,
    function=dict(entry_rva=hex(begin), code_end=hex(code_end), range_end=hex(end), body_bytes=end - begin,
                  body_sha256=digest.hex(), unwind_chain=chain, switches=switches,
                  switch_index_table_hex=index_table.hex(),
                  instructions_total=len(instructions),
                  required_instructions={hex(r): ' '.join(v).strip() for r, v in sorted(required.items())},
                  direct_callers=[hex(c) for c in callers], caller_function=[hex(caller_range[0]), hex(caller_range[1])]),
    patch=dict(site=hex(site), original_hex=original.hex(' '), replacement_hex=replacement.hex(' '),
               replacement_decoded=f'{decoded[0].mnemonic} {decoded[0].op_str}', changed_bytes=2,
               unique_in_code_section=True, word_aligned=True, single_cache_line=True),
    fob_guard=dict(producer='0x9beb31 call 0xa1f970; test al,1; or [r12+0x204],0x40200 else and ~0x40200',
                   mission_flags_global=hex(mission_flags_global), setter='0xa200f0 (Mission.SetMissionFlags per reader-characters)'),
    lua_enums={k: {n: enums[k][n] for n in v} for k, v in expected_enums.items()},
    get_player_type=dict(rva='0x9d30b0', argument='instanceIndex', mapping='Q+0x40[idx] 0..4 returned as-is; 5 (OCELOT), 6 (QUIET) and idx >= P+0x208 return 0',
                         body_bytes=len(get_player_type_bytes),
                         body_sha256=hashlib.sha256(get_player_type_bytes).hexdigest(),
                         null_checks='none: P=[root+8], [P+0x208], [P+0x60], [Q+0x40] are dereferenced unconditionally',
                         readback_guard=dict(root_getter=hex(getter_rva), root=hex(root_rva), player_slot=hex(player_slot_rva),
                                             player_table=hex(player_table_rva), limit_offset='0x208',
                                             parts_info_offset='0x60', type_array_offset='0x40')),
    provenance='Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e dynamitePatches.cpp: 0x1409cc6c0 0f 85 9c 07 00 00 -> 48 e9 9c 07 00 00 (old exe 1.0.15.3); GPLv3 + Commons Clause 1.0',
    mapping=('For the local instance outside parts-loading states 1/2, when vars.playerType is OCELOT (5) or QUIET (6) and '
             'neither enclosing+0x224 bit 18 (FOB missions) nor enclosing+0x284 bit 2 is set, the function writes '
             'vars.playerType = SNAKE and calls player vtable +0x2a8. The keep path forces matching parts/camo.'),
    adaptation=('Scoped, reversible two-byte interlocked exchange of jne -> jmp at 0x9cd370, gated on the complete-extent '
                'SHA-256 with the branch normalized to the original; refuses unowned or changed bytes.'),
    limitations=['Static disassembly only; no executable calls, Lua execution or live verification.',
                 'Semantic names are mappings (Dynamite symbol names), not recovered symbols.',
                 'Does not port Dynamite 0x149cfba54 (Ocelot dual revolvers) or the SetupStaffList sortie UI patch.',
                 'Remote appearance replication (synchronizer 0x12a5810 per reader-characters) is not covered or verified.',
                 'enclosing+0x284 bit 2 producer is not mapped (possibly sortie preparation; unverified).'])
(ROOT / 'docs/evidence/player-type-contract-2026-10-03.json').write_text(json.dumps(report, indent=2) + '\n')
print('Recorded UpdatePartsStatus extent, reset/keep dataflow, branch encoding, enums and readback; no game execution.')
