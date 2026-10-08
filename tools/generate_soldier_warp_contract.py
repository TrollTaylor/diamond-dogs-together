"""Record the soldier "set transform" message contract (debug "Spawn enemy here").

Static, read-only analysis of the fingerprinted installed executable; nothing
is executed or written outside this repository. Writes
native/soldier_warp_contract.hpp and docs/evidence/soldier-warp-contract-2026-10-08.json.

The native module (native/soldier_warp.cpp) sends the engine's own message
0x287158b07327 to one soldier GameObjectId through the GameObject manager's
SendCommand (manager vtable +0x38 = 0xbf3a80), exactly as the player carry code
does at 0x1055b66. Every byte sequence below is re-checked here instruction by
instruction and again in the running image (soldier_warp::Verify) before the
first call; a single difference refuses the module.

Usage: python tools/generate_soldier_warp_contract.py [--check]
  --check  verify only; exit 1 on any mismatch, do not write anything.
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs

ROOT = Path(__file__).resolve().parent.parent
EXE = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
GAME_SHA256 = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
OUT = ROOT / 'native' / 'soldier_warp_contract.hpp'
EVIDENCE = ROOT / 'docs' / 'evidence' / 'soldier-warp-contract-2026-10-08.json'

raw = EXE.read_bytes()
if hashlib.sha256(raw).hexdigest() != GAME_SHA256:
    raise SystemExit('Executable fingerprint mismatch')
pe = pefile.PE(data=raw, fast_load=True)
IB = pe.OPTIONAL_HEADER.ImageBase
if IB != 0x140000000:
    raise SystemExit('Unexpected image base')
if pe.OPTIONAL_HEADER.DATA_DIRECTORY[5].Size:
    raise SystemExit('Unexpected relocation directory: RVAs must be stable')
md = Cs(CS_ARCH_X86, CS_MODE_64)
failures = []


def rd(rva, n):
    data = pe.get_data(rva, n)
    if len(data) != n:
        raise SystemExit(f'Short read at {rva:#x}')
    return data


def q(rva):
    return struct.unpack('<Q', rd(rva, 8))[0]


def section_of(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.rstrip(b'\0').decode()
    return None


def disasm(rva, size):
    out = []
    for i in md.disasm(rd(rva, size), rva):
        out.append((i.address, i.size, i.mnemonic, i.op_str))
    if sum(x[1] for x in out) != size:
        failures.append(f'{rva:#x}: {size} bytes do not decode as whole instructions')
    return out


def expect(rva, size, rows, description):
    """rows: list of (address, mnemonic, operands) that must appear exactly."""
    ins = {a: (m, o) for a, _, m, o in disasm(rva, size)}
    for address, mnemonic, operands in rows:
        if ins.get(address) != (mnemonic, operands):
            failures.append(f'{description}: {address:#x} is {ins.get(address)}, expected {(mnemonic, operands)}')
    if section_of(rva) != '.xtext':
        failures.append(f'{description}: {rva:#x} is not in .xtext')
    return dict(rva=rva, size=size, description=description)


# (rva, size, instruction rows, description). Sizes end on instruction boundaries.
SITES = [
    expect(0xBF3920, 0x55, [
        (0xBF392D, 'mov', 'eax, dword ptr [rip + 0x20446dd]'),          # guard 0x2c38010
        (0xBF3933, 'test', 'al, 1'),
        (0xBF3947, 'mov', 'qword ptr [rip + 0x20446ba], rax'),          # 0x2c38008 = base vtable
        (0xBF394E, 'lea', 'rax, [rip + 0x171251b]'),                     # 0x2305e70
        (0xBF3955, 'mov', 'qword ptr [rip + 0x20446ac], rax'),          # 0x2c38008 = 0x2305e70
        (0xBF3969, 'lea', 'rax, [rip + 0x2044698]'),                     # return 0x2c38008
        (0xBF3974, 'ret', ''),
    ], 'GameObject manager getter: static object 0x2c38008, guard 0x2c38010 bit 0, vtable 0x2305e70'),
    expect(0xBF3A80, 0x58, [
        (0xBF3A8A, 'mov', 'rbx, rdx'),
        (0xBF3A92, 'movzx', 'edx, r8w'),
        (0xBF3A96, 'mov', 'rdi, r9'),
        (0xBF3A99, 'call', '0xbf4200'),
        (0xBF3A9E, 'cmp', 'qword ptr [rsp + 0x20], 0'),
        (0xBF3AA6, 'mov', 'dword ptr [rbx], 0xffffffff'),
        (0xBF3ABF, 'mov', 'r8, rdi'),
        (0xBF3AC2, 'mov', 'rdx, rbx'),
        (0xBF3AC5, 'call', '0xbf39d0'),
        (0xBF3ACA, 'mov', 'rax, rbx'),
        (0xBF3AD7, 'ret', ''),
    ], 'manager vtable +0x38 SendCommand(this, int* out, u16 id, msg*): missing handle -> *out = -1'),
    expect(0xBF4200, 0x6A, [
        (0xBF420F, 'movzx', 'ebx, dx'),
        (0xBF4215, 'call', '0xbf9a10'),
        (0xBF4223, 'call', 'qword ptr [r8 + 0x20]'),
        (0xBF422C, 'mov', 'qword ptr [rdi], rax'),
        (0xBF422F, 'mov', 'dword ptr [rdi + 8], 0x1ff'),
        (0xBF4250, 'and', 'r8d, 0x1ff'),
        (0xBF4257, 'call', '0xbf4270'),
    ], 'GetHandle(out, id): registry vt+0x20 type object, then idx = id & 0x1ff'),
    expect(0xBF4270, 0x6D, [
        (0xBF427F, 'mov', 'rax, qword ptr [rdx]'),
        (0xBF4293, 'call', 'qword ptr [rax + 0x40]'),
        (0xBF4296, 'cmp', 'dword ptr [rax + 4], esi'),
        (0xBF429B, 'mov', 'qword ptr [rbx], 0'),
        (0xBF42BC, 'mov', 'rax, qword ptr [rdi + 8]'),
        (0xBF42C0, 'mov', 'dword ptr [rbx + 8], esi'),
        (0xBF42C8, 'mov', 'rcx, qword ptr [rax + 0x28]'),
        (0xBF42CF, 'mov', 'qword ptr [rbx], rcx'),
        (0xBF42DC, 'ret', ''),
    ], 'handle = {*(*(type+8)+0x28), idx} when idx < type->vt[0x40]()->count'),
    expect(0xBF39D0, 0x2D, [
        (0xBF39DC, 'mov', 'rcx, qword ptr [rcx]'),
        (0xBF39DF, 'test', 'rcx, rcx'),
        (0xBF39E7, 'mov', 'r9, r8'),
        (0xBF39EA, 'mov', 'r8d, dword ptr [rdx + 8]'),
        (0xBF39EE, 'mov', 'rdx, rbx'),
        (0xBF39F1, 'call', 'qword ptr [rax + 0x10]'),
        (0xBF39FC, 'ret', ''),
    ], 'handle dispatch: handle.obj->vt[0x10](obj, out, idx, msg)'),
    expect(0xBF9A10, 0x8, [
        (0xBF9A10, 'mov', 'rax, qword ptr [rip + 0x203ef89]'),           # *(0x2c389a0)
        (0xBF9A17, 'ret', ''),
    ], 'registry getter: *(0x2c389a0)'),
    expect(0xBF99E0, 0x22, [
        (0xBF99E3, 'shr', 'rax, 9'),
        (0xBF99E7, 'mov', 'rax, qword ptr [rcx + rax*8 + 0x30]'),
        (0xBF99F1, 'mov', 'rax, qword ptr [rax + 8]'),
        (0xBF99FA, 'mov', 'rax, qword ptr [rax + 0x20]'),
        (0xBF99FE, 'ret', ''),
        (0xBF99FF, 'xor', 'eax, eax'),
    ], 'registry vt+0x20: type object = *(*(reg[0x30 + 8*(id>>9)]+8)+0x20)'),
    expect(0xBF8A69, 0xA, [
        (0xBF8A69, 'lea', 'rax, [rip + 0x170e950]'),                     # 0x23073c0
        (0xBF8A70, 'mov', 'qword ptr [rcx], rax'),
    ], 'registry constructor stores its final vtable 0x23073c0'),
    expect(0xBF9095, 0x7, [
        (0xBF9095, 'mov', 'qword ptr [rip + 0x203f904], rax'),           # 0x2c389a0
    ], 'registry creation publishes *(0x2c389a0)'),
    expect(0xC15500, 0xE, [
        (0xC15500, 'mov', 'rcx, qword ptr [rcx + 0x48]'),
        (0xC15504, 'mov', 'rax, qword ptr [rcx]'),
        (0xC15507, 'jmp', 'qword ptr [rax + 0xa8]'),
    ], 'argument-preserving forwarder to inner->vt[0xa8] (the shape soldier_warp accepts for a pinned hop)'),
    expect(0x1360F90, 0xFB, [
        (0x1360F90, 'mov', 'rax, rsp'),
        (0x136100B, 'mov', 'r12, r9'),
        (0x1361015, 'mov', 'r14d, r8d'),
        (0x136101D, 'mov', 'r15, rdx'),
        (0x1361027, 'mov', 'rsi, rcx'),
        (0x136102A, 'mov', 'ecx, dword ptr [rcx + 8]'),
        (0x136102D, 'cmp', 'r8d, ecx'),
        (0x1361032, 'mov', 'dword ptr [rdx], 0xffffffff'),
        (0x136103D, 'mov', 'rax, qword ptr [r9]'),
        (0x1361072, 'movabs', 'rdx, 0x287158b07327'),
        (0x1361085, 'je', '0x13615f5'),
    ], 'soldier native message handler (this, int* out, u32 idx, msg*): idx < this+8, hash 0x287158b07327 -> 0x13615f5'),
    expect(0x13615F5, 0x105, [
        (0x13615F5, 'mov', 'rax, qword ptr [rsi + 0x18]'),
        (0x136160A, 'mov', 'rdi, qword ptr [rax + 8]'),
        (0x136161F, 'test', 'byte ptr [r8 + rdi], dl'),
        (0x1361623, 'je', '0x1364e5b'),
        (0x1361632, 'call', 'qword ptr [rax + 0xf0]'),
        (0x1361645, 'je', '0x1364e5b'),
        (0x1361655, 'call', '0x9e5dd0'),
        (0x1361684, 'call', 'qword ptr [rax + 0x388]'),
        (0x1361690, 'movss', 'xmm1, dword ptr [rax + 0x160]'),
        (0x13616A3, 'addps', 'xmm10, xmmword ptr [r12 + 0x10]'),
        (0x13616B1, 'movaps', 'xmm11, xmmword ptr [r12 + 0x20]'),
        (0x13616BF, 'mov', 'rax, qword ptr [rsi + 0x98]'),
        (0x13616C6, 'cmp', 'r14d, dword ptr [rax + 0x10]'),
        (0x13616CC, 'imul', 'rbx, rbx, 0x70'),
        (0x13616DA, 'mov', 'ecx, dword ptr [rbx + 0x34]'),
        (0x13616DD, 'and', 'ecx, 0xf'),
        (0x13616E8, 'jbe', '0x1361976'),
        (0x13616F4, 'ja', '0x13619be'),
    ], 'set-transform branch: realized bit idx of *(*(this+0x18)+8), T = CS->vt[0x388], pos.y += T[+0x160], stance nibble rec[+0x34]&0xf (1-2 extra 0xa6 message, 3-4 rotated pose)'),
    expect(0x13619BE, 0x34, [
        (0x13619C1, 'lea', 'r8, [rbp + 0x110]'),
        (0x13619C8, 'lea', 'rdx, [rbp + 0x1a0]'),
        (0x13619D2, 'call', 'qword ptr [rax + 0x68]'),
        (0x13619E2, 'call', 'qword ptr [rax + 0x70]'),
        (0x13619E5, 'mov', 'byte ptr [r12 + 0x33], r13b'),
        (0x13619EA, 'mov', 'dword ptr [r15], ebx'),
        (0x13619ED, 'jmp', '0x1364e62'),
    ], 'set-transform tail: T->vt[0x68](pos, quat), T->vt[0x70](quat), msg+0x33 = 1, *out = 0'),
    expect(0x1364E5B, 0x7, [
        (0x1364E5B, 'mov', 'dword ptr [r15], 0xffffffff'),
    ], 'handler refusal: *out = -1 (unrealized or no character)'),
    expect(0x105577D, 0x17, [
        (0x105577D, 'call', '0xbff050'),
        (0x1055790, 'mov', 'r12, qword ptr [rax + 0x60]'),
    ], 'precedent: player carry code takes the manager from context+0x60'),
    expect(0xBF37DA, 0x9, [
        (0xBF37DA, 'call', '0xbf3920'),
        (0xBF37DF, 'mov', 'qword ptr [rbx + 0x60], rax'),
    ], 'precedent: context+0x60 is the static manager returned by 0xbf3920'),
    expect(0x10557A8, 0xA, [
        (0x10557A8, 'movabs', 'rax, 0x287158b07327'),
    ], 'precedent: player carry code builds message 0x287158b07327'),
    expect(0x1055B66, 0x17, [
        (0x1055B66, 'mov', 'rax, qword ptr [r12]'),
        (0x1055B6A, 'lea', 'r9, [rbp - 0x79]'),
        (0x1055B6E, 'lea', 'rdx, [rsp + 0x20]'),
        (0x1055B73, 'movzx', 'r8d, bx'),
        (0x1055B77, 'mov', 'rcx, r12'),
        (0x1055B7A, 'call', 'qword ptr [rax + 0x38]'),
    ], 'precedent: the carry code sends it through manager vtable +0x38'),
    expect(0x1055B84, 0x4, [
        (0x1055B84, 'cmp', 'byte ptr [rbp - 0x46], sil'),
    ], 'precedent: the carry code reads the handled flag msg+0x33 back'),
]

# (vtable rva, slot offset, expected target rva, description). Data qwords are absolute (no relocations).
SLOTS = [
    (0x2305E70, 0x00, 0xBF3800, 'manager vtable +0x00'),
    (0x2305E70, 0x08, 0xBF38C0, 'manager vtable +0x08 = GetHandle wrapper (0xbf4200)'),
    (0x2305E70, 0x38, 0xBF3A80, 'manager vtable +0x38 = SendCommand'),
    (0x23073C0, 0x20, 0xBF99E0, 'registry vtable +0x20 = type-object lookup'),
    (0x23E78F0, 0xA8, 0x1360F90, 'soldier subobject vtable A +0xa8 = native message handler'),
    (0x23E8060, 0xA8, 0x1360F90, 'soldier subobject vtable B +0xa8 = native message handler'),
    (0x23E78F0, 0xD0, 0x1356170, 'soldier subobject vtable A +0xd0 = Lua command dispatcher'),
    (0x23E8060, 0xD0, 0x1356170, 'soldier subobject vtable B +0xd0 = Lua command dispatcher'),
    (0x230C040, 0xA0, 0xC15500, 'wrapper interface vtable +0xa0 = 0xc15500 (static shape reference only)'),
    (0x230C140, 0xA0, 0xC15500, 'wrapper interface vtable +0xa0 = 0xc15500 (static shape reference only)'),
]
for table, offset, target, description in SLOTS:
    value = q(table + offset)
    if value != IB + target:
        failures.append(f'{description}: [{table + offset:#x}] = {value:#x}, expected {IB + target:#x}')
    if section_of(table) != '.edata':
        failures.append(f'{description}: {table:#x} is not in .edata')
# Only these two vtables hold the handler; only these two hold the forwarder.
for target, tables in ((0x1360F90, {0x23E7998, 0x23E8108}), (0xC15500, {0x230C0E0, 0x230C1E0})):
    found = set()
    sec = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.edata'][0]
    data = sec.get_data()
    needle = struct.pack('<Q', IB + target)
    start = 0
    while True:
        k = data.find(needle, start)
        if k < 0:
            break
        found.add(sec.VirtualAddress + k)
        start = k + 1
    if found != tables:
        failures.append(f'{target:#x} vtable references {sorted(hex(x) for x in found)} != {sorted(hex(x) for x in tables)}')
for rva, sec in ((0x2C38008, '.data2'), (0x2C38010, '.data2'), (0x2C389A0, '.data2')):
    if section_of(rva) != sec:
        failures.append(f'{rva:#x} is not in {sec}')
# A fault inside the call is caught by soldier_warp's __except: every framed
# function on the path needs its own unwind entry (the leaf stubs 0xbf9a10,
# 0xbf99e0 and the 0xc15500 tail jump allocate no frame).
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
UNWIND = {e.struct.BeginAddress: e.struct.EndAddress for e in getattr(pe, 'DIRECTORY_ENTRY_EXCEPTION', [])}
for begin, end in ((0xBF3A80, 0xBF3AD8), (0xBF4200, 0xBF426A), (0xBF4270, 0xBF42DD), (0xBF39D0, 0xBF3A0C), (0x1360F90, 0x1364EFC)):
    if UNWIND.get(begin) != end:
        failures.append(f'{begin:#x}: unwind entry {UNWIND.get(begin)} != {end:#x}')

# Source contract: soldier_warp::Warp has exactly two callers, each on the Lua
# owner thread behind the lab key, the startup Verify() result and the
# per-command gate (docs/debug-menu.md): the host menu receipt (DEBUG_WARP_V1)
# and the guest spawn-sync grant receipt (WORLD_WARP_V1, target from the
# world::Core grant, never from Lua).
EXPECTED_CALLERS = {('game_script_observer.cpp', 'SoldierWarpReceipt'), ('game_script_observer.cpp', 'GuestWarpReceipt')}
CALLER_GATES = ('debug_soldier_warp', 'soldier_warp_ready', 'LabMode::lab', 'soldier_warp_gate.Admit(', 'mission_terminating')
callers = []
for path in sorted((ROOT / 'native').glob('*.cpp')):
    if path.name == 'soldier_warp.cpp':
        continue
    text = path.read_text(encoding='utf-8', errors='replace')
    for match in re.finditer(r'\b(?:sw|soldier_warp)::Warp\s*\(', text):
        heads = list(re.finditer(r'^[A-Za-z_][\w:*&<> ]*?\b(\w+)\([^;{}]*\)\s*noexcept\s*\{', text[:match.start()], re.M))
        name = heads[-1].group(1) if heads else '?'
        body = text[heads[-1].start():match.start()] if heads else ''
        callers.append((path.name, name))
        for gate in CALLER_GATES:
            if gate not in body:
                failures.append(f'{path.name} {name}: calls soldier_warp::Warp without {gate!r} before the call')
if len(callers) != len(EXPECTED_CALLERS) or set(callers) != EXPECTED_CALLERS:
    failures.append(f'soldier_warp::Warp callers {sorted(callers)} != {sorted(EXPECTED_CALLERS)}')
if 'GuestWarpReceipt' in {name for _, name in callers}:
    guest = (ROOT / 'native' / 'game_script_observer.cpp').read_text(encoding='utf-8', errors='replace')
    start = guest.find('const char* GuestWarpReceipt(')
    body = guest[start:guest.find('\n}\n', start)] if start >= 0 else ''
    for needle in ('world_link::FindGrant(seq)', 'request.pos[i] = grant.pos[i]', 'request.player[i] = snapshot.remote_pos[i]',
                   'snapshot.remote_age_ms > 1000', 'LabRole::guest', '0x80000000u | grant.serial'):
        if needle not in body.replace('\r\n', '\n'):
            failures.append(f'GuestWarpReceipt: missing {needle!r} (target from the grant, range from the remote pose, gate id from its serial)')

if failures:
    print('\n'.join(failures))
    raise SystemExit(1)
if '--check' in sys.argv:
    print(f'soldier warp contract: {len(SITES)} sites and {len(SLOTS)} slots match {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}; '
          f'Warp callers: {", ".join(name for _, name in sorted(callers))}')
    raise SystemExit(0)

lines = [
    '// Generated by tools/generate_soldier_warp_contract.py from the fingerprinted',
    f'// executable (SHA-256 {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}). Do not edit by hand.',
    '// Debug "Spawn enemy here": the engine set-transform message to one soldier.',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    'namespace mgscoop::soldier_warp_contract {',
    '// --- Manager and SendCommand (RVAs; the image has no relocations) --------',
    'inline constexpr std::uintptr_t manager_object_rva=0x2c38008;  // static object returned by 0xbf3920',
    'inline constexpr std::uintptr_t manager_guard_rva=0x2c38010;   // dword, bit 0 = constructed',
    'inline constexpr std::uintptr_t manager_vtable_rva=0x2305e70;',
    'inline constexpr std::size_t manager_send_slot=0x38;',
    'inline constexpr std::uintptr_t send_rva=0xbf3a80;              // SendCommand(this, int* out, u16 id, msg*)',
    'inline constexpr std::uintptr_t registry_slot_rva=0x2c389a0;    // pointer to the type registry',
    'inline constexpr std::uintptr_t registry_vtable_rva=0x23073c0;',
    'inline constexpr std::size_t registry_lookup_slot=0x20;',
    'inline constexpr std::uintptr_t registry_lookup_rva=0xbf99e0;',
    '// type object = *(*(reg + 0x30 + 8*type) + 8) + 0x20 (load); handle object = *(*(type + 8) + 0x28).',
    'inline constexpr std::size_t registry_types=0x30, type_entry_inner=0x8, type_inner_object=0x20;',
    'inline constexpr std::size_t type_object_inner=0x8, inner_handle_object=0x28, handle_send_slot=0x10;',
    'inline constexpr std::uint32_t registry_type_count=0x80;',
    '// --- Soldier handler -------------------------------------------------------',
    'inline constexpr std::uint32_t soldier_type=2;                  // GameObjectId >> 9',
    'inline constexpr std::uintptr_t soldier_vtable_a_rva=0x23e78f0;',
    'inline constexpr std::uintptr_t soldier_vtable_b_rva=0x23e8060;',
    'inline constexpr std::size_t soldier_handler_slot=0xa8;',
    'inline constexpr std::uintptr_t handler_rva=0x1360f90;',
    'inline constexpr std::uintptr_t forwarder_rva=0xc15500;',
    '// Handler reads (this = soldier subobject): count u32 +8; realized bit idx of *(*(this+0x18)+8);',
    '// records *(this+0x98): data +8, count u32 +0x10, stride 0x70, stance nibble u32 +0x34 & 0xf.',
    'inline constexpr std::size_t leaf_count=0x8, leaf_realize=0x18, realize_bits=0x8;',
    'inline constexpr std::size_t leaf_records=0x98, records_data=0x8, records_count=0x10, record_stride=0x70, record_stance=0x34;',
    '// --- Message (16-byte aligned; movaps/addps on +0x10 and +0x20) ------------',
    'inline constexpr std::uint64_t message_hash=0x287158b07327ull;',
    'inline constexpr std::size_t message_size=0x40, message_pos=0x10, message_quat=0x20, message_player=0x30, message_handled=0x33;',
    '// --- Expected bytes ---------------------------------------------------------',
]
for n, site in enumerate(SITES):
    data = rd(site['rva'], site['size'])
    lines.append(f'// {site["description"]}')
    lines.append(f'inline constexpr unsigned char site{n}[]={{' + ','.join(f'0x{b:02x}' for b in data) + '};')
lines += ['struct Site {std::uintptr_t rva;std::size_t size;const unsigned char* bytes;};',
          'inline constexpr Site sites[]={']
for n, site in enumerate(SITES):
    lines.append(f'{{{site["rva"]:#x},sizeof(site{n}),site{n}}},')
lines += ['};',
          'struct Slot {std::uintptr_t rva;std::uintptr_t target;};  // qword at rva == base + target',
          'inline constexpr Slot slots[]={']
for table, offset, target, description in SLOTS:
    lines.append(f'{{{table + offset:#x},{target:#x}}},  // {description}')
lines += ['};', '}', '']
OUT.write_text('\n'.join(lines).replace('\n', '\r\n'), newline='')

report = dict(
    executable=str(EXE), executable_sha256=GAME_SHA256, generated_by='tools/generate_soldier_warp_contract.py',
    sites=[dict(rva=hex(s['rva']), size=s['size'], sha256=hashlib.sha256(rd(s['rva'], s['size'])).hexdigest(),
                description=s['description']) for s in SITES],
    slots=[dict(rva=hex(t + o), target=hex(v), description=d) for t, o, v, d in SLOTS],
    unwind_entries=[dict(begin=hex(b), end=hex(UNWIND[b])) for b in (0xBF3A80, 0xBF4200, 0xBF4270, 0xBF39D0, 0x1360F90)],
    call=dict(target='0xbf3a80 via [0x2305e70+0x38] on the static object 0x2c38008',
              arguments='rcx=manager, rdx=int out[4], r8=u16 GameObjectId, r9=0x40-byte 16-aligned message',
              precedent='0x1055730 (player carry) builds the same hash and calls [mgr]+0x38 at 0x1055b7a, then reads msg+0x33'),
    open_hop=('The per-type handle object *(*(type+8)+0x28) and its vtable +0x10 are created by protected code and '
              'cannot be pinned statically. soldier_warp resolves the chain read-only at runtime, logs a census line '
              'and reports leaf=pinned only when the hop is the handler itself or an argument-preserving forwarder to '
              'a soldier subobject whose vtable +0xa8 is 0x1360f90.'),
    limitations=['Static analysis only; no game code was executed.',
                 'The open hop above is proven only by a runtime census and handled=1.',
                 'Positions are host-local; guards may walk back to their route or be moved by navmesh correction.',
                 'Thread: the engine sends this message from player update; this mod sends it from the Lua owner thread '
                 '(the same thread that already moves the partner body through the same transform slots).'])
EVIDENCE.write_text(json.dumps(report, indent=2) + '\n')
print(f'Emitted {len(SITES)} sites and {len(SLOTS)} slots to {OUT.relative_to(ROOT)}')
