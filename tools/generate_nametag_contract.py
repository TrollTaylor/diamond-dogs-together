"""Record the camera contract of the partner name tag and friendly crosshair.

Static, read-only analysis of the fingerprinted installed executable; nothing
is executed or written outside this repository. Writes
native/nametag_contract.hpp and docs/evidence/nametag-contract-2026-10-08.json.

The name tag (native/nametag_game.cpp) only READS the game's own render
viewport and its camera: no game function is called and nothing is written.
The chain and every field it reads are the ones the game's player update reads
on the same (Lua owner) thread at 0x9c236c..0x9c2446:

  sel = *(0x2ba1668)            CameraSelector main instance (0x438a60 reads it)
  vp  = *(*(sel + 0x58) + 0x18) render viewport (getter 0x438db0)
  vp+0x300 projection, vp+0x340 view, vp+0x380 view x projection (row vectors),
  vp+0x570 camera, vp+0x584..0x593 rect (x0, y0, width, height fractions),
  vp+0x5d8/0x5dc pixel size, vp+0x5e0 pixel aspect.

The viewport update 0x1b9490 fills them (0x4380b4 from the selector update):
an orthographic camera (camera+0x100 bit 2) takes another path (0x1b8e80) and
is refused; otherwise view = camera+0x70, projection = 0x241b00(...) and
VP = View x Projection. This script re-runs 0x241b00 instruction by
instruction in a small SSE emulator and asserts the projection shape the
runtime checks every tick (P23 = 1, P33 = 0: clip.w = view z, the camera looks
along +Z; P00 = p*b, P11 = p*c, P20/P21 the lens shift; vp+0x300 maps near to
+1 and far to -1, vp+0x280 is reversed-Z).

Every byte sequence below is re-checked here instruction by instruction and
again in the running image (nametag_game::Install) before the first read; a
single difference refuses the feature (nametag=refused; reason=contract).

Usage: python tools/generate_nametag_contract.py [--check]
  --check  verify only (including that the committed header is this script's
           output); exit 1 on any mismatch, do not write anything.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

ROOT = Path(__file__).resolve().parent.parent
EXE = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
GAME_SHA256 = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
OUT = ROOT / 'native' / 'nametag_contract.hpp'
EVIDENCE = ROOT / 'docs' / 'evidence' / 'nametag-contract-2026-10-08.json'

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


def f32(rva):
    return struct.unpack('<f', rd(rva, 4))[0]


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


def expect(rva, size, rows, description, section='.xtext'):
    """rows: (address, mnemonic, operands) that must appear exactly."""
    ins = {a: (m, o) for a, _, m, o in disasm(rva, size)}
    for address, mnemonic, operands in rows:
        if ins.get(address) != (mnemonic, operands):
            failures.append(f'{description}: {address:#x} is {ins.get(address)}, expected {(mnemonic, operands)}')
    if section_of(rva) != section:
        failures.append(f'{description}: {rva:#x} is not in {section}')
    return dict(rva=rva, size=size, description=description)


def rip_target(address):
    """The RVA a [rip + disp] operand of the instruction at `address` names."""
    ins = next(md.disasm(rd(address, 16), address), None)
    if ins is None or '[rip + ' not in ins.op_str:
        return None
    disp = int(ins.op_str.split('[rip + ')[1].split(']')[0], 16)
    return ins.address + ins.size + disp


def data_site(rva, size, value_check, description):
    if section_of(rva) != '.edata':
        failures.append(f'{description}: {rva:#x} is not in .edata')
    if not value_check(rd(rva, size)):
        failures.append(f'{description}: unexpected value {rd(rva, size).hex()}')
    return dict(rva=rva, size=size, description=description)


SITES = [
    expect(0x438DB0, 0x10, [
        (0x438DB0, 'mov', 'rax, qword ptr [rcx + 0x58]'),
        (0x438DB4, 'test', 'rax, rax'),
        (0x438DB7, 'je', '0x438dbe'),
        (0x438DB9, 'mov', 'rax, qword ptr [rax + 0x18]'),
        (0x438DBD, 'ret', ''),
        (0x438DBE, 'repz ret', ''),
    ], 'render viewport getter: vp = *(*(sel+0x58)+0x18), null handle -> null'),
    expect(0x438A7C, 0x7, [
        (0x438A7C, 'mov', 'rdx, qword ptr [rip + 0x2768be5]'),
    ], 'CameraSelector GetMainInstance reads the main instance slot 0x2ba1668'),
    expect(0x9C236C, 0xDA, [
        (0x9C236C, 'mov', 'rcx, qword ptr [rip + 0x21df2f5]'),
        (0x9C2373, 'call', '0x438db0'),
        (0x9C237B, 'test', 'rax, rax'),
        (0x9C237E, 'je', '0x9c2446'),
        (0x9C2384, 'movaps', 'xmm0, xmmword ptr [rax + 0x340]'),
        (0x9C2392, 'movaps', 'xmm1, xmmword ptr [rax + 0x350]'),
        (0x9C23A0, 'movaps', 'xmm0, xmmword ptr [rax + 0x360]'),
        (0x9C23AE, 'movaps', 'xmm1, xmmword ptr [rax + 0x370]'),
        (0x9C23BC, 'movaps', 'xmm0, xmmword ptr [rax + 0x300]'),
        (0x9C23CA, 'movaps', 'xmm1, xmmword ptr [rax + 0x310]'),
        (0x9C23D8, 'movaps', 'xmm0, xmmword ptr [rax + 0x320]'),
        (0x9C23E6, 'movaps', 'xmm1, xmmword ptr [rax + 0x330]'),
        (0x9C23F4, 'mov', 'eax, dword ptr [rax + 0x5d8]'),
        (0x9C2400, 'mov', 'eax, dword ptr [rcx + 0x5dc]'),
        (0x9C240C, 'movd', 'xmm1, dword ptr [rcx + 0x5d8]'),
        (0x9C2417, 'mulss', 'xmm1, dword ptr [rcx + 0x5e0]'),
        (0x9C241F, 'movd', 'xmm0, dword ptr [rcx + 0x5dc]'),
        (0x9C242A, 'divss', 'xmm1, xmm0'),
    ], 'player update (same function as the 0x9c2b51 input hook, Lua owner thread): unlocked reads of view, projection, pixel size and aspect'),
    expect(0x1B9510, 0x35, [
        (0x1B9510, 'mov', 'rcx, qword ptr [rcx + 0x570]'),
        (0x1B9517, 'test', 'rcx, rcx'),
        (0x1B9520, 'mov', 'eax, dword ptr [rcx + 0x100]'),
        (0x1B9526, 'shr', 'eax, 2'),
        (0x1B9529, 'test', 'al, 1'),
        (0x1B952B, 'je', '0x1b9545'),
        (0x1B9540, 'jmp', '0x1b8e80'),
    ], 'viewport update: camera = vp+0x570; camera+0x100 bit 2 takes the non-perspective path 0x1b8e80'),
    expect(0x1B9545, 0x65, [
        (0x1B9545, 'movaps', 'xmm0, xmmword ptr [rcx + 0x70]'),
        (0x1B9549, 'movss', 'xmm2, dword ptr [rip + 0x1f38763]'),
        (0x1B9551, 'movss', 'xmm3, dword ptr [rip + 0x1eeeb27]'),
        (0x1B9559, 'movaps', 'xmmword ptr [rdi + 0x340], xmm0'),
        (0x1B9560, 'movaps', 'xmm1, xmmword ptr [rcx + 0x80]'),
        (0x1B9577, 'movaps', 'xmmword ptr [rdi + 0x350], xmm1'),
        (0x1B957E, 'movaps', 'xmm0, xmmword ptr [rcx + 0x90]'),
        (0x1B958D, 'movaps', 'xmmword ptr [rdi + 0x360], xmm0'),
        (0x1B9594, 'movaps', 'xmm1, xmmword ptr [rcx + 0xa0]'),
        (0x1B95A3, 'movaps', 'xmmword ptr [rdi + 0x370], xmm1'),
    ], 'viewport update: view vp+0x340..0x37f = camera+0x70..0xaf'),
    expect(0x1B95AA, 0xE7, [
        (0x1B95AA, 'movd', 'xmm7, dword ptr [rdi + 0x5d8]'),
        (0x1B95B2, 'movd', 'xmm1, dword ptr [rdi + 0x5dc]'),
        (0x1B95BA, 'mov', 'rax, qword ptr [rdi + 0x570]'),
        (0x1B95D6, 'movss', 'xmm9, dword ptr [rax + 0x118]'),
        (0x1B95DF, 'mulss', 'xmm0, dword ptr [rip + 0x1f386c9]'),
        (0x1B95F1, 'movss', 'xmm10, dword ptr [rax + 0x11c]'),
        (0x1B95FA, 'mulss', 'xmm8, xmm2'),
        (0x1B95FF, 'comiss', 'xmm0, xmm8'),
        (0x1B9603, 'jbe', '0x1b9628'),
        (0x1B9605, 'movss', 'xmm7, dword ptr [rdi + 0x5e0]'),
        (0x1B960D, 'divss', 'xmm8, xmm0'),
        (0x1B9612, 'xorps', 'xmm8, xmmword ptr [rip + 0x1eeeb26]'),
        (0x1B961A, 'mulss', 'xmm7, xmm2'),
        (0x1B961E, 'mulss', 'xmm7, dword ptr [rip + 0x1f38686]'),
        (0x1B9628, 'mulss', 'xmm7, dword ptr [rdi + 0x5e0]'),
        (0x1B9630, 'movaps', 'xmm8, xmm3'),
        (0x1B9634, 'divss', 'xmm7, xmm1'),
        (0x1B9638, 'movss', 'xmm2, dword ptr [rax + 0x168]'),
        (0x1B9640, 'movss', 'xmm0, dword ptr [rax + 0x16c]'),
        (0x1B9648, 'movss', 'xmm6, dword ptr [rip + 0x1eedc7c]'),
        (0x1B9650, 'lea', 'rcx, [rdi + 0x300]'),
        (0x1B9657, 'movss', 'xmm1, dword ptr [rax + 0x10c]'),
        (0x1B965F, 'movss', 'dword ptr [rsp + 0x48], xmm0'),
        (0x1B9665, 'movss', 'dword ptr [rsp + 0x40], xmm2'),
        (0x1B966B, 'movaps', 'xmm2, xmm8'),
        (0x1B966F, 'movss', 'dword ptr [rsp + 0x38], xmm6'),
        (0x1B9675, 'movss', 'dword ptr [rsp + 0x30], xmm3'),
        (0x1B967B, 'movss', 'dword ptr [rsp + 0x28], xmm10'),
        (0x1B9682, 'movaps', 'xmm3, xmm7'),
        (0x1B9685, 'movss', 'dword ptr [rsp + 0x20], xmm9'),
        (0x1B968C, 'call', '0x241b00'),
    ], 'viewport update: aspect (16:9 clamp) and projection vp+0x300 = 0x241b00(p, b, c, shift, -1, 1, near, far)'),
    expect(0x1B9691, 0x93, [
        (0x1B9698, 'lea', 'rcx, [rdi + 0x280]'),
        (0x1B9708, 'xorps', 'xmm0, xmm0'),
        (0x1B970B, 'movss', 'dword ptr [rsp + 0x30], xmm0'),
        (0x1B971F, 'call', '0x241b00'),
    ], 'viewport update: reversed-Z projection vp+0x280 (depth argument 0)'),
    expect(0x1B9724, 0x123, [
        (0x1B9724, 'movaps', 'xmm2, xmmword ptr [rdi + 0x340]'),
        (0x1B972B, 'movaps', 'xmm7, xmmword ptr [rdi + 0x310]'),
        (0x1B9732, 'movaps', 'xmm6, xmmword ptr [rdi + 0x300]'),
        (0x1B9739, 'movaps', 'xmm9, xmmword ptr [rdi + 0x330]'),
        (0x1B9741, 'movaps', 'xmm8, xmmword ptr [rdi + 0x320]'),
        (0x1B975B, 'shufps', 'xmm10, xmm2, 0'),
        (0x1B976F, 'mulps', 'xmm10, xmm6'),
        (0x1B9776, 'movaps', 'xmm2, xmmword ptr [rdi + 0x350]'),
        (0x1B97AF, 'movaps', 'xmm2, xmmword ptr [rdi + 0x360]'),
        (0x1B97F0, 'movaps', 'xmm2, xmmword ptr [rdi + 0x370]'),
        (0x1B97F7, 'movaps', 'xmmword ptr [rdi + 0x380], xmm10'),
        (0x1B9802, 'movaps', 'xmmword ptr [rdi + 0x390], xmm5'),
        (0x1B9817, 'movaps', 'xmmword ptr [rdi + 0x3a0], xmm4'),
        (0x1B9840, 'movaps', 'xmmword ptr [rdi + 0x3b0], xmm3'),
    ], 'viewport update: VP rows vp+0x380..0x3bf = view rows x projection (row vectors)'),
    expect(0x241B00, 0x232, [
        (0x241B00, 'mov', 'rax, rsp'),
        (0x241B03, 'sub', 'rsp, 0x88'),
        (0x241CB2, 'movaps', 'xmmword ptr [rcx], xmm6'),
        (0x241CC0, 'movaps', 'xmmword ptr [rcx + 0x10], xmm4'),
        (0x241D01, 'movaps', 'xmmword ptr [rcx + 0x20], xmm3'),
        (0x241D26, 'movaps', 'xmmword ptr [rcx + 0x30], xmm2'),
        (0x241D2A, 'add', 'rsp, 0x88'),
        (0x241D31, 'ret', ''),
    ], 'projection builder (leaf, emulated below)'),
    expect(0x4380AF, 0xA, [
        (0x4380AF, 'mov', 'dl, 1'),
        (0x4380B1, 'mov', 'rcx, rsi'),
        (0x4380B4, 'call', '0x1b9490'),
    ], 'CameraSelector update calls the viewport update'),
    expect(0x437C24, 0x27, [
        (0x437C24, 'lea', 'rcx, [r15 + 0x58]'),
        (0x437C2B, 'call', '0x16090'),
        (0x437C44, 'mov', 'rdi, qword ptr [rsi + 0x570]'),
    ], 'CameraSelector update stores the viewport handle at sel+0x58 and reads its camera'),
    expect(0x1B7F19, 0x14, [
        (0x1B7F23, 'lea', 'rax, [rip + 0x1f39cde]'),
        (0x1B7F2A, 'mov', 'qword ptr [rdi], rax'),
    ], 'GrViewport constructor stores vtable 0x20f1c08'),
    expect(0x1B829A, 0xA, [
        (0x1B829A, 'mov', 'dword ptr [rdi + 0x5e0], 0x3f800000'),
    ], 'GrViewport constructor: pixel aspect 1.0'),
    expect(0x1B83A9, 0x3A, [
        (0x1B83A9, 'movss', 'dword ptr [rdi + 0x584], xmm7'),
        (0x1B83B1, 'movss', 'dword ptr [rdi + 0x588], xmm6'),
        (0x1B83C2, 'subss', 'xmm0, xmm7'),
        (0x1B83C6, 'movss', 'dword ptr [rdi + 0x58c], xmm0'),
        (0x1B83D7, 'subss', 'xmm1, xmm6'),
        (0x1B83DB, 'movss', 'dword ptr [rdi + 0x590], xmm1'),
    ], 'GrViewport constructor: rect x0 +0x584, y0 +0x588, width +0x58c = x1-x0, height +0x590 = y1-y0'),
    expect(0x1B8459, 0x2E, [
        (0x1B8462, 'mov', 'dword ptr [rdi + 0x5d8], eax'),
        (0x1B8468, 'mulss', 'xmm2, dword ptr [rdi + 0x590]'),
        (0x1B8481, 'mov', 'dword ptr [rdi + 0x5dc], eax'),
    ], 'GrViewport constructor: pixel width +0x5d8 and height +0x5dc'),
    expect(0x6EEF98, 0x10, [
        (0x6EEF98, 'call', '0x1b7ee0'),
        (0x6EEF9E, 'lea', 'rax, [rip + 0x1afaec3]'),
        (0x6EEFA5, 'mov', 'qword ptr [rdi], rax'),
    ], 'TppSubViewport (mirrors, photos) overrides the vtable to 0x21e9e68: refused'),
    expect(0x1B38C1, 0xA, [
        (0x1B38C1, 'lea', 'rax, [rip + 0x1f3d000]'),
        (0x1B38C8, 'mov', 'qword ptr [rbx], rax'),
    ], 'GrCamera constructor (single factory 0x1b3bfa) stores vtable 0x20f08c8'),
    expect(0x1B392C, 0xA, [
        (0x1B392C, 'mov', 'dword ptr [rbx + 0x100], 0x18'),
    ], 'GrCamera constructor: flags 0x18 (bit 2 clear: perspective)'),
    expect(0x1B3A03, 0x14, [
        (0x1B3A03, 'mov', 'dword ptr [rbx + 0x168], 0x3d4ccccd'),
        (0x1B3A0D, 'mov', 'dword ptr [rbx + 0x16c], 0x457a0000'),
    ], 'GrCamera constructor: near +0x168 = 0.05, far +0x16c = 4000'),
    expect(0x1B3BF7, 0x8, [
        (0x1B3BFA, 'call', '0x1b38a0'),
    ], 'GrCamera factory'),
    data_site(0x20F1CAC, 0xC, lambda b: struct.unpack('<3f', b) == (struct.unpack('<f', struct.pack('<f', 1 / 9))[0], 9.0, 16.0),
              'aspect constants 1/9, 9, 16 (0x1b95df, 0x1b9549, 0x1b961e)'),
    data_site(0x20A8080, 0x4, lambda b: struct.unpack('<f', b)[0] == -1.0, 'b = -1 for aspects up to 16:9; depth argument of vp+0x300'),
    data_site(0x20A72CC, 0x4, lambda b: struct.unpack('<f', b)[0] == 1.0, 'constant 1.0 (projection argument)'),
    data_site(0x20A8140, 0x10, lambda b: struct.unpack('<4I', b)[0] == 0x80000000, 'sign mask (b = -(16h)/(9w) above 16:9)'),
]
SITES = [s for s in SITES if s]

# RIP-relative targets the rows above name.
for address, target, description in [
    (0x9C236C, 0x2BA1668, 'player update selector slot'), (0x438A7C, 0x2BA1668, 'GetMainInstance selector slot'),
    (0x1B7F23, 0x20F1C08, 'viewport vtable'), (0x6EEF9E, 0x21E9E68, 'sub viewport vtable'), (0x1B38C1, 0x20F08C8, 'camera vtable'),
    (0x1B9549, 0x20F1CB4, '16.0'), (0x1B9551, 0x20A8080, '-1.0'), (0x1B95DF, 0x20F1CB0, '9.0'), (0x1B961E, 0x20F1CAC, '1/9'),
    (0x1B9612, 0x20A8140, 'sign mask'), (0x1B9648, 0x20A72CC, '1.0'),
]:
    if rip_target(address) != target:
        failures.append(f'{description}: rip target of {address:#x} is {rip_target(address)}, expected {target:#x}')
if section_of(0x2BA1668) != '.data2':
    failures.append('selector slot 0x2ba1668 is not in .data2')

# --- 0x241b00 emulation -----------------------------------------------------
mdd = Cs(CS_ARCH_X86, CS_MODE_64)
mdd.detail = True


class Emu:
    """Straight-line SSE subset (exactly what 0x241b00 uses); any other
    instruction stops the check."""
    def __init__(self):
        self.x = {f'xmm{i}': [0.0] * 4 for i in range(16)}
        self.g = {r: 0 for r in ('rax', 'rcx', 'rdx', 'rsp')}
        self.mem = {}
        self.g['rsp'] = 0x7000_0000

    @staticmethod
    def f(value):
        return struct.unpack('<f', struct.pack('<f', value))[0]

    def rb(self, address, n):
        out = bytearray()
        for i in range(n):
            a = address + i
            if a in self.mem:
                out.append(self.mem[a])
            elif 0 <= a < 0x10000000:
                out += rd(a, 1)
            else:
                out.append(0)
        return bytes(out)

    def wb(self, address, data):
        for i, b in enumerate(data):
            self.mem[address + i] = b

    def address(self, ins, op):
        m = op.mem
        a = m.disp
        if m.base:
            name = ins.reg_name(m.base)
            a += ins.address + ins.size if name == 'rip' else self.g[name]
        if m.index:
            raise RuntimeError('indexed operand')
        return a

    def get(self, ins, op, n=16):
        if op.type == X86_OP_REG:
            return list(self.x[ins.reg_name(op.reg)])
        data = self.rb(self.address(ins, op), n)
        values = list(struct.unpack(f'<{n // 4}f', data))
        return values + [0.0] * (4 - len(values))

    def put(self, ins, op, v, n=16):
        v = [self.f(a) for a in v]
        if op.type == X86_OP_REG:
            self.x[ins.reg_name(op.reg)] = v
        else:
            self.wb(self.address(ins, op), struct.pack(f'<{n // 4}f', *v[:n // 4]))

    def run(self, rva):
        pc = rva
        for _ in range(400):
            ins = next(mdd.disasm(rd(pc, 16), pc))
            mn, ops = ins.mnemonic, ins.operands
            if mn == 'ret':
                return
            if mn == 'sub' and ins.reg_name(ops[0].reg) == 'rsp':
                self.g['rsp'] -= ops[1].imm
            elif mn == 'add' and ops[0].type == X86_OP_REG and ins.reg_name(ops[0].reg) == 'rsp':
                self.g['rsp'] += ops[1].imm
            elif mn == 'mov' and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG:
                self.g[ins.reg_name(ops[0].reg)] = self.g[ins.reg_name(ops[1].reg)]
            elif mn == 'movaps':
                self.put(ins, ops[0], self.get(ins, ops[1]))
            elif mn == 'movss':
                if ops[1].type == X86_OP_MEM:
                    self.put(ins, ops[0], [self.get(ins, ops[1], 4)[0], 0.0, 0.0, 0.0])
                elif ops[0].type == X86_OP_MEM:
                    self.put(ins, ops[0], self.get(ins, ops[1]), 4)
                else:
                    d = self.get(ins, ops[0]); d[0] = self.get(ins, ops[1])[0]; self.put(ins, ops[0], d)
            elif mn == 'xorps':
                if ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
                    self.put(ins, ops[0], [0.0] * 4)
                else:
                    a = struct.unpack('<4I', struct.pack('<4f', *self.get(ins, ops[0])))
                    b = struct.unpack('<4I', struct.pack('<4f', *self.get(ins, ops[1])))
                    self.put(ins, ops[0], list(struct.unpack('<4f', struct.pack('<4I', *[x ^ y for x, y in zip(a, b)]))))
            elif mn in ('mulps', 'addps', 'subps'):
                a, b = self.get(ins, ops[0]), self.get(ins, ops[1])
                op = {'mulps': lambda x, y: x * y, 'addps': lambda x, y: x + y, 'subps': lambda x, y: x - y}[mn]
                self.put(ins, ops[0], [op(x, y) for x, y in zip(a, b)])
            elif mn in ('mulss', 'addss', 'subss', 'divss'):
                a = self.get(ins, ops[0]); b = self.get(ins, ops[1], 4 if ops[1].type == X86_OP_MEM else 16)
                a[0] = {'mulss': a[0] * b[0], 'addss': a[0] + b[0], 'subss': a[0] - b[0],
                        'divss': a[0] / b[0] if b[0] else float('inf')}[mn]
                self.put(ins, ops[0], a)
            elif mn == 'unpcklps':
                a, b = self.get(ins, ops[0]), self.get(ins, ops[1])
                self.put(ins, ops[0], [a[0], b[0], a[1], b[1]])
            elif mn == 'shufps':
                a, b, i = self.get(ins, ops[0]), self.get(ins, ops[1]), ops[2].imm
                self.put(ins, ops[0], [a[i & 3], a[(i >> 2) & 3], b[(i >> 4) & 3], b[(i >> 6) & 3]])
            else:
                raise RuntimeError(f'unsupported instruction at {pc:#x}: {mn} {ins.op_str}')
            pc += ins.size
        raise RuntimeError('0x241b00 did not return')


def projection(p, b, c, sx, sy, depth, one, near, far):
    e = Emu()
    out = 0x6000_0000
    e.g['rcx'] = out
    e.x['xmm1'][0], e.x['xmm2'][0], e.x['xmm3'][0] = e.f(p), e.f(b), e.f(c)
    caller = e.g['rsp']
    e.g['rsp'] = caller - 8   # the call pushed the return address
    for i, v in enumerate((sx, sy, depth, one, near, far)):
        e.wb(caller + 0x20 + 8 * i, struct.pack('<f', v) + b'\0\0\0\0')
    e.run(0x241B00)
    return [list(struct.unpack('<4f', e.rb(out + 16 * r, 16))) for r in range(4)]


def clip(m, v):
    return [sum(v[i] * m[i][j] for i in range(4)) for j in range(4)]


EMULATION = []
near, far = 0.05, 4000.0
for name, w, h in (('16:9', 1920, 1080), ('21:9', 2560, 1080), ('4:3', 1600, 1200)):
    # 0x1b95aa..0x1b9638 for pixel aspect 1.
    if w * 9.0 > h * 16.0:
        b, c = -(h * 16.0) / (w * 9.0), 16.0 / 9.0
    else:
        b, c = f32(0x20A8080), w * 1.0 / h
    for p, sx, sy in ((1.2, 0.0, 0.0), (0.9, 0.03, -0.02)):
        m = projection(p, b, c, sx, sy, f32(0x20A8080), f32(0x20A72CC), near, far)
        r = projection(p, b, c, sx, sy, 0.0, f32(0x20A72CC), near, far)
        ok = True
        tol = 1e-5
        shape = [(m[0][0], p * b), (m[1][1], p * c), (m[2][0], sx), (m[2][1], sy), (m[2][3], 1.0), (m[3][3], 0.0),
                 (m[0][1], 0.0), (m[0][2], 0.0), (m[0][3], 0.0), (m[1][0], 0.0), (m[1][2], 0.0), (m[1][3], 0.0),
                 (m[3][0], 0.0), (m[3][1], 0.0)]
        for got, want in shape:
            ok = ok and abs(got - want) <= tol * max(1.0, abs(want))
        zn, zf = clip(m, [0, 0, near, 1]), clip(m, [0, 0, far, 1])
        rn, rf = clip(r, [0, 0, near, 1]), clip(r, [0, 0, far, 1])
        depth = (zn[2] / zn[3], zf[2] / zf[3], rn[2] / rn[3], rf[2] / rf[3])
        ok = ok and abs(depth[0] - 1) < 1e-3 and abs(depth[1] + 1) < 1e-3 and abs(depth[2] - 1) < 1e-3 and abs(depth[3]) < 1e-3
        ok = ok and zn[3] == near and zf[3] == far   # clip.w = view z
        EMULATION.append(dict(aspect=name, p=p, b=b, c=c, shift=[sx, sy], ok=ok,
                              row0=m[0], row1=m[1], row2=m[2], row3=m[3], depth_near_far=list(depth)))
        if not ok:
            failures.append(f'0x241b00 emulation {name} p={p}: unexpected projection {m} depth {depth}')

HEADER = [
    '// Generated by tools/generate_nametag_contract.py from the fingerprinted',
    f'// executable (SHA-256 {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}). Do not edit by hand.',
    '// Partner name tag: read-only camera chain (no game call, no write).',
    '#pragma once',
    '#include <cstddef>',
    '#include <cstdint>',
    'namespace mgscoop::nametag_contract {',
    '// --- Chain (RVAs; the image has no relocations) -----------------------------',
    'inline constexpr std::uintptr_t selector_slot_rva=0x2ba1668;   // CameraSelector main instance',
    'inline constexpr std::size_t selector_handle=0x58, handle_viewport=0x18;  // getter 0x438db0',
    'inline constexpr std::uintptr_t viewport_vtable_rva=0x20f1c08;  // GrViewport (ctor 0x1b7f23)',
    'inline constexpr std::uintptr_t sub_viewport_vtable_rva=0x21e9e68;  // TppSubViewport: refused',
    'inline constexpr std::uintptr_t camera_vtable_rva=0x20f08c8;    // GrCamera (ctor 0x1b38c1)',
    '// --- Viewport fields -----------------------------------------------------------',
    '// Projection +0x300, view +0x340 and view x projection +0x380 (4x4 float rows, row vectors);',
    '// camera +0x570; rect x0,y0,width,height +0x584; pixel size u32 +0x5d8/+0x5dc; pixel aspect +0x5e0.',
    'inline constexpr std::size_t vp_matrices=0x300, vp_matrices_size=0xc0;',
    'inline constexpr std::size_t vp_proj=0x0, vp_view=0x40, vp_viewproj=0x80;  // inside the matrices block',
    'inline constexpr std::size_t vp_tail=0x570, vp_tail_size=0x78;',
    'inline constexpr std::size_t vp_camera=0x0, vp_rect=0x14, vp_pixels=0x68, vp_pixel_aspect=0x70;  // inside the tail block',
    '// --- Camera fields ---------------------------------------------------------------',
    '// flags u32 +0x100 (bit 2 = non-perspective), focal +0x10c, lens shift +0x118/+0x11c, near +0x168, far +0x16c.',
    'inline constexpr std::size_t cam_block=0x100, cam_block_size=0x70;',
    'inline constexpr std::size_t cam_flags=0x0, cam_focal=0xc, cam_shift=0x18, cam_near=0x68, cam_far=0x6c;  // inside the block',
    'inline constexpr std::uint32_t cam_flag_nonperspective=0x4;',
    '// --- Expected bytes -------------------------------------------------------------',
]
for n, site in enumerate(SITES):
    HEADER.append(f'// {site["description"]}')
    HEADER.append(f'inline constexpr unsigned char site{n}[]={{' + ','.join(f'0x{b:02x}' for b in rd(site['rva'], site['size'])) + '};')
HEADER += ['struct Site {std::uintptr_t rva;std::size_t size;const unsigned char* bytes;};',
           'inline constexpr Site sites[]={']
for n, site in enumerate(SITES):
    HEADER.append(f'{{{site["rva"]:#x},sizeof(site{n}),site{n}}},')
HEADER += ['};', '}', '']
TEXT = '\n'.join(HEADER)

# Layout constants the C++ uses must match the offsets proven above.
expected_layout = {
    'vp_proj': 0x300 - 0x300, 'vp_view': 0x340 - 0x300, 'vp_viewproj': 0x380 - 0x300,
    'vp_rect': 0x584 - 0x570, 'vp_pixels': 0x5D8 - 0x570, 'vp_pixel_aspect': 0x5E0 - 0x570,
    'cam_focal': 0x10C - 0x100, 'cam_shift': 0x118 - 0x100, 'cam_near': 0x168 - 0x100, 'cam_far': 0x16C - 0x100,
}
for key, value in expected_layout.items():
    if f'{key}={value:#x}' not in TEXT:
        failures.append(f'header layout {key} != {value:#x}')

if failures:
    print('\n'.join(failures))
    raise SystemExit(1)
if '--check' in sys.argv:
    committed = OUT.read_text(encoding='utf-8').replace('\r\n', '\n') if OUT.exists() else ''
    if committed != TEXT:
        print(f'{OUT.relative_to(ROOT)} differs from the generator output: regenerate it (python tools/generate_nametag_contract.py)')
        raise SystemExit(1)
    print(f'nametag contract: {len(SITES)} sites match {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}; '
          f'0x241b00 emulation {len(EMULATION)} cases ok')
    raise SystemExit(0)

OUT.write_text(TEXT.replace('\n', '\r\n'), newline='')
report = dict(
    executable=str(EXE), executable_sha256=GAME_SHA256, generated_by='tools/generate_nametag_contract.py',
    sites=[dict(rva=hex(s['rva']), size=s['size'], sha256=hashlib.sha256(rd(s['rva'], s['size'])).hexdigest(),
                description=s['description']) for s in SITES],
    emulation=EMULATION,
    chain='sel=*(0x2ba1668); vp=*(*(sel+0x58)+0x18); cam=*(vp+0x570)',
    precedent='0x9c236c..0x9c2446 (player update, same function as the 0x9c2b51 input hook) reads the same fields '
              'without a lock on the Lua owner thread',
    limitations=['Static analysis only; no game code was executed.',
                 'The main viewport is built by protected code (0x3b759ba calls the GrViewport constructor); that it keeps '
                 'vtable 0x20f1c08 is a static inference: a different runtime vtable refuses (vp_vtable) and is logged in '
                 'the census line.',
                 'Whether the Lua tick runs before or after the camera update in a frame is unknown: the tag may trail a '
                 'fast camera turn by one frame.',
                 'No game raycast is verified: tags show through walls.'])
EVIDENCE.write_text(json.dumps(report, indent=2) + '\n')
print(f'Emitted {len(SITES)} sites to {OUT.relative_to(ROOT)}')
