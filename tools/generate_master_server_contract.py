"""Record the retail master-server gate URL site. Static, read-only; never runs the game.

Verifies, for mgsvtpp.exe 1.0.15.4 (SHA-256 085c2f82...):
  * the `lea rdx,[rip+disp32]` at RVA 0x7D346C (disp32 at 0x7D346F) and its
    surrounding call context in case 1 of the gate state machine;
  * the 48-character URL string at RVA 0x2218708, its terminator, and the
    zero alignment bytes up to the next 16-byte boundary (0x2218740, where an
    unrelated constant begins: byte 0x2218747 is 0x80);
  * that, across every section, exactly one decodable rip-relative instruction
    and no absolute 64-bit pointer or 32-bit RVA reference targets the window
    0x2218700..0x2218760 (string plus neighbours).
Writes native/master_server_contract.hpp. Requires pefile, capstone and numpy.
"""
import hashlib
import struct
import sys
from pathlib import Path

import numpy as np
import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

ROOT = Path(__file__).resolve().parent.parent
EXE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
GAME_SHA256 = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
IMAGE_BASE = 0x140000000
LEA_RVA, LEA_SIZE, DISP_OFFSET = 0x7D346C, 7, 3
CONTEXT_RVA, CONTEXT_END = 0x7D3461, 0x7D347C  # lea rcx,[rsp+30h]; call; nop; lea rdx,[url]; lea rcx,[rbp-50h]; call
STRING_RVA = 0x2218708
ORIGINAL = b'https://mgstpp-game.konamionline.com/tppstm/gate'
ALIGNED_END = 0x2218740
WINDOW = (0x2218700, 0x2218760)

raw = EXE.read_bytes()
if hashlib.sha256(raw).hexdigest() != GAME_SHA256:
    raise SystemExit('Executable fingerprint mismatch')
pe = pefile.PE(data=raw, fast_load=True)
if pe.OPTIONAL_HEADER.ImageBase != IMAGE_BASE or pe.OPTIONAL_HEADER.DllCharacteristics & 0x40:
    raise SystemExit('Unexpected image base or relocatable image')


def read(rva, size):
    off = pe.get_offset_from_rva(rva)
    return raw[off:off + size]


md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
context = read(CONTEXT_RVA, CONTEXT_END - CONTEXT_RVA)
decoded = [(i.address, i.mnemonic, i.op_str) for i in md.disasm(context, CONTEXT_RVA)]
expected = [
    (0x7D3461, 'lea', 'rcx, [rsp + 0x30]'),
    (0x7D3466, 'call', '0x7d2160'),
    (0x7D346B, 'nop', ''),
    (0x7D346C, 'lea', 'rdx, [rip + 0x1a45295]'),
    (0x7D3473, 'lea', 'rcx, [rbp - 0x50]'),
    (0x7D3477, 'call', '0x61ac0'),
]
if decoded != expected:
    raise SystemExit(f'Gate URL call context changed: {decoded}')
disp = struct.unpack_from('<i', read(LEA_RVA + DISP_OFFSET, 4))[0]
if LEA_RVA + LEA_SIZE + disp != STRING_RVA:
    raise SystemExit('lea does not resolve to the URL string')

string_window = read(STRING_RVA, ALIGNED_END - STRING_RVA)
if string_window[:len(ORIGINAL)] != ORIGINAL or any(string_window[len(ORIGINAL):]):
    raise SystemExit('URL string, terminator or alignment padding changed')
padding_after_terminator = ALIGNED_END - STRING_RVA - len(ORIGINAL) - 1
if read(ALIGNED_END + 7, 1) != b'\x80':
    raise SystemExit('Following constant changed; padding bound is no longer evident')

# Reference scan: every 4-byte window in every section as a rip-relative disp32
# with 0, 1, 2 or 4 trailing immediate bytes; then decode to confirm.
confirmed = set()
absolute = []
for section in pe.sections:
    data = raw[section.PointerToRawData:section.PointerToRawData + min(section.SizeOfRawData, section.Misc_VirtualSize)]
    if len(data) < 8:
        continue
    b = np.frombuffer(data, dtype=np.uint8).astype(np.int64)
    d = b[:-3] | (b[1:-2] << 8) | (b[2:-1] << 16) | (b[3:] << 24)
    d = np.where(d >= 2 ** 31, d - 2 ** 32, d)
    pos = np.arange(len(d), dtype=np.int64) + section.VirtualAddress
    for imm in (0, 1, 2, 4):
        target = pos + 4 + imm + d
        for h in np.nonzero((target >= WINDOW[0]) & (target < WINDOW[1]))[0]:
            disp_rva, tgt = int(pos[h]), int(target[h])
            for start in range(disp_rva - 12, disp_rva):
                insn = next(md.disasm(read(start, 16), start, 1), None)
                if insn is None or insn.address + insn.size != disp_rva + 4 + imm or insn.disp_offset != disp_rva - start:
                    continue
                if any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP and
                       insn.address + insn.size + op.mem.disp == tgt for op in insn.operands):
                    confirmed.add((section.Name.rstrip(b'\0').decode(), insn.address, insn.mnemonic, insn.op_str, tgt))
                    break
    for addr in range(IMAGE_BASE + WINDOW[0], IMAGE_BASE + WINDOW[1]):
        if data.find(struct.pack('<Q', addr)) != -1:
            absolute.append((section.Name, hex(addr)))
    if data.find(struct.pack('<I', STRING_RVA)) != -1:
        absolute.append((section.Name, 'rva32'))
string_refs = [c for c in confirmed if STRING_RVA <= c[4] < ALIGNED_END]
code_refs = [c for c in string_refs if c[0] == '.xtext']
if absolute:
    raise SystemExit(f'Absolute references into the URL window: {absolute}')
if [(c[1], c[4]) for c in code_refs] != [(LEA_RVA, STRING_RVA)]:
    raise SystemExit(f'Unexpected code references to the URL string: {string_refs}')
# Non-.xtext "confirmed" decodes are data bytes that happen to decode (for
# example `and al,[rip+..]` inside .edata); none may target the string itself.
if len(string_refs) != 1:
    raise SystemExit(f'Additional decodable references to the URL string: {string_refs}')


def c_bytes(data):
    return ','.join(f'0x{x:02x}' for x in data)


header = f'''// Generated by tools/generate_master_server_contract.py from mgsvtpp.exe 1.0.15.4
// (SHA-256 {GAME_SHA256}). Static evidence only:
//  - case 1 of the gate state machine (0x7D3461..0x7D347C) builds the "GATE"
//    server entry from the C string addressed by `lea rdx,[rip+disp32]` at
//    0x7D346C (disp32 at 0x7D346F) and copies it (0x61AC0 -> 0x1D0E0);
//  - the string at 0x2218708 is {len(ORIGINAL)} chars + NUL, followed by
//    {padding_after_terminator} zero alignment bytes; an unrelated constant starts at 0x2218740;
//  - exactly one rip-relative instruction (the lea) and no absolute pointer or
//    RVA value in any section references 0x2218700..0x2218760.
#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::master_server_contract {{
inline constexpr std::uintptr_t context_rva=0x{CONTEXT_RVA:x};
inline constexpr unsigned char context_bytes[]={{{c_bytes(context)}}};
inline constexpr std::uintptr_t lea_rva=0x{LEA_RVA:x};
inline constexpr std::size_t lea_size={LEA_SIZE};
inline constexpr std::size_t disp_offset={DISP_OFFSET};
inline constexpr std::uintptr_t string_rva=0x{STRING_RVA:x};
inline constexpr char original_url[]="{ORIGINAL.decode()}";
inline constexpr std::size_t original_length={len(ORIGINAL)};
inline constexpr std::size_t padding_after_terminator={padding_after_terminator};
inline constexpr std::size_t string_code_references={len(code_refs)};
}}
'''
out = ROOT / 'native' / 'master_server_contract.hpp'
out.write_text(header, encoding='ascii', newline='\n')
print(f'wrote {out}; code_refs={len(code_refs)} padding_after_terminator={padding_after_terminator}')
