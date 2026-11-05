"""Record the M7 P13 sense contract (native/soldier_perceive.cpp InstallLocal, ApplyOnline, LogMasks).

Static, read-only analysis of the fingerprinted installed executable; nothing is executed. Writes
native/soldier_sense_contract.hpp: every byte window the native module verifies in the running image before it hooks
the player-info service's LOCAL accessors (docs/world-sync-design.md 7.13, scout P12f):

  - 0x14053C9D0, the local player's record (the function the stub replaces; it reads the local index 0x142BD3FD4 and
    the bank selector 0x142BD3FD0), and 0x14053C990, the local index (*rcx = index; al = index < 16);
  - 0x14053CA40, the indexed record the redirect answers with (bank selector 0x142BD3FD0, bank 0x142BD3FF0);
  - the service slots 0x1f8 (0x141E02B40, a tail jump to the local index) and 0x208 (0x141E02AD0, the local
    GameObjectId: a call through slot 0x1f8 returning to 0x141E02AE7 inside a 0x28-byte frame);
  - every direct call of 0x14053C9D0 in the image (linear scan of .xtext for E8 rel32): each lies in the thunk range
    0x141E02000..0x141E04000 and has exactly 0x28 bytes of frame below its return address at the call, so the code that
    asked has its return address at entry rsp + 0x30;
  - the leaf's mode dword [rdi+0x2a4] (rdi = the leaf from 0x14134FA09; read at 0x14134FA3A; bit 2 online copied from
    the descriptor at creation, 0x141352470..0x141352493) for the L4 research probe;
  - the notice per-slot update's masks: [sys+0x78] soldier records (0x140D658E1, stride 0x18 at 0x140D65953/0x140D6595F,
    the mask dword 0x140D65998 and level byte 0x140D659A1) and the CP records [sys+0x68] + slot*0x60 (0x140D65A1A ..
    0x140D65A26; +0x44 store 0x140D65BA4).

Usage: python tools/generate_soldier_sense_contract.py [--check]
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
OUT = ROOT / 'native' / 'soldier_sense_contract.hpp'
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

LOCAL_REC, LOCAL_IDX, REC = 0x53C9D0, 0x53C990, 0x53CA40
THUNK_LO, THUNK_HI, ID_THUNK_RET = 0x1E02000, 0x1E04000, 0x1E02AE7


def rd(rva, n):
    data = pe.get_data(rva, n)
    if len(data) != n:
        raise SystemExit(f'Short read at {rva:#x}')
    return data


def text(rva, n):
    return [(i.address - IB, i.mnemonic, i.op_str) for i in md.disasm(rd(rva, n), IB + rva)]


WINDOWS = []  # (name, rva, size)


def window(name, rva, expect):
    got = text(rva, 64)
    for k, want in enumerate(expect):
        if k >= len(got) or got[k][1:] != want:
            failures.append(f'{name}: instruction {k} at {rva:#x}: {got[k] if k < len(got) else None} != {want}')
            return
    last = got[len(expect) - 1]
    size = last[0] - rva + len(md.disasm(rd(last[0], 16), IB + last[0]).__next__().bytes)
    WINDOWS.append((name, rva, size))


window('local_rec', LOCAL_REC, [('mov', 'ecx, dword ptr [rip + 0x26975fe]'), ('movsxd', 'rax, dword ptr [rip + 0x26975f3]'),
                                ('cmp', 'ecx, 0x10'), ('jb', '0x14053c9f4')])
if LOCAL_REC + 6 + 0x26975FE != 0x2BD3FD4 or LOCAL_REC + 13 + 0x26975F3 != 0x2BD3FD0:
    failures.append('local_rec: the local index or the bank selector moved')
window('local_idx', LOCAL_IDX, [('mov', 'eax, dword ptr [rip + 0x269763e]'), ('cmp', 'eax, 0x10'), ('mov', 'dword ptr [rcx], eax'),
                                ('setb', 'al'), ('ret', '')])
if LOCAL_IDX + 6 + 0x269763E != 0x2BD3FD4:
    failures.append('local_idx: the local index moved')
window('rec', REC, [('movsxd', 'rax, dword ptr [rip + 0x2697589]'), ('cmp', 'ecx, 0x10'), ('jb', '0x14053ca5e'),
                    ('imul', 'rax, rax, 0xe00'), ('lea', 'rcx, [rip + 0x2697596]'), ('add', 'rax, rcx'), ('ret', ''),
                    ('shl', 'rax, 4'), ('mov', 'ecx, ecx'), ('add', 'rax, rcx'), ('lea', 'rcx, [rip + 0x2697582]'),
                    ('imul', 'rax, rax, 0xe0'), ('add', 'rax, rcx'), ('ret', '')])
window('slot_1f8', 0x1E02B40, [('mov', 'rcx, rdx'), ('jmp', '0x14053c990')])
window('slot_208', 0x1E02AD0, [('push', 'rbx'), ('sub', 'rsp, 0x20'), ('mov', 'rax, qword ptr [rcx]'), ('mov', 'rbx, rdx'),
                               ('lea', 'rdx, [rsp + 0x30]'), ('call', 'qword ptr [rax + 0x1f8]')])
if 0x1E02AE1 + 6 != ID_THUNK_RET:
    failures.append('slot_208: the call does not return to 0x1E02AE7')
# The vtable 0x1425564F0 points its slots 0x1f8 / 0x208 at those thunks.
vt = 0x25564F0
for slot, fn in ((0x1f8, 0x141E02B40), (0x208, 0x141E02AD0), (0x18, 0x141E02E00), (0x220, 0x141E02770)):
    if struct.unpack('<Q', rd(vt + slot, 8))[0] != fn:
        failures.append(f'vtable slot {slot:#x} is not {fn:#x}')
# The leaf's mode dword (L4 research) and its creation-time copy of the online byte.
window('leaf_this', 0x134FA09, [('mov', 'rdi, rcx'), ('call', '0x140bff050')])
window('leaf_mode_read', 0x134FA3A, [('mov', 'ecx, dword ptr [rdi + 0x2a4]')])
window('online_copy', 0x1352470, [('movzx', 'ecx, byte ptr [rdi + 0x30]'), ('shl', 'ecx, 2'), ('xor', 'ecx, dword ptr [rsi + 0x2c4]'),
                                  ('and', 'ecx, 4'), ('xor', 'ecx, dword ptr [rsi + 0x2c4]'), ('mov', 'eax, ecx'), ('shr', 'eax, 1'),
                                  ('not', 'eax'), ('xor', 'eax, ecx'), ('and', 'eax, 2'), ('xor', 'eax, ecx'),
                                  ('mov', 'dword ptr [rsi + 0x2c4], eax')])
# The notice masks LogMasks reads.
window('mask_records', 0xD658E1, [('mov', 'r12, qword ptr [rcx + 0x78]')])
window('mask_stride', 0xD65950, [('mov', 'ecx, dword ptr [r8]'), ('lea', 'rax, [rcx + rcx*2]'), ('lea', 'r9, [rcx + rcx*4]'),
                                 ('mov', 'ecx, dword ptr [rsi + r9*4]'), ('lea', 'rdx, [r12 + rax*8]')])
window('mask_read', 0xD65998, [('mov', 'eax, dword ptr [rdx]'), ('inc', 'dword ptr [rsp + 0x4c]'), ('or', 'r15d, eax'),
                               ('cmp', 'byte ptr [rdx + 0x15], 3')])
window('cp_records', 0xD65A1A, [('mov', 'rsi, qword ptr [r14 + 0x68]'), ('lea', 'rcx, [r9 + r9*2]'), ('shl', 'rcx, 5'),
                                ('add', 'rsi, rcx')])
window('cp_mask44', 0xD65BA4, [('mov', 'dword ptr [rsi + 0x44], r15d')])

# Every direct call of the local record: inside the thunk range with a 0x28-byte frame.
xt = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.xtext'][0]
code = raw[xt.PointerToRawData:xt.PointerToRawData + xt.SizeOfRawData]
callers = []
for i in range(len(code) - 5):
    if code[i] == 0xE8 and xt.VirtualAddress + i + 5 + struct.unpack_from('<i', code, i + 1)[0] == LOCAL_REC:
        callers.append(xt.VirtualAddress + i)
if len(callers) != 28:
    failures.append(f'local_rec: {len(callers)} direct callers, expected 28')


def frame_at(site):
    start = site & ~0xF
    while rd(start - 1, 1) != b'\xcc':
        start -= 16
        if site - start > 0x100:
            return None
    off = 0
    for ins in md.disasm(rd(start, site - start), IB + start):
        m, ops = ins.mnemonic, ins.op_str
        if m == 'push':
            off += 8
        elif m == 'sub' and ops.startswith('rsp,'):
            off += int(ops.split(',')[1], 0)
        elif m in ('pop', 'add', 'lea') and ops.startswith('rsp'):
            return None
    return off


for site in callers:
    if not (THUNK_LO <= site < THUNK_HI):
        failures.append(f'local_rec caller {site:#x} outside the thunk range')
    elif frame_at(site) != 0x28:
        failures.append(f'local_rec caller {site:#x}: frame {frame_at(site)} != 0x28')
idx_calls = [xt.VirtualAddress + i for i in range(len(code) - 5)
             if code[i] == 0xE8 and xt.VirtualAddress + i + 5 + struct.unpack_from('<i', code, i + 1)[0] == LOCAL_IDX]
for site in idx_calls:
    if THUNK_LO <= site < THUNK_HI:
        failures.append(f'local_idx called (not tail-jumped) from the thunk range at {site:#x}')

if failures:
    print('\n'.join(failures))
    raise SystemExit(1)

lines = [
    '// Generated by tools/generate_soldier_sense_contract.py from the fingerprinted',
    f'// executable (SHA-256 {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}). Do not edit by hand.',
    '// M7 P13 (soldier_perceive.hpp InstallLocal, ApplyOnline, LogMasks): the windows verified before the LOCAL hooks.',
    f'// The 28 direct callers of 0x14053C9D0 lie in 0x141E02000..0x141E04000 with a 0x28-byte frame at the call.',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    'namespace mgscoop::soldier_sense_contract {',
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
    print(f'soldier sense contract: {len(WINDOWS)} windows match {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}')
    raise SystemExit(0)
OUT.write_bytes(content.encode())
print(f'Emitted {len(WINDOWS)} windows to {OUT.relative_to(ROOT)}')
