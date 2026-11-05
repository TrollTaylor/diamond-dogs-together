"""Record the soldier request manager sites the act census (and later the act mirror) reads, calls and hooks.

Static, read-only analysis of the fingerprinted installed executable; nothing is executed. Writes
native/soldier_act_contract.hpp: the byte windows and data qwords soldier_act::Install checks in the running image
before it hooks the manager update or calls any of the request functions (docs/world-sync-design.md 7.15, enemy sync
M7 FM1 "far mirror"; the plan's owner-brains act layer).

The engine's own FOB act sync (Ghidra decompile, mgsvtpp 1.0.15.4):
  - the request manager M (the +0x20 sub-object of P, vtable 0x23e8a48; P = M-0x20 has 0x23e8a18; M+0x50 = the soldier impl S, M+0x58 = the action codec, M+0x10 the
    0x1e0-byte request records R, M+8 the 0xd0-byte locomotion commands) runs 0x13735c0 every frame: offline
    0x1373c20 + 0x1373800 per soldier, online ([S+0x2a4] bit 2) 0x13738b0 + 0x1373800;
  - 0x13738b0: the non-owner (S+0x2a8 != owner byte *(*(S+0x18)+0x18)+idx) unpacks a 0x70 request with 0x136f810 and
    a 0x18 order with 0x13701f0 (then R+0x19e |= 0x80, R+0x1d2 |= 1); the owner packs them with 0x13704a0 / 0x1370900;
  - 0x136f810 takes the network position entry *(*(S+0xe0)+8)+idx*0x18 only when idx < *(S+0xe0)+0x10 (else NULL, and
    dest modes 0 (flag +0x14 bit 0 test) / 1 / 2 dereference it: U1), and warps the copy through 0x136ece0 beyond
    15 m (225.0 squared at 0x2329c00);
  - the codec (vtable 0x23e9638: vt[0] 0x1381630 / vt[1] 0x1381660 pack, vt[2] 0x13815b0 / vt[3] 0x13815e0 unpack)
    is one symmetric serializer 0x1386640 with a direction word (0 = R -> buffer, 1 = buffer -> R);
  - the request post 0x1368980(M, idx, req[0xc0]) copies req to R (R+0xc0 while R+0x19e & 0x100) and sets 0x80;
  - SetPuppet is bit 27 of dword +8 of *(*(S+0x110)+8)+idx*0x28; the brain tick gate 0x14dc8b0 skips a puppet;
  - the shoot builder 0x13a1820 reads the arm bit R'+0x11e (per-weapon record).

Usage: python tools/generate_soldier_act_contract.py [--check]
  --check  verify only; exit 1 on any mismatch or when the header differs, do not write anything.
"""
import hashlib
import struct
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs

ROOT = Path(__file__).resolve().parent.parent
EXE = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
GAME_SHA256 = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
OUT = ROOT / 'native' / 'soldier_act_contract.hpp'
CHECK = '--check' in sys.argv[1:]

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


def expect(rva, size, rows, description):
    """rows: (address, mnemonic, operands) that must decode exactly; the window ends on an instruction boundary."""
    decoded = [(i.address, i.size, i.mnemonic, i.op_str) for i in md.disasm(rd(rva, size), rva)]
    if sum(x[1] for x in decoded) != size:
        failures.append(f'{description}: {rva:#x}+{size:#x} does not decode as whole instructions')
    ins = {a: (m, o) for a, _, m, o in decoded}
    for address, mnemonic, operands in rows:
        if ins.get(address) != (mnemonic, operands):
            failures.append(f'{description}: {address:#x} is {ins.get(address)}, expected {(mnemonic, operands)}')
    if section_of(rva) != '.xtext':
        failures.append(f'{description}: {rva:#x} is not in .xtext')
    return (rva, size, description)


SITES = [
    expect(0x13735C0, 0x1B, [
        (0x13735C0, 'mov', 'qword ptr [rsp + 0x18], rbx'),
        (0x13735D0, 'sub', 'rsp, 0x30'),
        (0x13735D4, 'mov', 'r8, rcx'),
        (0x13735D7, 'mov', 'rdx, qword ptr [rcx + 0x10]'),
    ], 'manager update (this = M, the hook site): records M+0x10'),
    expect(0x1373625, 0x26, [
        (0x1373625, 'mov', 'rax, qword ptr [r8 + 0x50]'),
        (0x137362B, 'lea', 'rbx, [rdx + 0x1e0]'),
        (0x1373632, 'mov', 'r13, qword ptr [rax + 0x18]'),
        (0x1373636, 'mov', 'r14d, dword ptr [rax + 0x2a4]'),
        (0x1373644, 'mov', 'r15d, dword ptr [r13 + 0x40]'),
    ], 'manager update: S = M+0x50, record stride 0x1e0, soldier count *(S+0x18)+0x40, mode S+0x2a4'),
    expect(0x137394F, 0x2F, [
        (0x137394F, 'mov', 'rdx, qword ptr [r15 + 0x70]'),
        (0x1373953, 'mov', 'eax, dword ptr [rdx + 0x2a4]'),
        (0x1373959, 'shr', 'eax, 2'),
        (0x1373968, 'mov', 'rax, qword ptr [rdx + 0x18]'),
        (0x137396C, 'mov', 'rcx, qword ptr [rax + 0x18]'),
        (0x1373970, 'movzx', 'eax, byte ptr [rsi + rcx]'),
        (0x1373974, 'cmp', 'byte ptr [rdx + 0x2a8], al'),
    ], 'online step: S+0x2a4 bit 2 online, owner byte *(*(S+0x18)+0x18)+idx against S+0x2a8'),
    expect(0x13704A0, 0x2E, [
        (0x13704A0, 'push', 'rbp'),
        (0x13704BB, 'mov', 'eax, dword ptr [r8 + 0xb0]'),
        (0x13704C2, 'mov', 'r15, qword ptr [rcx + 0x78]'),
    ], 'request pack (M-0x20, out70, R, idx): reads R+0xb0, codec = M+0x58'),
    expect(0x13707E3, 0x2E, [
        (0x13707E3, 'mov', 'rax, qword ptr [r14 + 0x70]'),
        (0x13707EC, 'mov', 'r8, qword ptr [rax + 0xa0]'),
        (0x13707F3, 'mov', 'rax, qword ptr [r8 + 8]'),
        (0x13707F7, 'movzx', 'ecx, byte ptr [rax + rsi*8 + 6]'),
        (0x13707FC, 'mov', 'rax, qword ptr [r8 + 0x48]'),
        (0x1370803, 'test', 'byte ptr [rsi + rax], 1'),
        (0x137080D, 'mov', 'rax, qword ptr [r8 + 0x10]'),
    ], 'request pack: weapons W = S+0xa0 (slot byte *(W+8)+idx*8+6, shots-off W+0x48, magazine W+0x10)'),
    expect(0x1370900, 0x1B, [
        (0x1370900, 'mov', 'qword ptr [rsp + 8], rbx'),
        (0x137090A, 'movzx', 'ecx, byte ptr [r8 + 0xc]'),
        (0x1370918, 'mov', 'byte ptr [rdx + 0xe], cl'),
    ], 'order pack (M-0x20, out18, R+0x1b0): target mode R+0x1bc & 7'),
    expect(0x136F810, 0x79, [
        (0x136F810, 'mov', 'r11, rsp'),
        (0x136F844, 'mov', 'r15, qword ptr [rcx + 0x78]'),
        (0x136F868, 'mov', 'r9, qword ptr [r14 + 0x70]'),
        (0x136F86C, 'mov', 'rdx, qword ptr [r9 + 0xe0]'),
        (0x136F873, 'cmp', 'esi, dword ptr [rdx + 0x10]'),
        (0x136F876, 'jae', '0x136f886'),
        (0x136F878, 'mov', 'rax, qword ptr [rdx + 8]'),
        (0x136F87C, 'lea', 'rcx, [rsi + rsi*2]'),
        (0x136F880, 'lea', 'r8, [rax + rcx*8]'),
        (0x136F886, 'mov', 'r8, r12'),
    ], 'request unpack (M-0x20, R, in70, idx): network position entry *(*(S+0xe0)+8)+idx*0x18, NULL when idx >= +0x10'),
    expect(0x136FBED, 0x5, [
        (0x136FBED, 'test', 'byte ptr [r8 + 0x14], 1'),
    ], 'request unpack, dest mode 0: the entry flag +0x14 bit 0 (no null check)'),
    expect(0x13701F0, 0x1F, [
        (0x13701F0, 'push', 'rbp'),
        (0x1370207, 'movzx', 'ecx, byte ptr [r8 + 0xf]'),
        (0x137020C, 'mov', 'byte ptr [rdx + 0x18], cl'),
    ], 'order unpack (M-0x20, R+0x1b0, in18): kind -> R+0x1c8'),
    expect(0x136ECE0, 0x2C, [
        (0x136ECE0, 'push', 'rdi'),
        (0x136ECFE, 'mov', 'rax, qword ptr [rcx + 0x70]'),
        (0x136ED08, 'mov', 'r9, qword ptr [rax + 0x18]'),
    ], 'network-position warp (M-0x20, idx, netpos): the copy position *(*(S+0x18)+0x20)+idx*0x10'),
    expect(0x136ED75, 0x8, [
        (0x136ED75, 'cmpleps', 'xmm0, xmmword ptr [rip + 0xfbae83]'),
    ], 'network-position warp: beyond 225.0 (15 m squared) at 0x2329c00'),
    expect(0x1373C20, 0x1C, [
        (0x1373C20, 'mov', 'qword ptr [rsp + 8], rbx'),
        (0x1373C34, 'movzx', 'eax, word ptr [r8 + 0x19e]'),
    ], 'offline step (M-0x20, idx, R, S+0x18): R+0x19e bit 0x80 starts the request'),
    expect(0x1368980, 0x35, [
        (0x1368980, 'mov', 'qword ptr [rsp + 8], rbx'),
        (0x1368995, 'imul', 'r9, r9, 0x1e0'),
        (0x136899C, 'add', 'r9, qword ptr [rcx + 0x10]'),
        (0x13689A5, 'lea', 'rsi, [r9 + 0x19e]'),
        (0x13689AF, 'test', 'cx, di'),
    ], 'request post (M, idx, req 0xc0): copies to R (R+0xc0 while R+0x19e & 0x100), R+0x19e = &~0x10 | 0x80'),
    expect(0x13A1820, 0x2A, [
        (0x13A1820, 'mov', 'r11, rsp'),
        (0x13A1842, 'movzx', 'eax, byte ptr [r8 + 0x11e]'),
    ], 'shoot builder (weaponComp, idx, rec): the arm bit rec+0x11e'),
    expect(0x14DC8BA, 0x34, [
        (0x14DC8BA, 'mov', 'rax, qword ptr [rcx + 0x38]'),
        (0x14DC8C3, 'mov', 'r9, qword ptr [rax + 0x110]'),
        (0x14DC8CA, 'cmp', 'edx, dword ptr [r9 + 0x10]'),
        (0x14DC8DA, 'lea', 'rcx, [rbx + rbx*4]'),
        (0x14DC8DE, 'lea', 'r9, [rax + rcx*8]'),
        (0x14DC8E2, 'mov', 'eax, dword ptr [r9 + 8]'),
        (0x14DC8E6, 'shr', 'rax, 0x1b'),
        (0x14DC8EA, 'test', 'al, 1'),
    ], 'brain tick gate: SetPuppet bit 27 of dword +8 of *(*(S+0x110)+8)+idx*0x28 skips the brain update'),
    expect(0x1381630, 0x2C, [
        (0x1381643, 'add', 'rcx, -0x20'),
        (0x138164A, 'mov', 'dword ptr [rsp + 0x20], 0'),
        (0x1381652, 'call', '0x1386640'),
    ], 'codec vt[0] action A pack: the serializer 0x1386640 with direction 0 (R -> buffer)'),
    expect(0x1381660, 0x1D, [
        (0x1381664, 'add', 'rcx, -0x20'),
        (0x1381668, 'mov', 'dword ptr [rsp + 0x20], 0'),
    ], 'codec vt[1] action B pack: direction 0'),
    # Enemy round 8 track A (quiet_place, docs/world-sync-design.md 7.22): soldier_act::QuietOne calls the warp itself.
    expect(0x136ED8C, 0x50, [
        (0x136ED9E, 'mov', 'dword ptr [rdx + rcx*8], eax'),
        (0x136EDB6, 'mov', 'dword ptr [rdx + rcx*8 + 0xc], eax'),
        (0x136EDBA, 'mov', 'edx, dword ptr [r9 + 0x44]'),
        (0x136EDC6, 'mov', 'r9, qword ptr [r9 + 0x28]'),
        (0x136EDD2, 'movzx', 'ecx, word ptr [r9 + rax*2]'),
    ], 'network-position warp: writes the slot position and yaw, then finds idx in the realized list *(S+0x18)+0x28 (count +0x44)'),
    expect(0x136EDF5, 0x5C, [
        (0x136EDF5, 'mov', 'rcx, qword ptr [r10 + 0x70]'),
        (0x136EE21, 'call', 'qword ptr [rax + 0x100]'),
        (0x136EE4B, 'call', 'qword ptr [rax + 0x388]'),
    ], 'network-position warp: the controller (S vt[0x100](S, idx), then the component vt[0x388])'),
    expect(0x136F002, 0x36, [
        (0x136F005, 'call', 'qword ptr [rax + 0x70]'),
        (0x136F008, 'movss', 'xmm1, dword ptr [rbx + 0x160]'),
        (0x136F035, 'call', 'qword ptr [rax + 0x68]'),
    ], 'network-position warp: rotation vt+0x70, position vt+0x68 (y + controller +0x160); no message, no brain'),
]

# (vtable rva, slot offset, expected target rva, description). Data qwords are absolute (no relocations).
SLOTS = [
    (0x23E8A18, 0x40, 0x13735C0, 'request manager vtable +0x40 = the manager update 0x13735c0'),
    (0x23E8A48, 0x10, 0x13735C0, 'its +0x20 sub-object M (vtable 0x23e8a48) +0x10 = the same update, this = M'),
    (0x23E9638, 0x00, 0x1381630, 'action codec vtable +0x00 = pack A'),
    (0x23E9638, 0x08, 0x1381660, 'action codec vtable +0x08 = pack B'),
    (0x23E9638, 0x10, 0x13815B0, 'action codec vtable +0x10 = unpack A'),
    (0x23E9638, 0x18, 0x13815E0, 'action codec vtable +0x18 = unpack B'),
]
for table, offset, target, description in SLOTS:
    value = q(table + offset)
    if value != IB + target:
        failures.append(f'{description}: [{table + offset:#x}] = {value:#x}, expected {IB + target:#x}')
    if section_of(table) != '.edata':
        failures.append(f'{description}: {table:#x} is not in .edata')
FAR = struct.unpack('<f', rd(0x2329C00, 4))[0]
if FAR != 225.0:
    failures.append(f'network-position warp radius at 0x2329c00 is {FAR}, expected 225.0')

if failures:
    print('\n'.join(failures))
    raise SystemExit(1)

lines = [
    '// Generated by tools/generate_soldier_act_contract.py from the fingerprinted',
    f'// executable (SHA-256 {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}). Do not edit by hand.',
    '// Enemy sync M7 FM1 (far mirror): the soldier request manager, its FOB pack/unpack halves and the SetPuppet gate.',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    'namespace mgscoop::soldier_act_contract {',
    'inline constexpr std::uintptr_t update_rva=0x13735c0;          // manager update (this = M), hooked',
    'inline constexpr std::uintptr_t pack_request_rva=0x13704a0;    // (M-0x20, out70, R, idx)',
    'inline constexpr std::uintptr_t pack_order_rva=0x1370900;      // (M-0x20, out18, R+0x1b0)',
    'inline constexpr std::uintptr_t unpack_request_rva=0x136f810;  // (M-0x20, R, in70, idx)',
    'inline constexpr std::uintptr_t unpack_order_rva=0x13701f0;    // (M-0x20, R+0x1b0, in18)',
    'inline constexpr std::uintptr_t post_rva=0x1368980;            // (M, idx, req 0xc0)',
    'inline constexpr std::uintptr_t quiet_warp_rva=0x136ece0;      // (M-0x20, idx, entry): the network-position warp (quiet_place)',
    'inline constexpr std::uintptr_t manager_vtable_rva=0x23e8a48;  // M = P+0x20 (P, the constructed object: vtable 0x23e8a18)',
    'inline constexpr std::uintptr_t codec_vtable_rva=0x23e9638;',
    '// M: records +0x10 (stride 0x1e0), S +0x50, codec +0x58; the FOB functions take M-0x20.',
    'inline constexpr std::size_t m_records=0x10, m_soldier=0x50, m_codec=0x58, fob_this=0x20, record_stride=0x1e0;',
    '// S: +0x18 slot table (exists bits +0, realized +8, active +0x10, owner bytes +0x18, positions +0x20 stride 0x10,',
    '// count u32 +0x40); +0x98 records (data +8, count +0x10, stride 0x70; aim point +0x08, stance nibble +0x34);',
    '// +0xa0 weapons (+8 stride 8: slot byte +6; +0x10 magazine u16 (idx*3+2); +0x48 shots-off byte); +0xe0 network',
    '// positions (data +8, count u32 +0x10, stride 0x18, flags +0x14); +0x110 flag records (data +8, count +0x10, stride',
    '// 0x28; dword +8 bit 27 puppet); +0x2a4 mode (bit 2 online, bit 1 publish); +0x2a8 this peer\'s byte.',
    'inline constexpr std::size_t s_slots=0x18, slots_exists=0x0, slots_realized=0x8, slots_active=0x10, slots_owner=0x18,',
    '                             slots_pos=0x20, slots_pos_stride=0x10, slots_count=0x40;',
    'inline constexpr std::size_t s_records=0x98, rec_data=0x8, rec_count=0x10, rec_stride=0x70, rec_aim=0x8, rec_stance=0x34;',
    'inline constexpr std::size_t s_weapons=0xa0, wep_ammo=0x8, wep_ammo_stride=0x8, wep_mag=0x10, wep_off=0x48;',
    'inline constexpr std::size_t s_netpos=0xe0, net_data=0x8, net_count=0x10, net_stride=0x18, net_flags=0x14;',
    'inline constexpr std::size_t s_flags=0x110, flag_data=0x8, flag_count=0x10, flag_stride=0x28, flag_dword=0x8;',
    'inline constexpr std::uint32_t flag_puppet_bit=27;',
    'inline constexpr std::size_t s_mode=0x2a4, s_peer=0x2a8;',
    '// R (0x1e0): request +0x00 (0xc0; +0xb0 bits, +0xb4/+0xb5 kinds), +0x19e u16 flags (0x80 new, 0x100 second buffer),',
    '// +0x1a0 u16, order +0x1b0 (0x20: target +0x1b0, mode +0x1bc, kind +0x1c8, +0x1ca, priority +0x1cc), +0x1d2 u16 (1 new).',
    'inline constexpr std::size_t r_request=0x0, r_request_size=0xc0, r_bits=0xb0, r_kind_a=0xb4, r_kind_b=0xb5, r_flags=0x19e,',
    '                             r_1a0=0x1a0, r_order=0x1b0, r_order_mode=0x1bc, r_order_kind=0x1c8, r_order_flags=0x1d2;',
    'inline constexpr std::size_t wire_request=0x70, wire_order=0x18;',
    'inline constexpr float netpos_warp_sq=225.0f;  // 0x2329c00',
]
for n, (rva, size, description) in enumerate(SITES):
    lines.append(f'// {description}')
    lines.append(f'inline constexpr unsigned char site{n}[]={{' + ','.join(f'0x{b:02x}' for b in rd(rva, size)) + '};')
lines += ['struct Site {std::uintptr_t rva;std::size_t size;const unsigned char* bytes;};', 'inline constexpr Site sites[]={']
for n, (rva, size, _) in enumerate(SITES):
    lines.append(f'{{{rva:#x},sizeof(site{n}),site{n}}},')
lines += ['};', 'struct Slot {std::uintptr_t rva;std::uintptr_t target;};  // qword at rva == base + target',
          'inline constexpr Slot slots[]={']
for table, offset, target, description in SLOTS:
    lines.append(f'{{{table + offset:#x},{target:#x}}},  // {description}')
lines += ['};', '}', '']
content = '\r\n'.join(lines)
if CHECK:
    have = OUT.read_bytes().decode() if OUT.exists() else ''
    if have != content:
        print(f'{OUT.relative_to(ROOT)} differs from the executable contract')
        raise SystemExit(1)
    print(f'soldier act contract: {len(SITES)} sites and {len(SLOTS)} slots match {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}')
    raise SystemExit(0)
OUT.write_bytes(content.encode())
print(f'Emitted {len(SITES)} sites and {len(SLOTS)} slots to {OUT.relative_to(ROOT)}')
