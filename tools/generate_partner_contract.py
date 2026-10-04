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
]

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
]

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
if failures:
    print('\n'.join(failures))
    raise SystemExit(1)
if '--check' in sys.argv:
    print(f'{len(SITES)} code sites and {len(SLOTS)} vtable slots match {GAME_SHA256}')
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
w('// --- Code gate: exact bytes in .xtext ---------------------------------------')
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
