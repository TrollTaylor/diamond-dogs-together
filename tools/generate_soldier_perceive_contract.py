"""Record the M7 per-soldier perception contract (native/soldier_perceive.cpp InstallPick).

Static, read-only analysis of the fingerprinted installed executable; nothing is executed. Writes
native/soldier_perceive_contract.hpp: every byte window the native module verifies in the running image before it
hooks the offense index getter 0x141E02770 (a register-saving stub) and the notice system's per-soldier update
0x140D65850 (docs/world-sync-design.md 7.13).

Checked here, instruction by instruction:
  - the getter head and the per-soldier update head (the four arguments: rcx sys, edx soldier, xmm2 dt, r9b flag);
  - the per-soldier loop 0x140D5AE50 -> 0x140D65850 at 0x140D5B04D (edx = ebx, rcx = rsi - 0x28);
  - the cache word sys+0x1ac: written from the getter at 0x140D5AF51 ([rsi+0x184], rsi = sys + 0x28) and read at the
    top of the per-soldier update (0x140D65908);
  - each live per-soldier caller of the getter: the `mov <reg>, edx` at its entry, the getter call right before the
    return address, and (linear sweep of the function's .pdata chunks) no other write to that register at an address
    between the two;
  - the leaf snapshot's getter call (ret 0x1350035) and its store to [rdi+0x28c].

Usage: python tools/generate_soldier_perceive_contract.py [--check]
  --check  verify only; exit 1 on any mismatch or when the header differs, do not write anything.
"""
import bisect
import hashlib
import struct
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs

ROOT = Path(__file__).resolve().parent.parent
EXE = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
GAME_SHA256 = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
OUT = ROOT / 'native' / 'soldier_perceive_contract.hpp'
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


def text(rva, n):
    return [(i.address - IB, i.mnemonic, i.op_str) for i in md.disasm(rd(rva, n), IB + rva)]


# .pdata: (begin, end, primary) with chained entries resolved to their primary function.
def r2o(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return rva - s.VirtualAddress + s.PointerToRawData
    raise SystemExit(f'rva {rva:#x} outside every section')


pdata = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
entries = []
for k in range(pdata.Size // 12):
    b, e, u = struct.unpack_from('<III', raw, r2o(pdata.VirtualAddress) + 12 * k)
    prim, uu = b, u
    for _ in range(16):
        if uu & 1:
            prim, _, uu = struct.unpack_from('<III', raw, r2o(uu & ~1))
            continue
        uo = r2o(uu)
        if (raw[uo] >> 3) & 4:
            n = raw[uo + 2]
            prim, _, uu = struct.unpack_from('<III', raw, uo + 4 + ((n + 1) & ~1) * 2)
            continue
        break
    entries.append((b, e, prim))
entries.sort()
starts = [x[0] for x in entries]


def primary(rva):
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0 or not (entries[i][0] <= rva < entries[i][1]):
        raise SystemExit(f'no .pdata entry for {rva:#x}')
    return entries[i][2]


def chunks(prim):
    return sorted((b, e) for b, e, p in entries if p == prim)


WINDOWS = []  # (name, rva, size, expected instructions [(rva, mnemonic, operands)])


def window(name, rva, expect):
    size = 0
    got = text(rva, 64)
    for k, want in enumerate(expect):
        if k >= len(got) or got[k][1:] != want:
            failures.append(f'{name}: instruction {k} at {rva:#x}: {got[k] if k < len(got) else None} != {want}')
            return
    last = got[len(expect) - 1]
    size = last[0] - rva + len(md.disasm(rd(last[0], 16), IB + last[0]).__next__().bytes)
    WINDOWS.append((name, rva, size))


window('getter_head', 0x1E02770, [('sub', 'rsp, 0x28'), ('call', '0x140bff050'), ('mov', 'rcx, qword ptr [rax + 0x78]'),
                                  ('mov', 'rax, qword ptr [rcx]'), ('call', 'qword ptr [rax + 0x40]')])
window('update_head', 0xD65850, [('mov', 'r11, rsp'), ('push', 'rbp'), ('push', 'r13'), ('push', 'r14'),
                                 ('lea', 'rbp, [r11 - 0x28]'), ('sub', 'rsp, 0x110'),
                                 ('movaps', 'xmmword ptr [r11 - 0x78], xmm9')])
window('update_args', 0xD65876, [('mov', 'r10d, edx'), ('mov', 'r13d, edx'), ('mov', 'byte ptr [rsp + 0x40], r9b'),
                                 ('shl', 'r10, 4'), ('movaps', 'xmm9, xmm2'), ('mov', 'r14, rcx')])
window('update_cache_read', 0xD65908, [('movzx', 'eax, word ptr [rcx + 0x1ac]')])
window('loop_call', 0xD5B040, [('lea', 'rcx, [rsi - 0x28]'), ('movaps', 'xmm2, xmm6'), ('movzx', 'r9d, bpl'),
                               ('mov', 'edx, ebx'), ('call', '0x140d65850')])
window('cache_write', 0xD5AF41, [('mov', 'rcx, qword ptr [rsi + 0x368]'), ('mov', 'rax, qword ptr [rcx]'),
                                 ('call', 'qword ptr [rax + 0x220]'), ('mov', 'word ptr [rsi + 0x184], ax')])
window('leaf_call', 0x1350025, [('mov', 'rcx, qword ptr [rdi + 0x188]'), ('mov', 'rax, qword ptr [rcx]'),
                                ('call', 'qword ptr [rax + 0x220]'), ('mov', 'rcx, qword ptr [rdi + 0x188]'),
                                ('mov', 'dword ptr [rdi + 0x28c], eax')])

ALIASES = {'rsi': ('rsi', 'esi', 'si', 'sil'), 'rdi': ('rdi', 'edi', 'di', 'dil'), 'rbx': ('rbx', 'ebx', 'bx', 'bl', 'bh'),
           'rbp': ('rbp', 'ebp', 'bp', 'bpl'), 'r12': ('r12', 'r12d', 'r12w', 'r12b'),
           'r14': ('r14', 'r14d', 'r14w', 'r14b'), 'r15': ('r15', 'r15d', 'r15w', 'r15b')}
# ret, register, the mov at entry (rva, mnemonic, operands), the getter call (rva, operands)
PICK = [
    (0xD6B87F, 'rsi', (0xD6B81C, 'mov', 'esi, edx'), (0xD6B879, 'qword ptr [rax + 0x220]')),
    (0xD6D82F, 'rsi', (0xD6D7ED, 'mov', 'esi, edx'), (0xD6D829, 'qword ptr [rdi + 0x220]')),
    (0xD6D9CE, 'r14', (0xD6D950, 'mov', 'r14d, edx'), (0xD6D9C8, 'qword ptr [rdi + 0x220]')),
    (0xD6638E, 'r15', (0xD6622E, 'mov', 'r15d, edx'), (0xD66388, 'qword ptr [rax + 0x220]')),
    (0xD782E3, 'rbx', (0xD78260, 'mov', 'ebx, edx'), (0xD782DD, 'qword ptr [rax + 0x220]')),
    (0xD59430, 'r12', (0xD59340, 'mov', 'r12d, edx'), (0xD5942A, 'qword ptr [rax + 0x220]')),
    (0xD56834, 'rbp', (0xD5681F, 'movzx', 'ebp, dx'), (0xD5682E, 'qword ptr [rax + 0x220]')),
    (0x14DCA77, 'rdi', (0x14DC9FC, 'mov', 'edi, edx'), (0x14DCA71, 'qword ptr [rax + 0x220]')),
]
for ret, reg, (mrva, mm, mo), (crva, co) in PICK:
    window(f'site_{ret:x}_mov', mrva, [(mm, mo)])
    window(f'site_{ret:x}_call', crva, [('call', co)])
    if crva + 6 != ret:
        failures.append(f'site {ret:#x}: the call does not end at the return address')
    prim = primary(ret)
    if primary(mrva) != prim:
        failures.append(f'site {ret:#x}: the mov is in another function')
    lo, hi = mrva, crva
    for cb, ce in chunks(prim):
        for ins in md.disasm(rd(cb, ce - cb), IB + cb):
            a = ins.address - IB
            if not (lo < a < hi):
                continue
            ops = [o.strip() for o in ins.op_str.split(',')]
            writes = ins.mnemonic not in ('cmp', 'test', 'push', 'bt') and ops and ops[0] in ALIASES[reg]
            writes = writes or (ins.mnemonic == 'xchg' and any(o in ALIASES[reg] for o in ops))
            # a pop on an early-return path is allowed only when followed by ret (an epilogue)
            if writes and ins.mnemonic == 'pop':
                continue
            if writes:
                failures.append(f'site {ret:#x}: {reg} written at {a:#x} ({ins.mnemonic} {ins.op_str})')

if failures:
    print('\n'.join(failures))
    raise SystemExit(1)

lines = [
    '// Generated by tools/generate_soldier_perceive_contract.py from the fingerprinted',
    f'// executable (SHA-256 {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}). Do not edit by hand.',
    '// M7 per-soldier perception (soldier_perceive.hpp): the windows InstallPick verifies before it hooks.',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    'namespace mgscoop::soldier_perceive_contract {',
]
for n, (name, rva, size) in enumerate(WINDOWS):
    lines.append(f'inline constexpr unsigned char w{n}[]={{' + ','.join(f'0x{b:02x}' for b in rd(rva, size)) + f'}};  // {name}')
lines += ['struct Window {const char* name;std::uintptr_t rva;std::size_t size;const unsigned char* bytes;};',
          'inline constexpr Window windows[]={']
for n, (name, rva, size) in enumerate(WINDOWS):
    lines.append(f'{{"{name}",{rva:#x},sizeof(w{n}),w{n}}},')
lines += ['};', '}', '']
content = '\r\n'.join(lines)
if CHECK:
    have = OUT.read_bytes().decode() if OUT.exists() else ''
    if have != content:
        print(f'{OUT.relative_to(ROOT)} differs from the executable contract')
        raise SystemExit(1)
    print(f'soldier perceive contract: {len(WINDOWS)} windows match {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}')
    raise SystemExit(0)
OUT.write_bytes(content.encode())
print(f'Emitted {len(WINDOWS)} windows to {OUT.relative_to(ROOT)}')
