"""Record the offline partner spike's player-instance contract. Disassembly only, no game execution.

Reads the fingerprinted installed executable (read-only) and writes
native/partner_player_contract.hpp. Every byte sequence and vtable slot the
native partner code relies on (docs/offline-partner-spike-plan.md 2.3 item 5,
plus the extra sites listed below) is re-read here and the generator refuses
on any difference, so a regenerated header always matches a re-verified
executable. The native code gate (partner_player.cpp CodeGate) checks the same
bytes and slots in the running image before any read or write is attempted.

Usage: python tools/generate_partner_contract.py [--check]
  --check  verify only; exit 1 on any mismatch, do not write the header.
"""
import hashlib
import struct
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parent.parent
EXE = Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
GAME_SHA256 = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
OUT = ROOT / 'native' / 'partner_player_contract.hpp'

raw = EXE.read_bytes()
if hashlib.sha256(raw).hexdigest() != GAME_SHA256:
    raise SystemExit('Executable fingerprint mismatch')
pe = pefile.PE(data=raw, fast_load=True)
IB = pe.OPTIONAL_HEADER.ImageBase
if IB != 0x140000000:
    raise SystemExit('Unexpected image base')


def rd(rva, n):
    data = pe.get_data(rva, n)
    if len(data) != n:
        raise SystemExit(f'Short read at {rva:#x}')
    return data


def section_of(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.rstrip(b'\0').decode()
    return None


# (rva, expected hex, description). Spec fingerprints first, then semantic sites.
SITES = [
    (0x9CEAE0, '4c8bdc5741574881eca8000000488b05', 'Warp (P vtable +0x1B0) prologue'),
    (0x9DCD20, '48895c2408574883ec20488bd9e89efb', 'Player.SetAllInstanceActiveMode prologue'),
    (0x121B5E0, '48895c24185556574154415541564157', 'realize policy prologue'),
    (0x12CC5F0, '48895c24184889742420574883ec200f', 'per-frame exists pre-update prologue'),
    (0x9D13F0, '40574883ec3048c7442420feffffff48', 'Player.CopyPartsInfoFromLocalPlayer prologue'),
    (0x9DF530, '405556574154415541564157488bec48', 'Player.SetPartsInfoAtInstanceIndex prologue'),
    (0xFDEEF0, '488b81e8010000c3', 'S getter: mov rax,[rcx+0x1E8]; ret'),
    (0x121B94B, '858194010000', 'policy: test [Q+0x194], 1<<i'),
    (0x121B953, '858198010000', 'policy: test [Q+0x198], 1<<i'),
    (0x12CC633, '418b89040200008bc1c1e80da8017473', 'pre-update: P+0x204 bit 13 (all-active) branch'),
    # Extra sites re-checked for this implementation.
    (0xAEC8D0, '488d055163f901c3', 'root getter: lea rax,[0x2A82C28]; ret (P = *(root+8) = *(0x2A82C30))'),
    (0x9DCD2D, 'e89efb100083caff488b7808', 'SetAllInstanceActiveMode: P = [root getter()+8]'),
    (0x9D6369, 'e862651100488b50084885d27421f6820402000001', 'Player.IsInstanceValid: P non-null and P+0x204 bit 0'),
    (0x12CC69D, '895f74899e70010000', 'pre-update writes the exists mask to Q+0x170'),
    (0x12CC6B6, 'c1e908f6c1010f849d000000', 'pre-update: P+0x204 bit 8 (S_IS_ONLINE) branch'),
    (0x12CC6CC, '498b89a8000000488b01ff90c8010000', 'S_IS_ONLINE path dereferences the synchronizer P+0xA8'),
    (0x9CEB4E, 'ff5010418bd7488b482048c1e204f30f113c0a', 'Warp writes (*(S+0x20))[idx], stride 16'),
    (0x9CEB7F, '488b4778', 'Warp reads pool P+0x78'),
    (0x9CEB8B, '410fb6140780faff', 'Warp: pool[idx] != 0xFF realized guard'),
    (0x9CEB9C, '488b4770', 'Warp reads ids P+0x70'),
    (0x9CEBB9, '420fb72c78', 'Warp: ids[idx] (u16)'),
    (0x9CEBBE, '4869f6a80600004803b778030000', 'Warp: blocks P+0x378, stride 0x6A8'),
    (0x9DCD45, '818f0402000000200000', 'SetAllInstanceActiveMode sets P+0x204 bit 0x2000'),
    (0x9D152E, '41098180010000', 'CopyPartsInfo sets bit idx in dword Q+0x180'),
    (0x12CCA34, '098794010000', 'online apply ORs dword Q+0x194'),
    (0x12CCCC9, '098798010000', 'online apply ORs dword Q+0x198'),
    (0x121B60A, '488b4108', 'policy: this+8 is P'),
    (0x121B614, '488b7060', 'policy: Q = P+0x60'),
    (0x121B8FF, '41385d10', 'policy: relax byte this+0x10 (area rectangle)'),
    (0x121B933, '41385d10', 'policy: relax byte this+0x10 (area callback)'),
    (0x121B5B5, '884830', 'policy factory zeroes the relax byte (object+0x30)'),
    (0x121B5C2, '488d0defeb1a0148894820', 'policy factory stores final interface vtable 0x23CA1B8 at object+0x20'),
    (0x121B4B0, '488d05a1ec1a0148894120', 'policy destructor writes the base vtable 0x23CA158 back (base = mid-destruction: refused)'),
    (0xBF7149, '488b4950488b01488bd3ff5010', 'realizer: policy = this+0x50, calls policy vtable +0x10'),
    (0x9C263F, '488b8fc0000000488b01ff5010', 'player update: realizer P+0xA0 vtable +0x10'),
    (0x9C5260, '488b0d915e2502e9f4eafeff', 'P vtable +0x1A0 guard wrapper uses global 0x2C1B0F8'),
    (0x9C5730, '488b0dc1592502e9a4e8feff', 'P vtable +0x198 wrapper uses global 0x2C1B0F8'),
    (0x9B3D60, '488b41104883b8b8000000007503b001c383b820010000020f94c0c3', 'slot guard: [[g+0x10]+0xB8]==0 or [+0x120]==2'),
    (0x9B3FE0, '833d0971260200', 'all-active realize helper reads dword 0x2C1B0F0'),
    (0x9B3FFF, '4c8b4910418b816c010000410381680100003bd0', 'parts-block slot bound +0x168 + +0x16C'),
    (0x9B4020, '418b84897c010000', 'parts-block flag +0x17C + 4*slot'),
    (0x9B3D93, '83bc91c010000003', 'parts-block state +0x10C0 + 4*slot == 3'),
    (0x1085088, '488b829000000080780103', 'mirror code: parts state P+0x90[1] == 3'),
    (0x9BD98A, '488986a0030000', 'allocation stores the realized bitset object at P+0x380'),
    (0xBF726A, '488d054ff57001488901', 'bitset object vtable 0x23067C0'),
    (0xBF72D7, '48895e10', 'bitset object data pointer at +0x10'),
    (0x99850C, '4c8b050de427024981c0ec000000', 'vars playerPosX = *(0x2C16920)+0xEC'),
    (0x998577, '4c8b05a2e327024981c0f0000000', 'vars playerPosY = *(0x2C16920)+0xF0'),
    (0x9985E2, '4c8b0537e327024981c0f4000000', 'vars playerPosZ = *(0x2C16920)+0xF4'),
    (0x53CA10, 'b8010000008bc92b05b37569024898', 'record writer bank (1 - [0x2BD3FD0])'),
    (0x53CA26, '488d0dc37569024869c0e0000000', 'record base 0x2BD3FF0, stride 0xE0'),
    (0x9DCD5C, '81a704020000ffdfffff', 'SetAllInstanceActiveMode(false) clears P+0x204 bit 0x2000; P is not null-checked'),
    (0x9DF320, '40534883ec2083caffe8922d030185c00f95c3e898d51000', 'Player.SetOtherInstanceIsForMirrorMode prologue'),
    (0x9DF338, '488b480884db741281890402000000400000', 'mirror: P = [root+8] (no null check); true ORs P+0x204 bit 0x4000'),
    (0x9DF352, '488b01ff5028', 'mirror(false): calls P vtable +0x28'),
    (0x9CCB40, '81a104020000ffbfffff488b0d2f242602e98abc0f00', 'P vtable +0x28: clears bit 0x4000, resets the engine mirror object *(0x2C2EF80)'),
    (0xAC87E0, '48895c24084889742410574883ec20488bf1488d99d0000000', 'mirror object reset dereferences the object without a null check'),
    (0xAC81B2, '48c705c36d160200000000', 'engine finalize clears the mirror object pointer 0x2C2EF80'),
    (0xAC821A, '4889055f6d1602', 'engine init stores the mirror object pointer 0x2C2EF80'),
    # --- Animation and appearance (docs/anim-appearance-findings-2026-10-04.md, partner-anim).
    # Q allocator 0xFDF010: n = *(u32*)(S+0x40); u16 Q+0x18, u8 Q+0x30/0x38, u8 Q+0x40..0x58, u16 Q+0x60, u8 Q+0x68.
    (0xFDEFE5, '488b4620488d4e20ff5010488bce8b5040', 'Q array build: n = *(u32*)(S+0x40) (S via Q.vt+0x10) before tail-calling 0xFDF010'),
    (0xFDF146, 'b902000000', 'Q allocator: Q+0x18 elements are 2-byte aligned (u16)'),
    (0xFDF185, '48894638', 'Q allocator stores the Q+0x18 array pointer (obj+0x38)'),
    (0xFDF22D, '48894650', 'Q allocator stores the Q+0x30 array pointer (u8, obj+0x50)'),
    (0xFDF265, '48894658', 'Q allocator stores the Q+0x38 array pointer (u8, obj+0x58)'),
    (0xFDF29D, '48894660', 'Q allocator stores the Q+0x40 type array pointer (u8, obj+0x60)'),
    (0xFDF353, 'b902000000', 'Q allocator: Q+0x60 face elements are 2-byte aligned (u16)'),
    (0xFDF392, '48898680000000', 'Q allocator stores the Q+0x60 face array pointer (obj+0x80)'),
    (0xFDF3CD, '48898688000000', 'Q allocator stores the Q+0x68 face-equip array pointer (u8, obj+0x88)'),
    (0x9DF6D2, '498b404044883406498b404844883c06498b405044882406498b405844882c06498b406066890c70',
     'SetPartsInfoAtInstanceIndex stores u8 Q+0x40/48/50/58 and u16 Q+0x60 for the index'),
    (0x9CD2D5, '443ba6340200000f8586140000', 'UpdatePartsStatus: the Quiet/Ocelot reset runs only for the local index (receiver needs no bypass)'),
    # Blocks (0x6A8 bytes) and the PS / CS components Warp walks.
    (0x9BAC04, '488d0505a68d01488903', 'block ctor stores vtable 0x2295210'),
    (0x9BAD39, '4889ab38010000897308897b0c', 'block ctor: +0x138 = P, +8 id count, +0xC base id'),
    (0x9BEA69, '488b9e98030000', 'block setup: blocks = *(E+0x398) = *(P+0x378)'),
    (0x9BEA93, '488b467049891c06', 'block setup: *(P+0x50)[k] = &blocks[k]'),
    (0x9BEAC0, '488b4670498b0c0648894e78', 'block setup: the block holding the local id is stored at P+0x58'),
    (0xFEDF58, '488d05912a3c01488903', 'PS ctor stores vtable 0x23B09F0'),
    (0xFF20FA, '488d056feb3b01488903', 'player PS subclass ctor (after base ctor 0xFEDEF0) stores vtable 0x23B0C70'),
    (0xFF5B10, '40534883ec20488b8198020000448bca4c8bd9442b4908', 'PS.vt+0 SetStance prologue: stance word *(PS+0x298)[id - PS+8]'),
    (0xFF5B51, '488b8108040000488b8838010000488b4160', 'SetStance: PS+0x408 -> blk, blk+0x138 -> P, P+0x60 -> Q'),
    (0xFF5B65, '488b403044880401', 'SetStance: u8 Q+0x30[id] = stance'),
    (0xFF5B6D, '498b8b60020000', 'SetStance: *(PS+0x260) sub-object (vt+0 called, result+0x64 written)'),
    (0xFF3220, '4883ec28488b8158060000488b40304885c07406488b4810eb0233c9488b01', 'PS.vt+0xC8 stance component getter: *(*(PS+0x658)+0x30)+0x10'),
    (0xFF323F, '48baba767a1e15dd0000ff5018', 'PS.vt+0xC8: component lookup by hash 0xDD151E7A76BA (vt+0x18)'),
    (0x9DB191, '488b4f58488b4978488bd84c8b0141ff90c80000008b9714020000448bc34c8b08488bc841ff5110',
     'RequestToSetTargetStance: X = *(P+0x58)->PS.vt+0xC8(); X.vt+0x10(X, P+0x214 index, stance)'),
    (0xAF07D7, '488d0592587f01488903', 'CS ctor stores vtable 0x22E6070'),
    (0xAF1130, '2b510c488b4110488d145248c1e206488b4402604885c074054883c010c3',
     'CS.vt+0x388 GetTransform: entry = *(CS+0x10)+(id-*(CS+0xC))*0xC0; T = *(entry+0x60)+0x10'),
    (0x9CEBD4, '488b46788bd5488b8838020000488b01ff9088030000488d5424204c8b00488bc8488bd841ff5070',
     'Warp: CS = *(*(blk+0x78)+0x238); T = CS.vt+0x388(ids[idx]); T.vt+0x70(T, &quat)'),
    (0x9CEBFC, '4c8b0b4c8d442420498bd6488bcb41ff5168', 'Warp: T.vt+0x68(T, pos, &quat)'),
    (0x9CEC0E, '488b8ec00000008bd5488b01ff9090000000488b8ea00000008bd5488b01ff9080000000',
     'Warp resets: *(blk+0xC0).vt+0x90(id), then *(blk+0xA0).vt+0x80(id)'),
    (0xFFACEA, 'ff90880300000f5c7820', 'plugin: T+0x20 is the body position'),
    (0x10BEF4E, 'ff9088030000488d4860e8a31313ff', 'plugin: T+0x60 is the body rotation quaternion (0x1F0300 yaw)'),
    # --- Round 3 (docs/partner-anim-appearance.md "Round 3"): the block-1 context window.
    (0x9C2810, '8b8f34020000488b87900000000fb70c48898f38020000', 'player update: P+0x218 = ids[P+0x214]'),
    (0x9C2A8E, '488b8fd0000000488b01ff10488b8fd8000000488b01ff9060020000', 'pad update then camera update, before the block loop'),
    (0x9C2B01, '8b9724020000c1ea124022d6', 'block loop: alternate dispatch bit P+0x204 0x40000 (sil = 1 at 0x9C2A42)'),
    (0x9C2A42, 'be01000000', 'player update: esi = 1 (the alternate dispatch selector mask)'),
    (0x9C2B30, '488b8798030000498d0c00', 'block loop: blocks = *(P+0x378)'),
    (0x9C2B40, '84d27517ba08000000498b8c0090010000e87a362300e9cc000000', 'default dispatch: call 0xBF61D0(*(blk+0x190), 8) at 0x9C2B51'),
    (0x9C2C27, '4c8b4424404981c0a8060000', 'block loop stride 0x6A8 (after the call only stack slots and rdi are used)'),
    (0xBF61D0, 'e9cbfeffff', 'jmp 0xBF60A0'),
    (0xBF60D0, '8b5f248b6f204c8b773803eb3bdd7314498b06448bc38bd6498bceff5020', 'node per-id loop: node+0x38->vt+0x20(phase, id)'),
    (0x10BED82, '2b7824488b41d88bcd488b50784869ff900300004903bfa0010000', 'activation: rec stride 0x390, plugin+0x1A0, node+0x24'),
    (0x10BEDB0, '488b46d8488b8838010000418bc43ba918020000498bcf0f94c03387ac02000083e0013187ac020000', 'activation: rec+0x2AC bit0 = (id == P+0x218)'),
    (0x10BED66, '488b41084c8d79d0', 'activation: this = plugin+0x30, node = *(plugin+0x38)'),
    (0x109DE4A, '488d05e7713101488903488d050572310148894310488d052a72310148894330488d055f72310148894348', 'base plugin vtables'),
    (0x109F61E, '48b8ba767a1e15dd0000483bd0741d', 'plugin QI hash 0xDD151E7A76BA'),
    (0x109F64A, '488d41f04885c0742c488d4138', 'plugin QI -> this+0x38'),
    (0x109F675, '4885c074054883c008c3', 'plugin QI: +8 (returns plugin+0x50)'),
    (0xFF3251, '4883c0f8', 'PS.vt+0xC8: X = QI - 8'),
    (0x9BDA6D, '4883c0f0', 'PP = QI - 0x10'), (0x9BDA76, '488986d0000000', 'P+0xB0 = PP'),
    (0x9CBD6B, '4883c0b0', 'CAM = QI - 0x50'), (0x9CBD74, '488986d8000000', 'P+0xB8 = CAM'),
    (0x1114024, '488d0525422a0148894320', 'CAM factory: vtable 0x23B8250 at object+0x20 (object 0x11A0 bytes)'),
    (0x120612C, '488981a0020000', 'PP+0x280 = P'), (0x120617F, '41b878000000', 'src acquire'), (0x1206194, '48894728', 'PP+8 = src'),
    (0x1205F00, '488b41088b504885d275068b91600400008b4128f7d023c2c3', 'PP vt+0x20'),
    (0x1205DB0, '488b41088b504c85d275068b91640400008b4128f7d023c2c3', 'PP vt+0x30'),
    (0x1205E80, '488b41088b505085d275068b91680400008b4128f7d023c2c3', 'PP vt+0x40'),
    (0x1205EE0, '488b41088b505485d275068b916c0400008b4128f7d023c2c3', 'PP vt+0x58'),
    (0x1205F40, '8b412c0fa3d073040f57c0c385d2751cf68188040000017409f30f108170040000c3488b4108f30f104058c3f68188040000027409f30f108174040000c3488b4108f30f104060c3', 'PP vt+0x68 X'),
    (0x1205FD0, '8b412c0fa3d073040f57c0c385d2751cf68188040000017409f30f108178040000c3488b4108f30f10405cc3f68188040000027409f30f10817c040000c3488b4108f30f104064c3', 'PP vt+0x70 Y'),
    (0x1206060, '85d2751ef6818804000004740af30f108180040000eb27488b4108f30f104068eb1cf6818804000008740af30f108184040000eb09488b4108f30f10406c8b41300f', 'PP vt+0x78 trigger'),
    (0x1205C30, '4883ec288b41280fa3d0730733c04883c428c3', 'PP vt+0x90 classifier mask exit'),
    (0x1205CC2, '488b4908488b014883c42848ff6010', 'PP vt+0x90 tail src->vt+0x10'),
    (0x1206BC3, '4c8b7e08498bcf498b07ff5038', 'PP update src->vt+0x38'),
    (0x1206BD7, '4c89a6600400004c89a668040000', 'PP update clears PP+0x460..0x46F'),
    (0x12070C4, '410f28c9410f28c2e839ef7f00', 'PP update: atan2f(xmm0 = LX, xmm1 = LY) through the IAT stub 0x1A0600A'),
    (0x12070F3, 'f3440f115e10f30f114614f3440f116e18', 'PP+0x10/+0x14/+0x18'),
    (0x1207220, 'f30f117e1cf30f114620f3440f114624', 'PP+0x1C/+0x20/+0x24'),
    (0x1206562, '488db9c0020000', 'button mask table PP+0x2C0'), (0x12065A1, '83f808', '8 entries'), (0x12065BA, '895e28', 'PP+0x28 = OR'),
    (0x12065DC, '488db900030000', 'stick mask table PP+0x300'), (0x1206662, '488db940030000', 'trigger mask table PP+0x340'),
    (0x1206BD0, '488b8e80030000', 'PP+0x380 is a pointer (vt+0x910 called): never touched'),
    # Direct motion (round 3 part D).
    (0x1198DD0, '4883c1d0e907000000', 'DM slot0 thunk'),
    (0x1198DEA, '488b41388bda440fb6c22b5824488b4108bf000000004869db5002000048035968488b8838010000', 'DM rec / P'),
    (0x1198E12, '8b8104020000488b4948488d5328c1e808a801440f44c7e8d2fcffff', 'DM comparator (patch 0x1198E25)'),
    (0x1198B06, 'f68188030000044c8bca488bd90f84c500000044384128', 'DM request compare'),
    (0x1198D56, 'c7820002000001000000', 'DM entry playback writes CAM+0x200'),
    (0x1198DB5, '488982f0010000', 'DM entry playback writes CAM+0x1F0 (qword)'),
    (0x1198D9D, '48c744244000000000488b442440c7820002000000000000', 'DM entry, no locator: CAM+0x1F0 = 0 and CAM+0x200 = 0'),
    (0x11992E9, '488981f00100004489b100020000', 'DM End writes CAM+0x1F0 = 0 and CAM+0x200 = 0 (any id)'),
    (0x11992FB, '80a088030000c3', 'DM End clears the pending g+0x388 bits 0x3C (global)'),
    # p218 diagnostic hazards: P+0x218 is paired with P+0x214 / the local block P+0x58.
    (0x11AD05D, '3b9018020000', 'cmp id, [P+0x218] ...'), (0x11AD072, '8bb014020000', '... then index [P+0x214] (host index 0)'),
    (0xFE8F85, '488b48588b9018020000', 'local block *(P+0x58)->vt+0x20 called with id [P+0x218]'),
    (0x1197D0E, '488d0593812201488901', 'DM primary vtable'), (0x1197D23, '488d05d6812201488941', 'DM iface vtable'),
    (0x9BCC3C, '498984c9b8030000', 'blk+0x3B8[idx] interfaces'), (0x9D2650, '488d05c9982402c3', 'g = 0x2C1BF20'),
    (0x9BA9BA, 'e8917c010048894668', 'P+0x48 = g'), (0x9DAC99, '800d0816240204', 'Play: g+0x388 |= 4'),
    # Snake-load fix (round 3 part L).
    (0x9B4368, '488dbbf0000000', 'rdi = desc+0xF0'), (0x9B442D, '4b89bce500110000', 'M+0x1100[slot] = desc+0xF0'),
    (0x9B42C1, '3883f0000000', 'descriptor look compare starts at desc+0xF0 (type)'),
    (0x9B47D0, '833d1969260200751f8b41183bd0750b488b491033d2e9c5f9ffff7302ffc2488b4910e9b8f9ffff', 'slot mapping'),
    (0xFDFD11, '488b4660c60402ff488b4668c60402ff488b4670c60402ff488b4678c60402ff', 'Q reset: look = 0xFF'),
    (0x9CD6CE, '4180f9ff0f84e2fbffff', 'state 0 waits while Q parts == 0xFF'),
    # --- Round 4 (docs/partner-anim-appearance.md "Round 4").
    # Body transform T (live census t_vt 0x24C8EF0): +0x58 sets the position only,
    # +0x60 the rotation, +0x68 = +0x58 then +0x60, +0x70 queues a rotation request.
    (0x1AD5660, '0f281a33c00f57c00f2959200f57d2f30f108960010000', 'T.vt+0x58: T+0x20 = pos (movaps), foot = pos - (0, T+0x160, 0)'),
    (0x1AD5680, '0f29591083a1a0010000fb0fc6db558981a4010000488981f0010000f30f119974010000c3',
     'T.vt+0x58: T+0x10 foot, T+0x1A0 &= ~4, T+0x1A4 = 0, T+0x1F0 = 0, T+0x174 = foot y; ret'),
    (0x1AD56B0, '0f28020f29416083a1a0010000fb48c781f001000000000000c3', 'T.vt+0x60: T+0x60 = quat'),
    (0x1AD56D0, '48895c2408574883ec20488b01498bd8488bf9ff5058488b07488bd3488bcfff5060', 'T.vt+0x68 = vt+0x58(pos) then vt+0x60(quat)'),
    (0x1AD4450, '0f28020f29818000000081897801000000010000c3', 'T.vt+0x70: T+0x80 = quat, T+0x178 |= 0x100 (pending rotation)'),
    # The game's per-id velocity (diagnostic read): PS+0x268[id - PS+8] = T+0x40 / (S+0x38[id] * S+0x48).
    (0xFF235E, 'ff90880300000f287040', 'PS per-id update: T = CS.vt+0x388(id); xmm6 = T+0x40'),
    (0xFF23BC, '488b87680200008bce2b4f088bd64803c90f2904c8', 'PS per-id update stores the velocity at *(PS+0x268) + (id - PS+8)*16'),
    # PP hold classifier and stance consumers (hold=own, words=hpr).
    (0x1205C43, 'f681880400001074', 'PP vt+0x90: PP+0x488 bit 0x10 selects the key-manager path, else src->vt+0x10'),
    (0x434ED0, '48895c240848897c24104c8bc9bf010000008bca448bd7458bd841d3e245855154',
     'src vt+0x10 classifier: bit = 1 << btn; released = src+0x54 & bit'),
    (0x434EF8, '45855148746f498b5108458b494033c94532c08bc14585c97427448512750685c9751eeb11ffc185c0450fb6c0440f44c7413bcb730bffc04883c204413bc172d9',
     'src vt+0x10 classifier: held src+0x48, history *(src+8), n = src+0x40, leading zeros skipped, run counted, newest = entry 0'),
    (0x434F39, '84db7415413bcb7310b802000000', 'src vt+0x10 classifier: released && count < frames -> 2'),
    (0x434F52, '4584c07416b803000000413bcb0f42c7', 'src vt+0x10 classifier: newest -> count < frames ? 1 : 3'),
    (0x10CE317, 'a808740f488b03488bcbff5040d1e8a8017520f68761030000080f85eafcffff488b03488bcbff5050d1e8a8010f84d7fcffff',
     'stance toggle: press edge vt+0x40 bit 1 when rec+0x361 bit 8, else release edge vt+0x50 bit 1'),
    (0x10CE021, '488b03ba01000000488bcb448d420bff909000000083f803', 'prone: PP vt+0x90(btn 1, 12 frames) == 3'),
    # PP key-manager slots (the window answers them from the injected words).
    (0x1206260, '4883ec284c8bc1488b89880300008bd2488b01498b94d090030000ff9048010000', 'PP vt+0xF8(idx): km vt+0x148(handle[idx])'),
    (0x1206290, '4883ec284c8bc1488b89880300008bd2488b01498b94d090030000ff90f8000000', 'PP vt+0xE8(idx): km vt+0xF8 (pressed)'),
    (0x12062C0, '488b018bd2488b94d19003000048ffa008010000', 'PP vt+0x100(idx): jmp PP vt+0x108(handle[idx])'),
    (0x12062E0, '4883ec28488b8988030000488b01ff9008010000', 'PP vt+0x108(handle): km vt+0x108 (held)'),
    (0x1206300, '4883ec284c8bc1488b89880300008bd2488b01498b94d090030000ff9018010000', 'PP vt+0xF0(idx): km vt+0x118 (released)'),
    (0x1206330, '488b018bd2488b94d19003000048ffa018010000', 'PP vt+0x110(idx): jmp PP vt+0x118(handle[idx])'),
    (0x1206350, '4883ec28488b8988030000488b01ff9088010000', 'PP vt+0x118(handle): km vt+0x188 (pressed, second set)'),
    (0x1206370, '488b018bd2488b94d19003000048ffa038010000', 'PP vt+0x130(idx): jmp PP vt+0x138(handle[idx])'),
    (0x1206390, '4883ec28488b8988030000488b01ff9098010000', 'PP vt+0x138(handle): km vt+0x198 (held, second set)'),
    (0x12063B0, '488b018bd2488b94d19003000048ffa028010000', 'PP vt+0x120(idx): jmp PP vt+0x128(handle[idx])'),
    (0x12063D0, '4883ec28488b8988030000488b01ff90a8010000', 'PP vt+0x128(handle): km vt+0x1A8 (released, second set)'),
    (0x1205DF0, '4883ec280f57d2418bc04c8bc9488b89880300008bd2498b94d190030000', 'PP vt+0x140(idx, frames): km classifier by handle[idx]'),
    (0x1205E1E, 'ff90b0010000', 'PP vt+0x140: km vt+0x1B0 (codes mapped 1->3, 2->1, 3->2)'),
    (0x1206C30, '488b06418bee488bce488b94ee90030000ff9008010000', 'PP update: binding i held through PP vt+0x108(PP+0x390[i]) -> bit (1 << i)'),
    (0x1206C17, '48b9989f16bfa0b80000', 'PP update: unbound handle 0xB8A0BF169F98 (48-bit compare)'),
    # PP fields instance-1 code writes (shadowed by the window).
    (0x1206540, '80a1b0040000f98089b004000010c3', 'PP vt+0x18: PP+0x4B0 &= ~6, |= 0x10 (rebind request)'),
    (0x1206A8A, 'f686b004000010', 'PP update: rebind when PP+0x4B0 bit 0x10'),
    (0x120714E, '4439a678020000', 'PP update: mouse look gated on PP+0x278'),
    (0x11FB528, '83a378020000fe', 'plugin: PP+0x278 &= ~1'),
    (0x1206A5F, 'f20f588170020000f20f118170020000', 'PP update: PP+0x270 += P+0x22C (frame time)'),
    # PP+0x14 is the world move heading src->vt+0x00(src, LX, LY).
    (0x12070D1, '498b07410f28d1498bcf410f28ca440f28e8ff10', 'PP update: PP+0x14 source = src->vt+0x00(src, LX, LY)'),
    # Camera request setters (any id; restored by the window's camera sandbox).
    (0x111A93D, '4183c802448981380d00008b028981800d0000', 'CAM vt+0xE0: CAM+0xD38 |= 2, CAM+0xD80..'),
    (0x111A727, '8389380d0000048b028981b00d0000', 'CAM vt+0xF8: CAM+0xD38 |= 4, CAM+0xDB0..'),
    (0x111A760, '8189380d0000000001008b028981d00d0000', 'CAM vt+0x100: CAM+0xD38 |= 0x10000, CAM+0xDD0..'),
    (0x111A8E7, '8bc248c1e0044803c8418b0089819c0e0000', 'CAM vt+0x128: CAM+0xE9C + 16k'),
    (0x10CDFCC, '0f298050010000', 'plugin: CAM+0x150 vector store'),
    # Other walks of the block trees (wrap census/window).
    (0xBF6420, '488bca418bd0e965feffff', 'UpdateTree service +0x28: (ctx, root, phase) -> active walker 0xBF6290'),
    (0xBF6430, 'e95bfeffff', 'jmp 0xBF6290 (active walker)'),
    (0xBF61C0, '488bca418bd0e9d5feffff', 'UpdateTree service +0x30: (ctx, root, phase) -> all-ids walker 0xBF60A0'),
    (0xBF61E0, '418bc0488bca458bc18bd0e950ffffff', 'UpdateTree service +0x40: (ctx, root, phase, id) -> per-id walker 0xBF6140'),
    (0xBF61F0, 'e94bffffff', 'jmp 0xBF6140 (per-id walker)'),
    (0xBF62F8, '488b471844850401750e498b06448bc38bd6498bceff5018', 'active walker: node vt+0x18(phase, id) for ids active and not paused'),
    (0x9C1D9B, 'ba07000000488bcfe888462300', 'player stage 0x9C1CD0: active walk of each block root, phase 7 (call 0x9C1DA3)'),
    (0x9C1AF4, '448bc6ba0b000000498bcfe8ec462300', 'player stage 0x9C1A30: per-id walk, phase 11 (call 0x9C1AFF)'),
    # --- Native puppet (docs/native-puppet.md): block 1's per-block job deferred
    # out of the parallel batch (H1, slot 0x2295420) and drained on the main
    # thread after the batch is joined (H2, call 0xBF4D55).
    (0xBF4D20, '40574883ec20488bb988000000', 'ticker vt+0x10: rdi = groups *(ticker+0x88)'),
    (0xBF4D40, '0fb7d6488bcfe8e5030000', 'ticker: 0xBF5130(group, index) (GO vt+0x48 / vt+0x60) before the batches'),
    (0xBF4D50, '8bd3488bcfe8c6010000ffc383fb0272ef', 'ticker: call 0xBF4F20(group, mode) at 0xBF4D55 for modes 0 and 1 (H2)'),
    (0xBF4D61, '488bcfe8a7000000', 'ticker: 0xBF4E10(group) (GO vt+0x58) after both batches'),
    (0xBF4F20, '8954241048894c2408535556574154415541564157 4883ec78'.replace(' ', ''), 'batch 0xBF4F20 prologue'),
    (0xBF4F53, '488b394c8b51084c2bd749c1fa03', 'batch: the group vector [*(group), *(group+8)) holds entry pointers'),
    (0xBF4FB4, '488d4c2430e8b2f443ff', 'batch: scratch scope ctor 0x34470(&[rsp+0x30]) before the jobs are built'),
    (0xBF4FD0, '488d4c2430488b442430ff5010', 'batch: the job array comes from the scope allocator (scope vt+0x10)'),
    (0xBF4FF0, '4c8d05e9147101', 'batch: job vtable 0x23064E0 (its only reference)'),
    (0xBF508C, '4d8970284189583041897834', 'batch: job+0x28 = entry, +0x30 = mode, +0x34 = block'),
    (0xBF50E0, '458bc6442bc341ffc84d6bc0384d03c5488d9424c0000000498bcce880dd56ffffc3413bde72d9',
     'batch: join every job in reverse (0x162E80)'),
    (0xBF5107, '488d4c2430e83ff443ff', 'batch: scope dtor 0x34550(&[rsp+0x30]) after the join'),
    (0x34204, '488bcfff10', 'join: a still-queued job runs inline on the waiter (job vt[0], no extra context)'),
    (0xBF4C64, '488b6928448b7930448b6134448b7514', 'job body: entry, mode, block, sub count entry+0x14'),
    (0xBF4C90, '488b452844897c24284489642420488944243 08b450c897424248944242c'.replace(' ', ''),
     'job body: param {block, sub, mode, nbits entry+0xC, bits entry+0x28} on the stack'),
    (0xBF4CB0, '8b5d1c83fbff74f8448d4301488d4d1c8bd3e8a9d142ff3bc375e5', 'job body: entry read lock (spin while -1, cmpxchg +1) per sub'),
    (0xBF4CCB, '488b4d204885c9741a488b41088b5038c1ea04f6c201740b', 'job body: GO = entry+0x20, gate [[GO+8]+0x38] bit 4'),
    (0xBF4CE3, '488b01488d542420ff5050', 'job body: GO vt+0x50(GO, &param); returns to 0xBF4CEE'),
    (0xBF4CEE, '488d4d1ce8a9d142ff', 'job body: entry read unlock 0x21EA0'),
    (0x21E70, '8bc2f0440fb101c3', 'read-lock helper: mov eax,edx; lock cmpxchg [rcx],r8d; ret'),
    (0x21EA0, '83c8fff00fc101c3', 'unlock helper: or eax,-1; lock xadd [rcx],eax; ret'),
    (0x34470, '48894c2408534883ec3048c7442420feffffff488bd9e865feffff', 'scope ctor 0x34470 -> 0x342F0 (push)'),
    (0x34550, '48894c24084883ec3848c7442420feffffff4883c438e945ffffff', 'scope dtor 0x34550 -> 0x344B0 (pop)'),
    (0x3432B, '8b153f77c30265488b042558000000b918000000488b3cd04803f9', 'scope push: slot = TLS[*(u32*)(0x2C6BA70)] + 0x18'),
    (0x34349, '48894310', 'scope push: this+0x10 = the previous scope'),
    (0x3450C, '8b0d5e75c30265488b042558000000ba18000000488b0cc8488b46104889040a', 'scope pop: slot = this+0x10'),
    (0x9C1B90, '4c8bdc564883ec40f6812402000001', 'per-block job 0x9C1B90: GO+0x224 = P+0x204 bit 0'),
    (0x9C1BA8, '837a04004d8963f0448b62080f850a010000', 'per-block job: sub != 0 returns; r12 = mode'),
    (0x9C1BC2, '8b3a4d896be84d8973e04d897bd84869ffa80600004803b998030000', 'per-block job: blk = *(GO+0x398) + block*0x6A8'),
    (0x9C1C15, '3b5e0c731c488b4610488bcd48c1e9030fb614010fb6cb83e107d3ea83e201eb05ba01000000413bd4',
     'per-block job: bit = id < nbits ? bits[id>>3]>>(id&7)&1 : 1, compared with mode'),
    (0x9C1C9C, '488b4f70458bc5418bd4488b01ff10', 'per-block job: driver *(blk+0x70) vt[0](mode, count)'),
    (0x9BB130, '488d0599a28d01488901488d054fa38d0148894120', 'P dtor: GO+0 = 0x22953D0, GO+0x20 = 0x2295490 (P = GO+0x20)'),
    (0x9BA754, '488d0575ac8d01488906', 'P ctor: GO+0 = 0x22953D0'),
    # Look angles (computed in PS phase 0, not phase 2) and the aim inputs.
    (0xFF2B40, '83fa060f87a7000000', 'PS comp vt+0x18 (comp = PS+0x3E8): phases 0..6'),
    (0xFF2B67, '4881c118fcffff8bd74883c4205fe956fdffff', 'PS phase 0: PS = comp-0x3E8; jmp 0xFF28D0'),
    (0xFF28C9, '4883c4205fc3cc', '0xFF2780 (phase 2) ends with ret at 0xFF28CE'),
    (0xFF28D0, '48896c2418564883ec208bf22b5108', 'PS phase-0 handler prologue (id - PS+8)'),
    (0xFF292A, '8b1401d1eaf6c2017432', 'PS phase 0: per-id record bit 1 skips the look computation'),
    (0xFF29F7, '8bd6488bcde8ff810000', 'PS phase 0: 0xFFAC00(PS, id), its only call'),
    (0xFFAD38, '2b5f08488b8f90020000', 'look store: index id - PS+8 into *(PS+0x290)'),
    (0xFFAD57, '488904d9', 'look store: {f32 pitch, f32 yaw} at stride 8'),
    (0xFEDF70, '488d05592c3c01488983e8030000', 'base PS ctor: [PS+0x3E8] = 0x23B0BD0 (vtable + 0x1E0)'),
    (0xFF2112, '488d0537ed3b01488983e8030000', 'player PS ctor: [PS+0x3E8] = 0x23B0E50 (vtable + 0x1E0)'),
    (0x1054210, '488b81b00000004532fff6402808740433c0eb090fb6807c0200002401',
     'gun eligibility: PP+0x28 & 8 ? 0 : PP+0x27C & 1'),
    (0x12072A6, '488b06488bceff5030c1e80333867c020000', 'HOLD latch writer: PP vt+0x30() >> 3 ...'),
    (0x12072C0, '83e00131867c020000', '... bit 0 only into PP+0x27C'),
    (0x10C68AF, '498b88b0000000f6412c010f85bd000000f30f107110', 'idle-to-move gate: PP+0x2C bit 0, PP+0x10'),
]
# The H1 slot itself (value checked as a slot below) and the patched sites:
# the code gate runs once per process before any lazy install.

# (vtable rva, slot offset, expected target rva).
SLOTS = [
    (0x2295490, 0x1B0, 0x9CEAE0, 'P vtable +0x1B0 = Warp'),
    (0x2295490, 0x1A0, 0x9C5260, 'P vtable +0x1A0 = slot guard wrapper'),
    (0x2295490, 0x198, 0x9C5730, 'P vtable +0x198 = all-active realize wrapper'),
    (0x23AD970, 0x08, 0xFDEEF0, 'Q vtable +0x08 = S getter'),
    (0x23AD970, 0x10, 0xFDEEF0, 'Q vtable +0x10 = S getter'),
    (0x23CA1B8, 0x10, 0x121B5E0, 'policy (final) vtable +0x10 = realize policy'),
    (0x23CA158, 0x10, 0x121B5E0, 'policy (base) vtable +0x10 = realize policy'),
    (0x2306730, 0x10, 0xBF7140, 'realizer vtable +0x10 = realizer update'),
    (0x2295490, 0x28, 0x9CCB40, 'P vtable +0x28 = mirror off'),
    (0x23B09F0, 0x00, 0xFF5B10, 'PS vtable +0x00 = SetStance'),
    (0x23B09F0, 0xC8, 0xFF3220, 'PS vtable +0xC8 = stance request component getter'),
    (0x23B0C70, 0x00, 0xFF5B10, 'PS subclass vtable +0x00 = SetStance (same as base)'),
    (0x23B0C70, 0xC8, 0xFF3220, 'PS subclass vtable +0xC8 = stance request component getter (same as base)'),
    (0x22E6070, 0x388, 0xAF1130, 'CS vtable +0x388 = GetTransform'),
    (0x23AD970, 0x18, 0xFE0080, 'Q vtable +0x18 = aim SetTarget (NOT a movement driver; never called)'),
    # Round 3: PlayerPad (42 slots, slot -1 = 0), base action plugin, camera, direct motion.
    (0x23C8D40, 0x00, 0x1206A00, 'PP vt+0x00 = update'),
    (0x23C8D40, 0x20, 0x1205F00, 'PP vt+0x20 = held'),
    (0x23C8D40, 0x30, 0x1205DB0, 'PP vt+0x30 = word src+0x4C'),
    (0x23C8D40, 0x40, 0x1205E80, 'PP vt+0x40 = word src+0x50'),
    (0x23C8D40, 0x58, 0x1205EE0, 'PP vt+0x58 = word src+0x54'),
    (0x23C8D40, 0x68, 0x1205F40, 'PP vt+0x68 = stick X'),
    (0x23C8D40, 0x70, 0x1205FD0, 'PP vt+0x70 = stick Y'),
    (0x23C8D40, 0x78, 0x1206060, 'PP vt+0x78 = trigger'),
    (0x23C8D40, 0x90, 0x1205C30, 'PP vt+0x90 = hold classifier'),
    (0x23C8D40, 0xB0, 0x1206550, 'PP vt+0xB0 = SetButtonMask'),
    (0x23C8D40, 0xB8, 0x1206410, 'PP vt+0xB8 = ResetButtonMask'),
    (0x23C8D40, 0xC0, 0x12065D0, 'PP vt+0xC0 = SetStickMask'),
    (0x23C8D40, 0xC8, 0x1206470, 'PP vt+0xC8 = ResetStickMask'),
    (0x23C8D40, 0xD0, 0x1206650, 'PP vt+0xD0 = SetTriggerMask'),
    (0x23C8D40, 0xD8, 0x12064E0, 'PP vt+0xD8 = ResetTriggerMask'),
    (0x23B5060, 0x18, 0x109F600, 'base plugin +0x10 vtable slot +0x18 = QI'),
    (0x23B5090, 0x08, 0x10BED50, 'base plugin +0x30 vtable slot +0x08 = activation'),
    (0x23B5090, 0x20, 0x645940, 'base plugin +0x30 vtable slot +0x20'),
    (0x23B8250, 0x00, 0x11142B0, 'CAM vt+0x00 = camera vector getter'),
    (0x23B8250, 0x260, 0x1119A30, 'CAM vt+0x260 = update (called at 0x9C2AA4)'),
    (0x23BFF00, 0x00, 0x1198DD0, 'DM iface slot 0 = consume thunk'),
    (0x23BFF00, 0x08, 0x119A350, 'DM iface slot 1 = Start'),
    (0x23BFF00, 0x20, 0x1198680, 'DM iface slot 4 = phase-8 per-id update'),
    # Round 4.
    (0x24C8EF0, 0x58, 0x1AD5660, 'T vtable +0x58 = set position (nudge)'),
    (0x24C8EF0, 0x60, 0x1AD56B0, 'T vtable +0x60 = set rotation'),
    (0x24C8EF0, 0x68, 0x1AD56D0, 'T vtable +0x68 = set pose'),
    (0x24C8EF0, 0x70, 0x1AD4450, 'T vtable +0x70 = rotation request'),
    (0x21534E0, 0x10, 0x434ED0, 'src vtable +0x10 = hold classifier (replicated by hold=own)'),
    (0x21534E0, 0x38, 0x435260, 'src vtable +0x38 = frame update (called by the PP update at 0x1206BC3)'),
    (0x23C8D40, 0x18, 0x1206540, 'PP vt+0x18 = rebind request'),
    (0x23C8D40, 0x98, 0x12063F0, 'PP vt+0x98 = keyboard mode (PP+0x488 bit 6)'),
    (0x23C8D40, 0xE8, 0x1206290, 'PP vt+0xE8 = key pressed (idx)'),
    (0x23C8D40, 0xF0, 0x1206300, 'PP vt+0xF0 = key released (idx)'),
    (0x23C8D40, 0xF8, 0x1206260, 'PP vt+0xF8 = km vt+0x148 (idx)'),
    (0x23C8D40, 0x100, 0x12062C0, 'PP vt+0x100 = key held (idx)'),
    (0x23C8D40, 0x108, 0x12062E0, 'PP vt+0x108 = key held (handle)'),
    (0x23C8D40, 0x110, 0x1206330, 'PP vt+0x110 = key pressed 2 (idx)'),
    (0x23C8D40, 0x118, 0x1206350, 'PP vt+0x118 = key pressed 2 (handle)'),
    (0x23C8D40, 0x120, 0x12063B0, 'PP vt+0x120 = key released 2 (idx)'),
    (0x23C8D40, 0x128, 0x12063D0, 'PP vt+0x128 = key released 2 (handle)'),
    (0x23C8D40, 0x130, 0x1206370, 'PP vt+0x130 = key held 2 (idx)'),
    (0x23C8D40, 0x138, 0x1206390, 'PP vt+0x138 = key held 2 (handle)'),
    (0x23C8D40, 0x140, 0x1205DF0, 'PP vt+0x140 = key hold classifier (idx, frames)'),
    (0x23B8250, 0xE0, 0x111A920, 'CAM vt+0xE0 = request setter (CAM+0xD80)'),
    (0x23B8250, 0xF8, 0x111A710, 'CAM vt+0xF8 = request setter (CAM+0xDB0)'),
    (0x23B8250, 0x100, 0x111A760, 'CAM vt+0x100 = request setter (CAM+0xDD0)'),
    (0x23B8250, 0x128, 0x111A8A0, 'CAM vt+0x128 = request setter (CAM+0xE9C)'),
    (0x2306660, 0x28, 0xBF6420, 'UpdateTree service vtable +0x28 = active walk (wrap)'),
    (0x2306660, 0x30, 0xBF61C0, 'UpdateTree service vtable +0x30 = all-ids walk (wrap)'),
    (0x2306660, 0x40, 0xBF61E0, 'UpdateTree service vtable +0x40 = per-id walk (wrap)'),
    # --- Native puppet.
    (0x22953D0, 0x48, 0x9C1F00, 'GO (P primary view) vt+0x48 = player update'),
    (0x22953D0, 0x50, 0x9C1B90, 'GO vt+0x50 = per-block job (H1 slot 0x2295420)'),
    (0x22953D0, 0x58, 0x9C1CD0, 'GO vt+0x58 = player stage (walk7)'),
    (0x22953D0, 0x60, 0x9C1A30, 'GO vt+0x60 = gate-closed update (walk11)'),
    (0x23064E0, 0x00, 0xBF4C50, 'batch job vtable slot 0 = job body'),
    (0x2306480, 0x10, 0xBF4D20, 'ticker vtable +0x10 = ticker'),
    (0x23B09F0, 0x1F8, 0xFF2B40, 'PS vtable +0x1F8 (comp +0x18) = per-id phase handler'),
    (0x23B0C70, 0x1F8, 0xFF2B40, 'player PS vtable +0x1F8 (comp +0x18) = per-id phase handler'),
    (0x23B3FA0, 0x00, 0x1053360, 'gun node callbacks [0] (activation -> eligibility 0x1054210)'),
    (0x23B3FA0, 0x08, 0x1099AB0, 'gun node callbacks [8]'),
    (0x23B3FA0, 0x10, 0x1061F50, 'gun node callbacks [0x10]'),
    (0x23B3FA0, 0x18, 0x1041000, 'gun node callbacks [0x18]'),
    (0x23B3FA0, 0x20, 0x10410A0, 'gun node callbacks [0x20]'),
    (0x2306660, 0x18, 0xBF6470, 'UpdateTree service vtable +0x18 = activation'),
    (0x2306660, 0x20, 0xBF6450, 'UpdateTree service vtable +0x20'),
    (0x23B8250, 0x40, 0x1112D50, 'CAM vt+0x40 (aim init: forwards to *(CAM+0x360) vt+0x50)'),
    (0x23B8250, 0x1A0, 0x1115400, 'CAM vt+0x1A0 (aim init: sub-controller lookup by hash)'),
]


def rel_refs(target, opcodes=(0xE8, 0xE9)):
    """Every E8/E9 rel32 in .xtext whose destination is target (rva)."""
    hits = []
    for s in pe.sections:
        if s.Name.rstrip(b'\0').decode() != '.xtext':
            continue
        data = s.get_data()
        va = s.VirtualAddress
        for op in opcodes:
            i = data.find(bytes([op]))
            while 0 <= i <= len(data) - 5:
                if va + i + 5 + struct.unpack_from('<i', data, i + 1)[0] == target:
                    hits.append(va + i)
                i = data.find(bytes([op]), i + 1)
    return sorted(hits)


def qword_hits(value):
    """Every occurrence of the absolute (image-based) qword in any section."""
    needle = struct.pack('<Q', IB + value)
    hits = []
    for s in pe.sections:
        data = s.get_data()
        i = data.find(needle)
        while i >= 0:
            hits.append(s.VirtualAddress + i)
            i = data.find(needle, i + 1)
    return sorted(hits)


def lea_rip_refs(target):
    """7-byte REX.W lea r64, [rip+disp32] in .xtext resolving to target (rva)."""
    hits = []
    for s in pe.sections:
        if s.Name.rstrip(b'\0').decode() != '.xtext':
            continue
        data = s.get_data()
        va = s.VirtualAddress
        i = data.find(b'\x8d')
        while 0 < i <= len(data) - 6:
            if data[i - 1] in (0x48, 0x4C) and (data[i + 1] & 0xC7) == 0x05:
                if va + (i - 1) + 7 + struct.unpack_from('<i', data, i + 2)[0] == target:
                    hits.append(va + i - 1)
            i = data.find(b'\x8d', i + 1)
    return sorted(hits)


# Reference and containment facts the native puppet relies on (docs/native-puppet.md).
def native_assertions():
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    need(rel_refs(0xBF4F20) == [0xBF4D55], f'0xBF4F20 single caller 0xBF4D55: {[hex(x) for x in rel_refs(0xBF4F20)]}')
    need(qword_hits(0x9C1B90) == [0x2295420], f'0x1409C1B90 occurs only at the H1 slot: {[hex(x) for x in qword_hits(0x9C1B90)]}')
    need(rel_refs(0x9C1B90, (0xE8, 0xE9)) == [], 'no direct call/jmp to 0x9C1B90')
    need(lea_rip_refs(0x22953D0) == [0x9BA754, 0x9BB130], f'two stores of 0x22953D0: {[hex(x) for x in lea_rip_refs(0x22953D0)]}')
    need(0x2295420 % 8 == 0, 'H1 slot 8-aligned')
    need((0xBF4D56 & 63) + 4 <= 64, 'H2 rel32 0xBF4D56 within one cache line')
    callers = rel_refs(0xFFAC00)
    need(callers == [0xFF29FC], f'0xFFAC00 single caller 0xFF29FC: {[hex(x) for x in callers]}')
    # Phase 2 (0xFF2780) ends at 0xFF28CE; 0xFF28D0 is the phase-0 handler and
    # contains the look call (no ret+int3 function padding before it).
    need(rd(0xFF28CE, 2) == b'\xc3\xcc', '0xFF28CE: ret; int3 (end of 0xFF2780)')
    body = rd(0xFF28D0, 0xFF29FC - 0xFF28D0)
    need(b'\xc3\xcc' not in body, 'no function boundary between 0xFF28D0 and 0xFF29FC')
    need(0xFF28D0 < 0xFF29FC < 0xFF2B40, 'the look call lies inside 0xFF28D0')
    need(rel_refs(0xFF28D0) == [0xFF2B75], f'0xFF28D0 reached only from the phase-0 jump 0xFF2B75: {[hex(x) for x in rel_refs(0xFF28D0)]}')
    jt = [struct.unpack('<i', rd(0xFF2BF4 + 4 * k, 4))[0] for k in range(7)]
    need(jt[0] == 0xFF2B67 and jt[2] == 0xFF2B7A, f'phase jump table: 0 -> 0xFF2B67, 2 -> 0xFF2B7A ({[hex(x) for x in jt]})')
    # The scope pair lies inside 0xBF4F20 (prologue at 0xBF4F20, ret at 0xBF5121).
    need(rd(0xBF5121, 1) == b'\xc3', '0xBF4F20 returns at 0xBF5121')
    need(0x34470 in [0xBF4FB9 + 5 + struct.unpack('<i', rd(0xBF4FBA, 4))[0]], 'scope ctor call target 0x34470')
    need(0x34550 in [0xBF510C + 5 + struct.unpack('<i', rd(0xBF510D, 4))[0]], 'scope dtor call target 0x34550')
    need(0xBF4F20 < 0xBF4FB9 < 0xBF510C < 0xBF5121, 'scope ctor/dtor pair inside 0xBF4F20')
    # 0xBF4CEE is the instruction after the job body's only vt+0x50 call.
    job = rd(0xBF4C50, 0xBF4D10 - 0xBF4C50)
    need(job.count(b'\xff\x50\x50') == 1 and 0xBF4C50 + job.find(b'\xff\x50\x50') + 3 == 0xBF4CEE,
         'the job body has exactly one ff 50 50 call, returning to 0xBF4CEE')
    need(rel_refs(0x34470) and len(rel_refs(0x34470)) >= 100 and len(rel_refs(0x34550)) >= 100,
         'the scope ctor/dtor pair is a common game idiom')
    return out

failures = []
for rva, expected, what in SITES:
    if section_of(rva) != '.xtext':
        failures.append(f'{rva:#x} not in .xtext ({what})')
        continue
    actual = rd(rva, len(expected) // 2).hex()
    if actual != expected:
        failures.append(f'{rva:#x} {what}: expected {expected}, found {actual}')
for vtable, offset, target, what in SLOTS:
    value = struct.unpack('<Q', rd(vtable + offset, 8))[0]
    if value != IB + target:
        failures.append(f'{vtable:#x}+{offset:#x} {what}: expected {IB + target:#x}, found {value:#x}')
    if section_of(target) != '.xtext':
        failures.append(f'slot target {target:#x} not in .xtext')
failures += native_assertions()
if failures:
    print('\n'.join(failures))
    raise SystemExit(1)
if '--check' in sys.argv:
    print(f'{len(SITES)} code sites, {len(SLOTS)} vtable slots and the native reference/containment assertions match {GAME_SHA256}')
    raise SystemExit(0)


def cbytes(hexstr):
    data = bytes.fromhex(hexstr)
    return ','.join(f'0x{b:02x}' for b in data)


lines = []
w = lines.append
w('// Generated by tools/generate_partner_contract.py from the fingerprinted')
w(f'// executable (SHA-256 {GAME_SHA256[:8]}...{GAME_SHA256[-4:]}). Do not edit by hand.')
w('// Offline partner spike: docs/offline-partner-spike-plan.md 2.3 item 5.')
w('#pragma once')
w('#include <cstddef>')
w('#include <cstdint>')
w('namespace mgscoop::partner_player_contract {')
w('// --- Pointer chain (RVAs; the image has no relocations) -------------------')
w('inline constexpr std::uintptr_t player_slot_rva=0x2a82c30;   // P = *(base+slot), secondary view')
w('inline constexpr std::uintptr_t p_vtable_rva=0x2295490;')
w('inline constexpr std::uintptr_t q_vtable_rva=0x23ad970;')
w('// The only constructor (factory 0x121b550) leaves the final interface vtable')
w('// 0x23ca1b8 at the policy pointer. 0x23ca158 is its base class, present only')
w('// while the object is being constructed or destroyed (destructors 0x121b490 /')
w('// 0x121b4f0 write it back): it is refused, only the final vtable is accepted.')
w('inline constexpr std::uintptr_t policy_vtable_rva=0x23ca1b8;')
w('inline constexpr std::uintptr_t policy_base_vtable_rva=0x23ca158;  // refused (construction/destruction)')
w('inline constexpr std::uintptr_t policy_function_rva=0x121b5e0;  // both policy vtables, slot +0x10')
w('inline constexpr std::uintptr_t realizer_vtable_rva=0x2306730;')
w('inline constexpr std::uintptr_t bitset_vtable_rva=0x23067c0;')
w('inline constexpr std::uintptr_t glob_suppress_rva=0x2c1b0f0;  // dword, compare-only in .xtext')
w('inline constexpr std::uintptr_t glob_parts_rva=0x2c1b0f8;     // pointer g')
w('inline constexpr std::uintptr_t vars_slot_rva=0x2c16920;      // pointer to vars')
w('inline constexpr std::uintptr_t record_bank_rva=0x2bd3fd0;    // int32 published bank (0/1)')
w('inline constexpr std::uintptr_t record_base_rva=0x2bd3ff0;')
w('// Engine mirror object (created/destroyed by the engine init/finalize chains')
w('// 0x991bf0/0x991970). Mirror off (P vtable +0x28 = 0x9ccb40) dereferences it')
w('// without a null check, so MIRROR_OFF is only sent while it is non-null.')
w('inline constexpr std::uintptr_t mirror_object_rva=0x2c2ef80;')
w('// --- P (player game object, secondary view) offsets -------------------------')
w('inline constexpr std::size_t p_q=0x60, p_ids=0x70, p_pool=0x78, p_parts=0x90, p_realizer=0xa0, p_sync=0xa8;')
w('inline constexpr std::size_t p_flags=0x204, p_count=0x208, p_realized_count=0x20c, p_local=0x214, p_bitset=0x380;')
w('inline constexpr std::uint32_t flag_valid=0x1;        // Player.IsInstanceValid')
w('inline constexpr std::uint32_t flag_online=0x100;     // S_IS_ONLINE: NEVER written by this project')
w('inline constexpr std::uint32_t flag_all_active=0x2000;')
w('inline constexpr std::uint32_t flag_mirror=0x4000;')
w('// --- Q (per-instance player data, *(P+0x60)) -------------------------------')
w('inline constexpr std::size_t q_exists_mask=0x170, q_parts_dirty=0x180, q_bits_a=0x194, q_bits_b=0x198, q_s=0x1e8;')
w('// Anim survey (read-only, [S] element types): arrays at +0x18/+0x28/+0x30/+0x38/+0xd8,')
w('// motion records +0x1d8 (stride 0x40), +0x1e0 (stride 0x20), +0x1f8 (stride 0x12, 0x11 bytes).')
w('inline constexpr std::size_t q_anim_18=0x18, q_anim_28=0x28, q_anim_30=0x30, q_anim_38=0x38, q_anim_d8=0xd8;')
w('inline constexpr std::size_t q_rec_1d8=0x1d8, q_rec_1d8_size=0x40, q_rec_1e0=0x1e0, q_rec_1e0_size=0x20;')
w('inline constexpr std::size_t q_rec_1f8=0x1f8, q_rec_1f8_size=0x11;')
w('// --- S (*(Q+0x1e8)) ----------------------------------------------------------')
w('inline constexpr std::size_t s_exists=0x0, s_local=0x8, s_pos=0x20, slot_stride=0x10;  // slot {x,y,z,yaw}')
w('// --- Realizer / policy ------------------------------------------------------')
w('inline constexpr std::size_t realizer_policy=0x50, policy_owner=0x8, policy_relax=0x10;')
w('inline constexpr std::size_t bitset_data=0x10;')
w('// --- Parts-block globals: g=*(0x2c1b0f8), x=*(g+0x10) ------------------------')
w('inline constexpr std::size_t g_x=0x10, g_local=0x18;')
w('inline constexpr std::size_t x_guard_ptr=0xb8, x_guard_mode=0x120, x_slot_count_a=0x168, x_slot_count_b=0x16c;')
w('inline constexpr std::size_t x_flag=0x17c, x_state=0x10c0;')
w('inline constexpr std::uint8_t parts_loaded_state=3;')
w('// --- Published player records and vars --------------------------------------')
w('inline constexpr std::size_t record_stride=0xe0, record_bank_entries=16, record_pos=0x10, record_status=0xc0;')
w('inline constexpr std::size_t vars_pos_x=0xec, vars_pos_y=0xf0, vars_pos_z=0xf4;')
w('// --- Animation / appearance arrays (Q allocator 0xfdf010; n = *(u32*)(S+0x40) = 2) ---')
w('inline constexpr std::size_t s_count=0x40;')
w('inline constexpr std::size_t q_look_type=0x40, q_look_parts=0x48, q_look_camo=0x50, q_look_hand=0x58;')
w('inline constexpr std::size_t q_look_face=0x60, q_look_face_equip=0x68;  // u16 face, u8 others')
w('// --- Blocks, PS and CS components (Warp 0x9ceae0 walks blk -> PS -> CS -> T) -----')
w('inline constexpr std::uintptr_t block_vtable_rva=0x2295210;')
w('inline constexpr std::uintptr_t ps_vtable_rva=0x23b09f0;')
w('inline constexpr std::uintptr_t ps_vtable_alt_rva=0x23b0c70;  // live player PS subclass (ctor 0xff20fa); slots 0..51 equal the base')
w('inline constexpr std::uintptr_t set_stance_rva=0xff5b10;     // PS.vt+0x00')
w('inline constexpr std::uintptr_t ps_component_rva=0xff3220;   // PS.vt+0xc8')
w('inline constexpr std::uintptr_t cs_vtable_rva=0x22e6070;')
w('inline constexpr std::uintptr_t get_transform_rva=0xaf1130;  // CS.vt+0x388')
w('inline constexpr std::uintptr_t code_begin_rva=0x1000, code_end_rva=0x208d000;  // .xtext')
w(f'inline constexpr std::uintptr_t image_span=0x{pe.OPTIONAL_HEADER.SizeOfImage:x};  // SizeOfImage')
w('// T (per-id body transform), A0 and C0 (Warp\'s reset targets): their classes')
w('// were not resolved statically. 0 = unpinned: a call needs the instance-1')
w('// vtable equal to the local instance\'s and slot targets inside .xtext. Pin the')
w('// RVAs from a reviewed partner_census line, then calls also need equality.')
w('inline constexpr std::uintptr_t t_vtable_rva=0, a0_vtable_rva=0, c0_vtable_rva=0;')
w('inline constexpr std::size_t p_block_table=0x50, p_local_block=0x58, p_equip=0xc8, p_blocks=0x378, block_stride=0x6a8;')
w('inline constexpr std::size_t blk_id_count=0x8, blk_base_id=0xc, blk_b70=0x70, blk_ps=0x78, blk_a0=0xa0, blk_c0=0xc0, blk_owner=0x138;')
w('inline constexpr std::size_t ps_base_id=0x8, ps_cs=0x238, ps_sub260=0x260, ps_rec280=0x280, ps_stance=0x298;')
w('inline constexpr std::size_t ps_byte2d0=0x2d0, ps_byte2d8=0x2d8, ps_blk=0x408, ps_comp=0x658, ps_rec738=0x738, ps_mask740=0x740;')
w('inline constexpr std::size_t ps_rec280_stride=0x20, ps_rec738_stride=0x28, comp_inner=0x30, comp_object=0x10;')
w('inline constexpr std::size_t cs_base_id=0xc, cs_entries=0x10, cs_entry_stride=0xc0, cs_entry_transform=0x60, transform_view=0x10;')
w('inline constexpr std::size_t t_pos=0x20, t_quat=0x60;')
w('inline constexpr std::size_t t_vt_set_pose=0x68, t_vt_set_rot=0x70, a0_vt_reset=0x80, c0_vt_reset=0x90, x_vt_request=0x10;')
w('inline constexpr std::size_t t_vt_set_position=0x58;  // round 4: position only (nudges)')
w('inline constexpr std::size_t ps_vel268=0x268, ps_vel268_stride=0x10;  // round 4: per-id velocity (diagnostic)')
w('// --- Round 3 (docs/partner-anim-appearance.md "Round 3") -------------------')
w('// P and block: g (script object 0x2c1bf20) = *(P+0x48); PP = *(P+0xb0); CAM = *(P+0xb8);')
w('// P+0x218 = ids[P+0x214] (0x9c2810); blocks P+0x210; the root of block k is *(blk_k+0x190).')
w('inline constexpr std::size_t p_g=0x48, p_pad=0xb0, p_cam=0xb8, p_local_id=0x218, p_block_count=0x210;')
w('inline constexpr std::uint32_t flag_alt_dispatch=0x40000;')
w('inline constexpr std::size_t blk_root=0x190, blk_iface=0x3b8, dm_iface_index=5;')
w('// PlayerPad PP (vtable 0x23c8d40, 42 slots, slot -1 = 0) and its raw pad src = *(PP+8).')
w('inline constexpr std::uintptr_t pp_vtable_rva=0x23c8d40;')
w('inline constexpr std::size_t pp_vtable_slots=42, pp_vt_hold=0x90;')
w('inline constexpr std::uintptr_t pp_hold_rva=0x1205c30;  // PP vt+0x90 (u32 fn(PP, btn, frames))')
w('inline constexpr std::size_t pp_owner=0x280, pp_src=0x8, pp_derived=0x10, pp_derived_size=0x18;')
w('inline constexpr std::size_t pp_masks=0x28, pp_masks_size=0xc, pp_mask_tables=0x2c0, pp_mask_tables_size=0xc0;')
w('inline constexpr std::size_t pp_kb=0x460, pp_kb_size=0x28, pp_kb_flags=0x488;')
w('inline constexpr std::uint8_t kb_flags_clear=0x5f;')
w('inline constexpr std::size_t src_words=0x48, src_floats=0x58, src_size=0x28;')
w('// Base action plugin (factory 0x109de10): obj = *(*(*(PS+0x658)+0x30)+0x10) == plugin+0x10.')
w('inline constexpr std::uintptr_t plugin_vt0_rva=0x23b5038, plugin_vt1_rva=0x23b5060, plugin_vt2_rva=0x23b5090, plugin_vt3_rva=0x23b50d0;')
w('inline constexpr std::size_t plugin_obj_off=0x10, plugin_blk=0x8, plugin_node=0x38, node_base=0x24, node_count=0x20;')
w('inline constexpr std::size_t plugin_recs=0x1a0, plugin_rec_stride=0x390, rec_local=0x2ac;')
w('// Camera CAM (vtable 0x23b8250); CAM+0x20c mode (0 = vt+0 returns CAM+0x120).')
w('inline constexpr std::uintptr_t cam_vtable_rva=0x23b8250;')
w('inline constexpr std::size_t cam_mode=0x20c;')
w('// DM entry/End write the camera connect handle (qword) and its on flag (dword); 0 when idle.')
w('inline constexpr std::size_t cam_connect=0x1f0, cam_connect_on=0x200;')
w('// The hooked call 0x9c2b51 (e8 rel32 -> 0xbf61d0).')
w('inline constexpr std::uintptr_t hook_block_rva=0x9c2b40, hook_site=0x9c2b51, hook_rel32=0x9c2b52, hook_next_ip=0x9c2b56;')
w('inline constexpr std::uint32_t hook_rel32_orig=0x0023367a;')
w('inline constexpr std::uintptr_t dispatch_rva=0xbf61d0;')
w('// Direct motion: prim vtable 0x23bfea8, iface (prim+0x30) vtable 0x23bff00 at blk+0x3b8+8*5.')
w('inline constexpr std::uintptr_t dm_prim_vt_rva=0x23bfea8, dm_iface_vt_rva=0x23bff00, g_rva=0x2c1bf20;')
w('inline constexpr std::size_t dm_iface_prim=0x30, dm_prim_blk=0x8, dm_prim_recs=0x68, dm_prim_node=0x38, dm_rec_stride=0x250;')
w('inline constexpr std::size_t g_flags=0x388, g_req=0x28, g_name=0x30;')
w('inline constexpr std::size_t q_act=0x18;  // u16 *(Q+0x18)[i]; 0x8b = direct motion')
w('inline constexpr std::uint16_t q18_dm_act=0x8b;')
w('inline constexpr std::uintptr_t dm_patch_rva=0x1198e25, dm_patch_block_rva=0x1198e12;')
w('inline constexpr std::uint32_t dm_patch_old=0xc7440f44, dm_patch_new=0x00401f0f;  // little-endian dwords of 44 0f 44 c7 / 0f 1f 40 00')
w('// Parts: the requested (descriptor) look of slot s is *(u64*)(M+0x1100+8*s) (0x9b442d).')
w('inline constexpr std::size_t x_req_look=0x1100;')
w('// --- Round 4 (docs/partner-anim-appearance.md "Round 4") -------------------')
w('// PP key-manager slots answered inside the window (by binding index or by handle).')
w('inline constexpr std::size_t pp_vt_rebind=0x18, pp_vt_kb_mode=0x98, pp_vt_km_hold=0x140;')
w('inline constexpr std::size_t pp_vt_km_press_idx=0xe8, pp_vt_km_release_idx=0xf0, pp_vt_km_148_idx=0xf8, pp_vt_km_held_idx=0x100;')
w('inline constexpr std::size_t pp_vt_km_held_h=0x108, pp_vt_km_press2_idx=0x110, pp_vt_km_press2_h=0x118, pp_vt_km_release2_idx=0x120;')
w('inline constexpr std::size_t pp_vt_km_release2_h=0x128, pp_vt_km_held2_idx=0x130, pp_vt_km_held2_h=0x138;')
w('inline constexpr std::uintptr_t pp_km_hold_rva=0x1205df0;  // PP vt+0x140')
w('inline constexpr std::size_t pp_bindings=0x390, pp_binding_count=26;')
w('// PP fields instance-1 code writes: the mouse-look gate (dword) and the rebind byte (shadowed).')
w('inline constexpr std::size_t pp_mouse_gate=0x278, pp_rebind=0x4b0;')
w('// The raw pad: src vtable 0x21534e0; +0x10 = 0x434ed0 is the hold classifier hold=own replicates.')
w('inline constexpr std::uintptr_t src_vtable_rva=0x21534e0, src_hold_rva=0x434ed0;')
w('// Other walks of the block trees (wrap=census|window): the UpdateTree service')
w('// vtable data slots (read-only data, swapped with VirtualProtect) and two rel32 calls.')
w('inline constexpr std::uintptr_t svc_vtable_rva=0x2306660;')
w('inline constexpr std::size_t svc_vt_active=0x28, svc_vt_all=0x30, svc_vt_id=0x40;')
w('inline constexpr std::uintptr_t svc_active_rva=0xbf6420, svc_all_rva=0xbf61c0, svc_id_rva=0xbf61e0;')
w('inline constexpr std::uintptr_t walk7_site=0x9c1da3, walk7_next_ip=0x9c1da8, walk7_target=0xbf6430;')
w('inline constexpr std::uint32_t walk7_rel32_orig=0x00234688;')
w('inline constexpr std::uintptr_t walk11_site=0x9c1aff, walk11_next_ip=0x9c1b04, walk11_target=0xbf61f0;')
w('inline constexpr std::uint32_t walk11_rel32_orig=0x002346ec;')
w('// --- Native puppet (docs/native-puppet.md) --------------------------------')
w('// H2: the only call of the batch 0xbf4f20(group, mode) at 0xbf4d55 (ticker 0xbf4d20, modes 0 then 1).')
w('inline constexpr std::uintptr_t ticker_vtable_rva=0x2306480;              // +0x10 = 0xbf4d20')
w('inline constexpr std::uintptr_t batch_block_rva=0xbf4d50, batch_site=0xbf4d55, batch_next_ip=0xbf4d5a;')
w('inline constexpr std::uint32_t batch_rel32_orig=0x000001c6;               // -> 0xbf4f20')
w('inline constexpr std::uintptr_t batch_target_rva=0xbf4f20, batch_join_rva=0xbf50e0;')
w('inline constexpr std::uintptr_t batch_scope_ctor_site=0xbf4fb4, batch_scope_dtor_site=0xbf5107;')
w('// H1: the per-block job slot (GO primary vtable +0x50; GO = P - 0x20).')
w('inline constexpr std::uintptr_t go_vtable_rva=0x22953d0, job_slot_rva=0x2295420, job_rva=0x9c1b90;')
w('inline constexpr std::uintptr_t job_vtable_rva=0x23064e0, job_body_rva=0xbf4c50, job_call_return_rva=0xbf4cee;')
w('inline constexpr std::uintptr_t job_entry_rva=0xbf4c64, job_param_rva=0xbf4c90, job_lock_rva=0xbf4cb0, job_gate_rva=0xbf4ccb;')
w('inline constexpr std::uintptr_t job_call_rva=0xbf4ce3, job_unlock_rva=0xbf4cee, lock_helper_rva=0x21e70, unlock_helper_rva=0x21ea0;')
w('inline constexpr std::uintptr_t job_prologue_rva=0x9c1b90, job_sub_rva=0x9c1ba8, job_blk_rva=0x9c1bc2, job_bit_rva=0x9c1c15;')
w('inline constexpr std::uintptr_t job_driver_rva=0x9c1c9c, inline_run_rva=0x34204;')
w('// The game\'s TLS scratch scope (0xbf4f20 pushes it around its batch; LIFO per thread).')
w('inline constexpr std::uintptr_t scope_ctor_rva=0x34470, scope_dtor_rva=0x34550, scope_tls_index_rva=0x2c6ba70;')
w('inline constexpr std::uintptr_t scope_push_rva=0x3432b, scope_pop_rva=0x3450c;')
w('inline constexpr std::size_t scope_size=0x38, scope_buffer=0x40, scope_tls_slot=0x18, scope_prev=0x10;  // buffer alignas(16)')
w('inline constexpr std::size_t go_to_p=0x20, go_flags_ptr=0x8, go_flags=0x38;')
w('inline constexpr std::uint32_t go_flags_bit=0x10;                         // [[GO+8]+0x38] & 0x10')
w('inline constexpr std::size_t entry_nbits=0xc, entry_jobs=0x10, entry_subs=0x14, entry_lock=0x1c, entry_go=0x20, entry_bits=0x28;')
w('inline constexpr std::size_t job_param_size=0x18;')
w('// HOLD latch (bit 0 only; gun eligibility 0x1054210 reads it).')
w('inline constexpr std::size_t pp_hold_latch=0x27c;')
w('// Look angles {f32 pitch, f32 yaw} computed in PS phase 0 (0xff28d0 -> 0xffac00); per-id record bit 1 = skipped.')
w('inline constexpr std::size_t ps_look=0x290, ps_look_stride=8, ps_rec=0x280, ps_rec_stride=0x20, ps_phase_comp=0x3e8, ps_phase_comp_vt_delta=0x1e0;')
w('inline constexpr std::uint32_t ps_rec_skip_bit=0x2;')
w('inline constexpr std::uintptr_t ps_phase_rva=0xff2b40;  // comp vt+0x18 (PS vtable +0x1f8)')
w('// Camera fields (semantics of 0x270/0x274 unverified: names neutral) and the aim sub-controller.')
w('inline constexpr std::size_t cam270=0x270, cam274=0x274, cam_aim_sub=0x360;')
w('inline constexpr std::size_t cam_sub_begin=0x3c, cam_sub_end=0x48, cam_sub_b8=0xb8, cam_sub_bc=0xbc;')
w('// Gun node identity (telemetry reads only; builder row 0x3d).')
w('inline constexpr std::size_t blk_gun_node=0x378, blk_gun_iface=0x5a0, gun_iface_prim=0x30, prim_gun_rec=0x58, prim_blk=0x8, prim_node=0x38;')
w('inline constexpr std::size_t gun_flags=0x3c0, gun_mode=0x2c1, gun_rec_size=0x610, gun_node_body=0x24, gun_node_row=0x2c, gun_node_bitmap=0x8;')
w('inline constexpr std::uintptr_t gun_node_vtable_rva=0x2306628, gun_prim_vt_rva=0x23b3e80, gun_iface_vt_rva=0x23b3fa0;')
w('inline constexpr std::uint32_t gun_flag_in_hand=0x4, gun_flag_hold=0x40000;')
w('struct Site { std::uintptr_t rva; std::size_t size; unsigned char bytes[80]; };')
w('inline constexpr Site code_sites[]={')
for rva, expected, what in SITES:
    n = len(expected) // 2
    if n > 80:
        raise SystemExit('site too long')
    w(f'    {{0x{rva:x},{n},{{{cbytes(expected)}}}}}, // {what}')
w('};')
w('struct Slot { std::uintptr_t vtable_rva; std::size_t offset; std::uintptr_t target_rva; };')
w('inline constexpr Slot vtable_slots[]={')
for vtable, offset, target, what in SLOTS:
    w(f'    {{0x{vtable:x},0x{offset:x},0x{target:x}}}, // {what}')
w('};')
w('}')
OUT.write_text('\n'.join(lines) + '\n', newline='\n')
print(f'wrote {OUT.relative_to(ROOT)}: {len(SITES)} code sites, {len(SLOTS)} vtable slots')
