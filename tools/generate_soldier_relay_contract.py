"""Record the typed soldier dispatch that answers a set-transform with out=0 handled=0 (the enemy latch).

Static, read-only analysis of the fingerprinted installed executable; nothing is executed. Writes
native/soldier_relay_contract.hpp: the byte windows and data qwords soldier_warp::Verify checks in the running image
before soldier_warp::Warp may call an out=0 handled=0 answer "relayed" instead of an anomaly
(docs/world-sync-design.md 7.14).

The live chain of every soldier warp (census of run e583ee62: hop=0xbf5450, leaf=engine on every line):
  - handle vtable 0x23063f8 +0x10 = 0xbf5450: [handle+0x30] == 2 -> *out = 0x8ce64801 (never handled); else
    (handle+0x20)->vt[0x70](out, idx, msg); no object -> *out = -1;
  - soldier type vtable 0x229c4b0 +0x70 = 0x9e9ab0: leaf = (*(type+0x40)+0x10)->vt[0x18](0x3fef1d0298aa) - 0xc, then
    leaf->vt[0xa8] (the handler 0x1360f90, pinned by generate_soldier_warp_contract.py) into a local; 0 -> *out = 0
    (handled 1 from the handler); -1 -> allocated bit idx of *[leaf+0x18] clear -> *out = -1; allocated but realize bit
    idx of *([leaf+0x18]+8) clear -> [leaf+0x58]->vt[0x10](idx, 0) and the relay 0xbf6570 -> 0xbf6490, whose answer
    goes to *out;
  - 0xbf6490 writes *out = 0 first (0xbf64b1) and hands the message to each component ([c+0x38]->vt[0x28]); the
    handler never runs, so msg+0x33 (handled) stays 0 and the guard does not move.
So on this chain out=0 handled=0 means: the guard's slot exists on this copy but is not realized here.

Usage: python tools/generate_soldier_relay_contract.py [--check]
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
OUT = ROOT / 'native' / 'soldier_relay_contract.hpp'
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
    expect(0xBF5450, 0x45, [
        (0xBF5456, 'cmp', 'dword ptr [rcx + 0x30], 2'),
        (0xBF545F, 'mov', 'dword ptr [rdx], 0x8ce64801'),
        (0xBF546E, 'mov', 'rcx, qword ptr [rcx + 0x20]'),
        (0xBF5477, 'mov', 'rax, qword ptr [rcx]'),
        (0xBF547A, 'call', 'qword ptr [rax + 0x70]'),
        (0xBF5486, 'mov', 'dword ptr [rdx], 0xffffffff'),
        (0xBF5494, 'ret', ''),
    ], 'soldier handle vtable +0x10 (the live hop): mode 2 -> *out = 0x8ce64801; (handle+0x20)->vt[0x70]; none -> -1'),
    expect(0x9E9AB0, 0x145, [
        (0x9E9AC2, 'mov', 'rcx, qword ptr [rcx + 0x40]'),
        (0x9E9AD6, 'mov', 'rcx, qword ptr [rcx + 0x10]'),
        (0x9E9AE2, 'movabs', 'rdx, 0x3fef1d0298aa'),
        (0x9E9AEC, 'call', 'qword ptr [rax + 0x18]'),
        (0x9E9AEF, 'lea', 'rbx, [rax - 0xc]'),
        (0x9E9B0C, 'call', 'qword ptr [rax + 0xa8]'),
        (0x9E9B12, 'mov', 'eax, dword ptr [rsp + 0x70]'),
        (0x9E9B18, 'jne', '0x9e9b22'),
        (0x9E9B1A, 'mov', 'dword ptr [r14], edi'),
        (0x9E9B22, 'movabs', 'rcx, 0x8e51a39f7eda'),
        (0x9E9B3F, 'mov', 'r8, qword ptr [rbx + 0x18]'),
        (0x9E9B52, 'mov', 'rcx, qword ptr [r8]'),
        (0x9E9B59, 'test', 'byte ptr [r9 + rcx], dl'),
        (0x9E9B5D, 'je', '0x9e9bed'),
        (0x9E9B63, 'mov', 'rax, qword ptr [r8 + 8]'),
        (0x9E9B67, 'test', 'byte ptr [r9 + rax], dl'),
        (0x9E9B6B, 'je', '0x9e9bc9'),
        (0x9E9BC9, 'mov', 'rcx, qword ptr [rbx + 0x58]'),
        (0x9E9BD5, 'call', 'qword ptr [rax + 0x10]'),
        (0x9E9BE6, 'call', '0xbf6570'),
        (0x9E9BEB, 'mov', 'eax, dword ptr [rax]'),
        (0x9E9BED, 'mov', 'dword ptr [r14], eax'),
    ], 'soldier type vtable +0x70: handler 0 -> *out = 0; -1 and allocated but not realized -> relay 0xbf6570 into *out'),
    expect(0xBF6570, 0x17, [
        (0xBF6576, 'mov', 'rbx, rcx'),
        (0xBF6579, 'call', '0xbf6490'),
        (0xBF6586, 'ret', ''),
    ], 'relay wrapper: 0xbf6490(out, entity, idx, msg)'),
    expect(0xBF6490, 0xA1, [
        (0xBF64AE, 'mov', 'rsi, rcx'),
        (0xBF64B1, 'mov', 'dword ptr [rcx], 0'),
        (0xBF64D0, 'mov', 'rcx, qword ptr [rdi + 0x38]'),
        (0xBF64E0, 'call', 'qword ptr [rax + 0x28]'),
        (0xBF64FD, 'call', '0xbf6810'),
        (0xBF6530, 'ret', ''),
    ], 'relay: *out = 0 first, then each component [c+0x38]->vt[0x28]; msg+0x33 untouched'),
]

# (vtable rva, slot offset, expected target rva, description). Data qwords are absolute (no relocations).
SLOTS = [
    (0x23063F8, 0x10, 0xBF5450, 'soldier handle vtable +0x10 = the live hop 0xbf5450'),
    (0x229C4B0, 0x40, 0x9E9140, 'soldier type vtable +0x40 = count call (census count_fn)'),
    (0x229C4B0, 0x70, 0x9E9AB0, 'soldier type vtable +0x70 = the soldier type dispatcher 0x9e9ab0'),
]
for table, offset, target, description in SLOTS:
    value = q(table + offset)
    if value != IB + target:
        failures.append(f'{description}: [{table + offset:#x}] = {value:#x}, expected {IB + target:#x}')
    if section_of(table) != '.edata':
        failures.append(f'{description}: {table:#x} is not in .edata')

if failures:
    print('\n'.join(failures))
    raise SystemExit(1)

lines = [
    '// Generated by tools/generate_soldier_relay_contract.py from the fingerprinted',
    f'// executable (SHA-256 {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}). Do not edit by hand.',
    '// Enemy latch: the typed soldier dispatch whose out=0 handled=0 answer is a relay to an unrealized guard.',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    'namespace mgscoop::soldier_relay_contract {',
    'inline constexpr std::uintptr_t handle_vtable_rva=0x23063f8;   // soldier handle object vtable',
    'inline constexpr std::uintptr_t hop_rva=0xbf5450;              // its +0x10',
    'inline constexpr std::uintptr_t type_vtable_rva=0x229c4b0;     // the object at handle+0x20',
    'inline constexpr std::uintptr_t dispatcher_rva=0x9e9ab0;       // its +0x70',
    'inline constexpr std::size_t handle_type_object=0x20, handle_mode=0x30, type_dispatch_slot=0x70;',
    'inline constexpr std::uint32_t mode2_answer=0x8ce64801u;       // [handle+0x30] == 2: written to *out, never handled',
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
    print(f'soldier relay contract: {len(SITES)} sites and {len(SLOTS)} slots match {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}')
    raise SystemExit(0)
OUT.write_bytes(content.encode())
print(f'Emitted {len(SITES)} sites and {len(SLOTS)} slots to {OUT.relative_to(ROOT)}')
