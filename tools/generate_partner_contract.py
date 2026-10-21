"""Record the offline partner spike's player-instance contract. Disassembly only, no game execution.

Reads the fingerprinted installed executable (read-only) and writes
native/partner_player_contract.hpp. Every byte sequence and vtable slot the
native partner code relies on (docs/offline-partner-spike-plan.md 2.3 item 5,
plus the extra sites listed below) is re-read here and the generator refuses
on any difference, so a regenerated header always matches a re-verified
executable. The native code gate (partner_player.cpp CodeGate) checks the same
bytes and slots in the running image before any read or write is attempted.

Usage: python tools/generate_partner_contract.py [--check]
  --check  verify only; exit 1 on any mismatch (including a committed header
           that differs from the generator output), do not write the header.
"""
import hashlib
import struct
import sys
from pathlib import Path

import pefile

import partner_aim_sites

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
    # The seated vehicle rider (partner_ride_seat, docs/anim-round1-seat.md, critic.md 5.2): the
    # anchor 0x1198770 and entry playback 0x1198CC0 bodies, the DM End camera stores, the phase-6
    # apply and the seat matrix 0xB7AF90 (seat-1 remap in 0xB799C0) the four patch dwords rely on.
    (0x1198770, '48895c2418555657488d6c24e04881ec20010000488b05a50b9b014833c4488945b00fb699a0010000b8ffff0000488bf10fb7bc59c0000000663bf87507b001e9c5000000', 'DM anchor 0x1198770: entry target id; 0xFFFF -> true'),
    (0x11987B5, '488b9cd9d00000004c89b42448010000440fb7f76641c1ee09e87d68a6ff488b8898000000664183fe320f85b4000000', 'DM anchor: CNP hash, type = id >> 9, vehicle type 50'),
    (0x11987E5, '488b89a802000033c041b9ff010000488945a0488b014c8d45a0488d542420664423cfff50188338000f85ce02000048b8ffffffffffff00004823d848b851e228daca2c0000483bd80f85ae020000', 'DM anchor: vehicle service vt+0x18 lookup; CNP_ppos_c only'),
    (0x1198834, '488b4da0488d54243041b002e84b279eff0f28000f2986600100000f2848100f298e700100000f2840200f2986800100000f2848300f298e90010000', 'DM anchor: 0xB7AF90(unit, out, seat imm 2) -> rec+0x160..0x19F (ride seat dword 0x119883C)'),
    (0x1198870, 'b0014c8bb42448010000488b4db04833cce8bad28600488b9c24500100004881c4200100005f5e5dc3664183fe0475d0', 'DM anchor: return; a target of neither type 50 nor 4 returns true without an anchor'),
    (0x1198CC0, '40574883ec3048c7442420feffffff48895c24484889742458488bf9488b41388bda2b58244869db50020000480359680fb6b3a00100000fb6841eb8000000', 'DM entry playback 0x1198CC0: rec and entry flags'),
    (0x1198CFF, 'a802745fa808750e4c8b4cf3384c8b05bdd29301eb084533c94c8b44f378e81e040000', 'DM entry playback: locator flag 2; named state flag 8 -> 0x1199140(rec+0x78[i])'),
    (0x1198D22, '488b4708488b8838010000488b91b80000000fb78473c000000048b9ffffffffffffff074823c148b90000000000000088480bc1c7820002000001000000eb53', 'DM entry, locator: CAM loads and stores (ride seat skip 0x1198D56 -> 0x1198DBC)'),
    (0x1198D62, 'a808750e4c8b4cf3384c8b0556d29301eb084533c94c8b44f378e8bf030000488b4708488b8838010000488b91b8000000488d4424404889442450', 'DM entry, no locator: 0x1199140 then CAM (ride seat skip 0x1198D81 -> 0x1198DBC)'),
    (0x1198DBC, '488b5c2448488b7424584883c4305fc3', 'DM entry playback epilogue (both entry skips land here; no rax/rdx use)'),
    (0x11992C3, '488b46d8488b9038010000488b8ab8000000488d44246048894424704c89742460488b442460488981f00100004489b100020000488b424880a088030000c3', 'DM End: rdx = P+0x138 object, CAM stores, then [rdx+0x48] (ride seat skip 0x11992E9 -> 0x11992F7)'),
    (0x1199508, 'f68418b8000000027444488b41080f2883600100008bce488b50782b4a08488b8270020000488b14c80f2942100f288b700100000f294a200f2883800100000f2942300f288b900100000f294a40', 'DM phase 6: a locator entry copies rec+0x160..0x19F to *(PS+0x270)[id]+0x10..0x4F'),
    (0xB7AF90, '488bc44889580848897010574881ecf00000000f2970e80f2978d8440f2940c8488bf2488d542420410fb6d8488bf9440f2948b8440f2950a8e8b22b9eff488d542460440fb6c3488bcfe8e1e9ffff', 'seat world matrix 0xB7AF90(unit, out, seat): 0x55DB80 world x 0xB799C0 seat'),
    (0xB79A3F, '4080ff017515b900fe0000400fb6ff66854e32b9000000000f44f9', '0xB799C0: seat 1 -> 0 unless word(unit+0x32) & 0xFE00 (ride seat caveat)'),
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
    # --- Phase-1 local decision (docs/native-puppet.md "Phase-1 detour"). The base
    # action plugin's per-id handler 0x109F0E0 (node vtable 0x23B5090 +0x18, phases
    # 0..6) sends phase 1 to 0x109E130, which picks local (event 4: the act handlers
    # sample the pad's move intent through 0x10BCD90) or remote (event 5, then the
    # network follow 0x109CFE0) by id == P+0x218, never by isLocal (rec+0x2AC bit 0).
    (0x109F0E0, '83fa06776a', 'base plugin per-id handler: phases 0..6 (cmp edx,6; ja)'),
    (0x109F0E5, '4c8d0d140ff6fe', 'per-id handler: r9 = image base (the jump table holds RVAs)'),
    (0x109F107, '4883c1d0418bd0e91df0ffff', 'phase 1: rcx = node - 0x30 (the plugin), edx = id; jmp 0x109E130'),
    (0x109E14A, '488b4138448bc28bf2442b4024488b4108488bf9488b8038010000418bd841be010000004869db90030000480399a0010000',
     'phase 1: rec = *(plugin+0x1A0) + (id - node base)*0x390; rax = P = *(*(plugin+8)+0x138)'),
    (0x109E17C, '3b9018020000400f95c5', 'phase 1: cmp edx,[P+0x218]; setne bpl (remote = id != the local id): the detour site'),
    (0x109E186, '4883bb90020000000f84ca0000004084ed0f8597000000488b80b0000000',
     'phase 1: flags recomputed, bpl selects the branch; 0x109E19D reads PP = [rax+0xB0] (rax = P must survive)'),
    (0x109E1C5, '458d4104', 'phase 1 local: the act state callback gets event 4'),
    (0x109E244, '458d4105', 'phase 1 remote: event 5'),
    (0x109E2DE, '440fb68ba10200004585c9741a0fb683a0020000443bc8740e448bc6488bd7488bcb',
     'phase 1: next act rec+0x2A1 != current act rec+0x2A0 -> act transition 0x109EFA0'),
    (0x109E11D, '4883c4205bc3cccccccccccccccccccccccccc', 'end of 0x109E060 (ret at 0x109E122), then int3 padding up to 0x109E130'),
    (0x10A5BAA, '4c8bc38bd7e8dc710100', 'act handler 0x10A5950 event 4: the move-intent sampler 0x10BCD90'),
    (0x10BCEF3, 'f30f11bbc0020000', 'move-intent sampler: rec+0x2C0 = move magnitude (PP+0x10 unless PP+0x2C bit 0)'),
    (0x10BCF08, 'f30f107714f30f11b3c4020000', 'move-intent sampler: rec+0x2C4 = PP+0x14 heading'),
    # --- Native actions (docs/native-puppet.md "Actions"): the PlayerPad enum
    # pairs the masks use (0x1A156E0(table, name, value): mov r9d,1; r8d = value;
    # lea rdx,[name]; PAD_ENUM below re-decodes name and value), the three EVADE
    # (0x40000) press consumers that set act 27 (Q+0x18[id] = 0x1B) and their
    # network-remote gates (true only with S_IS_ONLINE, P+0x204 bit 8).
    (0x9D3A90, '41b901000000458d4101488d15bb598c01', 'PlayerPad.STANCE = 0x2'),
    (0x9D3AA9, '41b901000000458d4103488d158e277c01', 'PlayerPad.DASH = 0x4'),
    (0x9D3AC2, '41b901000000458d4107488d152d658301', 'PlayerPad.HOLD = 0x8'),
    (0x9D3ADB, '41b901000000458d410f488d151c658301', 'PlayerPad.FIRE = 0x10'),
    (0x9D3AF4, '41b901000000458d411f488d155f598c01', 'PlayerPad.ACTION = 0x20 (never passed)'),
    (0x9D3B0D, '41b901000000458d413f488d154e598c01', 'PlayerPad.RELOAD = 0x40'),
    (0x9D3C17, '41b90100000041b800000100488d1586588c01', 'PlayerPad.SIDE_ROLL = 0x10000 (no player consumer)'),
    (0x9D3C66, '41b90100000041b800000400488d1567588c01', 'PlayerPad.EVADE = 0x40000'),
    (0x9D3CB7, '41b90100000041b800002000488d153e588c01', 'PlayerPad.VEHICLE_DASH = 0x200000 (keyboard binding 21)'),
    (0x9D3D23, '41b901000000458d413f488d15ec267c01', 'PlayerPad.CARRY = 0x40 (RELOAD alias)'),
    (0x9D3D3C, '41b901000000458d411f488d1503588c01', 'PlayerPad.RIDE_ON = 0x20 (ACTION alias)'),
    (0x9D3DBD, '41b901000000458d411f488d15b2887b01', 'PlayerPad.CQC_KNIFE_KILL = 0x20 (ACTION alias)'),
    (0x1148C4E, 'e8cd4e000084c075b4488b4608488b8838010000488b89b0000000488b01ff5040c1e812a8017495488b4608418bd6488b8838010000'
     '488b4160488b4818b81b0000006642890471',
     'EVADE A: remote gate 0x114DB20; PP vt+0x40 (press) >> 18 & 1; Q+0x18[id] = 0x1B'),
    (0x12997FC, '488bcfe8ccc8010084c00f85d7060000488b4740488b8838010000488b89b0000000488b01ff5040c1e812a8010f84b4060000'
     '488b4740488b8838010000488b4160488b4818b81b00000066890459',
     'EVADE B: remote gate 0x12B60D0; press bit 18; Q+0x18[id] = 0x1B'),
    (0x129B7B4, '8bd3488bcfe812a9010084c00f85a8060000488b4740488b8838010000488b89b0000000488b01ff5040c1e812a801488b4740741d'
     '488b8838010000488b4160488b4818b81b00000066890459',
     'EVADE C: remote gate 0x12B60D0; press bit 18; Q+0x18[id] = 0x1B'),
    (0x114DB20, '48895c2408574883ec20488b41088bfa488bd94c8b8038010000418b8004020000c1e808a8017437e80315abff488b8888000000'
     '488b01ff504883f8027413488b4308488b8838010000',
     'remote gate A: P+0x204 bit 8 (S_IS_ONLINE) clear -> 0 (offline instance 1 passes)'),
    (0x114DB6A, '3bb918020000740db001488b5c24304883c4205fc332c0488b5c24304883c4205fc3', 'remote gate A: id != P+0x218 -> 1 only online'),
    (0x12B60D0, '48895c2408574883ec20488b41408bfa488bd94c8b8038010000418b8004020000c1e808a8017437e8538f94ff488b8888000000'
     '488b01ff504883f8027413488b4340488b8838010000',
     'remote gate B: P+0x204 bit 8 (S_IS_ONLINE) clear -> 0'),
    (0x12B611A, '3bb918020000740db001488b5c24304883c4205fc332c0488b5c24304883c4205fc3', 'remote gate B: id != P+0x218 -> 1 only online'),
    # --- Native rates (docs/native-puppet.md "Rates"): the per-id movement rate
    # records of *(PS+0x260) (sp: interface vtable 0x23AD380 at object+0x20;
    # rec = *(sp+0x10) + (id - *(u32)(sp+8))*0x88, count *(u32)(sp+0xC);
    # record vtable 0x23AD2B0) and every site that reads or writes the rates.
    (0xFDD610, '2b51088bc24869c08800000048034110c3', 'sp vt[0]: rec = *(sp+0x10) + (id - *(u32)(sp+8))*0x88'),
    (0xFDD4ED, '488d0d5cfe3c01488908488d0d82fe3c0148894820', 'sp factory: object+0 = 0x23AD350, object+0x20 (sp) = 0x23AD380'),
    (0xFDD5E0, '48b8fed112748aba0000483bd075134885c9740e488d41204885c074054883c008c333c0c3',
     'sp object QI(0xBA8A7412D1FE) = object+0x28 (= sp+8)'),
    (0xFFF31D, '488b89280400004885c97404488b4910488b0148bafed112748aba0000ff50184885c074044883c0f848898760020000',
     'PS setup: PS+0x260 = QI(0xBA8A7412D1FE) - 8 = sp'),
    (0xFDD630, '488b02488941388b420889412c8b420c894128e908000000', 'sp init: count sp+0xC = [rdx+8], base sp+8 = [rdx+0xC]'),
    (0xFDD66C, '8b712c8bde8bce4869c988000000ba0400000041b848001b00e8561ac2ff48894730',
     'sp init: records sp+0x10 = alloc(count * 0x88)'),
    (0xFDD69C, '4c8d0dbdfb3c014c8d1506fc3c01660f1f440000488b4f304803ca48894c244048894c244874764c89094c8941084c8941104c894118'
     '4c8941204c8941284c89413044894138',
     'record init: base vtable 0x23AD260, final 0x23AD2B0'),
    (0xFDD6E2, '48c741540000803fc7415c0000003f48c741600000003f4c894168448941704c8941784c898180000000', 'record init: +0x54..+0x80'),
    (0xFDD70C, 'c7413c0000803fc741400000803fc741440000803fc741480000803fc7414c0000803fc741500000803f4c8911',
     'record init: rates +0x3C..+0x50 = 1.0, then the final vtable'),
    (0xFDD30D, '488d05e4ff3c01488901488d050a003d0148894120', 'sp dtor: base vtables 0x23AD2F8 / 0x23AD328 (refused)'),
    (0xFDD416, '488d0593fe3c01488901488d0539fe3c01488901', 'record dtor: 0x23AD2B0 then base 0x23AD260 (refused)'),
    (0x9CB7A5, '488b8638010000488bb830010000488b4678488b8860020000488b01418bd6ff10488bd84c8b0f41b801000000418bd6488bcf41ff5128',
     'instance setup: provider *(P+0x130) vt+0x28(id, 1) ...'),
    (0x9CB7DC, 'f30f114348f30f59434cf30f1143504c8b0f41b802000000418bd6488bcf41ff5128f30f11433cf30f594340f30f114344',
     '... +0x48 = dash rate, +0x50 = +0x48 * +0x4C; vt+0x28(id, 2): +0x3C = crawl rate, +0x44 = +0x3C * +0x40'),
    (0x109F690, '4883ec284183f902751e488b4108488b4878488b8960020000488b01ff10f30f1040444883c428c3',
     'rate getter 0x109F690(plugin, id, class, stance): stance 2 (crawl) -> rec+0x44'),
    (0x109F6B8, '4183f803751e488b4108488b4878488b8960020000488b01ff10f30f1040504883c428c3f30f1005e87b00014883c428c3',
     'rate getter: class 3 (dash) -> rec+0x50, else 1.0'),
    (0x10B9DB5, '448bcf458bc6418bd4498bcfe8ca58fefff6437240f3440f59e0', 'locomotion speed A: base 0x109F7E0 * rate 0x109F690'),
    (0x10BA6AA, '448bcf418bd4498bcf440f28e0e8d44ffeff4d8b4708498b4078f3440f59e0', 'locomotion speed B: base * rate'),
    (0x10BD1C3, 'f30f11774cf30f597748f30f117750', 'per-frame dash factor: +0x4C, +0x50 = +0x4C * +0x48'),
    (0x10A4C6B, '488b4878488b8960020000488b01ff10448970688b4848c7404c0000803f894850', 'dash reset: +0x4C = 1, +0x50 = +0x48'),
    (0x11403C9, '41b817000000488b88380100008bd6488b8930010000488b01ff5028488b8b600200008bd6488b010f28f0ff108bd6f30f117040'
     'f30f59703cf30f117044',
     'crawl factor: +0x40 = provider vt+0x28(id, 0x17), +0x44 = +0x40 * +0x3C'),
    (0x11313ED, '488b4878488b8960020000488b01ff108b483cc740400000803f894844', 'crawl reset: +0x40 = 1, +0x44 = +0x3C'),
    # --- Native weapons (docs/native-puppet.md "Weapons"). Fire visuals: the
    # partner's shots stay visual on the viewer's machine (RequestBullet owner
    # filter, partner_fire_filter.cpp); reload and magazine (gun record fields,
    # partner_input_hook.cpp MirrorMag); loadout (vars registrations).
    # RequestBullet 0xD32C60(iface, record): only reached through the bullet
    # system's vtable slot 0 (0x2337DE8 live, 0x2337D18 while destroying).
    (0xD32C60, '48895c2408574883ec20488bfa488bd9e8dbc3ecff', 'RequestBullet prologue: rdi = record (rdx), rbx = iface (rcx)'),
    (0xD32CB1, '4885c0740b488bd7488bc8e8cff9ffff488b5c24304883c4205fc3',
     'RequestBullet: a null slot skips the copy 0xD32690 and returns 0 (the filter returns 0 the same way)'),
    (0xD32D0A, '488d05a7506001488903488d05cd50600148894320', 'bullet system ctor: +0 = 0x2337DB8, +0x20 (iface) = 0x2337DE8'),
    (0xD32619, '488d05f8566001488907', 'bullet system dtor: iface vtable 0x2337D18 while destroying'),
    (0xD32639, 'e812caecff488b88980000004839b918020000750b48c7811802000000000000',
     'bullet system dtor: gm+0x218 (= *(svc+0x98)+0x218) cleared if it is this iface'),
    (0x1306A3A, 'e811868fff488b88a8000000488b4108488983a0000000e8fa858fff488b8898000000488b8118020000488983a8000000',
     'model group setup: group+0xA0 = *(*(svc+0xA8)+8) (noise), group+0xA8 = *(*(svc+0x98)+0x218) (bullets)'),
    (0x1302FA6, '498bf9', 'model group shot 0x1302F50: rdi = record (r9)'),
    (0x1302FE9, 'b800fe0000b9000000006685472a', 'model group shot: record+0x2A is the owner GameObjectId (type bits 0xFE00 tested)'),
    (0x13031BB, '488b8ea8000000488bd7488b01ff10', 'model group shot: RequestBullet(group+0xA8, record) (call at 0x13031C8)'),
    (0x13031F7, '488bd7ff10', 'model group shot: second RequestBullet (call at 0x13031FA)'),
    (0x1303432, '488b8ea8000000488b01ff10', 'model group shot: pellet-loop RequestBullet (call at 0x130343C, rdx = rdi)'),
    (0x1303CD4, '807c244000752d807c24410474260fb7472a0f2807488b8ea000000066c745e04501660f7f45d0668945e2488b01488d55d0ff10',
     'model group shot: unsuppressed shot noise {pos, 0x145, owner} through group+0xA0 vt+0 (not filtered; census only)'),
    # The other shot request builders (asserted complete for the vt+0x138 queue below) stamp a typed owner,
    # so only player instance 1 itself can carry the filter's owner id 1.
    (0x13A18B2, 'b9ff0100000f2945c7660f7f4dd76623c133ff48c7450bffffffff48c74513ffffffffb90004000048c745e700000000660bc1',
     'soldier shot request: owner = (id & 0x1FF) | 0x400 (type 2, never a player id)'),
    (0x13A190E, '668945f1', 'soldier shot request: the typed owner at request+0x2A ([rbp-0x39]+0x2A)'),
    (0x1453329, 'b9ff01000048c745fbffffffff48c74503ffffffff0f2945b7660f7f4dc70fb7c64c897dd76644897ddf6623c1b900040000c745e3ffff0000660bc1',
     'second soldier shot request: owner = (id & 0x1FF) | 0x400'),
    (0x1453385, '668945e1', 'second soldier shot request: the typed owner at request+0x2A ([rbp-0x49]+0x2A)'),
    (0xEDABB9, '410fb7b068010000', 'system shot request: the owner type = word [this+0x70]+0x168'),
    (0xEDABD2, '66c1e609', 'system shot request: type << 9'),
    (0xEDABE6, '660bf1', 'system shot request: | (index & 0x1FF)'),
    (0xEDAC19, '668974247a', 'system shot request: the typed owner at request+0x2A ([rsp+0x50]+0x2A)'),
    (0x1041180, '0fb6bac0030000', 'shoot 0x1041110: edi = byte rec+0x3C0'),
    (0x10411D8, '4080e701', 'shoot: dil = rec+0x3C0 bit 0 (the local player)'),
    (0x10411EC, '40887c2444', 'shoot: [rsp+0x44] = local bit'),
    (0x1041219, '410fb7c4b9ff0100006623c1668945aa', 'shoot: request+0x2A = id & 0x1FF (the owner stamp)'),
    (0x10414F1, '498b4f604c8d4580488b01ff9038010000', 'shoot: the shot request [rbp-0x80] queued through equip vt+0x138'),
    (0x10415AB, '807c244400895c24400f84e4070000',
     'shoot: a non-local shooter jumps to 0x1041D9E (no ammo, camera kick, shot origin, shoot count, suppressor life)'),
    (0x1041229, '4084ff0f842a010000', 'shoot: non-local skips the local request fields (je 0x104135C)'),
    (0x1041E67, 'f686c003000001743b', 'shoot tail: rec+0x3C0 bit 0 clear -> 0x1041EAB'),
    # The local-only side effects the non-local path never reaches (asserted below).
    (0x1041650, 'e88b730100', 'shoot (local only): ammo 0x10589E0'),
    (0x1041772, '488b01ff9010010000', 'shoot (local only): camera kick CAM vt+0x110'),
    (0x10417D0, '488b01418bd4ff9050010000', 'shoot (local only): suppressor life equip ctl vt+0x150'),
    (0x1041BDB, '0f298bb0000000', 'shoot (local only): CAM+0xB0'),
    (0x1041C54, 'e8f7d3bbff488d5580488b8898000000488b8918020000488b01ff5008',
     'shoot (local only): bullet system vt+8 (last shot origin)'),
    (0x1041C9E, '498b87880000004401a844010000', 'shoot (local only): vars.playerShootCountInMission += pellets'),
    # Gun record fields (R1 telemetry and the magazine mirror).
    (0x10435D0, 'f781c0030000000000107446448b817c020000410fbae00973090fb78112020000eb070fb781100200000fb7d0',
     'magazine getter: rec+0x3C0 bit 28 = full; capacity u16 rec+0x212 (flags rec+0x27C bit 9) or rec+0x210'),
    (0x10435FD, '8b814c02000005d5fbffff83f8037709410fbae014730203d2410fbae0137302d1ea8bc2c3',
     'magazine getter: capacity x2 for equip rec+0x24C in 0x42B..0x42E with flag bit 20, /2 with bit 19'),
    (0x1043622, 'f7817c02000000020000740b0fb7815e0200000fb7c0c30fb7815c0200000fb7c0c3',
     'magazine getter: u16 rec+0x25E (flags bit 9) or rec+0x25C'),
    (0x1051F36, 'f7818002000000000800488bf1', 'CanReload: rec+0x280 bit 19 refuses'),
    (0x1051F4D, 'f781c003000000000020', 'CanReload: rec+0x3C0 bit 29 selects the capacity-based stock check'),
    (0x1051F76, 'f783cc0000000002000074090fb7bbb2000000eb070fb7bbb0000000e83916ffff0fb7cf2bc8',
     'CanReload: stock u16 rec+0x262/+0x260 minus the magazine'),
    (0x1051FA0, '8b8bcc0000000fbae10973060fb74362eb040fb743600fb7f88b839c00000005d5fbffff83f80377080fbae114730203ff0fbae1137302d1ef488bcee8ef15ffff3bc77315',
     'CanReload: magazine < capacity (the getter rule)'),
    (0x1094820, '488bcfe808d7fbff84c0', 'reload request 0x1094770: CanReload'),
    (0x1094861, '488b06488bceff5040c1e8060fb6c8c1e114338fc003000081e100001000318fc0030000',
     'reload request while aiming: rec+0x3C0 bit 20 = PP vt+0x40 press bit 6'),
    (0x10948D3, '81a7c0030000ffffefff', 'reload request: CanReload false clears rec+0x3C0 bit 20'),
    (0x103E3A4, '8b8b7c0200004c897424488bc1c1e80683e00783f8010f858400000081e10018000041be00080000413bce747383bb4c02000000746a80bbd5030000047461448bc6488bd3488bcfe87f630500',
     'gun update reload gate: class (rec+0x27C>>6)&7 == 1, rec+0x24C != 0, rec+0x3D5 != 4 -> 0x1094770'),
    (0x1058ABF, '8b937c020000c1ea09f6c201740a6644898b5e020000eb086644898b5c020000',
     'ammo setter 0x10589E0: the magazine is u16 rec+0x25E (flags bit 9) or rec+0x25C'),
    # Loadout (pure reads of vars = *(0x2C16920)).
    (0x99B0BA, '4c8b055fb827024981c048050000', 'vars registration: weapons at vars+0x548'),
    (0x99B0F1, 'c744c810da14dde6c744c81406000708', 'vars registration: weapons u16[6]'),
    (0x99C2E4, '4c8b0535a627024981c0060b0000', 'vars registration: customizedWeapon at vars+0xB06'),
    (0x99C31B, 'c744c810bccaa14fc744c81403000708', 'vars registration: customizedWeapon u16[3]'),
    (0x99C412, '4c8b0507a527024981c0300b0000', 'vars registration: chimeraParts at vars+0xB30'),
    (0x99C449, 'c744c8101fdde3ccc744c81424000508', 'vars registration: chimeraParts u8[36]'),
    (0x998DE4, '4c8b0535db27024981c044010000', 'vars registration: playerShootCountInMission at vars+0x144 (leak check)'),
    # The viewer's weapon/ammo vars block vars+0x548..+0x670 (read-only leak digest; contiguity asserted below).
    (0x99B839, '4c8b05e0b027024981c014060000', 'vars registration: the last weapon/ammo block member at vars+0x614'),
    (0x99B870, 'c744c810826b9ae1c744c8142e000708', 'vars registration: u16[46] at +0x614 (the block ends at +0x670)'),
    # Service root (read-only census of the noise interface).
    (0xBFEFAD, '488b05ecb80302', 'service root: *(0x2C3A8A0)'),
    (0xBFF050, '4883ec28e847ffffff488b40084883c428c3', 'service locator 0xBFF050 = *(root+8)'),
    # --- Native polish: facing D3, the look source hook (docs/native-puppet.md "Facing"; partner_look_source.cpp).
    # The call 0xFF29FC (pinned with 0xFF29F7 above) is followed by edx/rcx reloads: rax is never read.
    (0xFF2A01, '8bd6488bcde8a5b40000', 'PS phase 0 after the look call: mov edx,esi; mov rcx,rbp; call 0xFFDEB0 (rax unused)'),
    (0xFFAC00, '48895c2408574883ec40', 'look computation 0xFFAC00(PS, id) prologue'),
    (0xFFAC19, 'e8528effff84c0488b87080400000f8598000000', '0xFFAC00: 0xFF3A70(PS, id) (S_IS_ONLINE gate); offline -> camera path'),
    (0xFFAC34, '4c8b80380100000f28ef0f28cf498b80b80000000fc6cfc90fc6efd20f289890000000',
     'offline look: the viewer camera quaternion *(*(*(PS+0x408)+0x138)+0xB8)+0x90'),
    (0xFFAD42, '0f287424300f287c2420f30f11442464488b442460488904d9488b5c24504883c4405fc3',
     'look store tail: the pair at 0xFFAD57, ret at 0xFFAD65 (int3 padding follows)'),
    # --- Native polish: weapons F1 (the pad's sample rings in drain windows).
    (0x1096137, '41c1ee1841f6c6010f844c020000', 'trigger 0x1095DF0: rec+0x27C bit 24 selects the history path'),
    (0x1096171, '488b4708410f28f5f30f5ef0488b48088b7044488b0748894c2450488bcfff5020488bcf89442444488b07ff909800000041bf01000000'
     '84c0740f488d542444418bf74889542450eb05488b542450',
     'trigger history: ring = [[PP+8]+8], count = [[PP+8]+0x44]; keyboard mode (PP vt+0x98) uses the held word'),
    (0x1096289, 'a81074d2f644ba041075cb', 'trigger (semi-auto): an entry with FIRE whose older entry lacks FIRE fires'),
    (0x10964F7, '81a3c0030000fffffffe84c98883df030000400f95c583e501c1e51809abc0030000',
     'trigger: rec+0x3C0 bit 24 = a shot this update, rec+0x3DF = the shot count'),
    (0x103E485, 'e866790500', 'gun update: the trigger 0x1095DF0 (its only call)'),
    (0x103E53A, '0fb683c00200002c093c01760a', 'gun update: rec+0x2C0 is the current mode (9/10 fire), rec+0x2C1 the next'),
    (0x4353C3, '488b45708b88a80100008b4540', 'src update (src vt+0x38 0x435260): n = min(device samples, cap src+0x40)'),
    (0x435435, '488b54c510488b5cc520', 'src update: stick i history X at src+0x10+8i, Y at src+0x20+8i'),
    (0x435485, '488b54c530', 'src update: trigger i history at src+0x30+8i'),
    (0x43550C, '488b45084533ff45891406', 'src update: the held history at src+8 (u32 entries, newest first)'),
    (0x4355B3, '894544', 'src update: src+0x44 = the samples of this update'),
    (0x1206BC3, '4c8b7e08498bcf498b07ff5038', 'PP update: the src update src vt+0x38 (PP+8)'),
    (0x112344F, '488b4608488bce4c8b78184c8b60288b6844448b7040', 'right-stick history consumer: src+0x18/+0x28 rings, src+0x44, cap src+0x40'),
    (0x12063F0, '0fb68188040000c1e80683e001c3', 'PP vt+0x98 keyboard mode = PP+0x488 bit 6'),
    # --- Native polish: weapons F2 (the magazine's per-instance store row).
    (0x103E083, '448bc7488bd3488bcee82fe80400', 'gun phase 0 (0x103E010): the ammo copy 0x108C8C0(prim, rec, id) every frame'),
    (0x104032B, '8bd6488bcde8dbdcffff', 'gun iface phase 0 (0x1040100): 0x103E010'),
    (0x108C8DE, '488daab0010000418b8104020000c1e808a8017427', 'ammo copy: rbp = rec+0x1B0; S_IS_ONLINE gate'),
    (0x108C94B, '498b81c80000004c8b60080fb682da030000458b44240c498b542450450fafc54403c0',
     'ammo copy: Y = *(*(P+0xC8)+8); idx = id * [Y+0xC] + rec+0x3DA; rows [Y+0x50]'),
    (0x108C96E, 'f786d0030000000000084b8d0c40440fb73c4a4b8d0c40440fb7744a020f8418010000',
     'ammo copy: rec+0x3D0 bit 27 gates the tamper check; word0/word1 of the 6-byte row'),
    (0x108CAAE, '664489bdac000000664489b5ae000000', 'ammo copy: rec+0x25C = word0, rec+0x25E = word1'),
    # The support classes 2/3 branch (C(b) viewer-own rebase reads instance 0's slot sub): the stock word of the held
    # item is k = Y vt+0x68(rec+0x3DA slot, rec+0x3DC sub) (store vt+0x68 0xEE90A0: slot 3 -> 6 + sub), the same pair
    # the mode-37/46 spend passes to ctl vt+0xB0 before the stock setter ctl vt+0xC0.
    (0x108CB57, '498b81c8000000440fb682dc0300000fb692da030000488b5808',
     'ammo copy classes 2/3: ctl = *(P+0xC8), r8d = u8 rec+0x3DC (the slot sub), edx = u8 rec+0x3DA (the slot), Y = *(ctl+8)'),
    (0x108CB74, '488b3bff5768', 'ammo copy classes 2/3: k = Y vt+0x68(slot, sub)'),
    (0x1041660, '8b8104020000b9c83f0000c1e812a801742c', 'shoot: rec+0x3D0 bit 27 only with P+0x204 bit 18'),
    (0x107CB59, '8b8104020000c1e812a801741c', 'bit 27 setter 2: only with P+0x204 bit 18'),
    (0x1087D01, '8b8104020000c1e812a801741d', 'bit 27 setter 3: only with P+0x204 bit 18'),
    (0xEE96C0, '4c8bd18b490c458bd9498b42400fafca4103c88b14888bc283e01f83f809750e498b4250488d0c496644890c48c3',
     'store setter (Y vt+0x20): type = [Y+0x40][idx] & 0x1F; type 9: word0 only, no notifier'),
    (0xEE96EE, 'ffc883f8077731440fb64c2428498b4250488d0c494584c974086644895c4802eb056644891c48498b4a58458bc3488b0148ffa000010000',
     'store setter: types 1..8 word0 or word1 (sub flag), then the notifier [Y+0x58] vt+0x100 (never called by us)'),
    (0xEE8C23, '488d0556084a01488901488d057c084a0148894120', 'store ctor: vtables 0x2389480 / 0x23894B0 (iface +0x20)'),
    (0xEE9026, '488d056b054a01488903488d0599054a0148894320', 'derived store ctor: vtables 0x2389598 / 0x23895D0 (iface +0x20)'),
    # The store init (object vt+8, both classes): ids at object+0x28 (Y+0x8), the stride at +0x2C (Y+0xC) and the
    # total ids * stride at +0x30 (Y+0x10); the types (+0x60 = Y+0x40) get total * 4 bytes and the rows (+0x70 =
    # Y+0x50) total * 6: the mirror's row index must stay below Y+0x10.
    (0xEE930C, '44894928448b42044489412c8b4208450fafc18981a80000008b420c44894130',
     'store init: object+0x28 = ids, +0x2C = stride, +0x30 = ids * stride (Y+0x10, the row count)'),
    (0xEE9331, '8b573048c1e202', 'store init: the types array is the row count * 4 bytes'),
    (0xEE9384, '488947608b4730488d14404803d2', 'store init: types at object+0x60 (Y+0x40); the rows are the row count * 6 bytes'),
    (0xEE93EA, '48894770', 'store init: rows at object+0x70 (Y+0x50)'),
    (0x1054A97, '4585d2741d', 'reload completion: the local branch runs the ammo setter; non-local -> 0x1054AB9'),
    (0x1054AB9, '440fb687da0300006644898fe8030000498b4508488b8838010000418bd4488b81c8000000488b48088b877c0200004c8b11c1e809240188442420'
     '41ff5220', 'reload completion (non-local): rec+0x3E8 and the store row through Y vt+0x20'),
    # --- Native polish: weapons F3 (the shot noise filter).
    (0x515270, '48895c240848896c2410488974241857', 'AddNoise 0x515270 (noise iface vt+0) prologue'),
    (0x5152F9, '0fb74512668984be10480000', 'AddNoise keeps the record +0x12 owner'),
    # --- Native weapons: the partner's own reload and weapon switch (docs/native-puppet.md "Weapons", R1..R6, W1..W6).
    # The gun iface dispatcher (iface vt+0x18) runs phases 0..7 inside block 1's drain (svc28 mask 0x7f).
    (0x1041000, '83fa0777764c8d0df4effbfe8bc2418b9481801004014903d1ffe2',
     'gun iface dispatcher 0x1041000: edx = phase 0..7 through the jump table 0x1041080 (r9 = image base)'),
    (0x1041027, '4883c1d0418bd0e99dd2ffff', 'gun iface phase 1 -> the gun update 0x103E2D0 (prim = iface - 0x30)'),
    (0x1041063, '4883c1d0418bd0e901dbffff', 'gun iface phase 6 -> 0x103EB70'),
    (0x103F99C, 'c683df0300000081a3c0030000ffefeffc',
     'phase 6 (0x103EB70): rec+0x3DF = 0, rec+0x3C0 &= 0xFCEFEFFF (bits 12/20/24/25 never survive the drain)'),
    (0x103E3E3, '448bc6488bd3488bcfe87f630500', 'gun update (phase 1): the reload request 0x1094770(prim, rec, id)'),
    (0x1094887, '488b06ba06000000488bce448d4206ff9090000000',
     'reload request (holstered): the hold classifier PP vt+0x90(6 = RELOAD, 12 frames)'),
    (0x1040A54, '488d15454cc101', 'mode transition 0x1040A00: lea rdx, the mode table 0x2C556A0 (16-byte entries)'),
    (0x1040A78, '838bc003000002', 'mode transition: rec+0x3C0 |= 2'),
    (0x1040A88, '4088abc0020000', 'mode transition: rec+0x2C0 = the new mode (the current mode)'),
    (0x1040A9A, 'c683c102000000', 'mode transition: rec+0x2C1 = 0 (the request consumed)'),
    (0x103E515, 'e8e6240000', 'gun update: the mode transition 0x1040A00'),
    (0x1072D12, '8bbec00300004533e4418bdc4084ff0f8808010000', 'aim decider (mode 7 0x1071D70): rec+0x3C0 bit 7 -> mode 8 (weapon change)'),
    (0x1072D95, '8b86c00300000fbae01472190fbae015', 'aim decider: rec+0x3C0 bit 20 (or 21) -> reload'),
    (0x1072DBA, '8b867c020000418bdcc1e81ca8010f95c383c30b', 'aim decider: reload mode = 0xB + (rec+0x27C bit 28), i.e. 11 or 12'),
    (0x1077A0A, '488b4160498bce488b402041c6040705', 'reload enter (mode 11/12 op 1): Q+0x20[id] = 5 (Q = *(P+0x60))'),
    (0x1077A36, '8b8fc003000081a7c8030000f2f9fffdc78734030000000080bf8bc18bd181e1dfffbfffc1e812c1ea13',
     'reload enter: aim = (rec+0x3C0 >> 18 | >> 19) & 1, old & 0xFFBFFFDF'),
    (0x1077A60, '41b80b0000000bd08b87c4030000253ffefeff4123d50fbae81481ca020020008987c40300008b87cc030000c1e20525ffffffaf0bd10fbae81a8997c00300008987cc030000',
     'reload enter: rec+0x3C0 = ((aim | 0x200002) << 5) | old (bit 26 set while reloading)'),
    (0x107833C, '668987e8030000', 'reload (op 1): rec+0x3E8 = the magazine minus rec+0x3E4 (the completion base)'),
    (0x10790AA, '81a7c00300009fffbffb', 'reload exit (op 2 0x107903C): rec+0x3C0 bits 5, 6, 22 and 26 cleared'),
    (0x1079B6E, 'e88dadfdff', 'reload events (op 12 0x10796C8): the completion 0x1054900 (its only caller)'),
    # Weapon switching: the equipment row of instance id is *(Q+0xC0) + id*0x3A; row+0x36 low nibble = the current slot.
    (0x11AD240, '4883ec48488b41188bd2488b4060486bd23a488b80', 'GetCurrentEquipSlot 0x11AD240 (ctl vt+0x38): row = *(*(*(ctl+0x18)+0x60)+0xC0) + id*0x3A'),
    (0x11AD2A2, '0fb644243683e00f', 'GetCurrentEquipSlot: row+0x36 & 0xF'),
    (0x11AFA80, '40555356574154415541564157488d6c', 'SetCurrentSlot 0x11AFA80 (ctl vt+0x10) prologue (the call variant)'),
    (0x11AFAA7, '488b4118448be2488bf1488b5060b801000000418bccd3e0418bfc4d63f0',
     'SetCurrentSlot: P = ctl+0x18, Q = P+0x60, eax = 1 << id, r14 = the slot type'),
    (0x11AFAC5, '09827c010000', 'SetCurrentSlot: Q+0x17C |= 1 << id'),
    (0x11AFACB, '488b4618410fb6d9486bff3a4803bac0000000', 'SetCurrentSlot: row = *(Q+0xC0) + id*0x3A'),
    (0x11AFADE, '443ba014020000', 'SetCurrentSlot: local = id == P+0x214'),
    (0x11AFAED, '4183fe0474194584ed74074489b274060000', 'SetCurrentSlot: slot type 4 skips the nibble; the local side also writes [ctl+0x20]+0x674'),
    (0x11AFAFF, '806736f0410fb6c6240f084736', 'SetCurrentSlot: row+0x36 low nibble = the slot'),
    (0x11AFB0C, '4533ff4c8d0dea04e5fe41b8ff0700004183fe067718438b8cb16cfe1a014903c9ffe1',
     'SetCurrentSlot: types 0..6 through the jump table 0x11AFE6C (r9 = image base), others to 0x11AFB3A'),
    (0x11AFB2F, '0fb74702664123c0668907', 'SetCurrentSlot type 0: row+0 = row+2 & 0x7FF'),
    (0x11AFB3A, '4584ed0f8408030000', 'SetCurrentSlot: a non-local id exits to 0x11AFE4B'),
    (0x11AFC08, '0fb74704e922ffffff0fb74706e919ffffff0fb7470ae910ffffff0fb74708e90bffffff',
     'SetCurrentSlot types 1/2/5/6: row+0 from row+4/+6/+0xA (& 0x7FF) or row+8'),
    (0x11AFE4B, '488b4dff4833cce8e95c85004881c498000000415f415e415d415c5f5e5b5dc3', 'SetCurrentSlot epilogue (the cookie check, ret)'),
    (0x103AF1C, '3c077510488b07418bd0488bcfff5038', 'phase-0 slot compare 0x103AED0: rec+0x3DE == 7 -> the slot from ctl vt+0x38'),
    (0x103AF6A, '418b4d0c498b4540410fafcf03ce8b04888bc8418bd6c1e91066413bc874078bd0c1ea0523d5',
     'phase-0 slot compare: handle = inventory+0x40[id*stride+slot]; equip = (handle >> 5) & 0x7FF unless handle >> 16 == 0xFFFF'),
    (0x103AFA7, '0fb683d90300003bf075043bd1740a818bc003000080000000',
     'phase-0 slot compare: slot != rec+0x3D9 or the equip differs -> rec+0x3C0 |= 0x80'),
    (0x103E049, '8b83c003000084c07930a820752ca8407528f7837c02000000000800740e448bc7488bd3488bcee8cbdc0400448bc7488bd3488bcee82d1d0200',
     'gun phase 0: bit 7 with bits 5/6 clear -> the activation 0x105FDB0 (else the active mode decides: mode 8)'),
    (0x105FDE9, '0fb682de030000418bf0488b99c8', 'activation 0x105FDB0: rec+0x3DE, ctl = *(P+0xC8)'),
    (0x105FEB4, '488b47408bce0faf4f0c03ca8b1c88b9ffff00008bc3c1e810663bc1751b498bd6498b',
     'activation: handle = inventory+0x40[id*stride+slot]; an empty handle -> 0x10517C0 (no weapon)'),
    (0x13030B7, '410fb65e7feb05410fb65e7e', 'shot handler 0x1302F50: the shot class byte +0x7F (second mode) or +0x7E (class 3 = projectile, not filtered)'),
    # --- Weapons 3: the sender's base ids (docs/native-puppet.md "Loadout mirroring"). The type getter A2A3E0 for the
    # customized aliases 0x367..0x36C: receiver r = u8 [alias table + (id - 0x367)*0xE + 0x12], then the equipment
    # resource's part record r (6 bytes, 1-based: [+0x10] + r*6 - 2) names a kind whose 3-byte record ([+0x68]) holds the
    # type (& 0x1F); and the equip id -> type table index mapping (ids 0x400.. sub 0x1D0, ids 0x600.. sub 0x380).
    (0xA2A3E6, '8d8299fcffff8bda83f8057740e898933d018d9399fcffff486bd20e0fb65c1012e8a421010085db7458488b4810488d145b0fb65451fe'
     '488d0c524803486874410fb60183e01f4883c4205bc3',
     'type getter 0xA2A3E0 aliases 0x367..0x36C: r = [0x2C934A0 + k*0xE + 0x12], type = [[R+0x68] + [[R+0x10] + r*6 - 2]*3] & 0x1F'),
    (0xA2A44B, '8d82d0fdffff3dcf010000761081fa500400007c1081fa000600007d1033c04883c4205bc381fa000600007c17',
     'type getter: ids 0x230..0x3FF and 0x450..0x5FF have no table index (type 0)'),
    (0xA2A478, '81eb800300004863c30fb744410883e03f4883c4205bc381fa000400007c0681ebd00100004863c30fb744410883e03f4883c4205bc3',
     'type getter: index = id - 0x380 (ids 0x600..), id - 0x1D0 (ids 0x400..0x44F) or id; type = u16 [table + 8 + index*2] & 0x3F'),
    (0x1E03790, '488d0509fde800c3', 'alias table getter: lea rax, 0x2C934A0; ret'),
    (0xA3C5B0, '488d05394c0302c3', 'equipment resource getter: lea rax, 0x2A711F0; ret'),
]
# --- Anim round 1, weapons (docs/anim-round1-weapons.md) ------------------------------------
# The reload rate rec+0x310 (partner_native_reload_rate=adapt): the reload handler 0x1077840 (modes 11/12) op 1
# stores the per-id provider's answer *(*(P+0x138)+0x130) vt+0x28(id, 6) at its enter, doubles it with the per-id
# status bit 26 (*(P+0x78)+0x280)[id - base]+0xC, and its op 2 (the exit) writes 1.0; the motion segment requests
# 0x1059620 / 0x10596B0 pass the current value by value to the motion controller vt+0x1C8; op 0 (every frame)
# requests the later segments through 0x1059620 (0x107A550). The hook writes only inside modes 11/12.
SITES += [
    (0x10778C8, '488b823801000041b806000000418bd7488b88300100000f29b42490000000488b01ff5028418bcff30f118710030000',
     'reload enter (op 1): rec+0x310 = provider vt+0x28(id, 6) (store at 0x10778F0, the native reload rate)'),
    (0x1077903, '488b828002000048c1e1058b54010cc1ea1af6c2017410f30f59052e250301f30f118710030000',
     'reload enter: per-id status bit 26 doubles rec+0x310 (store at 0x1077922)'),
    (0x10790D9, 'c787100300000000803f', 'reload exit (op 2): rec+0x310 = 1.0'),
    (0x1059666, '92b8010000488b4f60448b8b7c020000f30f1093100300004c8b018b934002000041c1e9094180e10141ff90c80100',
     'motion segment 0x1059620: motion vt+0x1C8(rec+0x240, rec+0x310 by value)'),
    (0x10596F6, '92b8010000488b4f60448b8b7c020000f30f1093100300004c8b018b934402000041c1e9094180e10141ff90c80100',
     'motion segment 0x10596B0: motion vt+0x1C8(rec+0x244, rec+0x310 by value)'),
    (0x107A541, '4c8d45774c8bcfba02000000498bcee8cbf0fdff', 'reload op 0 (per frame): a later segment through 0x1059620'),
    # Fire census (partner_fire_census=1, read-only): the RequestBullet record fields 0x1302F50 fills before the
    # call (+0x2E the bullet id u16 from the equip parameters +0x74/+0x76, +0x30 / +0x31 bytes, +0x32 flags).
    (0x1303170, '84ff7407410fb74676eb05410fb746746689472e4584ff7407410fb6467deb05410fb6467c884730',
     'model group shot: record+0x2E = the bullet id (+0x76/+0x74), record+0x30 = the kind byte (+0x7D/+0x7C)'),
    # The class-3 projectile request (0x13030B7: shot class 3 -> 0x1303450): the request on the stack at rbp-0x70
    # carries the shot record's owner at +0x32 and goes to *(*(*(0xBFF050())+0x98)+0x210) vt+0 (rax unused after).
    (0x13035A6, '488b8e80000000668955be0fb7d2488b01ff1083f80a7515488b8e800000000fb755be488b01ff5008668945be0fb7472a',
     'projectile request: +0x2E = the projectile id (equip parameter +0x30/+0x32, remapped by group+0x80)'),
    (0x13035D3, '0fb7472a668945c20fb7472c668945c4', 'projectile request: +0x32 = shot record+0x2A (the owner), +0x34 = record+0x2C'),
    (0x1303680, 'e8cbb98fff488d5590488b8898000000488b891002000048894c2468488b01ff10',
     'projectile request: gm+0x210 vt+0 (rcx = the service iface, rdx = the request rbp-0x70)'),
    (0x1337760, '48895c2408574883ec20488bfa488bd9e8db788cff', 'projectile service request 0x1337760 (iface vt+0) prologue'),
    (0x133648A, '488d0587c80a01488903488d05b5c80a0148894320',
     'projectile service factory: +0 = 0x23E2D18, the live iface +0x20 = 0x23E2D50 (slot 0 = 0x1337760)'),
    (0x1336065, '488d7920488d0560cb0a01488907488d0506cc0a0148894128488d052bcc0a0148894138e8c28f8cff488b88980000004839b910020000750b48c7811002000000000000',
     'projectile service dtor: iface +0x20 = 0x23E2BD0; gm+0x210 cleared if it is this iface'),
    # The rocket punch (gun mode 53, 0x107FA30): the request at rsp+0x40 gets the owner id & 0x1FF at +0x32 and goes to
    # the projectile service's vt+0x58 = 0x1337920 (slot 11; rax unused after the call).
    (0x1080624, 'b9ff0100006623c16689442472e81aeab7ff488d542440488b8898000000488b9910020000488bcb488b03ff5058',
     'rocket punch request: +0x32 = id & 0x1FF (rsp+0x72), gm+0x210 vt+0x58 with rdx = rsp+0x40'),
    (0x1337920, '48895c24084889742410574883ec50488b8180000000488bf9488bf2', 'projectile service vt+0x58 0x1337920 prologue (this, request)'),
    # Thrown objects (gun mode 37 0x1087210 and its helper 0x1061D90): the request gets the owner id & 0x1FF at +0x32
    # and goes to gm+0x200 vt+8 = 0x15E0230; skipped only for an online non-local id (P+0x204 bit 8 and id != P+0x218).
    (0x1061DF9, 'b9ff0100004c89b424b00000000f29b424a00000000f57f60f2975c70f2975e70fb7c741b8ffff000041beff0700006623c166448945f766448945ff668945f9',
     'throw request 0x1061D90: +0x32 = id & 0x1FF (rbp-7; base rbp-0x39)'),
    (0x1061F06, 'e845d1b9ff488d55c7488b8898000000488b8900020000488b01ff5008', 'throw request 0x1061D90: gm+0x200 vt+8 with rdx = rbp-0x39'),
    (0x10879BF, '488b8a380100008b8104020000c1e808a80174083bb918020000751780bbc202000000750e448bc7488bd3498bcee89ea3fdff',
     'throw (mode 37): 0x1061D90 skipped only online for a non-local id'),
    (0x1087A27, '498b56084c8b8238010000418b8004020000c1e808a801740d413bb8180200000f8567030000',
     'throw (mode 37): the main request skipped only online for a non-local id'),
    (0x1087A93, '41bdff010000668945c30fb7c741b9ffff00006644894db7664123c5668945b9',
     'throw (mode 37): +0x32 = id & 0x1FF (rbp-0x47; base rbp-0x79)'),
    (0x1087BD8, 'e87374b7ff488d5587488b8898000000488b8900020000488b01ff5008', 'throw (mode 37): gm+0x200 vt+8 with rdx = rbp-0x79'),
    (0x15E0230, '48895c2418574883ec20f6423e01488bfa488bd9', 'thrown-object service vt+8 0x15E0230 prologue (this, request)'),
    (0x15DE2AA, '488d05efb7e400488903488d0515b8e40048894320', 'thrown-object service factory: the live iface +0x20 = 0x2429AD0'),
    (0x15DDF9B, '488d05ceb9e400488901488d7920488d05f0b9e400488907488d0576bae40048894150488d058bbae40048894160e8821062ff488b88980000004839b900020000750b48c7810002000000000000',
     'thrown-object service dtor: iface +0x20 = 0x24299A0; gm+0x200 cleared if it is this iface'),
    # Placed objects (gun mode 46 0x107BAC0, rbp = rsp+0x100): the request at rsp+0x60 gets the equip at +0x30 and the
    # owner id & 0x1FF at +0x32 and goes to gm+0x208 vt+0x48 = 0xFCCD80.
    (0x107BAC0, '4c8bdc55535657415441554156488d6c24b04881ec50010000', 'place (mode 46) frame: rbp = rsp + 0x100'),
    (0x107C7DC, '498b4608b9ff0100004c8ba0380100000fb7874c020000668945900fb7c64c8965f06623c166894592',
     'place (mode 46): +0x30 = rec+0x24C (rbp-0x70), +0x32 = id & 0x1FF (rbp-0x6E)'),
    (0x107CA18, 'e83326b8ff488d542460488b8898000000488b8908020000488b01ff5048', 'place (mode 46): gm+0x208 vt+0x48 with rdx = rsp+0x60'),
    (0xFCCD80, '48895c24084889742410574883ec20488bd9488b89b0010000', 'placed-object service vt+0x48 0xFCCD80 prologue (this, request)'),
    (0xFC79DA, '488d05b7253e01488903488d05e5253e0148894320', 'placed-object service factory: the live iface +0x20 = 0x23A9FD0'),
    (0xFC52DB, '488d050e4a3e01488901488d7920488d05304a3e01488907488d05364b3e0148894140488d05bb4b3e0148894150e8429dc3ff488b88980000004839b908020000750b48c7810802000000000000',
     'placed-object service dtor: iface +0x20 = 0x23A9D20; gm+0x208 cleared if it is this iface'),
]
# --- Native thrown support v1 (docs/anim-round2-throws.md).
SITES += [
    (0x108C887, 'c3cccccccccccccccc', 'support stock guard: predecessor ret and all eight int3 bytes (padding ends at next function 0x108C890)'),
    (0x108C8C0, '48894c2408555641554883ec50488b4108458be8488bf24c8b8838010000488daab0010000418b8104020000', 'support stock guard: ammo-copy entry, P chain and full seven-byte load at 0x108C8E5'),
    (0x108C8EC, 'c1e808a8017427453b8118020000741e48c785ac000000ffffffff818ac0030000000000204883c450415d5e5dc3', 'support stock guard: original online/local test, non-consuming counts and bit-29 branch through ret'),
    (0x1087BF5, '498b4e608b9340020000488b014533c0ff9060010000f783c0030000000000200f8591010000', 'support throw: mode-37 [prim+0x60] vt+0x160(rec+0x240, 0) at 0x1087C05 (resolved: 0xDC1740) and bit-29 branch over all stock writes'),
    (0x10880CC, '488b4e60488b01ff90600100004d8bcf448b', 'support throw: mode-30 [prim+0x60] vt+0x160(rec+0x240, 0) at 0x10880D3 (resolved: 0xDC1740)'),
    (0x108AA84, '488b4d608b9740020000488b01ff9060010000', 'support throw: mode-33 [prim+0x60] vt+0x160(rec+0x240, 1) at 0x108AA91 (resolved: 0xDC1740)'),
    (0x15E023A, 'f6423e01488bfa488bd974164881c100010000488b5c24404883c4205fe9c4ddffff', 'support entry filter must precede preview-bit branch and shared service+0x100 copy'),
    (0x15E02B5, '4885c0740b488bd7488bc8e85bddffff488b6c2430488b742438488b5c244048', 'support entry filter: null pool result returns zero in original return domain'),
    (0x15DE020, '0f28024c8bc141b9001800000f29010f284a100f2949100f2842200f2941200f', 'support preview copy helper entry (read-only evidence; owner filter drops before it)'),
    (0x11AC2E0, '40535556415441564881eca0000000488b053ad099014833', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0x1156640, '488b8168010000488b89a80100002b500c33c04869d2b000', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0x1209EC0, '488bc4555356574154415541564157488da8e8fcffff4881', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0x11B0700, '4053555657415641574883ec68488b051c8c99014833c448', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0x11B0900, '48895c240848896c24104889742418574883ec400fb68424', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0x11AFF30, '48895c241848896c242056574155415641574883ec30488b', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0xEE9880, '48895c240848896c24104889742420574154415541564157', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0xEE9D70, '48895c240848896c24104889742418574883ec20448b490c', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0xB0C720, '44894c2420574883ec4048c7442420feffffff48895c2450', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0xDC3250, '44894c24205341544155415641574883ec204c8be1488b49', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0xDC2A50, '664489442418895424104883ec288bc24c8bc983e01f0f84', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
    (0xDC63A0, '488974241889542410574883ec208bc28bf2488bf983e01f', 'support equip audited callee; complete body SHA256 also pinned by equipment_stock_writer_pins.hpp'),
]
# --- WS3 vehicles (docs/vehicle-ws3.md): the driver seated in an unnamed vehicle by id (B1)
# and the viewer's copy tilted with the driver's record angles (A1, census first).
VEHICLE_SITES = [
    # B1: the DM binding turns a locator into the entry's target id and flag 2 (an empty locator
    # stores 0xFFFF and no flag); the native id path writes exactly those two fields later.
    (0x9DAAE1, '488b4424308038007437488d542458488d4c2430e8366b71ff488b542458488d4c2420e8079621000fb70866894c754866413bcd0f8481000000804c354002eb7a6644896c7548eb72',
     'DM binding: locator "" -> id 0xFFFF, no flag; else id = 0xBF4110(name) and flag 2 when id != 0xFFFF'),
    # The vehicle unit lookup (service vt+0x18, the DM anchor's own call): object = service+0xB0,
    # count = object vt+0, live bit object+0x28, record = object+0x10 + (id & 0x1FF) * 0x80.
    (0xA272B5, '488bf1488b89b0000000410fb7f94d8bf0488bda4885c97508c702ffffffffeb4b488b01ff10663bf87208c703ffffffffeb394c8b96b0000000440fb7cf498b5228',
     'vehicle lookup 0xA272A0: object = service+0xB0, index < object vt+0(), bitmap object+0x28'),
    (0xA272F7, '410fb6c1458bc1241f49c1e8050fb6c8428b04820fa3c873d049c1e107c703000000004d034a104d890e488b742438488b7c2440488bc3488b5c24304883c420415ec3',
     'vehicle lookup: live bit, record = object+0x10 + index << 7'),
    (0xB2DD30, '0fb781c0010000c3', 'vehicle object vt+0 = count: u16 object+0x1C0'),
    (0xB2B7AD, '498b6d304c89b424b00000004c89bc24a80000000f29b424900000004d8bf24d8bfa49c1e6074903ee',
     'TppVehicle2 SendCommand: record = primary+0x30 + index << 7 (object = primary+0x20)'),
    (0xB2BA5D, '498b4530f3410f104c062c0f5ac9e9', 'TppVehicle2 GetPosition: rotY = record+0x2C'),
    (0xB2BA9F, '3d485692540f85a2fdffff', 'TppVehicle2 SendCommand: SetPosition (0x54925648)'),
    (0xB2BB2D, '498b4520498d4d200f57c9f20f5ac80f28c60f557424304c8d442460488bd50fc6c9000f54c10f56c60f29442460ff5078',
     'SetPosition: object vt+0x78(object, record, vec4(x, y, z, rotY))'),
    (0xB36780, '488b89a8010000488b0148ff6070', 'object vt+0x78 0xB36780: V = object+0x1A8; jmp V vt+0x70'),
    (0x16FBEDC, '807a6eff498bd8488bfa488bf1740d488b8998000000488b01ff50780f28030f294720',
     '0x16FBEC0: realized (record+0x6E != 0xFF) -> RB = V+0x98, RB vt+0x78; then record+0x20 = vec4'),
    (0x16FDDC0, '440fb64a6c488b417049c1e9044a8b0cc84d8bc8440fb6426f488b010fb7527848ffa0f0010000',
     '0x16FDDC0: cat = (RB+0x70)[record+0x6C >> 4]; cat vt+0x1F0(cat, record+0x78, record+0x6F, vec4)'),
    (0xBA9747, '488b81c8010000410fb6f849897310488b34f8488b01400fb6d74d8bf1488bd9ff50',
     '0xBA9710: body = (cat+0x1C8)[slot]'),
    (0xBA97CA, '488d542430488bce440f299c24d0000000440f294c2440440f29a424c0000000e8b1459bff',
     '0xBA9710: 0x55DDA0(body) (a small move keeps the body Euler angles and adds the yaw delta)'),
    (0xBA9812, '0f2f05b3da4f010f83e90300000f28c70f540507e94f010f2f0538e84f010f83d2030000',
     '0xBA9710: the small move only below 1.0 (squared m, 0x20A72CC) and 0.1 rad (0x20A8068)'),
    (0x55DBC8, '440f286920488bda488d5597e8c7010000', '0x55DB80 (the seat anchor world matrix): position record+0x20, 0x55DDA0(record)'),
    (0x55DDF6, 'f30f104148', '0x55DDA0 reads +0x48'),
    (0x55DF6D, 'f30f104144', '0x55DDA0 reads +0x44'),
    (0x55E0D9, 'f30f10492c', '0x55DDA0 reads +0x2C (yaw)'),
]
SITES += VEHICLE_SITES
# The two small-move limits 0xBA9710 compares against (read-only .edata dwords; vehicle_assertions
# and partner_vehicle::VerifyImage check them): 1.0f squared metres and 0.1f radians.
VEHICLE_DATA = [(0x20A72CC, 0x3F800000), (0x20A8068, 0x3DCCCCCD)]
# --- F1 vehicles (partner_board, docs/native-puppet.md "Vehicle boarding", research re-vehicle.md): the board block
# and passengers both ways. Site A 0x12458AC (the ride pre-query 0x1245820's CALL 0x124B3A0) and site B 0x1251479
# (phase 1 0x1251200's CALL 0x124B360) are retargeted to the board wrappers; their rel32 operands are masked by
# partner_board::VerifyImage (the code gate checks the original bytes once, before any lazy install). The wrappers
# call the nearest search and the two getters below; block relies on the pre-query failing CanExec and on phase 1's
# stock "no seat" exit, passenger on phase 1 passing a back seat unchanged to TryToRide and selecting the passenger
# phase (act 0x6E); the local passenger report reads P+0x120[body] (the ridden unit) and Q+0xE0[body] (the vehicle id).
BOARD_SITES = [
    (0x1245877, '498bce0f284020660f7f8424a0000000e834247eff41b901000000488d9424a0000000fec80fb6c8413ac9440f42c94533c0498bce'
     'e8ef5a0000488d542454498bce440fb6f8e8ce580000',
     'board site A: the pre-query 0x1245820 bounds first 0 / last min(count-1, 1) and calls 0x124B3A0(unit, pos) at '
     '0x12458AC; the seat goes to r15b'),
    (0x125145C, '8bd6488b4878488b8938020000488b01ff9088030000498bce488d5020e8e29effff440fb6e03c040f844fffffff498bcee85ed330ff'
     '8be884c0757f',
     'board site B: phase 1 0x1251200 calls 0x124B360(unit, CS vt+0x388(body)+0x20) at 0x1251479; 4 -> the no-seat '
     'exit 0x12513D9; then GetVehicleType 0x55E7F0'),
    (0x1246A3F, '488d442440418bd7488bce4889442420e8ccedffff84c00f8417040000',
     'RideVehicle CanExec 0x1246650: the pre-query 0x1245820 false -> 0x1246E73 (return false: no ride icon)'),
    (0x124B360, '48895c2408574883ec20488bda488bf9e84bc97dfffec841b901000000488bd3440fb6c0488bcf453ac1450f42c84533c0488b5c2430'
     '4883c4205fe900000000',
     'SearchNearestRideSeatForDriver 0x124B360 (whole body): last = min(count-1, 1), first 0, jmp 0x124B3A0'),
    (0x124B3A0, '4056574154415641574881ecd0000000450fb6f1410fb6f84c8be24889542420488bf1e8883c9bff4c8b90980000000fb6566c498b'
     '8aa8020000c1ea04488b01ff90680100004c8bf84885c07512',
     'nearest free seat 0x124B3A0(unit, pos, first r8b, last r9b): the category through service vt+0x168'),
    (0x124B3EE, 'b0044881c4d0000000415f415e415c5f5ec3', 'nearest free seat: no category -> 4'),
    (0x124B454, 'f30f10355cf3e500400fb6c740b50441b8ffff00000f287c2460440f28442450440f284c2440440f28542430',
     'nearest free seat: radius 100.0 (0x20AA7B8, 10 m squared), result 4 until a free seat is nearer'),
    (0x124B480, '3bc30f8da50000000f1f840000000000413afe0f8775010000400fb6c76644394446307579',
     'nearest free seat: seats first..last below the base count, only rider[seat] == 0xFFFF'),
    (0x55E7F0, '40534883ec20488bd9e852086a00488b9098000000488b8aa80200000fb6536c488b01c1ea04ff90680100004885c0750d0fb6436c'
     'c1e8044883c4205bc30fb680b60100004883c4205bc3',
     'GetVehicleType 0x55E7F0 (whole body): cat+0x1B6, or unit+0x6C >> 4 without a category'),
    (0xA27CC0, '48895c240848896c24104889742418574883ec20488be932dbe872731d00488b9098000000488bbaa80200000fb6556c488b07c1ea04'
     '488bcfff9068010000488bf04885c07428',
     'seat count 0xA27CC0 (1/2): the category through service vt+0x168; none -> 0'),
    (0xA27D07, '488b070fb6556c488bcfff90700100004885c074070fb698b50000000fb686b70100000fb6cb03c1488b5c2430488b6c2438488b7424'
     '404883c4205fc3',
     'seat count 0xA27CC0 (2/2): cat+0x1B7 + the attachment +0xB5 (service vt+0x170)'),
    (0xA27220, '885424104883ec28488b010fb6d2c1ea04ff90680100004885c075054883c428c30fb64c2438488b80d001000083e10f488b04c8488'
     '3c428c3',
     'service vt+0x170 0xA27220 (whole body): the attachment = (category+0x1D0)[type & 0xF]'),
    (0xA27260, '4863c2488b84c1b8000000c3', 'service vt+0x168 0xA27260 (whole body): the category = service+0xB8[index]'),
    (0x1251822, '4d85f60f84910000000fb64b570fb6c1240f3c0272080fb6c183e00feb03418bc7440fb7c60fb6d0498bce664523c4e81a7ca6ff',
     'phase 1: seat = (sel & 0xF) < 2 ? 0 : sel (a back seat passes unchanged); TryToRide 0xCB9470(unit, seat, id)'),
    (0x125188E, '0fb64357240f3c020f92c00402884355e8add79aff488b8898000000410fb74678488b91a802000066894220',
     'phase 1: next phase = (sel < 2) + 2 (3 driver, 2 passenger 0x1250CF0); VehicleSystem+0x20 = unit+0x78'),
    (0xA2956A, '488d0c77b8ffff0000663941307405403' '2ffeb240fb7442458668941304084f67513b900fe00006685c17509b800400000660947'
     '7c',
     'TryToRide 0xA29530: stores the id only into an empty rider[seat]; the player-driver flag +0x7C 0x4000 only '
     'for seat 0'),
    (0x1250E96, '488b823801000041b8040000008bd7488b4860488b4118b96e00000066890c78',
     'passenger phase 0x1250CF0 enter: act (*(Q+0x18))[body] = 0x6E'),
    (0x125AD61, '488b4508488b6c2450488b8038010000488b802001000048891cf0',
     '0x125AD00: (*(P+0x120))[body] = the ridden unit (the local passenger report)'),
    (0x12CF42F, '498b85e00000000fb70c706641898c2410010000',
     'UpdatePlayerInfo 0x12CE870: vars+0x110 (playerVehicleGameObjectId) = (*(Q+0xE0))[body]'),
]
SITES += BOARD_SITES
# --- Handle call hardening (WS1, docs/anim-round2-throws.md "Handle call resolved"): [prim+0x60] is the
# process-wide equipment-handle manager X = [[gm]+0x98]+0x1E8; X vt+0x160 = 0xDC1740 (pin bit + model part),
# vt+0x38 = 0xDC64E0 (model instance release), vt+0x40 = 0xDC0350 (has-model). rec+0x240 is the handle every
# gun-plugin call passes; partner_input_hook requires it to equal the Match-verified slot-3 handle (B) and
# checks its X+0x140 row per body-1 job (C). Whole bodies are pinned in <= 80-byte parts.
SITES += [
    (0x101E1D4, 'e8770ebeff4c8b8098000000498b88e801000048894f60',
     'gun prim base ctor 0x101DE90: prim+0x60 = [[0xBFF050()]+0x98]+0x1E8 (the equipment-handle manager X)'),
    (0x103C944, 'e84715feff90488d052f753701488903488d050d76370148894310488d053a76370148894330488d056f76370148894348488d05c476370148894350',
     'gun prim factory 0x103C910: base ctor, then vtables 0x23B3E80, +0x10 0x23B3F68, +0x30 0x23B3FA0, +0x48 0x23B3FE0, +0x50 0x23B4040'),
    (0x1089289, 'c683c00200001c83c9304088bbc2020000c783740300006666563f25fffdfffe03c94533c00bc8898bcc0300004183a5cc000000f3488b4d60488b018b9340020000ff9060010000',
     'mode-28 hold hub: mode 0x1C, flag r8d = 0, then [prim+0x60] vt+0x160(rec+0x240, 0) at 0x10892CB'),
    (0x1062629, '8b8f7c020000c1e90683e1078d41fe83f8017639',
     'gun iface reset 0x1061F50: (rec+0x27C>>6)&7 in {2,3} -> 0x1062676'),
    (0x1062676, '488b4e30488b018b9740020000ff5038488b4e30488b014533c08b9740020000ff9060010000',
     'gun iface reset: [iface+0x30 = prim+0x60] vt+0x38(rec+0x240) at 0x1062683, then vt+0x160(rec+0x240, 0) at 0x1062696'),
    (0x105FDD8, '488b41084533e44c8bf9488b88380100000fb682de030000418bf0488b99c800000044894424384c8bf2488b7b08',
     'activation 0x105FDB0 entry: r15 = prim, rcx = [[prim+8]+0x138], esi = id, rbx = ctl = [rcx+0xC8], r14 = rec, '
     'rdi = Y = [ctl+8] (the operands of the 0x105FEB4 load and the 0x105FF1A call)'),
    (0x105FEA5, '410fb696d9030000418886dc030000',
     'activation 0x105FDB0: the handle slot index is rec+0x3D9 (hand slot) for the load at 0x105FEB4'),
    (0x105FF1A, '498b4f608bd3488b01ff5020450fb68ed9030000448be88bc383e01f89442440',
     'activation: r13d = [prim+0x60] (X) vt+0x20(ebx = the loaded handle), stored as rec+0x24C at 0x10602F0; '
     'kind = ebx & 0x1F to eax and [rsp+0x40]'),
    (0x105FF60, '8b4424404181a680020000fffffffe498db6b00100004489642430c6442434014489a6a800000083f8090f85e9000000',
     'activation: eax = kind; rsi = rec+0x1B0 (rsi+0x90 = rec+0x240, rsi+0x9C = rec+0x24C); only kind 9 falls through '
     'to the slot-2 reload of ebx/r13d (0x1060000, 0x1060041), other kinds jump to 0x1060079'),
    (0x106018A, '8b7c2440c7442444ff0700008d47ff83f8070f87c3020000',
     'activation: edi = kind; kinds outside 1..8 (12 among them) -> 0x1060465'),
    (0x1060465, '448864243483ff0c0f8456020000', 'activation: kind 12 -> 0x10606C9'),
    (0x1060767, 'e93cfbffff', 'activation: the kind-12 block 0x10606C9 joins the rec+0x240/rec+0x24C stores at 0x10602A8'),
    (0x10602E8, '899e900000000bf84489ae9c000000',
     'activation: rec+0x240 = the loaded handle, rec+0x24C = X vt+0x20(handle)'),
    (0xDC1740, '48895c240889542410574883ec30c1ea10410fb6f8488bd90fb7c2488d1440488b8140010000806450047f4c8d0c504584c00f95c0c0e00741084104488b018b542448ff504084c0746b448b4c244844',
     'X vt+0x160 0xDC1740 (1/3): row = X+0x140 + (h>>16)*6; row[4] bit 7 = flag; no owner/bounds/equip check; then X vt+0x40(h)'),
    (0xDC1790, '8b830c01000033d2458bd141c1ea104585c0741d4c8b9b800100000f1f440000420fb60c1a413bca740affc2413bd072ef418bd0488b83400100004f8d0452488d4be0460fb6544004460fb704404183',
     'X vt+0x160 0xDC1740 (2/3): instance slot n = index of (h>>16) in X+0x180[0..X+0x10C); row equip word and row[4]&0x7F'),
    (0xDC17E0, 'e11f4183e27f448954242840887c2420e80b000000488b5c24404883c4305fc3',
     'X vt+0x160 0xDC1740 (3/3): 0xDC1800(X-0x20, n, equip, kind, flag, row[4]&0x7F); ret at 0xDC17FF'),
    (0xDC1800, '4889742410574883ec30418bf88bf23b912c0100000f83dc0000004c8b91b0010000418d41ff48895c2440488b99b801000083f8070f86b70000004183e90c744d41ffc974334183f9040f85a2000000',
     'handle part toggle 0xDC1800 (1/4): n < X+0x10C; kinds 1..8 return; kinds 12/13/17 dispatch'),
    (0xDC1850, '81eff3010000741583ff1a0f859100000049b85dcca7fa87950000eb5749b80b9e03a4789d0000eb4b4181f89d010000757049b8f2bdc59fe62e0000eb36807c246000498b0249b8fdfeca49210b0000',
     'handle part toggle 0xDC1800 (2/4): kind 17 equips 0x1F3/0x20D, kind 13 equip 0x19D, kind 12 model part 0xB2149CAFEFD'),
    (0xDC18A0, '410f94c1498bcaff90000100008d8782feffff83f802773a49b841c09f9fe24b00008bd6488b034533c9488bcb44384c2460741bc7442460989f16bf488b5c2440488b7424484883c4305f48ff6008ff',
     'handle part toggle 0xDC1800 (3/4): [X+0x190] vt+0x100(n, part, !flag); equips 0x17E..0x180 effect [X+0x198] vt+8/vt+0x10'),
    (0xDC18F0, '5010488b5c2440488b7424484883c4305fc3', 'handle part toggle 0xDC1800 (4/4): effect stop call and ret at 0xDC1901'),
    (0xDC0350, '8bc24c8bc983e01f7502f3c3ffc8c1ea1083f807771a488b8128010000440fb7c2488b481833c042833c81ff0f95c0c30fb7c2488d0c40498b81400100000fb644480583e001c3',
     'X vt+0x40 0xDC0350 (leaf, whole body): kind 0 -> 0; kinds 1..8 [[X+0x128]+0x18][idx] != -1; else row[5] bit 0'),
    (0xDC64E0, '4883ec28448bc2488bd1418bc083e01f7447ffc841c1e81083f807771e488b9128010000488b422042802400fe488b422042800c00024883c428c3488b82400100004b8d0c40f644480501740c488d4a',
     'X vt+0x38 0xDC64E0 (1/2): kinds 1..8 flag bytes; else if row[5] bit 0 -> 0xDC6540(X-0x20, idx) (model instance release)'),
    (0xDC6530, 'e0418bd0e8070000004883c428c3', 'X vt+0x38 0xDC64E0 (2/2): call 0xDC6540; ret at 0xDC653D'),
]
# --- WS4 step 0: the read-only placed / arm census (partner_placed_census, lab key partner_placed_census=1). No patch,
# no call: these pin the layouts the census reads. The placed-object service owner table as the detonation 0xFC54F0
# (placed iface vt+0x68, pinned below) reads it: owners u16 iface+0xB0[0..u32 iface+0x6C), per-owner slots u32
# iface+0x70 from u32 iface+0x68 + per*index, slot flags iface+0x90 (stride 3, bit 0), slot equip u16 iface+0x18
# (stride 4), age u16 iface+0xD8[per*index + j]. And mode 46 op 1's gate: offline (P+0x204 bit 8 clear) or the local
# id goes to 0x107BC9B (0x104CF00, then the plugin latch +0x350 bit 3 / u16 +0x3E8 at 0x107BD64..0x107BD8B); the
# online-remote branch folds rec+0x3CC bits 22-23 into 20-21 and jumps to 0x107BD93 past them.
SITES += [
    (0xFC557A, '448b436c0fb754244833c04585c07417488b8bb0000000663911740effc04883c102413bc072f0418bc0413bc074ab',
     'placed detonate 0xFC54F0: index of the owner (u16 [rsp+0x48]) in u16 iface+0xB0[0..iface+0x6C); unknown -> 0xFC5554'),
    (0xFC55A9, '8b4b708bbbe000000085c9745c448b4b68448bd14c897424404c8bb390000000440fafd0448bd94503ca4b8d044e41f6040101'
     '7424488b4318420fb70c88',
     'placed detonate 0xFC54F0: slots first iface+0x68 + per iface+0x70 * index; flags iface+0x90 stride 3 bit 0; '
     'equip u16 iface+0x18 stride 4'),
    (0xFC55E7, '3bcd7517488b83d8000000460fb70450443bc77306418bf8418bf141ffc141ffc249ffcb75c64c8b742440',
     'placed detonate 0xFC54F0: the oldest live slot of that equip by age u16 iface+0xD8[per*index + j]'),
    (0x107BC5B, '498b4608488b88380100008b8104020000c1e808a80174283bb11802000074208b87cc0300008bc8c1e90233c881e10000300033c8'
     '898fcc030000e9f8000000',
     'place (mode 46) op 1 gate: offline or the local id -> 0x107BC9B (the shared plugin latch); online remote folds '
     'rec+0x3CC bits 22-23 into 20-21, jmp 0x107BD93'),
]
# --- WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): the C4 and arm lanes. No patch. ChangeEquip 0x11AC2E0 (whole
# body SHA-pinned in equipment_stock_writer_pins.hpp) calls ctl vt+0x1F0 = 0x11B1B60 for type 14 and activates slot 6
# with flags bit 2. 0x11B1B60 (whole body below): equip outside 0x203..0x209 -> 0x207; for the local id only the
# energy/UI calls; current != equip -> ctl vt+0xA0(out, id, 6, equip, 0, 1, 1) and ctl vt+0x98(id, 6, 0, equip, 0, 0,
# 0); the local id also ctl vt+0x118. Read-only digest sites: the viewer's placement latch (mode 46 op 1's store
# block, r14 = the record's own prim) and the arm energy ctl vt+0xE8 0x11AF820 -> 0x11AF5E0 writes with no instance
# id (Y+0x1C[i], the stock word through ctl vt+0xC0, ctl+0x58[i], vars+0x670), read back through store vt+0x88.
# Bytes from the scratch REA .xtext dump; this generator re-reads them from the exact executable.
SITES += [
    (0x11AC379, '83f80e75244c8b1341b101458bc78bd5488bcbc644242801c64424200141ff92f0010000e9fe000000',
     'ChangeEquip: type 14 (cmp eax,0xe) calls ctl vt+0x1F0(ctl, id, equip, 1, 1, 1) instead of the restriction '
     'predicate and the lower writer, then the flags-4 activation 0x11AC4A0'),
    (0x11AC4C6, '83ff0e75058d77f8eb3d', 'ChangeEquip flags 4: type 14 -> slot 6 (lea esi,[rdi-8]), then SetCurrentSlot(id, 6, 0)'),
    (0x11B1B60, '48895c240848896c241848897424205741544155415641574883ec40488bf9418d80fdfdffffb90702000083f806488b4728418bf0'
     '4c8b47084c8b60100f47f1418b480c498b4040448bf233ed410fb6',
     'ctl vt+0x1F0 0x11B1B60 (1/7): arm equip; equip outside 0x203..0x209 -> 0x207; Y = [ctl+8]'),
    (0x11B1BB0, 'd90fafca83c106baffff00008b04888bc8c1e910663bca740fa81f740b8be8c1ed0581e5ff070000488b4718440fb6ac2490000000'
     '443bb014020000410f94c74584ff0f84d3000000498b04248bd649',
     'ctl vt+0x1F0 (2/7): current = (Y+0x40[id*[Y+0xC]+6] >> 5) & 0x7FF if valid; local = id == P+0x214; '
     'non-local -> 0x11B1CCC'),
    (0x11B1C00, '8bccff506080bc2498000000008944247874164c8b07ba05000000488bcf41ff90000100008b4424788d8ef9fdffff83f9020f8694'
     '00000085c00f848c00000084db0f8484000000488b07498b1c248b',
     'ctl vt+0x1F0 (3/7): local only: [[ctl+0x28]+0x10] vt+0x60, ctl vt+0x100(5)'),
    (0x11B1C50, 'd6488bcfff90380100008bd6498bcc448bc0ff5358488b4f08448b6424780fb7d8488b01458bc4448bcbba05000000895c2420ff50'
     '70488b47180fb7d3488b88d8000000488b01ff90b0010000488b07',
     'ctl vt+0x1F0 (4/7): local only: Y vt+0x70(5), UI [P+0xD8] vt+0x1B0'),
    (0x11B1CA0, '44886c2438c64424300041b905000000448bc60fb7d3488bcfc7442428060000004489642420ff90e00000003bee7459488b07c644'
     '243801c644243001488d54247841b906000000458bc6488bcfc744',
     'ctl vt+0x1F0 (5/7): local only ctl vt+0xE0; any id: current != equip -> ctl vt+0xA0(out, id, 6, equip, 0, 1, 1)'),
    (0x11B1CF0, '24280000000089742420ff90a0000000488b07c6442438004533c9c644243000418bd6458d4106488bcfc64424280089742420ff90'
     '980000004584ff741b4584ed7416488b074533c0448bce418d5006',
     'ctl vt+0x1F0 (6/7): ctl vt+0x98(id, 6, 0, equip, 0, 0, 0); the local id only: ctl vt+0x118'),
    (0x11B1D40, '488bcfff90180100004c8d5c2440498b5b30498b6b40498b7348498be3415f415e415d415c5fc3',
     'ctl vt+0x1F0 (7/7): epilogue, ret at 0x11B1D66'),
    (0x11AF820, '4883ec580f28d1f30f2cd281fa000000807421660f6ec20f5bc00f2ec274150f14d20f50c283e00183f00103d0660f6ed20f5bd2f348'
     '0f2cc2baf0d20000663bc2660f42d00fb68424980000004883c1',
     'ctl vt+0xE8 0x11AF820 (1/2): the arm energy setter (mode 53 via 0x10546F0); clamp to 0xD2F0; rcx = ctl - 0x20'),
    (0x11AF870, 'e0884424400fb6842490000000884424388b842488000000894424308b8424800000008944242844894c2420458bc8440fb7c2e838'
     'fdffff4883c458c3',
     'ctl vt+0xE8 (2/2): call 0x11AF5E0; ret'),
    (0x11AF5F1, '81ffeb010000743181ffec010000742281ffed01000074138d87fdfdffff83f8030f87ff01000033edeb13bd03000000eb0cbd0100'
     '0000eb05bd02000000',
     'arm energy 0x11AF5E0: index 0 for 0x203..0x206 (515..518), 2/1/3 for 0x1EB/0x1EC/0x1ED; other equips return'),
    (0x11AF65D, '488b46288b5424608bcd41bff0d2000066453bc7f30f115c881c',
     'arm energy 0x11AF5E0: Y = [ctl+8] (rsi = ctl - 0x20); movss [Y+0x1C+4*index] = the energy (no instance id)'),
    (0x11AF78D, '4863c5894c86788b8e84000000488b4640038e80000000034e7c034e78898870060000',
     'arm energy 0x11AF5E0: u32 [ctl+0x58+4*index]; vars+0x670 = the sum of ctl+0x58..+0x64 (vars = [ctl+0x20])'),
    (0xEE9050, '488b81980000004863d20fb70450c3', 'store vt+0x88 0xEE9050: u16 [[Y+0x98] + 2k] (the stock / arm energy word)'),
    (0x107BD64, '410fb786d8030000663bc3741241838e500300000866418986e8030000eb104183a650030000f76641899ee8030000',
     'place (mode 46) op 1 latch block (r14 = the record\'s own gun prim): +0x3D8 != 0xFFFF -> +0x350 |= 8 and '
     '+0x3E8 = +0x3D8, else +0x350 &= ~8 and +0x3E8 = 0xFFFF'),
    (0x1060465, '448864243483ff0c0f8456020000498b4760418bd5488b4808488b01ff505084c00f853d02000083ff0d0f8532010000',
     'activation: kind 12 (or a kind whose equip [X+8] vt+0x50 accepts) -> 0x10606C9; kind 13 -> 0x1060495'),
    (0x1060495, '81a6cc000000fffeffff818ecc000000c0000000', 'activation kind 13: rec+0x27C bit 8 clear, bits 6-7 set (class 3)'),
    (0x10605BF, '418bc4e9e1fcffff83ff0e0f858f000000',
     'activation: kind 13 joins the stores 0x10602A8; kind 14 -> 0x10605D0 (the equip jump table 0x1061C70)'),
]
# --- WS4 v2 (docs/anim-round2-throws.md "WS4 v2"): the remote's C4 place (46) / detonate (47) mirrored into body
# 1's own rec+0x3CC before its job (partner_place_mirror; lab key partner_native_place_mirror). No patch. The gun
# update 0x103E2D0 calls the decode 0x1095DF0 (0x103E485, pinned above), then the mode's op 4 and op 0 (the next mode
# into rec+0x2C1), then the transition 0x1040A00; mode 43 op 0 picks 46 on bit 18, mode 44 op 0 46 on bit 18 and 47 on
# bit 19; the decode's class-3 path sets 18 (clearing 19) only on FIRE with a preview class in 22-23 and sets 19 only
# on ACTION with rec+0x280 bit 23 (a live charge); mode 46 op 1 clears 16-18, takes its probe's class into 20-21 and
# folds 22-23 into 20-21 when the probe gives 0; its placement (op 9) runs only for the local record (rec+0x3C0 bit
# 0); mode 47 op 1 clears 19 and op 10's detonation 0x1053270 only clears 19 for a non-local record.
# Bytes from the scratch REA .xtext dump; this generator re-reads them from the exact executable.
SITES += [
    (0x103E48A, '4883bbb002000000745f41b8040000008bd60f288bb00200004533c9660f6fc1660f73d808660f7ec04863c866480f7ec84803cfffd0',
     'gun update: after the decode, the current mode\'s member handler [rec+0x2B0] with op 4 (r8d = 4)'),
    (0x103E4C0, '4883bbb00200000074290f288bb00200004533c94533c0660f6fc18bd6660f73d808660f7ec04863c866480f7ec84803cfffd0',
     'gun update: then the same handler with op 0 (r8d = 0): the next mode into rec+0x2C1'),
    (0x103E4F3, '440fb68bc10200004585c9741a0fb683c0020000443bc8740e448bc6488bd7488bcbe8e6240000',
     'gun update: rec+0x2C1 != 0 and != rec+0x2C0 -> the transition 0x1040A00(rec, plugin, id) in the same update'),
    (0x107EBD3, '8b8bc003000033ff84c90f889f0000000fbae1090f82950000008b83cc030000c1e812a801740abf2e000000e98e000000',
     'place hold (mode 43) op 0: rec+0x3C0 bit 7 / bit 9 -> the selector; else rec+0x3CC bit 18 -> next mode 0x2E (46)'),
    (0x107DC2A, '8b8ec003000033ff84c9784c0fbae1097246f786c4030000000004007407bf30000000eb438b96cc0300008bc2c1e812a8017407bf2e00'
     '0000eb2dc1ea13f6c2017407bf2f000000eb1e',
     'place ready (mode 44) op 0: rec+0x3C4 bit 18 -> 0x30; rec+0x3CC bit 18 -> 0x2E (46); bit 19 -> 0x2F (47)'),
    (0x10967A5, '83f8030f85400100008b93c00300008bca8bc2c1e913c1e8120ac8f6c1010f84d2fdffff0fbae21a0f82c8fdffff',
     'decode 0x1095DF0: class 3 (C4) needs rec+0x3C0 bit 18 or 19 and not bit 26'),
    (0x109687C, '8b83cc0300000fbaf0130fbae8128983cc030000eb0d',
     'decode class 3: the only bit-18 write: btr 19, bts 18 (FIRE, a preview class in 22-23, the placed manager says yes)'),
    (0x109689F, '418b85d00000000fbae0170f83ebfcffff0fbae0180f82e1fcffff8b83cc030000c1e812a8010f85d0fcffff488b07488bcfff5040c1e8'
     '05a8010f84bcfcffff818bcc03000000000800e9adfcffff',
     'decode class 3: bit 19 only with rec+0x280 bit 23 (a live charge), not bit 24, bit 18 clear and pad bit 5 (ACTION)'),
    (0x107BBE1, '81a7cc030000fffff8ff', 'place (mode 46) op 1: rec+0x3CC &= ~0x70000 (consumes the bit-18 request)'),
    (0x107BD27, 'e8d411fdff81a7cc030000ffffcfff83e003c1e0140987cc0300008b8fcc030000f7c10000300075148bc1c1e80233c1250000300033c1'
     '8987cc030000',
     'place (mode 46) op 1, offline / local branch: probe 0x104CF00 -> rec+0x3CC bits 20-21; 0 -> fold bits 22-23 into 20-21'),
    (0x107C6FA, 'f687c0030000010f841c05000080bfc2020000040f830f050000',
     'place (mode 46) op 9: the placement block only for the local record (rec+0x3C0 bit 0) and state < 4'),
    (0x107EFCD, '81a3cc030000fffff7ff', 'detonate (mode 47) op 1: rec+0x3CC &= ~0x80000 (consumes the bit-19 request)'),
    (0x107F2CA, 'f683c003000002740b448bc7488bd3e8923ffdfff683c003000001741141b101448bc7488bd3488bcee848ddfbff',
     'detonate (mode 47) op 10: 0x1053270 behind rec+0x3C0 bit 1; 0x103D040 only for the local record (bit 0)'),
    (0x105327F, 'f682c003000001418bf0488bfa488be90f84a9000000',
     'detonation 0x1053270: a non-local record (rec+0x3C0 bit 0 clear) -> 0x105333E (no placed vt+0x68 call)'),
    (0x105333E, '81a2cc030000fffff7ff', 'detonation 0x1053270, non-local: only rec+0x3CC &= ~0x80000'),
    # Hidden run 289badb6: a class in 22-23 plus a FIRE press in one body-1 job sends the decode's class-3 path to the
    # placed iface vt+0x40 0xFCB1F0, whose 0xFC9C00 claims the free owner row for id 1 (iface+0xB0 65535 -> 1). The
    # mirror writes the class only when the press word, read as PP vt+0x40 reads it, has no FIRE, and aligns 22-23
    # only inside 46 (rec+0x3C0 bit 26 set by 46 op 1, cleared with 20-24 by 46 op 2).
    (0x10967F8, '488b07488bcfff5040c1e804a8010f8493000000e83f88b6ff0fb7d6488b8898000000b8ff010000488b89080200006623d0f783cc0300'
     '000000c000745c',
     'decode class 3: PP vt+0x40 bit 4 (the FIRE press) -> the placed iface (0xBFF050 -> +0x98 -> +0x208) only when '
     'rec+0x3CC bits 22-23 hold a class'),
    (0x1096836, 'f783c003000000000020750b0fb7835c02000085c07445498b442408448bc64c8b4878452b4108498b81d802000041803c0044752748'
     '8b01458b859c000000ff504084c07416',
     'decode class 3: rec+0x3C0 bit 29 or rec+0x25C, the per-id byte [G+0x78]+0x2D8 == 0x44, then placed vt+0x40(id, equip)'),
    (0x1205E80, '488b41088b505085d275068b91680400008b4128f7d023c2c3',
     'PP vt+0x40 0x1205E80: the press word [PP+8]+0x50, or PP+0x468 when 0, & ~PP+0x28 (the decode\'s FIRE / ACTION)'),
    (0xFCB1F0, '48895c24086689542410574883ec20488bd94883c1e0418bf8e8f2e9ffff',
     'placed iface vt+0x40 0xFCB1F0: first 0xFC9C00(iface-0x20, owner)'),
    (0xFC9C00, '6689542410534883ec20448b898c00000033c0488bd94585c974184c8b81d0000000664139107446ffc04983c002413bc172ef',
     '0xFC9C00 (1/2): look up u16 id in iface+0xB0[0..iface+0x6C) (this = iface - 0x20)'),
    (0xFC9C33, '488b89d0010000488d93e0000000488b01ff5020488b8bd00000008bd00fb744243866890451488b83d8000000488d0c52488d04c84883c4205bc3',
     '0xFC9C00 (2/2): not found -> a new row from [iface+0x1B0] vt+0x20 with the id written into it (the owner-1 '
     'registration the mirror must never cause)'),
    (0x107C2AB, '81a7cc030000ffff0ffe', 'place (mode 46) op 2: rec+0x3CC &= ~0x1F00000 (bits 20-24, the classes) on the way out'),
]
SITES += partner_aim_sites.SITES
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
    (0x23B5090, 0x18, 0x109F0E0, 'base plugin +0x30 vtable slot +0x18 = per-id phase handler (phases 0..6)'),
    # --- Native rates.
    (0x23AD380, 0x00, 0xFDD610, 'sp interface vtable slot 0 = record getter'),
    (0x23AD328, 0x00, 0xFDD610, 'sp base interface vtable (destruction, refused) slot 0 = record getter'),
    (0x23AD350, 0x18, 0xFDD5E0, 'sp object primary vtable +0x18 = QI'),
    (0x23AD2B0, 0x00, 0xFDD440, 'rate record vtable slot 0'),
    # --- Native weapons: the bullet system iface vtables (live and destroying), slot 0 = RequestBullet.
    (0x2337DE8, 0x00, 0xD32C60, 'bullet iface vtable slot 0 = RequestBullet (fire filter slot)'),
    (0x2337D18, 0x00, 0xD32C60, 'bullet iface destruction vtable slot 0 = RequestBullet (fire filter slot)'),
    (0x2337DE8, 0x08, 0xD33350, 'bullet iface vtable slot 1 (shot origin; untouched)'),
    # Loadout base ids: the equip type table object (the stock46 writer contract checks the same method).
    (0x22AD180, 0x00, 0xA2A3E0, 'equip type table object 0x2A70920 vtable slot 0'),
    # --- Native polish: the noise filter slot and the store setter slots (never called by us).
    (0x21876A8, 0x00, 0x515270, 'noise iface vtable slot 0 = AddNoise (noise filter slot)'),
    (0x23894B0, 0x20, 0xEE96C0, 'store iface vtable +0x20 = store setter'),
    (0x23895D0, 0x20, 0xEE96C0, 'derived store iface vtable +0x20 = store setter'),
    (0x2389480, 0x08, 0xEE92F0, 'store object vtable +8 = the store init (row count Y+0x10)'),
    (0x2389598, 0x08, 0xEE92F0, 'derived store object vtable +8 = the same store init'),
    # --- Native weapons: the equip controller (P+0xC8) the slot follower and the activation use.
    (0x23C1720, 0x10, 0x11AFA80, 'equip ctl vt+0x10 = SetCurrentSlot (the slot follower call variant)'),
    (0x23C1720, 0x30, 0x11AD390, 'equip ctl vt+0x30 = the current weapon handle (phase 0 with rec+0x3DE == 7)'),
    (0x23C1720, 0x38, 0x11AD240, 'equip ctl vt+0x38 = GetCurrentEquipSlot (row+0x36 & 0xF)'),
    (0x23C1720, 0x40, 0x11AD210, 'equip ctl vt+0x40 = the active weapon slot getter'),
    (0x23C1720, 0x48, 0x11AF930, 'equip ctl vt+0x48 = the active weapon slot setter (activation)'),
]
    # --- Anim round 1: the projectile service iface (gm+0x210), census only (forwards every call).
SLOTS += [
    (0x23E2D50, 0x00, 0x1337760, 'projectile service live iface vtable slot 0 = request (fire census slot, forwards)'),
    (0x23E2BD0, 0x00, 0x1337760, 'projectile service base/destroying iface vtable slot 0 = request (fire census slot, forwards)'),
    (0x23E2D50, 0x58, 0x1337920, 'projectile service live iface vt+0x58 = the rocket punch request (fire census slot)'),
    (0x23E2BD0, 0x58, 0x1337920, 'projectile service base iface vt+0x58 = the rocket punch request (fire census slot)'),
    (0x2429AD0, 0x08, 0x15E0230, 'thrown-object service live iface vt+8 = request (fire census slot)'),
    (0x24299A0, 0x08, 0x15E0230, 'thrown-object service base iface vt+8 = request (fire census slot)'),
    (0x23A9FD0, 0x48, 0xFCCD80, 'placed-object service live iface vt+0x48 = request (fire census slot)'),
    (0x23A9FD0, 0x40, 0xFCB1F0, 'WS4 v2: placed iface vt+0x40 = can-place (claims the owner row through 0xFC9C00)'),
    (0x23A9D20, 0x40, 0xFCB1F0, 'WS4 v2: placed base iface vt+0x40 = can-place'),
    (0x23C8D40, 0x40, 0x1205E80, 'WS4 v2: PP vtable +0x40 = the press word the decode reads'),
    (0x23A9D20, 0x48, 0xFCCD80, 'placed-object service base iface vt+0x48 = request (fire census slot)'),
]
# Audited native support virtual call relationships.
SLOTS += [
    (0x23C1720, 0x198, 0x11AC2E0, 'support equip: ChangeEquip'),
    (0x23C1720, 0xA0, 0x11B0700, 'support equip: allocate and notify'),
    (0x23C1720, 0x18, 0x11B0900, 'support equip: inventory and model writer'),
    (0x23C1720, 0x90, 0x11AD420, 'support equip: pure sub-slot read'),
    (0x23C1720, 0x98, 0x11AFF30, 'support equip: per-id row writer'),
    (0x23894B0, 0x8, 0xEE9880, 'support inventory base: allocate'),
    (0x23895D0, 0x8, 0xEE9880, 'support inventory derived: allocate'),
    (0x23894B0, 0x10, 0xEE9D70, 'support inventory base: release'),
    (0x23895D0, 0x10, 0xEE9D70, 'support inventory derived: release'),
]
# WS3 vehicles: the unit lookup, the SetPosition chain and the realized category bodies
# (each set is every vtable holding that function: vehicle_assertions checks the qword hits).
VEHICLE_V_VTS = (0x2441010, 0x2441120)
VEHICLE_RB_VTS = (0x24416B0, 0x2441780)
VEHICLE_CAT_VTS = (0x22F7F90, 0x22F82E0, 0x22F8630, 0x22F8980, 0x22F8CD0, 0x22FCB50)
SLOTS += [
    (0x22AC8E0, 0x18, 0xA272A0, 'vehicle service vt+0x18 = the id lookup (the DM anchor calls it)'),
    (0x22ED9F0, 0x00, 0xB2DD30, 'vehicle object vt+0 = the count getter'),
    (0x22ED9F0, 0x78, 0xB36780, 'vehicle object vt+0x78 = the SetPosition forwarder'),
]
SLOTS += [(vt, 0x70, 0x16FBEC0, 'vehicle system V vt+0x70 = record SetPosition') for vt in VEHICLE_V_VTS]
SLOTS += [(vt, 0x78, 0x16FDDC0, 'vehicle realized-body manager RB vt+0x78 = realized SetPosition') for vt in VEHICLE_RB_VTS]
SLOTS += [(vt, 0x1F0, 0xBA9710, 'vehicle category vt+0x1F0 = the realized body pose') for vt in VEHICLE_CAT_VTS]
# WS1: the handle manager's two concrete vtables (the only image pointers to 0xDC1740 / 0xDC64E0 / 0xDC0350).
SLOTS += [
    (0x234B840, 0x160, 0xDC1740, 'handle manager vt+0x160 = pin bit + model part (the rec+0x240 call)'),
    (0x234B470, 0x160, 0xDC1740, 'handle manager sibling vt+0x160 = pin bit + model part'),
    (0x234B840, 0x38, 0xDC64E0, 'handle manager vt+0x38 = model instance release (iface reset 0x1062683)'),
    (0x234B470, 0x38, 0xDC64E0, 'handle manager sibling vt+0x38 = model instance release'),
    (0x234B840, 0x40, 0xDC0350, 'handle manager vt+0x40 = has-model test'),
    (0x234B470, 0x40, 0xDC0350, 'handle manager sibling vt+0x40 = has-model test'),
]
# WS4 step 0: the placed iface's detonation, the owner-table reader the census mirrors (never called by us).
SLOTS += [
    (0x23A9FD0, 0x68, 0xFC54F0, 'placed-object service live iface vt+0x68 = detonate(owner id & 0x1FF, equip)'),
]
# WS4 v1: the arm equip (ChangeEquip type 14), the arm energy setter and the store count read (read-only digests).
SLOTS += [
    (0x23C1720, 0x1F0, 0x11B1B60, 'arm equip: ctl vt+0x1F0 (ChangeEquip type 14)'),
    (0x23C1720, 0xE8, 0x11AF820, 'arm energy setter: ctl vt+0xE8 (the arm digest reads what it writes)'),
    (0x23894B0, 0x88, 0xEE9050, 'store base vt+0x88 = the stock / energy word read'),
    (0x23895D0, 0x88, 0xEE9050, 'store derived vt+0x88 = the stock / energy word read'),
]
SLOTS += partner_aim_sites.SLOTS
# F1 vehicles (partner_board): the vehicle service slots behind the getters the board wrappers call and phase 1's
# registration of the steered seat.
SLOTS += [
    (0x22AC8E0, 0x168, 0xA27260, 'vehicle service vt+0x168 = the category lookup (GetVehicleType, the seat count, the nearest search)'),
    (0x22AC8E0, 0x170, 0xA27220, 'vehicle service vt+0x170 = the attachment lookup (the seat count)'),
    (0x22AC8E0, 0x220, 0xA29530, 'vehicle service vt+0x220 = TryToRide (phase 1 registers the steered back seat)'),
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


def branch_refs(lo, hi):
    """Every E8/E9 rel32 and 0F 80..8F rel32 in .xtext whose destination is in [lo, hi)."""
    hits = []
    for s in pe.sections:
        if s.Name.rstrip(b'\0').decode() != '.xtext':
            continue
        data = s.get_data()
        va = s.VirtualAddress
        for op in (0xE8, 0xE9):
            i = data.find(bytes([op]))
            while 0 <= i <= len(data) - 5:
                if lo <= va + i + 5 + struct.unpack_from('<i', data, i + 1)[0] < hi:
                    hits.append(va + i)
                i = data.find(bytes([op]), i + 1)
        i = data.find(b'\x0f')
        while 0 <= i <= len(data) - 6:
            if 0x80 <= data[i + 1] <= 0x8F and lo <= va + i + 6 + struct.unpack_from('<i', data, i + 2)[0] < hi:
                hits.append(va + i)
            i = data.find(b'\x0f', i + 1)
    return sorted(hits)


def runtime_functions():
    """The exception directory's RUNTIME_FUNCTION ranges [begin, end)."""
    entry = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    data = pe.get_data(entry.VirtualAddress, entry.Size)
    return {struct.unpack_from('<II', data, i) for i in range(0, len(data) - 11, 12)}


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
    # Phase-1 detour: 0x109E130 is reached only through the phase-1 jump; the
    # jump table entry 1 is that jump; the padding the detour's rel32 jump lives
    # in is outside every function and never a branch target; the replaced
    # setne and the padding jump each lie inside one cache line.
    need(rel_refs(0x109E130) == [0x109F10E], f'0x109E130 reached only from 0x109F10E: {[hex(x) for x in rel_refs(0x109E130)]}')
    table = [struct.unpack('<I', rd(0x109F154 + 4 * k, 4))[0] for k in range(7)]
    need(table[1] == 0x109F107 and len(set(table)) == 7, f'per-id handler jump table: phase 1 -> 0x109F107 ({[hex(x) for x in table]})')
    funcs = runtime_functions()
    need((0x109E060, 0x109E123) in funcs and (0x109E130, 0x109E435) in funcs, '0x109E060 ends at 0x109E123, 0x109E130 spans to 0x109E435')
    need(not any(b < 0x109E130 and e > 0x109E123 for b, e in funcs), 'the padding 0x109E123..0x109E130 belongs to no function')
    into = branch_refs(0x109E123, 0x109E130) + branch_refs(0x109E17D, 0x109E186)
    need(into == [], f'no rel32 call/jmp/jcc into the padding or the replaced span: {[hex(x) for x in into]}')
    need((0x109E182 & 63) + 4 <= 64 and (0x109E128 & 63) + 5 <= 64, 'setne dword and padding jmp each inside one cache line')
    need(-128 <= 0x109E128 - 0x109E184 <= 127, 'jmp rel8 from 0x109E182 reaches the padding jump')
    out += weapon_assertions()
    out += polish_assertions()
    return out


def rip_target(rva, length):
    """The rip-relative target of the length-byte instruction at rva (disp32 at its end - 4)."""
    return rva + length + struct.unpack('<i', rd(rva + length - 4, 4))[0]


def nonlocal_shoot_reach():
    """Instruction starts of the shoot function 0x1041110 reachable for a
    non-local shooter (rec+0x3C0 bit 0 clear: the je at 0x104122C, 0x10415B4
    and 0x1041E6E taken), following every other branch both ways."""
    try:
        import capstone
    except ImportError as error:
        raise SystemExit('capstone is required for the weapon reachability assertion: ' + str(error))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    start, end = 0x1041110, 0x1042087
    ins = {i.address - IB: (i.size, i.mnemonic, i.op_str) for i in md.disasm(rd(start, end - start), IB + start)}
    forced = {0x104122C: 'taken', 0x10415B4: 'taken', 0x1041E6E: 'taken'}
    seen, work = set(), [start]
    while work:
        a = work.pop()
        while a not in seen and start <= a < end and a in ins:
            seen.add(a)
            size, m, o = ins[a]
            if m in ('ret', 'int3'):
                break
            if m == 'jmp':
                if o.startswith('0x'):
                    a = int(o, 16) - IB
                    continue
                break
            if m.startswith('j'):
                t = int(o, 16) - IB
                f = forced.get(a)
                if f == 'taken':
                    a = t
                    continue
                if f != 'fall':
                    work.append(t)
            a += size
    return seen


def shot_owner_stamps():
    """Every word store to request+0x2A in the 0x300 bytes before a
    `call [rax+0x138]` whose r8 is `lea r8, [reg+disp]` (the equip/weapon shot
    queue the model group's shot handler drains): the owner stamps of every
    shot request builder that uses this queue."""
    import re
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    stamps = []
    for s in pe.sections:
        if s.Name.rstrip(b'\0').decode() != '.xtext':
            continue
        data, va = s.get_data(), s.VirtualAddress
        i = data.find(b'\xff\x90\x38\x01\x00\x00')
        while i >= 0:
            site = va + i
            ins = []
            for back in range(0x300, 0x300 - 16, -1):
                ins = list(md.disasm(rd(site - back, back + 6), IB + site - back))
                if any(x.address - IB == site for x in ins):
                    break
            k = next((n for n, x in enumerate(ins) if x.address - IB == site), None)
            lea = None
            if k is not None:
                for x in reversed(ins[max(0, k - 8):k]):
                    if x.mnemonic == 'lea' and x.op_str.startswith('r8, '):
                        lea = re.fullmatch(r'r8, \[(\w+)(?: ([+-]) (0x[0-9a-f]+|\d+))?\]', x.op_str)
                        break
            if lea:
                reg = lea.group(1)
                disp = int(lea.group(3), 0) * (-1 if lea.group(2) == '-' else 1) if lea.group(2) else 0
                for x in ins[:k]:
                    m = re.fullmatch(r'word ptr \[(\w+)(?: ([+-]) (0x[0-9a-f]+|\d+))?\], \w+', x.op_str)
                    if x.mnemonic == 'mov' and m and m.group(1) == reg:
                        d = int(m.group(3), 0) * (-1 if m.group(2) == '-' else 1) if m.group(2) else 0
                        if d == disp + 0x2A:
                            stamps.append(x.address - IB)
            i = data.find(b'\xff\x90\x38\x01\x00\x00', i + 1)
    return sorted(stamps)


# Native weapons (docs/native-puppet.md "Weapons").
def weapon_assertions():
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    need(rel_refs(0xD32C60) == [], f'no direct call/jmp to RequestBullet 0xD32C60: {[hex(x) for x in rel_refs(0xD32C60)]}')
    need(qword_hits(0xD32C60) == [0x2337D18, 0x2337DE8],
         f'RequestBullet occurs only in the two bullet iface vtables: {[hex(x) for x in qword_hits(0xD32C60)]}')
    need(lea_rip_refs(0x2337DE8) == [0xD32D14] and lea_rip_refs(0x2337D18) == [0xD32619],
         f'the live iface vtable is stored only by the ctor, the destruction one only by the dtor: '
         f'{[hex(x) for x in lea_rip_refs(0x2337DE8)]} {[hex(x) for x in lea_rip_refs(0x2337D18)]}')
    need(0x2337DE8 % 8 == 0 and 0x2337D18 % 8 == 0, 'both filter slots 8-aligned (one locked qword exchange)')
    need(section_of(0x2337DE8) == '.edata' and section_of(0x2337D18) == '.edata', 'the filter slots are read-only data')
    need(rip_target(0xD32D14, 7) == 0x2337DE8 and rip_target(0xD32619, 7) == 0x2337D18, 'ctor/dtor lea targets')
    seen = nonlocal_shoot_reach()
    need(0x1041219 in seen and 0x10414FC in seen, 'the non-local shooter stamps the owner and queues the shot')
    for site in (0x1041650, 0x1041775, 0x10417D6, 0x1041946, 0x1041BDB, 0x1041BF4, 0x1041C6E, 0x1041CA5):
        need(site not in seen, f'local-only shoot side effect {site:#x} unreachable for a non-local shooter')
    for rva, offset in ((0x99B0BA, 0x548), (0x99C2E4, 0xB06), (0x99C412, 0xB30), (0x998DE4, 0x144)):
        need(rip_target(rva, 7) == 0x2C16920 and struct.unpack('<I', rd(rva + 10, 4))[0] == offset,
             f'vars registration {rva:#x}: *(0x2C16920)+{offset:#x}')
    need(rip_target(0xBFEFAD, 7) == 0x2C3A8A0, 'service root global 0x2C3A8A0')
    # The loadout base-id tables (runtime-filled .bss/.data): referenced by code, not in the file image.
    need(len(lea_rip_refs(0x2A70920)) >= 2 and len(lea_rip_refs(0x2C25C50)) >= 2,
         f'equip type table 0x2A70920 and GunBasic 0x2C25C50 are code-referenced: '
         f'{[hex(x) for x in lea_rip_refs(0x2A70920)]} {[hex(x) for x in lea_rip_refs(0x2C25C50)]}')
    need(section_of(0x2A70920) == '.data2' and section_of(0x2C25C50 + 0x1817) == '.data2',
         'both tables lie in writable image data (bounded reads)')
    need(rel_refs(0xBFEFA0, (0xE8,)).count(0xBFF054) == 1, 'the service locator calls the root getter')
    # The weapon/ammo vars block the leak digest reads: the registrations from weapons (+0x548) to the u16[46]
    # at +0x614 are contiguous (each offset + element size * count is the next offset) and end at +0x670.
    import re
    region = rd(0x99B0BA, 0x99B839 + 0x60 - 0x99B0BA)
    chain, expect, last = [], 0x548, None
    for m in re.finditer(rb'\x4c\x8b\x05(....)\x49\x81\xc0(....)', region, re.S):
        rva = 0x99B0BA + m.start()
        if rva > 0x99B839:
            break
        off = struct.unpack('<I', m.group(2))[0]
        if off == last:
            continue  # a scalar's second load of the same member (its accessor registration at 0x99B6E7)
        last = off
        kind_m = re.search(rb'\xc7\x44\xc8\x14(....)', region[m.end():m.end() + 0x60], re.S)
        kind = struct.unpack('<I', kind_m.group(1))[0] if kind_m else 0
        size = {0x0807: 2, 0x0805: 1}.get(kind >> 16)
        chain.append(rva)
        need(rip_target(rva, 7) == 0x2C16920 and off == expect and size is not None,
             f'vars weapon/ammo registration {rva:#x}: offset {off:#x} (expected {expect:#x}), kind {kind:#x}')
        if size is None:
            break
        expect = off + size * (kind & 0xFFFF)
    need(len(chain) == 14 and chain[0] == 0x99B0BA and chain[-1] == 0x99B839 and expect == 0x670,
         f'the weapon/ammo vars block is 14 contiguous registrations ending at +0x670: {[hex(x) for x in chain]}, {expect:#x}')
    # The owner filter drops owner 1 only: the player shoot stamps id & 0x1FF (instance 1 = 1); every other
    # builder of the shot queue stamps a typed owner (pinned sites above), so no soldier shot is ever dropped.
    stamps = shot_owner_stamps()
    need(stamps == [0xEDAC19, 0x1041225, 0x13A190E, 0x1453385],
         f'the shot request owner stamps are the player shoot plus the three typed builders: {[hex(x) for x in stamps]}')
    return out

# Native polish (docs/native-puppet.md "Facing" D3 and "Weapons" F1..F3).
def linear_branch_targets(start, end):
    """Branch targets of a linear capstone sweep of [start, end) (contiguous code)."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    out = []
    for i in md.disasm(rd(start, end - start), IB + start):
        if (i.mnemonic == 'jmp' or i.mnemonic.startswith('j') or i.mnemonic == 'call') and i.op_str.startswith('0x'):
            out.append(int(i.op_str, 16) - IB)
    return out


def bit27_setters():
    """Every `or dword [r64+0x3D0], 0x8000000` / `bts dword [r64+0x3D0], 27` in code."""
    import re
    pats = [re.compile(rb'(?:\x41)?\x81[\x88-\x8f]\xd0\x03\x00\x00\x00\x00\x00\x08'),
            re.compile(rb'(?:\x41)?\x0f\xba[\xa8-\xaf]\xd0\x03\x00\x00\x1b')]
    hits = []
    for s in pe.sections:
        if s.Name.rstrip(b'\0').decode() not in ('.text', '.xtext'):
            continue
        data = s.get_data()
        for p in pats:
            for m in p.finditer(data):
                at = m.start() + (1 if data[m.start()] == 0x41 else 0)
                hits.append(s.VirtualAddress + at)
    return sorted(hits)


def polish_assertions():
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    funcs = runtime_functions()
    # D3: the look call 0xFF29FC is 0xFFAC00's only reference; the padding after
    # its ret belongs to no function and nothing branches into it; the qword
    # the retarget exchanges is aligned, inside one cache line, and only its
    # rel32 low word changes (0xFFAC00 -> 0xFFAD66).
    need(qword_hits(0xFFAC00) == [], f'0xFFAC00 is in no table: {[hex(x) for x in qword_hits(0xFFAC00)]}')
    need(rd(0xFFAD65, 1) == b'\xc3' and rd(0xFFAD66, 10) == b'\xcc' * 10, 'ret at 0xFFAD65, int3 padding 0xFFAD66..0xFFAD6F')
    need((0xFFAC00, 0xFFAD66) in funcs and any(b == 0xFFAD70 for b, e in funcs), '0xFFAC00 ends at 0xFFAD66, a function starts at 0xFFAD70')
    need(not any(b < 0xFFAD70 and e > 0xFFAD66 for b, e in funcs), 'the padding 0xFFAD66..0xFFAD70 belongs to no function')
    need(branch_refs(0xFFAD66, 0xFFAD70) == [], f'no rel32 branch into the padding: {[hex(x) for x in branch_refs(0xFFAD66, 0xFFAD70)]}')
    end70 = min(e for b, e in funcs if b == 0xFFAD70)
    near = [t for t in linear_branch_targets(0xFFAC00, 0xFFAD66) + linear_branch_targets(0xFFAD70, end70) if 0xFFAD66 <= t < 0xFFAD70]
    need(near == [], f'no short branch of the neighbours into the padding: {[hex(x) for x in near]}')
    need(all(qword_hits(a) == [] for a in range(0xFFAD66, 0xFFAD70)), 'no table entry points into the padding')
    need(0xFF29F8 % 8 == 0 and (0xFF29F8 & 63) + 8 <= 64, 'the retarget qword 0xFF29F8 is aligned, inside one cache line')
    need(rd(0xFF29F8, 8) == bytes.fromhex('d6488bcde8ff8100'), 'the qword 0xFF29F8 holds the call (rel32 0x81FF -> 0xFFAC00)')
    need(struct.unpack('<Q', rd(0xFF29F8, 8))[0] == 0x0081FFE8CD8B48D6, 'look_src_qword_orig')
    need(0xFF2A01 + 0x81FF == 0xFFAC00 and 0xFF2A01 + 0x8365 == 0xFFAD66 and rd(0xFF29FF, 2) == b'\x00\x00',
         'the new rel32 0x8365 reaches the padding; its high word stays 0')
    need((0xFFAD66 & 63) + 5 <= 64, 'the padding jump lies inside one cache line')
    # F1: the trigger is called once; the ring and count readers are pinned above.
    need(rel_refs(0x1095DF0) == [0x103E485], f'the trigger 0x1095DF0 has one caller: {[hex(x) for x in rel_refs(0x1095DF0)]}')
    need(qword_hits(0x435260) == [0x2153518], f'src update 0x435260 is src vt+0x38 only: {[hex(x) for x in qword_hits(0x435260)]}')
    # F2: the ammo copy and its callers; the store setter only through its two iface vtables;
    # rec+0x3D0 bit 27 (the tamper check) is set only by the three P+0x204 bit-18 gated sites.
    need(rel_refs(0x108C8C0) == [0x103E08C], f'the ammo copy has one caller: {[hex(x) for x in rel_refs(0x108C8C0)]}')
    need(rel_refs(0x103E010) == [0x1040330, 0x10542F0], f'gun phase 0 callers: {[hex(x) for x in rel_refs(0x103E010)]}')
    need(rel_refs(0xEE96C0) == [] and qword_hits(0xEE96C0) == [0x23894D0, 0x23895F0],
         f'the store setter is only called through its two iface vtables: {[hex(x) for x in qword_hits(0xEE96C0)]}')
    need(lea_rip_refs(0x23894B0) == [0xEE8C2D, 0xEE8CBB, 0xEE8D20, 0xEE8DB0] and lea_rip_refs(0x23895D0) == [0xEE9030],
         f'store iface vtables stored by the ctors/dtors only: {[hex(x) for x in lea_rip_refs(0x23894B0)]} {[hex(x) for x in lea_rip_refs(0x23895D0)]}')
    need(bit27_setters() == [0x1041679, 0x107CB66, 0x1087D0E], f'rec+0x3D0 bit 27 setters: {[hex(x) for x in bit27_setters()]}')
    # F3: AddNoise is reached only through slot 0 of the noise iface vtable (read-only data, aligned).
    need(rel_refs(0x515270) == [] and qword_hits(0x515270) == [0x21876A8],
         f'AddNoise only in the noise iface vtable: {[hex(x) for x in qword_hits(0x515270)]}')
    need(lea_rip_refs(0x21876A8) == [0x51517F, 0x515246, 0x2074C5C],
         f'the noise iface vtable is stored by its ctor, dtor and static init: {[hex(x) for x in lea_rip_refs(0x21876A8)]}')
    need(0x21876A8 % 8 == 0 and section_of(0x21876A8) == '.edata', 'the noise slot is aligned read-only data')
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


# Pad enum pairs (string RVA and value) and the reference facts of the
# native actions and rates (docs/native-puppet.md "Actions", "Rates").
PAD_ENUM = [  # (site of mov r9d,1; name; value)
    (0x9D3A90, 'STANCE', 0x2), (0x9D3AA9, 'DASH', 0x4), (0x9D3AC2, 'HOLD', 0x8), (0x9D3ADB, 'FIRE', 0x10),
    (0x9D3AF4, 'ACTION', 0x20), (0x9D3B0D, 'RELOAD', 0x40), (0x9D3C17, 'SIDE_ROLL', 0x10000), (0x9D3C66, 'EVADE', 0x40000),
    (0x9D3CB7, 'VEHICLE_DASH', 0x200000), (0x9D3D23, 'CARRY', 0x40), (0x9D3D3C, 'RIDE_ON', 0x20), (0x9D3DBD, 'CQC_KNIFE_KILL', 0x20),
]


# Every PlayerPad word read that tests EVADE (bit 18): a call [r64+0x20|0x30|
# 0x40|0x58] (held, now, press, release) on a pad loaded from +0xB0 (P+0xB0)
# within the 24 bytes before, followed within 28 bytes by shr r,18 / bt r,18 /
# test|and r,0x40000. The three act-27 consumers are pinned above; the other
# six run in other action states and write per-id player state (0x11D8FDD also
# messages the object id held in its per-id record through a global service:
# docs/native-puppet.md "Actions"). A new consumer fails the check.
EVADE_CONSUMERS = [0x1097F74, 0x1148C6C, 0x11B56CF, 0x11B8347, 0x11BA26E, 0x11D8FDD, 0x12213B5, 0x1299821, 0x129B7DB]


def evade_census():
    import re
    pad_call = re.compile(rb'\xff[\x50-\x53\x55-\x57][\x20\x30\x40\x58]|\x41\xff[\x50-\x53\x55-\x57][\x20\x30\x40\x58]')
    pad_load = re.compile(rb'\x8b[\x80-\xbf]\xb0\x00\x00\x00')
    bit18 = re.compile(rb'(?:\x41)?\xc1[\xe8-\xef]\x12|(?:\x41)?\x0f\xba[\xe0-\xe7]\x12|\xa9\x00\x00\x04\x00|\x25\x00\x00\x04\x00|'
                       rb'(?:\x41)?\xf7[\xc0-\xc7]\x00\x00\x04\x00|(?:\x41)?\x81[\xe0-\xe7]\x00\x00\x04\x00')
    hits = []
    for s in pe.sections:
        if s.Name.rstrip(b'\0').decode() not in ('.text', '.xtext'):
            continue
        data = s.get_data()
        va = s.VirtualAddress
        for m in pad_call.finditer(data):
            at = m.start() + (1 if data[m.start()] == 0x41 else 0)
            if at < 24 or not pad_load.search(data, at - 24, at):
                continue
            if bit18.search(data, at + 3, at + 3 + 28):
                hits.append(va + at)
    return sorted(set(hits))


def actions_assertions():
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    census = evade_census()
    need(census == EVADE_CONSUMERS, f'EVADE (bit 18) pad consumers: {[hex(x) for x in census]}')
    for site, name, value in PAD_ENUM:
        b = rd(site, 19)
        if b[:6] != bytes.fromhex('41b901000000'):
            out.append(f'{site:#x}: not mov r9d,1')
            continue
        if b[6:9] == bytes.fromhex('458d41'):
            r8, lea = 1 + b[9], site + 10
        elif b[6:8] == bytes.fromhex('41b8'):
            r8, lea = struct.unpack_from('<I', b, 8)[0], site + 12
        else:
            out.append(f'{site:#x}: no r8d value')
            continue
        lb = rd(lea, 7)
        need(lb[:3] == bytes.fromhex('488d15'), f'{lea:#x}: lea rdx,[rip+x]')
        string = lea + 7 + struct.unpack_from('<i', lb, 3)[0]
        text = rd(string, len(name) + 1)
        need(text == name.encode() + b'\0', f'{site:#x}: string at {string:#x} is {text!r}, not {name}')
        need(r8 == value, f'{site:#x}: {name} = {r8:#x}, expected {value:#x}')
        call = rd(lea + 7, 8)
        need(call[3] == 0xE8 and lea + 7 + 8 + struct.unpack_from('<i', call, 4)[0] == 0x1A156E0,
             f'{site:#x}: the pair is registered by 0x1A156E0')
    need(0x9D3A78 <= min(s for s, _, _ in PAD_ENUM) and max(s for s, _, _ in PAD_ENUM) < 0x9D3EE5, 'enum sites inside 0x9D3A78..0x9D3EE5')
    gate_a, gate_b = rel_refs(0x114DB20), rel_refs(0x12B60D0)
    need(0x1148C4E in gate_a, f'EVADE A calls gate 0x114DB20: {[hex(x) for x in gate_a]}')
    need(0x12997FF in gate_b and 0x129B7B9 in gate_b, f'EVADE B/C call gate 0x12B60D0: {[hex(x) for x in gate_b]}')
    funcs = runtime_functions()
    need(any(b == 0x114DB20 for b, e in funcs) and any(b == 0x12B60D0 for b, e in funcs), 'both gates are function starts')
    # Rates: the only stores of the sp interface and record vtables, the getter's
    # only image references (final and destruction vtables), the two callers.
    need(lea_rip_refs(0x23AD380) == [0xFDD4F7], f'sp vtable 0x23AD380 stored once: {[hex(x) for x in lea_rip_refs(0x23AD380)]}')
    need(lea_rip_refs(0x23AD2B0) == [0xFDD416, 0xFDD6A3], f'record vtable 0x23AD2B0: {[hex(x) for x in lea_rip_refs(0x23AD2B0)]}')
    need(qword_hits(0xFDD610) == [0x23AD328, 0x23AD380], f'getter 0xFDD610 slots: {[hex(x) for x in qword_hits(0xFDD610)]}')
    need(rel_refs(0xFDD610) == [], 'the record getter is only called virtually')
    need(rel_refs(0x109F690) == [0x10B9DC1, 0x10BA6B7], f'rate getter callers: {[hex(x) for x in rel_refs(0x109F690)]}')
    need(rd(0xFDD6A3, 3) == bytes.fromhex('4c8d15') and 0xFDD6AA + struct.unpack('<i', rd(0xFDD6A6, 4))[0] == 0x23AD2B0,
         'record init r10 = 0x23AD2B0 (stored last at 0xFDD736)')
    need(rd(0xFDD736, 3) == bytes.fromhex('4c8911'), 'record init: mov [rcx], r10 (final vtable) after the rates')
    need(struct.unpack('<f', rd(0xFDD70C + 3, 4))[0] == 1.0, 'rates start at 1.0')
    return out


failures += actions_assertions()


class _AimContext:
    """The helpers tools/partner_aim_sites.py's assertions use."""
    IB = IB
    pe = pe
    rd = staticmethod(rd)
    rel_refs = staticmethod(rel_refs)
    qword_hits = staticmethod(qword_hits)
    lea_rip_refs = staticmethod(lea_rip_refs)
    rip_target = staticmethod(rip_target)
    section_of = staticmethod(section_of)

    @staticmethod
    def runtime_function_entries():
        entry = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        data = pe.get_data(entry.VirtualAddress, entry.Size)
        return sorted({struct.unpack_from('<III', data, i) for i in range(0, len(data) - 11, 12)})


failures += partner_aim_sites.assertions(_AimContext)


# Native weapons: the partner's own reload and weapon switch (docs/native-puppet.md "Weapons").
def mode_table_emulation(start=0x20252F0, limit=0x1000):
    """The static initializer of the gun mode table: a linear lea/mov/movups/movaps
    pipeline whose 16-byte stores go to .data2 (runtime-filled). Every
    instruction is interpreted; anything else fails the check."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    regs, slot, xmm, stores = {}, [None, None], None, {}
    for i in md.disasm(rd(start, limit), IB + start):
        m, o = i.mnemonic, i.op_str
        if (m, o) in (('push', 'rbp'), ('mov', 'rbp, rsp'), ('pop', 'rbp')) or (m in ('sub', 'add') and o.startswith('rsp,')):
            continue
        if m == 'ret':
            return stores
        if m == 'xor' and o in ('edx, edx', 'ecx, ecx', 'eax, eax'):
            regs['r' + o[1:3]] = 0
            continue
        if m == 'lea' and '[rip' in o:
            disp = o.split('rip')[1].strip(' ]').replace(' ', '')
            regs[o.split(',')[0].strip()] = i.address - IB + i.size + (int(disp, 16) if disp else 0)
            continue
        if m == 'mov' and o.startswith('qword ptr [rbp - 0x10], ') and o.split(', ')[1] in regs:
            slot[0] = regs[o.split(', ')[1]]
            continue
        if m == 'mov' and o == 'dword ptr [rbp - 8], edx':
            slot[1] = regs['rdx']
            continue
        if m == 'movups' and o == 'xmm0, xmmword ptr [rbp - 0x10]':
            xmm = tuple(slot)
            continue
        if m == 'movaps' and o.startswith('xmmword ptr [rip ') and o.endswith(', xmm0'):
            disp = o.split('rip')[1].split(']')[0].replace(' ', '')
            stores[i.address - IB + i.size + int(disp, 16)] = xmm
            continue
        raise SystemExit(f'mode table initializer: unexpected {i.address - IB:#x} {m} {o}')
    raise SystemExit('mode table initializer: no ret')


def jump_table(table, count, base=0):
    return [base + struct.unpack('<I', rd(table + 4 * k, 4))[0] for k in range(count)]


def calls_in(start, end):
    """Every call instruction of a linear capstone sweep of [start, end)."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    return [i.address - IB for i in md.disasm(rd(start, end - start), IB + start) if i.mnemonic == 'call']


def weapons2_assertions():
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    funcs = runtime_functions()
    entry_dir = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    pdata = pe.get_data(entry_dir.VirtualAddress, entry_dir.Size)
    # Primary function starts: RUNTIME_FUNCTIONs whose unwind info is not chained
    # (UNW_FLAG_CHAININFO marks the fragments of a function split by the compiler).
    primary = sorted(b for b, e, u in (struct.unpack_from('<III', pdata, i) for i in range(0, len(pdata) - 11, 12))
                     if not (rd(u, 1)[0] >> 3) & 4)

    def func_of(rva):
        import bisect
        k = bisect.bisect_right(primary, rva) - 1
        return (primary[k], None) if k >= 0 else None
    # R1: the gun iface dispatcher's jump table (r9 = the image base); phase 1 is
    # the gun update, phase 6 the post-update clear (bits 20/24 die in the drain).
    need(rip_target(0x1041005, 7) == 0, 'the dispatcher r9 = image base')
    jt = jump_table(0x1041080, 8)
    need(jt[1] == 0x1041027 and jt[6] == 0x1041063, f'gun iface jump table: {[hex(x) for x in jt]}')
    need(0x104102E + 5 + struct.unpack('<i', rd(0x104102F, 4))[0] == 0x103E2D0, 'phase 1 jmp -> 0x103E2D0')
    need(0x104106A + 5 + struct.unpack('<i', rd(0x104106B, 4))[0] == 0x103EB70, 'phase 6 jmp -> 0x103EB70')
    need(func_of(0x103F99C) is not None and func_of(0x103F99C)[0] == 0x103EB70, 'the phase-6 clear belongs to 0x103EB70')
    need(func_of(0x103E515) is not None and func_of(0x103E515)[0] == 0x103E2D0, 'the mode transition call is in the gun update')
    need(0x103E51A + struct.unpack('<i', rd(0x103E516, 4))[0] == 0x1040A00, 'the gun update calls the mode transition 0x1040A00')
    # The mode table (runtime .data2): the initializer's emulation and the transition's lea.
    need(rip_target(0x1040A54, 7) == 0x2C556A0 and section_of(0x2C556A0) == '.data2', 'the transition reads the mode table 0x2C556A0')
    table = mode_table_emulation()

    def entry(k):
        return table.get(0x2C556A0 + 16 * k) or (None, None)
    want = {7: 0x1071D70, 8: 0x1074070, 9: 0x106F4E0, 10: 0x106F4E0, 11: 0x1077840, 12: 0x1077840}
    for k, fn in want.items():
        need(entry(k) == (fn, 0), f'mode table entry {k}: {entry(k)} (expected {fn:#x})')
    need(qword_hits(0x20252F0) != [], 'the mode table initializer is registered (static init table)')
    # The reload handler (modes 11/12): op 1 enter, op 2 exit, op 12 the animation events.
    need(rip_target(0x10778B2, 7) == 0 and rd(0x10778BC, 7) == bytes.fromhex('8b8481d0a50701'), 'the reload op table 0x107A5D0 (r = image base)')
    ops = jump_table(0x107A5D0, 13)
    need(ops[1] == 0x10778C8 and ops[2] == 0x107903C and ops[12] == 0x10796C8, f'reload ops: {[hex(x) for x in ops]}')
    need(rel_refs(0x1054900) == [0x1079B6E], f'the reload completion 0x1054900 has one caller: {[hex(x) for x in rel_refs(0x1054900)]}')
    exits = [t for t in linear_branch_targets(0x107903C, 0x10790AA) if not (0x107903C <= t <= 0x10790B4)]
    need(set(exits) <= {0x1055EA0, 0x108BCA0, 0x10515F0, 0x1051570, 0x1058890},
         f'the reload exit reaches the bit-26 clear (only calls leave the span): {[hex(x) for x in exits]}')
    need(func_of(0x1077A36) is not None and func_of(0x1077A36)[0] == 0x1077840, 'the reload enter belongs to 0x1077840')
    # W4: SetCurrentSlot for a non-local id writes data only (Q+0x17C, the nibble,
    # row+0) for slot types 0/1/2/5/6 and exits before any call.
    sjt = jump_table(0x11AFE6C, 7)
    need(rip_target(0x11AFB0F, 7) == 0, 'SetCurrentSlot r9 = image base')
    need(sjt[0] == 0x11AFB2F and sjt[1] == 0x11AFC08 and sjt[2] == 0x11AFC11 and sjt[5] == 0x11AFC1A and sjt[6] == 0x11AFC23,
         f'SetCurrentSlot types 0/1/2/5/6: {[hex(x) for x in sjt]}')
    for site in (0x11AFC08, 0x11AFC11, 0x11AFC1A):
        need(site + 9 + struct.unpack('<i', rd(site + 5, 4))[0] == 0x11AFB33, f'{site:#x} joins the row+0 store')
    need(0x11AFC23 + 9 + struct.unpack('<i', rd(0x11AFC28, 4))[0] == 0x11AFB37, '0x11AFC23 joins the row+0 store')
    need(0x11AFB43 + struct.unpack('<i', rd(0x11AFB3F, 4))[0] == 0x11AFE4B, 'the non-local exit is 0x11AFE4B')
    need(calls_in(0x11AFA80, 0x11AFB43) == [], f'no call before the non-local exit: {[hex(x) for x in calls_in(0x11AFA80, 0x11AFB43)]}')
    need(calls_in(0x11AFC08, 0x11AFC2C) == [], 'no call in the type 1/2/5/6 stores')
    need(calls_in(0x11AFE4B, 0x11AFE6B) == [0x11AFE52], 'the epilogue only checks the cookie')
    need(func_of(0x11AFA80) is not None and func_of(0x11AFA80)[0] == 0x11AFA80, 'SetCurrentSlot is a function start')
    need(calls_in(0x11AD240, 0x11AD2AF) == [] and rd(0x11AD2AE, 1) == b'\xc3', 'GetCurrentEquipSlot reads only')
    # Phase 0 compares the slot every frame (its only caller) and activates on bit 7.
    need(rel_refs(0x103AED0) == [0x103E044], f'the slot compare 0x103AED0 has one caller: {[hex(x) for x in rel_refs(0x103AED0)]}')
    need(0x103E083 + struct.unpack('<i', rd(0x103E07F, 4))[0] == 0x105FDB0, 'gun phase 0 calls the activation 0x105FDB0')
    need(func_of(0x13030B7) is not None and func_of(0x13030B7)[0] == 0x1302F50, 'the shot class read belongs to the shot handler 0x1302F50')
    return out


failures += weapons2_assertions()


def weapons3_assertions():
    """The sender's base ids: the alias type path of A2A3E0 (pure reads of runtime-filled image data)."""
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    need(0xA2A3F3 + 5 + struct.unpack('<i', rd(0xA2A3F4, 4))[0] == 0x1E03790, 'A2A3E0 calls the alias table getter 0x1E03790')
    need(0xA2A407 + 5 + struct.unpack('<i', rd(0xA2A408, 4))[0] == 0xA3C5B0, 'A2A3E0 calls the equipment resource getter 0xA3C5B0')
    need(0xA2A4AE + 5 + struct.unpack('<i', rd(0xA2A4AF, 4))[0] == 0xA3C5B0, 'the blueprint branch uses the same resource getter')
    need(rip_target(0x1E03790, 7) == 0x2C934A0, 'the alias table is 0x2C934A0')
    need(rip_target(0xA3C5B0, 7) == 0x2A711F0, 'the equipment resource is 0x2A711F0')
    need(rd(0xA2A4CE, 4) == bytes.fromhex('488b4808'), 'the resource holds GunBasic at +8 (the blueprint branch)')
    # Both are writable image data (zero-initialized or runtime-filled): bounded pure reads.
    need(section_of(0x2C934A0) == '.data2' and section_of(0x2C934A0 + 5 * 0xE + 0x12) == '.data2', 'the alias table lies in .data2')
    need(section_of(0x2A711F0) == '.data2' and section_of(0x2A711F0 + 0x6F) == '.data2', 'the equipment resource lies in .data2')
    need(len(lea_rip_refs(0x2C934A0)) >= 1, 'the alias table is code-referenced')
    return out


failures += weapons3_assertions()


def ride_seat_assertions():
    """The seated vehicle rider's four reversible dwords (partner_ride_seat, docs/anim-round1-seat.md)."""
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    need(rel_refs(0x1198770) == [0x119980C], f'DM anchor 0x1198770 called only from phase 0 0x119980C: {[hex(x) for x in rel_refs(0x1198770)]}')
    need(rel_refs(0x1198CC0) == [0x1199566], f'DM entry playback called only from phase 6 0x1199566: {[hex(x) for x in rel_refs(0x1198CC0)]}')
    need(rel_refs(0x1199140) == [0x1198D1D, 0x1198D7C], f'0x1199140 called only by both playback paths: {[hex(x) for x in rel_refs(0x1199140)]}')
    need(rd(0x119956B, 7) == bytes.fromhex('0fb68348020000'), 'the playback caller overwrites rax at once (no return value used)')
    need(rd(0x119883C, 4) == bytes.fromhex('3041b002'), 'seat dword 0x119883C: lea tail 0x30 + mov r8b, 2')
    for site, size in ((0x119883C, 4), (0x1198D56, 4), (0x1198D81, 4), (0x11992E9, 4)):
        need((site & 63) + size <= 64, f'patch dword {site:#x} inside one cache line')
    # Each skip is a jmp rel8 from inside the function to its own later instruction.
    need(0x1198D56 + 2 + 0x64 == 0x1198DBC and 0x1198D81 + 2 + 0x39 == 0x1198DBC and 0x11992E9 + 2 + 0x0C == 0x11992F7,
         'skip displacements 0x64 / 0x39 / 0x0C reach 0x1198DBC / 0x1198DBC / 0x11992F7')
    funcs = runtime_functions()
    play = [f for f in funcs if f[0] <= 0x1198CC0 < f[1]]
    end = [f for f in funcs if f[0] <= 0x11992C3 < f[1]]
    need(len(play) == 1 and play[0][0] == 0x1198CC0 and play[0][1] >= 0x1198DCC, f'0x1198CC0 is one function through 0x1198DCC: {play}')
    need(len(end) == 1 and end[0][1] >= 0x1199302, f'one function covers the DM End stores 0x11992C3..0x1199302: {end}')
    # No branch lands inside the replaced or skipped spans.
    local = linear_branch_targets(0x1198CC0, 0x1198DCC) + linear_branch_targets(0x1199220, 0x119932A)
    for lo, hi in ((0x1198D57, 0x1198D60), (0x1198D82, 0x1198D85), (0x11992EA, 0x11992F7), (0x119883D, 0x1198840)):
        inside = [t for t in local if lo <= t < hi] + branch_refs(lo, hi)
        need(inside == [], f'no branch into {lo:#x}..{hi:#x}: {[hex(x) for x in inside]}')
    return out


failures += ride_seat_assertions()


def anim_round1_assertions():
    """Anim round 1 weapons: the reload rate writers, the census record fields, the projectile service slot."""
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    # Inside the reload handler 0x1077840 (op table 0x107A5D0, its last op ends before the table) the only stores to
    # [r+0x310] are the enter's provider value, its doubling and the exit's 1.0.
    stores = []
    for i in md.disasm(rd(0x1077840, 0x107A5D0 - 0x1077840), IB + 0x1077840):
        if i.op_str.startswith('dword ptr [r') and '+ 0x310]' in i.op_str.split(',')[0] and i.mnemonic in ('mov', 'movss'):
            stores.append(i.address - IB)
    need(stores == [0x10778F0, 0x1077922, 0x10790D9], f'reload handler rec+0x310 stores: {[hex(x) for x in stores]}')
    ops = jump_table(0x107A5D0, 13)
    need(ops[0] == 0x1079FD0 and ops[1] == 0x10778C8 and ops[2] == 0x107903C, f'reload ops 0/1/2: {[hex(x) for x in ops[:3]]}')
    need(0x107A555 + struct.unpack('<i', rd(0x107A551, 4))[0] == 0x1059620, 'op 0 calls the segment request 0x1059620')
    need(0x1079FD0 <= 0x107A550 < 0x107A5B2, 'the 0x1059620 call lies in op 0')
    need(0x10778EA + 3 == 0x10778ED and rd(0x10778EA, 3) == bytes.fromhex('ff5028'), 'the provider call vt+0x28 precedes the store')
    need(rd(0x1077919, 1) == b'\x10', 'the doubling is skipped without the status bit (je 0x107792A)')
    # The projectile service: iface vtable slot 0 in read-only image data, aligned, nowhere else; no direct caller.
    need(all(section_of(v) == '.edata' and v % 8 == 0 for v in (0x23E2BD0, 0x23E2D50)),
         'the projectile iface vtables lie in read-only .edata, aligned')
    need(qword_hits(0x1337760) == [0x23E2BD0, 0x23E2D50],
         f'0x1337760 is only slot 0 of 0x23E2BD0 / 0x23E2D50: {[hex(x) for x in qword_hits(0x1337760)]}')
    need(lea_rip_refs(0x23E2D50) == [0x1336494] and lea_rip_refs(0x23E2BD0) == [0x1336069],
         f'the iface vtables are stored by the factory / the dtor only: {[hex(x) for x in lea_rip_refs(0x23E2D50)]}')
    need(rel_refs(0x1337760) == [], f'0x1337760 has no direct caller: {[hex(x) for x in rel_refs(0x1337760)]}')
    need(rip_target(0x1336069, 7) == 0x23E2BD0 and rip_target(0x133605B, 7) == 0x23E2B98, 'the dtor stores the iface vtable at +0x20')
    need(0x13036A1 + 0 == 0x13036A1 and rd(0x13036A1, 5) == bytes.fromhex('b817040000'), 'the projectile request call ignores rax')
    need(0x1303685 + 0 == 0x1303685 and rd(0x1303685, 4) == bytes.fromhex('488d5590'), 'rdx = rbp-0x70 (the request base)')
    # The rocket / thrown / placed request slots: each target only in its base and live iface vtables (read-only,
    # aligned), no direct caller, the factories and dtors store those vtables, and gm+0x200 / +0x208 are the services.
    for target, vts in ((0x1337920, (0x23E2BD0 + 0x58, 0x23E2D50 + 0x58)), (0x15E0230, (0x24299A0 + 8, 0x2429AD0 + 8)),
                        (0xFCCD80, (0x23A9D20 + 0x48, 0x23A9FD0 + 0x48))):
        need(qword_hits(target) == list(vts), f'{target:#x} only in {[hex(v) for v in vts]}: {[hex(x) for x in qword_hits(target)]}')
        need(rel_refs(target) == [], f'{target:#x} has no direct caller: {[hex(x) for x in rel_refs(target)]}')
        need(all(section_of(v) == '.edata' and v % 8 == 0 for v in vts), f'{target:#x} slots lie in read-only .edata, aligned')
    need(rip_target(0x15DE2B4, 7) == 0x2429AD0 and rip_target(0x15DDFA9, 7) == 0x24299A0, 'thrown service: live and base iface vtables')
    need(rip_target(0xFC79E4, 7) == 0x23A9FD0 and rip_target(0xFC52E9, 7) == 0x23A9D20, 'placed service: live and base iface vtables')
    # The request builders: the owner store and the service call lie in the same frame (no push/sub between them).
    for lo, hi in ((0x1061E35, 0x1061F20), (0x1087AAF, 0x1087BF2), (0x107C801, 0x107CA33), (0x108062C, 0x108064F)):
        bad = [i for i in md.disasm(rd(lo, hi - lo), IB + lo) if i.mnemonic in ('push', 'pop') or
               (i.mnemonic in ('sub', 'add') and i.op_str.startswith('rsp'))]
        need(bad == [], f'the request frame {lo:#x}..{hi:#x} keeps rsp: {[hex(i.address - IB) for i in bad]}')
    # The census record fields: written before the RequestBullet calls of the same shot (0x13031C8 / 0x13031FA).
    need(0x1303180 < 0x13031C8 and 0x1303195 < 0x13031C8, 'record+0x2E/+0x30 are filled before RequestBullet')
    return out



def ws4_v2_assertions():
    """WS4 v2: the place family's handlers, the detonation's callers and its non-local exit (read-only)."""
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    table = mode_table_emulation()

    def entry(k):
        return table.get(0x2C556A0 + 16 * k) or (None, None)
    want = {43: 0x107E480, 44: 0x107D300, 45: 0x107DD00, 46: 0x107BAC0, 47: 0x107ECF0}
    for k, fn in want.items():
        need(entry(k) == (fn, 0), f'mode table entry {k}: {entry(k)} (expected {fn:#x})')
    callers = rel_refs(0x1053270, (0xE8,))
    need(callers == [0x1068A8C, 0x107F2D9, 0x107F77B, 0x108A46B], f'the detonation 0x1053270 callers: {[hex(x) for x in callers]}')
    need(0x1053295 + struct.unpack('<i', rd(0x1053291, 4))[0] == 0x105333E, 'the detonation\'s non-local exit is 0x105333E')
    need(calls_in(0x105333E, 0x1053350) == [], 'the non-local exit calls nothing')
    need(calls_in(0x107BD2C, 0x107BD64) == [], 'mode 46 op 1: no call between the probe and the latch block')
    need(0x107BD2C == 0x107BD27 + 5 and 0x107BD2C + struct.unpack('<i', rd(0x107BD28, 4))[0] == 0x104CF00,
         'mode 46 op 1 calls the probe 0x104CF00 at 0x107BD27')
    need(0x107C701 + 6 + struct.unpack('<i', rd(0x107C703, 4))[0] == 0x107CC23, 'mode 46 op 9: a non-local record skips to 0x107CC23')
    # The owner-row claim 0xFC9C00: its only direct callers are placed vt+0x40 (the decode's place path), vt+0x20's
    # body and the placement request processor 0xFC8020 (local placements only).
    owner = rel_refs(0xFC9C00, (0xE8,))
    need(owner == [0xFC8056, 0xFCB209, 0xFCDEA5], f'0xFC9C00 callers: {[hex(x) for x in owner]}')
    need(0xFCB20E + struct.unpack('<i', rd(0xFCB20A, 4))[0] == 0xFC9C00, 'placed vt+0x40 calls 0xFC9C00 first')
    need(0x109680C + 5 + struct.unpack('<i', rd(0x109680D, 4))[0] == 0xBFF050, 'the decode reaches the placed service through 0xBFF050')
    return out


def support_native_assertions():
    out = []
    def need(cond, what):
        if not cond:
            out.append(what)
    funcs = runtime_functions()
    need((0x108C878, 0x108C888) in funcs and (0x108C890, 0x108C8C0) in funcs and
         (0x108C8C0, 0x108CC91) in funcs, 'support guard: exact neighboring function ranges')
    need(not any(b < 0x108C890 and e > 0x108C888 for b, e in funcs),
         'support guard: eight-byte padding is outside every runtime function')
    need(rd(0x108C887, 9) == bytes.fromhex('c3cccccccccccccccc'),
         'support guard: ret then exactly eight int3 bytes at 0x108C888')
    need(branch_refs(0x108C888, 0x108C890) == [],
         'support guard: no rel32 call/jmp/jcc into its int3 padding')
    need(branch_refs(0x108C8E6, 0x108C8EC) == [],
         'support guard: no rel32 call/jmp/jcc into replaced instruction interior')
    need(0x108C888 - (0x108C8E5 + 2) == -95 and struct.pack('b', -95) == b'\xa1',
         'support guard: EB A1 reaches 0x108C888 (EB 9F would miss by two bytes)')
    need((0x108C8E5 & 63) + 4 <= 64 and (0x108C888 & 63) + 5 <= 64,
         'support guard: published dword and padding jump each inside one cache line')
    need(rel_refs(0x108C8C0) == [0x103E08C], 'support guard: ammo copy only called by gun phase 0')
    need(0x1087C1B + struct.unpack('<i', rd(0x1087C17, 4))[0] == 0x1087DAC,
         'support guard: bit-29 gate skips the stock decrement and ctl stock setter')
    need(rd(0x15E023A, 4) == bytes.fromhex('f6423e01') and rd(0x15E0246, 7) == bytes.fromhex('4881c100010000'),
         'support filter: preview bit is tested before service+0x100')
    # Every new direct call reuses the complete, existing stock writer pins;
    # keep all user-listed audited callees in that contract and verify their
    # complete bodies here as well as the short expected-byte sites above.
    import re
    pin_text = (ROOT / 'native' / 'equipment_stock_writer_pins.hpp').read_text(encoding='utf-8')
    pins = {}
    for m in re.finditer(r'\{0x([0-9A-Fa-f]+),0x([0-9A-Fa-f]+),\{([^}]+)\}\}', pin_text):
        body_rva, size = int(m.group(1), 16), int(m.group(2), 16)
        digest = bytes(int(x.strip(), 16) for x in m.group(3).split(','))
        pins[body_rva] = size, digest
    for body_rva in (0x11AC2E0, 0x1156640, 0x1209EC0, 0x11B0700, 0x11B0900, 0x11AFF30,
                     0xEE9880, 0xEE9D70, 0xB0C720, 0xDC3250, 0xDC2A50, 0xDC63A0, 0x11AFA80):
        need(body_rva in pins, f'support equip audited callee {body_rva:#x} has complete body SHA256 pin')
        if body_rva in pins:
            size, digest = pins[body_rva]
            need(hashlib.sha256(rd(body_rva, size)).digest() == digest,
                 f'support equip audited callee {body_rva:#x} complete body matches SHA256 pin')
    return out


def vehicle_assertions():
    """WS3 vehicles (docs/vehicle-ws3.md): the facts behind the ride census, the seat-by-id write and the tilt write."""
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)

    def call_target(site):
        return site + 5 + struct.unpack('<i', rd(site + 1, 4))[0]
    need(rd(0x9DAB04, 1) == b'\xe8' and call_target(0x9DAB04) == 0xBF4110, 'DM binding: the locator name -> id helper 0xBF4110')
    need(rd(0x1198840, 1) == b'\xe8' and call_target(0x1198840) == 0xB7AF90, 'the DM anchor seats with 0xB7AF90(unit, out, seat)')
    need(call_target(0xB7AFC9) == 0x55DB80, 'the seat matrix 0xB7AF90 starts from the unit world matrix 0x55DB80(unit)')
    need(call_target(0x55DBD4) == 0x55DDA0, 'the unit world matrix uses 0x55DDA0 on the same record')
    need(call_target(0xBA97EA) == 0x55DDA0, 'the realized body pose 0xBA9710 uses 0x55DDA0(body)')
    need(rip_target(0xBA9812, 7) == 0x20A72CC and rip_target(0xBA9829, 7) == 0x20A8068, 'the small-move limits are 0x20A72CC / 0x20A8068')
    need(struct.unpack('<f', rd(0x20A72CC, 4))[0] == 1.0 and abs(struct.unpack('<f', rd(0x20A8068, 4))[0] - 0.1) < 1e-7,
         'the small-move limits are 1.0f and 0.1f')
    for rva, value in VEHICLE_DATA:
        need(struct.unpack('<I', rd(rva, 4))[0] == value and section_of(rva) == '.edata', f'vehicle data {rva:#x} = {value:#x} in .edata')
    need(not any(s.Characteristics & 0x80000000 for s in pe.sections if s.Name.rstrip(bytes(1)).decode() == ".edata"), ".edata is not writable")
    # Every vtable that holds the chain functions is in the pinned sets (no other class reaches them).
    need(qword_hits(0x16FBEC0) == sorted(vt + 0x70 for vt in VEHICLE_V_VTS), f'V vtables: {[hex(x) for x in qword_hits(0x16FBEC0)]}')
    need(qword_hits(0x16FDDC0) == sorted(vt + 0x78 for vt in VEHICLE_RB_VTS), f'RB vtables: {[hex(x) for x in qword_hits(0x16FDDC0)]}')
    need(qword_hits(0xBA9710) == sorted(vt + 0x1F0 for vt in VEHICLE_CAT_VTS), f'category vtables: {[hex(x) for x in qword_hits(0xBA9710)]}')
    need(qword_hits(0xA272A0) == [0x22AC8E0 + 0x18] and qword_hits(0xB36780) == [0x22ED9F0 + 0x78] and
         qword_hits(0xB2DD30) == [0x22ED9F0], 'the lookup, the forwarder and the count getter each have one vtable slot')
    # 0x55DDA0 keeps rcx (the record or the body) through its three angle reads: no call, jump out or rcx write before 0x55E0D9.
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    starts, clobber = set(), []
    for i in md.disasm(rd(0x55DDA0, 0x55E0DE - 0x55DDA0), IB + 0x55DDA0):
        starts.add(i.address - IB)
        if i.mnemonic in ('call', 'ret', 'jmp') or any(md.reg_name(r) in ('rcx', 'ecx', 'cx', 'cl') for r in i.regs_access()[1]):
            clobber.append(hex(i.address - IB))
    need(not clobber and {0x55DDF6, 0x55DF6D, 0x55E0D9} <= starts, f'0x55DDA0 reads +0x48/+0x44/+0x2C of its rcx: {clobber}')
    # The DM record entry arrays of the seat-by-id write (pinned inside the ride seat blocks).
    need(rd(0x1198792, 7) == bytes.fromhex('0fb699a0010000'), 'the anchor reads the current entry rec+0x1A0')
    need(rd(0x11987A1, 8) == bytes.fromhex('0fb7bc59c0000000'), 'the anchor reads the entry target id rec+0xC0+2*cur')
    need(rd(0x11987B5, 8) == bytes.fromhex('488b9cd9d0000000'), 'the anchor reads the entry CNP rec+0xD0+8*cur')
    need(rd(0x1198CF7, 8) == bytes.fromhex('0fb6841eb8000000') and rd(0x1198D18, 5) == bytes.fromhex('4c8b44f378'),
         'entry playback reads the flags rec+0xB8+cur and the named state rec+0x78+8*cur')
    need(rd(0x1199508, 8) == bytes.fromhex('f68418b800000002'), 'phase 6 applies the anchor only with flag 2 at rec+0xB8+cur')
    need(rd(0x1198821, 10) == bytes.fromhex('48b851e228daca2c0000'), 'the anchor accepts CNP_ppos_c 0x2ccada28e251 only')
    return out


def board_assertions():
    """F1 vehicles (partner_board): the two retargeted calls and the stock facts block and passenger rely on."""
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)

    def call_target(site):
        return site + 5 + struct.unpack('<i', rd(site + 1, 4))[0]
    need(rd(0x12458AC, 1) == b'\xe8' and call_target(0x12458AC) == 0x124B3A0 and
         struct.unpack('<I', rd(0x12458AD, 4))[0] == 0x00005AEF, 'board site A: E8 rel32 0x00005AEF -> 0x124B3A0')
    need(rd(0x1251479, 1) == b'\xe8' and call_target(0x1251479) == 0x124B360 and
         struct.unpack('<I', rd(0x125147A, 4))[0] == 0xFFFF9EE2, 'board site B: E8 rel32 0xFFFF9EE2 -> 0x124B360')
    for site in (0x12458AD, 0x125147A):
        need((site & 63) + 4 <= 64, f'board patch dword {site:#x} inside one cache line')
    a_refs, b_refs, q_refs = rel_refs(0x124B3A0), rel_refs(0x124B360), rel_refs(0x1245820)
    need(a_refs == [0x12458AC, 0x124B39B], f'0x124B3A0 reached only from site A and the 0x124B360 tail jmp: {[hex(x) for x in a_refs]}')
    need(b_refs == [0x1251479], f'0x124B360 called only from site B: {[hex(x) for x in b_refs]}')
    need(q_refs == [0x1246A4F], f'the pre-query 0x1245820 called only by RideVehicle CanExec at 0x1246A4F: {[hex(x) for x in q_refs]}')
    need(call_target(0x1245887) == 0xA27CC0 and call_target(0x124B370) == 0xA27CC0, 'both searches bound last with the seat count 0xA27CC0')
    need(call_target(0x125148D) == 0x55E7F0, 'phase 1 reads GetVehicleType 0x55E7F0 right after site B')
    need(call_target(0x1251851) == 0xCB9470, 'phase 1 registers the seat through TryToRide 0xCB9470')
    need(rd(0xCB94A9, 6) == bytes.fromhex('ff9020020000'), 'TryToRide 0xCB9470 forwards to service vt+0x220')
    need(rip_target(0x124B454, 8) == 0x20AA7B8 and struct.unpack('<f', rd(0x20AA7B8, 4))[0] == 100.0,
         'the nearest search radius is 100.0 (10 m squared) at 0x20AA7B8')
    # Each site lies in one runtime function; no branch (rel32 anywhere, or rel8/rel32 inside that function's
    # linear sweep) lands inside a patched rel32 operand.
    funcs = runtime_functions()
    for site, lo, hi in ((0x12458AC, 0x12458AD, 0x12458B1), (0x1251479, 0x125147A, 0x125147E)):
        owner = [f for f in funcs if f[0] <= site < f[1]]
        need(len(owner) == 1, f'one runtime function covers {site:#x}: {owner}')
        local = linear_branch_targets(owner[0][0], owner[0][1]) if len(owner) == 1 else []
        inside = branch_refs(lo, hi) + [t for t in local if lo <= t < hi]
        need(inside == [], f'no branch into {lo:#x}..{hi:#x}: {[hex(x) for x in inside]}')
    need(any(f[0] == 0x1245820 and f[1] > 0x12458C2 for f in funcs), 'the pre-query 0x1245820 runs through site A')
    return out


# (call site, X vtable offset) of every census-found X call taking rec+0x240 (docs/anim-round2-throws.md "Other X
# calls"). Only the five vt+0x160 sites (and vt+0x38 before 0x1062696) are audited; the others are not.
# REC240_X_CALLS_BY_HAND load rcx before the census window. REC240_X_METHODS: the method in both concrete X vtables.
REC240_X_CALLS_BY_HAND = [(0x10565AE, 0x60), (0x1093F10, 0xD8)]
REC240_X_METHODS = {
    0x28: 0xDBE590, 0x30: 0xDC0920, 0x38: 0xDC64E0, 0x50: 0xDBF930, 0x58: 0xDC5540, 0x60: 0xDBF5C0, 0xA0: 0xDBD910,
    0xD8: 0xDC5350, 0xE0: 0xDBFCF0, 0xE8: 0xDC12A0, 0x120: 0xDC2E40, 0x128: 0xDC2F70, 0x140: 0xDBF180, 0x148: 0xDBDC50,
    0x160: 0xDC1740, 0x1B8: 0xDC26C0, 0x1C0: 0xDC1450, 0x1C8: 0xDC2890, 0x1D8: 0xDC1540,
}
REC240_X_CALLS = [
    (0x103DF15, 0x28), (0x103F4D7, 0x120), (0x1043BB8, 0x140), (0x1055ED3, 0x1C0), (0x1055EF1, 0x1C8),
    (0x1055F15, 0x1C0), (0x1055F33, 0x1C8), (0x1056562, 0x50), (0x10565DF, 0x148), (0x1059664, 0x1B8),
    (0x105968F, 0x1C8), (0x105DBAD, 0x148), (0x105DE8F, 0x148), (0x1062683, 0x38), (0x1062696, 0x160),
    (0x107905D, 0x1D8), (0x1087C05, 0x160), (0x10880D3, 0x160), (0x10892CB, 0x160), (0x108AA91, 0x160),
    (0x108B740, 0xE0), (0x108B775, 0xE8), (0x1098580, 0x58), (0x1098939, 0xA0), (0x1099BFC, 0x30),
    (0x1099E63, 0x128),
]


def handle_call_assertions():
    """WS1: the facts B/C rely on (docs/anim-round2-throws.md "Handle call resolved")."""
    import re
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    funcs = runtime_functions()
    need(rel_refs(0x101DE90) == [0x103C944], f'gun prim base ctor only called by the factory: {[hex(x) for x in rel_refs(0x101DE90)]}')
    need(rd(0x101DEB8, 3) == bytes.fromhex('488bf9'), 'gun prim base ctor: rdi = this before the prim+0x60 store')
    for target, hits in ((0xDC1740, [0x234B5D0, 0x234B9A0]), (0xDC64E0, [0x234B4A8, 0x234B878]), (0xDC0350, [0x234B4B0, 0x234B880])):
        need(qword_hits(target) == hits, f'{target:#x} is referenced only by the two manager vtable slots: {[hex(x) for x in qword_hits(target)]}')
        need(rel_refs(target) == [], f'no direct call/jmp to {target:#x}')
    need(rel_refs(0x1061F50) == [] and 0x23B3FB0 in qword_hits(0x1061F50),
         'iface reset 0x1061F50 is reached only through gun iface vtable slots (0x23B3FA0+0x10)')
    need((0xDC1740, 0xDC1800) in funcs and (0xDC64E0, 0xDC653E) in funcs, 'manager method bodies: exact runtime function ranges')
    need(rd(0xDC17FF, 1) == b'\xc3' and rd(0xDC1901, 2) == b'\xc3\xcc' and rd(0xDC653D, 2) == b'\xc3\xcc' and
         rd(0xDC0396, 10) == b'\xc3' + b'\xcc' * 9, 'manager method bodies end at the pinned ret')
    need(not any(b <= 0xDC0350 < e for b, e in funcs), '0xDC0350 is a frameless leaf (no unwind entry)')
    need(rd(0x106263B, 1) == b'\x76' and 0x106263D + rd(0x106263C, 1)[0] == 0x1062676,
         'iface reset: the class 2/3 jbe reaches the vt+0x38/vt+0x160 block')
    # Every call [rax+0x160] whose edx came from [reg+0x240] within the 48 bytes before: exactly the five
    # gun-plugin sites (a new site fails here before any partner support admission relies on B/C).
    xt = [s for s in pe.sections if s.Name.rstrip(b'\0').decode() == '.xtext'][0]
    data, va = xt.get_data(), xt.VirtualAddress
    load = re.compile(rb'(?:\x41)?\x8b[\x90-\x97]\x40\x02\x00\x00')
    sites = sorted(va + m.start() for m in re.finditer(rb'(?:\x41)?\xff[\x90-\x97]\x60\x01\x00\x00', data)
                   if load.search(data[max(0, m.start() - 48):m.start()]))
    need(sites == [0x1062696, 0x1087C05, 0x10880D3, 0x10892CB, 0x108AA91],
         f'rec+0x240 -> vt+0x160 call sites: {[hex(x) for x in sites]}')
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    # B's premise (review WS1-3): for a kind-12 handle the activation 0x105FDB0 stores at 0x10602E8 (rec+0x240) the
    # handle it loads at 0x105FEC0 from Y+0x40[id*[Y+0xC] + rec+0x3D9] and at 0x10602F0 (rec+0x24C) X vt+0x20 of it.
    # Data flow over the activation's control flow graph (recursive descent from its entry: every direct branch and
    # the one jump table followed, calls return and clobber the volatile registers, any other indirect jump fails;
    # only the paths that reach the use count). Each operand's writer must lie on every path from the entry to its
    # use and be the last write of that register before it. The kind-12 paths fix the three kind branches
    # (0x105FF8A: kind != 9; 0x106019C: kind outside 1..8; 0x106046D: kind 12), whose operand (eax / edi = the kind
    # saved at [rsp+0x40]) is checked the same way.
    lo, hi = 0x105FDB0, 0x1061C6F
    code, bad = {}, set()

    def insn(a):
        if a not in code:
            code[a] = next(md.disasm(rd(a, 15), a), None)
        return code[a]

    # The one indirect jump (kind 14, equips 0x203..0x209): jmp rcx through the 7 RVAs at 0x1061C70.
    need(insn(0x10605E2).op_str == 'eax, 6' and insn(0x10605E5).mnemonic == 'ja' and
         insn(0x10605F0).op_str == 'ecx, dword ptr [rdx + rax*4 + 0x1061c70]' and insn(0x10605FA).op_str == 'rcx',
         'activation: the kind-14 jump table 0x1061C70 has 7 entries')
    tables = {0x10605FA: jump_table(0x1061C70, 7)}

    def succ(a, force):
        i = insn(a)
        if a in force:
            return [force[a]]
        if a in tables:
            return tables[a]
        if i.mnemonic in ('ret', 'int3', 'ud2'):
            return []
        if i.group(capstone.CS_GRP_JUMP):
            if i.operands[0].type != capstone.x86.X86_OP_IMM:
                bad.add(a)
                return []
            return [i.operands[0].imm] if i.mnemonic == 'jmp' else [i.operands[0].imm, a + i.size]
        return [a + i.size]

    def on_paths(start, goal, force, dead=()):
        """Instructions on some path start -> goal inside the activation (goal excluded, dead removed)."""
        edges, todo = {}, [start]
        while todo:
            a = todo.pop()
            if a in edges or a == goal or a in dead:
                continue
            if not lo <= a < hi or insn(a) is None:
                bad.add(a)
                continue
            edges[a] = succ(a, force)
            todo += edges[a]
        live, grew = {goal}, True
        while grew:
            grew = False
            for a, nxt in edges.items():
                if a not in live and any(n in live for n in nxt):
                    live.add(a)
                    grew = True
        return sorted(live - {goal})

    def fam(reg):
        """Every capstone name of the 64-bit register reg (rax/rbx/rdx, rsi/rdi, r8..r15)."""
        if reg in ('rax', 'rbx', 'rcx', 'rdx'):
            return {reg, 'e' + reg[1:], reg[1:], reg[1] + 'l', reg[1] + 'h'}
        if reg in ('rsi', 'rdi', 'rbp', 'rsp'):
            return {reg, 'e' + reg[1:], reg[1:], reg[1:] + 'l'}
        return {reg, reg + 'd', reg + 'w', reg + 'b'}

    volatile = set().union(*(fam(r) for r in ('rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11')))

    def writes(path, regs):
        """The instructions in path that write regs (a call clobbers the volatile registers)."""
        return [a for a in path if ({insn(a).reg_name(r) for r in insn(a).regs_access()[1]} |
                                    (volatile if insn(a).mnemonic == 'call' else set())) & regs]

    def last_writer(reg, writer, use, force=None):
        force = force or {}
        dominates = writer in on_paths(lo, use, force) and on_paths(lo, use, force, dead={writer}) == []
        later = writes(on_paths(writer, use, force), fam(reg))
        return dominates and later == [writer], [hex(a) for a in later]

    kind12 = {0x105FF8A: 0x1060079, 0x106019C: 0x1060465, 0x106046D: 0x10606C9}
    for reg, writer, use, force, what in (
            ('r15', 0x105FDDF, 0x105FF1A, None, 'r15 = prim for X = [prim+0x60] at 0x105FF1A'),
            ('r14', 0x105FDFF, 0x105FEA5, None, 'r14 = rec for the slot index rec+0x3D9 at 0x105FEA5'),
            ('rdi', 0x105FE02, 0x105FEB4, None, 'rdi = Y for the load at 0x105FEB4'),
            ('rsi', 0x105FDF0, 0x105FEB8, None, 'esi = id for the load at 0x105FEB8'),
            ('rdx', 0x105FEA5, 0x105FEBE, None, 'edx = rec+0x3D9 for the load at 0x105FEBE'),
            ('rbx', 0x105FEC0, 0x105FF1E, None, 'ebx = the loaded handle for X vt+0x20 at 0x105FF1E'),
            ('rax', 0x105FF33, 0x105FF36, None, 'eax = ebx & 0x1F (the kind) saved at [rsp+0x40]'),
            ('rdi', 0x106018A, 0x106046A, kind12, 'edi = [rsp+0x40] (the kind) for the kind branches 0x106019C/0x106046D'),
            ('r14', 0x105FDFF, 0x105FF6F, kind12, 'r14 = rec for rsi = rec+0x1B0 at 0x105FF6F'),
            ('rsi', 0x105FF6F, 0x10602E8, kind12, 'rsi = rec+0x1B0 for the stores'),
            ('rbx', 0x105FEC0, 0x10602E8, kind12, 'kind 12: rec+0x240 = the handle loaded at 0x105FEC0'),
            ('r13', 0x105FF2E, 0x10602F0, kind12, 'kind 12: rec+0x24C = X vt+0x20 of that handle')):
        ok, later = last_writer(reg, writer, use, force)
        need(ok, f'activation: {what} (writer {writer:#x} on every path, later writes {later})')
    need(all(on_paths(lo, 0x10602E8, kind12, dead={a}) == [] for a in (0x1060465, 0x10606C9, 0x1060767)),
         'activation: every kind-12 path to the stores runs the pinned kind-12 route 0x1060465 -> 0x10606C9 -> 0x1060767')
    # eax at the kind-9 branch 0x105FF87: the kind saved at 0x105FF36, or its reload at 0x105FF60 after the
    # [prim+8] vt+0x140 call 0x105FF51; [rsp+0x40] (the kind) is written only at 0x105FF36 on the kind-12 paths to
    # 0x106018A.
    need(on_paths(lo, 0x105FF87, {}, dead={0x105FF36}) == [] and
         writes(on_paths(0x105FF36, 0x105FF87, {}), fam('rax')) == [0x105FF51, 0x105FF60] and
         on_paths(0x105FF51, 0x105FF87, {}, dead={0x105FF60}) == [],
         'activation: eax at 0x105FF87 is the kind (0x105FF36 on every path; after it only the 0x105FF51 call, '
         'always followed by the 0x105FF60 reload, writes eax)')
    stack40 = [a for a in on_paths(lo, 0x106018A, kind12) for op in insn(a).operands
               if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RSP and
               op.mem.index == 0 and op.mem.disp < 0x44 and op.mem.disp + op.size > 0x40 and op.access & capstone.CS_AC_WRITE]
    need(stack40 == [0x105FF36] and on_paths(lo, 0x106018A, kind12, dead={0x105FF36}) == [],
         f'activation: [rsp+0x40] writers before 0x106018A (kind 12): {[hex(a) for a in stack40]}')
    # Any kind: the other ebx/r13d writes between the load and the stores are kind 9's slot-2 reload (0x1060000,
    # 0x1060041) and 0x106065F's clear (ebx = r13d = 0) for a kind outside 1..8 and 12..14 whose equip [X+8] vt+0x50
    # refuses. Kind 12 reaches neither (the kind-12 checks above).
    anykind = on_paths(0x105FEC3, 0x10602E8, {})
    need(writes(anykind, fam('rbx')) == [0x1060000, 0x1060678] and
         writes(anykind, fam('r13')) == [0x105FF2E, 0x1060041, 0x106066D],
         f'activation: ebx/r13d writes between the load and the stores (any kind): '
         f'{[hex(a) for a in writes(anykind, fam("rbx"))]} / {[hex(a) for a in writes(anykind, fam("r13"))]}')
    # WS4 v1: B's premise for the C4 (kind 13) and arm (kind 14) lanes. A kind whose equip [X+8] vt+0x50 accepts
    # (0x1060486) takes the kind-12 route checked above; otherwise kind 13 sets class 3 and joins the stores at
    # 0x10602A8 (0x10605C2), kind 14 calls 0x10517C0 and the equip jump table 0x1061C70 (classes 4/5/6/0). On every
    # such path rec+0x240 is the handle loaded at 0x105FEC0 and rec+0x24C X vt+0x20 of it.
    kind13 = {0x105FF8A: 0x1060079, 0x106019C: 0x1060465, 0x106046D: 0x1060473, 0x1060486: 0x106048C,
              0x106048F: 0x1060495}
    kind14 = {0x105FF8A: 0x1060079, 0x106019C: 0x1060465, 0x106046D: 0x1060473, 0x1060486: 0x106048C,
              0x106048F: 0x10605C7, 0x10605CA: 0x10605D0}
    for force, name in ((kind13, 'kind 13'), (kind14, 'kind 14')):
        need(on_paths(lo, 0x10602E8, force) != [], f'activation: {name} reaches the stores')
        for reg, writer, use, what in (('rbx', 0x105FEC0, 0x10602E8, 'rec+0x240 = the handle loaded at 0x105FEC0'),
                                       ('r13', 0x105FF2E, 0x10602F0, 'rec+0x24C = X vt+0x20 of that handle'),
                                       ('rsi', 0x105FF6F, 0x10602E8, 'rsi = rec+0x1B0 for the stores')):
            ok, later = last_writer(reg, writer, use, force)
            need(ok, f'activation: {name}: {what} (writer {writer:#x} on every path, later writes {later})')
    need(on_paths(lo, 0x10602E8, kind13, dead={0x1060495}) == [],
         'activation: every kind-13 path to the stores sets class 3 at 0x1060495')
    need(not bad, f'activation: undecodable, out-of-range or indirect control flow at {[hex(a) for a in sorted(bad)]}')
    # WS4 v1: the arm equip and energy setter are reached only through their slots of the ctl vtable 0x23C1720 and
    # of its sibling 0x23C14B0 (no direct call or jump).
    for target, disp in ((0x11B1B60, 0x1F0), (0x11AF820, 0xE8)):
        need(qword_hits(target) == [0x23C14B0 + disp, 0x23C1720 + disp],
             f'{target:#x} is referenced only by its two ctl vtable slots: {[hex(x) for x in qword_hits(target)]}')
        need(rel_refs(target) == [], f'no direct call/jmp to {target:#x}')
    need((0x11B1B60, 0x11B1D67) in funcs, 'ctl vt+0x1F0 0x11B1B60: exact runtime function range (whole body pinned)')
    # Every other X call that takes rec+0x240 (review WS1-2): an indirect call whose rcx is [r+0x60] (prim+0x60)
    # or [r+0x30] (iface+0x30 = prim+0x60) and whose edx/r8d is [r+0x240], both loaded in the 48 bytes before and
    # neither rewritten nor crossed by another call. B only guards the input-driven modes 28/30/33/37 (four of the five
    # vt+0x160 sites); every other site, the iface reset among them, is covered only by C's per-job row check (it
    # latches support off on a foreign row). Only the vt+0x160/vt+0x38 methods were audited. 0x10565AE (vt+0x60) and 0x1093F10 (vt+0xD8) load rcx
    # before the window and are checked separately below.
    arg_re = re.compile(rb'(?:\x41)?\x8b[\x90-\x97]\x40\x02\x00\x00|[\x44\x45]\x8b[\x80-\x87]\x40\x02\x00\x00')
    rcx_re = re.compile(rb'[\x48\x49]\x8b[\x48-\x4b\x4d-\x4f][\x30\x60]')
    fams = {'edx': {'rdx', 'edx', 'dx', 'dl', 'dh'}, 'r8d': {'r8', 'r8d', 'r8w', 'r8b'}}
    xcalls = []
    for m in re.finditer(rb'(?:\x41)?\xff(?:[\x90-\x97]..\x00\x00|[\x50-\x57].)', data, re.S):
        s = m.start()
        args = [a.start() for a in arg_re.finditer(data, max(0, s - 48), s)]
        rcxs = [r.start() for r in rcx_re.finditer(data, max(0, s - 48), s)]
        if not args or not rcxs:
            continue
        start = min(args[-1], rcxs[-1])
        ins = list(md.disasm(data[start:m.end()], va + start))
        at = {i.address: i for i in ins}
        if not ins or ins[-1].address != va + s or ins[-1].mnemonic != 'call' or va + args[-1] not in at or va + rcxs[-1] not in at:
            continue
        arg, rcx = at[va + args[-1]], at[va + rcxs[-1]]
        reg = arg.op_str.split(',')[0]
        if reg not in fams or not arg.op_str.endswith('+ 0x240]') or not rcx.op_str.startswith('rcx, qword ptr ['):
            continue
        if any(i.mnemonic == 'call' or (i.address > arg.address and {i.reg_name(r) for r in i.regs_access()[1]} & fams[reg]) or
               (i.address > rcx.address and {i.reg_name(r) for r in i.regs_access()[1]} & {'rcx', 'ecx', 'cx', 'cl', 'ch'})
               for i in ins[:-1]):
            continue
        xcalls.append((va + s, ins[-1].operands[0].mem.disp))
    need(xcalls == REC240_X_CALLS, f'X calls taking rec+0x240: {[(hex(a), hex(d)) for a, d in xcalls]}')
    for site, disp in REC240_X_CALLS_BY_HAND:
        i = next(md.disasm(rd(site, 7), site), None)
        need(i is not None and i.mnemonic == 'call' and i.operands[0].type == capstone.x86.X86_OP_MEM and
             i.operands[0].mem.disp == disp and arg_re.search(data, site - va - 48, site - va) is not None and
             (site, disp) not in xcalls,
             f'{site:#x}: call [r+{disp:#x}] with edx/r8d = [r+0x240] in the 48 bytes before, outside the census')
    slots = {d for _, d in REC240_X_CALLS + REC240_X_CALLS_BY_HAND}
    need(set(REC240_X_METHODS) == slots, 'REC240_X_METHODS names exactly the X slots the rec+0x240 calls use')
    for disp in sorted(slots):
        for vt in (0x234B840, 0x234B470):
            got = struct.unpack('<Q', rd(vt + disp, 8))[0] - IB
            need(got == REC240_X_METHODS.get(disp), f'X vtable {vt:#x}+{disp:#x} = {got:#x} (REC240_X_METHODS)')
    return out


failures += anim_round1_assertions()
failures += ws4_v2_assertions()
failures += support_native_assertions()
failures += vehicle_assertions()
failures += board_assertions()
failures += handle_call_assertions()
if failures:
    print('\n'.join(failures))
    raise SystemExit(1)
CHECK = '--check' in sys.argv


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
w('// The seated vehicle rider (partner_ride_seat): little-endian dwords, original -> patched.')
w('inline constexpr std::uintptr_t ride_seat_rva=0x119883c, ride_cam_play_rva=0x1198d56, ride_cam_plain_rva=0x1198d81, ride_cam_end_rva=0x11992e9;')
w('inline constexpr std::uint32_t ride_seat_old=0x02b04130, ride_seat_base=0x00b04130;  // 30 41 b0 02 / 30 41 b0 <seat>')
w('inline constexpr std::uint32_t ride_cam_play_old=0x020082c7, ride_cam_play_new=0x020064eb;  // c7 82 00 02 / eb 64 00 02 (jmp 0x1198dbc)')
w('inline constexpr std::uint32_t ride_cam_plain_old=0x08478b48, ride_cam_plain_new=0x084739eb;  // 48 8b 47 08 / eb 39 47 08 (jmp 0x1198dbc)')
w('inline constexpr std::uint32_t ride_cam_end_old=0xf0818948, ride_cam_end_new=0xf0810ceb;  // 48 89 81 f0 / eb 0c 81 f0 (jmp 0x11992f7)')
w('// The contract blocks partner_ride_seat re-checks before every write (code_sites rvas).')
w('inline constexpr std::uintptr_t ride_seat_blocks[]={0x1198770,0x11987b5,0x11987e5,0x1198834,0x1198870,0x1198cc0,0x1198cff,')
w('    0x1198d22,0x1198d62,0x1198d9d,0x1198db5,0x1198dbc,0x11992c3,0x1199508};')
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
w('// Phase-1 local decision: the base action plugin\'s per-id handler 0x109f0e0 (node vtable 0x23b5090 +0x18)')
w('// sends phase 1 to 0x109e130: cmp edx,[P+0x218] / setne bpl at 0x109e17c (edx = id, rax = P); bpl = 0 is the')
w('// local branch (event 4: the act handlers sample the pad move intent into rec+0x2c0/+0x2c4), 1 the remote one')
w('// (event 5). The detour: the setne dword at 0x109e182 becomes jmp rel8 to a jmp rel32 written into the int3')
w('// padding 0x109e128 (outside every function), whose stub runs setne, forces bpl = 0 for the drain\'s id/P/thread')
w('// and jumps back to 0x109e186 (flags recomputed there; rax preserved).')
w('inline constexpr std::uintptr_t p1_handler_rva=0x109f0e0, p1_fn_rva=0x109e130, p1_cmp_rva=0x109e17c, p1_setne_rva=0x109e182;')
w('inline constexpr std::uintptr_t p1_back_rva=0x109e186, p1_pad_rva=0x109e128, p1_pad_begin_rva=0x109e123, p1_pad_end_rva=0x109e130;')
w('inline constexpr std::uint32_t p1_setne_orig=0xc5950f40;  // 40 0f 95 c5 (setne bpl)')
w('inline constexpr std::uint32_t p1_setne_new=0xcccca4eb;   // eb a4 (jmp 0x109e128) cc cc')
w('// Instance 1\'s base action record (rec1): current act, next act, sampled move magnitude and heading.')
w('inline constexpr std::size_t rec_act=0x2a0, rec_act_next=0x2a1, rec_move_mag=0x2c0, rec_move_heading=0x2c4;')
w('// --- Native actions: PlayerPad enum values (0x9d3a78..0x9d3ee5, pinned with their names) ---------')
w('inline constexpr std::uint32_t pad_stance=0x2, pad_dash=0x4, pad_hold=0x8, pad_fire=0x10, pad_action=0x20, pad_reload=0x40;')
w('inline constexpr std::uint32_t pad_side_roll=0x10000, pad_evade=0x40000, pad_vehicle_dash=0x200000;')
w('// EVADE press consumers (act 27 = 0x1b into Q+0x18[id]); their remote gates are true only with S_IS_ONLINE.')
w('inline constexpr std::uintptr_t evade_site_a=0x1148c6c, evade_site_b=0x1299821, evade_site_c=0x129b7db;')
w('inline constexpr std::uint16_t act_evade=0x1b;')
w('// --- Native rates: the per-id movement rate records of *(PS+0x260) ------------------------------')
w('// sp = *(PS+0x260) (interface vtable 0x23ad380 at object+0x20; 0x23ad328 while destroyed: refused);')
w('// rec = *(sp+0x10) + (id - *(u32)(sp+8))*0x88 with id - base < *(u32)(sp+0xc), record vtable 0x23ad2b0')
w('// (0x23ad260 while constructed/destroyed: refused). Crawl rate = +0x44 = +0x3c * +0x40; dash = +0x50 = +0x48 * +0x4c.')
w('inline constexpr std::uintptr_t rate_sp_vtable_rva=0x23ad380, rate_rec_vtable_rva=0x23ad2b0;')
w('inline constexpr std::size_t ps_rates=0x260, sp_base_id=0x8, sp_count=0xc, sp_records=0x10, rate_rec_stride=0x88;')
w('inline constexpr std::size_t rate_first=0x3c, rate_count=6;  // +0x3c crawl, +0x40 crawl factor, +0x44, +0x48 dash, +0x4c dash factor, +0x50')

w('// --- Native weapons (docs/native-puppet.md "Weapons") ---------------------------')
w('// Fire filter: RequestBullet 0xd32c60(iface, record) is reached only through slot 0 of the bullet iface vtables')
w('// 0x2337de8 (live) and 0x2337d18 (destroying); record+0x2a is the shooter (GameObjectId, type bits 0xfe00).')
w('inline constexpr std::uintptr_t fire_request_rva=0xd32c60, fire_slot_live_rva=0x2337de8, fire_slot_dying_rva=0x2337d18;')
w('inline constexpr std::size_t shot_owner=0x2a;')
w('inline constexpr std::uint16_t shot_owner_type_mask=0xfe00;')
w('// Gun record (rec = *(prim+0x58) + (id - node base)*0x610) fields: magazine getter 0x10435d0, CanReload 0x1051f30,')
w('// the reload request 0x1094770 (rec+0x3c0 bit 20) and its gate in 0x103e2d0.')
w('inline constexpr std::size_t gun_cap=0x210, gun_cap_sub=0x212, gun_equip=0x24c, gun_mag=0x25c, gun_mag_sub=0x25e;')
w('inline constexpr std::size_t gun_stock=0x260, gun_stock_sub=0x262, gun_flags27c=0x27c, gun_flags280=0x280, gun_b3d5=0x3d5, gun_slot=0x3da;')
w('inline constexpr std::uint32_t gun27c_sub=0x200, gun27c_double=0x100000, gun27c_half=0x80000, gun280_noreload=0x80000;')
w('inline constexpr std::uint32_t gun3c0_local=0x1, gun3c0_reload=0x100000, gun3c0_full_mag=0x10000000, gun3c0_cap_stock=0x20000000;')
w('inline constexpr std::uint32_t gun_double_equip_lo=0x42b, gun_double_equip_span=4;  // equip 0x42b..0x42e')
w('inline constexpr std::uint32_t gun_class_shift=6, gun_class_mask=7, gun_class_gun=1;')
w('// vars = *(vars_slot_rva): loadout and the shoot-count leak check (registrations pinned below).')
w('inline constexpr std::size_t vars_weapons=0x548, vars_customized=0xb06, vars_chimera=0xb30, vars_shoot_count=0x144;')
w('inline constexpr std::size_t vars_weapons_count=6, vars_customized_count=3, vars_chimera_count=36;')
w('// The viewer\'s weapon/ammo vars block (14 contiguous registrations, weapons .. u16[46] at +0x614): read-only leak digest.')
w('inline constexpr std::size_t vars_ammo_begin=0x548, vars_ammo_end=0x670;')
w('// Service root (read-only noise census): svc = *(*(0x2c3a8a0)+8); gm = *(svc+0x98), bullets = *(gm+0x218);')
w('// the shot noise iface (group+0xa0) = *(*(svc+0xa8)+8).')
w('inline constexpr std::uintptr_t service_root_rva=0x2c3a8a0;')
w('inline constexpr std::size_t root_service=0x8, svc_gm=0x98, svc_noise_owner=0xa8, noise_owner_iface=0x8, gm_bullets=0x218;')
w('// Loadout base ids (pure reads; the stock46 writer contract checks the same tables): the equip type table object')
w('// (vtable 0x22ad180) holds u16 packed descriptors at +8 + id*2 (row = packed >> 6, type = packed & 0x3f; 46 -> 0x6244),')
w('// GunBasic holds 12-byte recipes at (row - 1)*12 (byte 0 = receiver; row 393 = the stock46 recipe). Stock ids < 649.')
w('inline constexpr std::uintptr_t equip_types_rva=0x2a70920, equip_types_vt_rva=0x22ad180, gun_basic_rva=0x2c25c50;')
w('inline constexpr std::size_t equip_types_packed=0x8, gun_basic_row=12, gun_basic_rows=0x1818 / 12, stock_ids=649;')
w('inline constexpr std::uint16_t stock46_packed=0x6244;')
w('inline constexpr unsigned char stock46_recipe[12]={157,85,110,36,0,0,17,0,0,8,0,4};')
w('// The type table index of an equip id (A2A3E0 0xa2a478..0xa2a4a5): ids below 0x230 index themselves, ids 0x400..0x44f')
w('// index 0x230..0x27f (sub 0x1d0) and ids 0x600.. index 0x280.. (sub 0x380); the table holds stock_ids indices.')
w('inline constexpr std::uint32_t types_index_mid=0x230, types_index_hi=0x280, types_bias_mid=0x1d0, types_bias_hi=0x380;')
w('// A customized alias\'s own type (A2A3E0 0xa2a3e6..0xa2a432, aliases 0x367..0x36c): r = u8 [alias table 0x2c934a0 +')
w('// (id - 0x367)*0xe + 0x12] (getter 0x1e03790), R = the equipment resource 0x2a711f0 (getter 0xa3c5b0; R+8 = GunBasic):')
w('// k = u8 [*(R+0x10) + r*6 - 2], type = u8 [*(R+0x68) + k*3] & 0x1f; r == 0 or a null record: no type. Runtime-filled .data2.')
w('inline constexpr std::uintptr_t alias_table_rva=0x2c934a0, equip_resource_rva=0x2a711f0;')
w('inline constexpr std::uint16_t alias_first=0x367, alias_count=6;')
w('inline constexpr std::size_t alias_stride=0xe, alias_receiver=0x12, res_basic=0x8, res_parts=0x10, res_kinds=0x68;')
w('inline constexpr std::size_t part_stride=6, part_kind_back=2, kind_stride=3;')
w('inline constexpr std::uint32_t alias_type_mask=0x1f;')
w('// --- Native polish: facing D3, the look source hook (partner_look_source.cpp) ----------------')
w('// PS phase 0 calls 0xffac00(PS, id) only at 0xff29fc; the qword 0xff29f8 (aligned, one cache line) holds that call;')
w('// only its rel32 low word changes (0xffac00 -> the int3 padding 0xffad66, after the ret at 0xffad65).')
w('inline constexpr std::uintptr_t look_src_call_block_rva=0xff29f7, look_src_call_rva=0xff29fc, look_src_qword_rva=0xff29f8;')
w('inline constexpr std::uintptr_t look_src_next_ip=0xff2a01, look_fn_rva=0xffac00, look_fn_offline_rva=0xffac19;')
w('inline constexpr std::uintptr_t look_fn_cam_rva=0xffac34, look_fn_store_rva=0xffad42, look_pad_rva=0xffad66, look_pad_end_rva=0xffad70;')
w('inline constexpr std::uint64_t look_src_qword_orig=0x0081ffe8cd8b48d6ull;  // d6 48 8b cd e8 ff 81 00')
w('inline constexpr std::uint64_t look_src_qword_new=0x008365e8cd8b48d6ull;   // d6 48 8b cd e8 65 83 00')
w('// --- Native polish: weapons F1 (src rings), F2 (the magazine store row), F3 (shot noise) ----------')
w('// src = *(PP+8): u32 held history at +8, stick i X/Y at +0x10+8i/+0x20+8i, trigger i at +0x30+8i (newest first),')
w('// capacity +0x40, samples of this update +0x44 (src update 0x435260 = src vt+0x38, called by the PP update).')
w('inline constexpr std::size_t src_rings=0x8, src_ring_count=7, src_ring_cap=0x40, src_ring_new=0x44;')
w('// The ammo copy 0x108c8c0 (gun phase 0, every frame): Y = *(*(P+0xc8)+8) (iface vtable 0x23894b0 or 0x23895d0, setter')
w('// +0x20 = 0xee96c0), idx = id * u32[Y+0xc] + u8 rec+0x3da, type = u32[Y+0x40][idx] & 0x1f, row = [Y+0x50] + idx*6:')
w('// rec+0x25c = word0, rec+0x25e = word1 (sub magazine). rec+0x3d0 bit 27 (never set offline) gates a tamper check.')
w('inline constexpr std::size_t store_y=0x8, store_stride=0xc, store_types=0x40, store_rows=0x50, store_row_size=6;')
w('inline constexpr std::uint32_t store_type_mask=0x1f, store_type_word0_only=9;')
w('inline constexpr std::uintptr_t store_vt_rva=0x23894b0, store_vt_derived_rva=0x23895d0, store_set_rva=0xee96c0;')
w('inline constexpr std::size_t store_vt_set=0x20, gun_tamper=0x3d0;')
w('// The store init 0xee92f0 (object vt+8 of both classes) sizes types and rows by Y+0x10 = ids * stride.')
w('inline constexpr std::size_t store_total=0x10;')
w('inline constexpr std::uint32_t gun3d0_tamper_check=0x8000000;')
w('// The trigger (0x10964F7): rec+0x3C0 bit 24 = a shot this update, u8 rec+0x3DF = its count; rec+0x2C0 = the current mode.')
w('inline constexpr std::size_t gun_mode_cur=0x2c0, gun_shots=0x3df;')
w('// The slot-3 spend (C(b) viewer-own rebase): u8 rec+0x2c2 is the mode state (mode 37 tests it at 0x10879bf and sets')
w('// 1 -> 2 in the throw block before its decrement; mode 46 sets 3 -> 4/5 after its decrement); u8 rec+0x3dc is the slot')
w('// sub (the classes 2/3 ammo copy: k = Y vt+0x68(rec+0x3da, rec+0x3dc) at 0x108cb57, slot 3 -> k = 6 + sub).')
w('inline constexpr std::size_t gun_state=0x2c2, gun_sub=0x3dc;')
w('inline constexpr std::uint32_t gun3c0_shot=0x1000000;')
w('inline constexpr std::uint8_t gun_mode_fire=9, gun_mode_fire2=10;')
w('// AddNoise 0x515270 = slot 0 of the noise iface vtable 0x21876a8; the record keeps its owner at +0x12.')
w('inline constexpr std::uintptr_t noise_fn_rva=0x515270, noise_vtable_rva=0x21876a8;')
w('inline constexpr std::size_t noise_owner=0x12;')
for line in partner_aim_sites.HEADER:
    w(line)
w('// --- Native weapons: the partner\'s own reload and weapon switch (docs/native-puppet.md "Weapons") ------')
w('// The gun record modes (rec+0x2C0 current, written by the transition 0x1040a00 from the table 0x2c556a0): 7 aim,')
w('// 8 weapon change (0x1074070), 11/12 reload (0x1077840: the enter sets rec+0x3C0 bit 26 and Q+0x20[id] = 5 and')
w('// rec+0x3E8 = the completion base; the exit 0x10790aa clears bit 26; the only completion 0x1054900 is event driven).')
w('inline constexpr std::uint8_t gun_mode_aim=7, gun_mode_change=8, gun_mode_reload=11, gun_mode_reload2=12;')
w('inline constexpr std::uint32_t gun3c0_switch=0x80, gun3c0_reload_mode=0x4000000;')
w('inline constexpr std::size_t gun_slot_hand=0x3d9, gun_slot_src=0x3de, gun_m3e4=0x3e4, gun_m3e6=0x3e6, gun_m3e8=0x3e8;')
w('inline constexpr std::uint8_t gun_slot_follow=7;  // rec+0x3DE == 7: phase 0 takes the slot from the equip controller')
w('inline constexpr std::size_t q_upper=0x20;          // u8 *(Q+0x20)[id] (5 = reload, 0x1077a0a)')
w('inline constexpr std::uint8_t q_upper_reload=5;')
w('// The equip controller ctl = *(P+0xC8) (vtable 0x23c1720; ctl+0x18 = P); the inventory *(ctl+8) is the store Y with')
w('// the handles u32 [Y+0x40][id*stride + slot] (empty: handle >> 16 == 0xffff; equip (handle >> 5) & 0x7ff; type & 0x1f).')
w('// The equipment row of instance id is *(Q+0xC0) + id*0x3a: u16 +0 the current equip, +2/+4/+6 slots 0/1/2, the low')
w('// nibble of +0x36 the current slot. SetCurrentSlot 0x11afa80 for a non-local id and slot types 0/1/2/5/6 writes')
w('// Q+0x17C |= 1 << id, the nibble and row+0 = (row+2+2*slot) & 0x7ff, then exits at 0x11afe4b before any call.')
w('inline constexpr std::uintptr_t equip_ctl_vt_rva=0x23c1720, set_current_slot_rva=0x11afa80;')
w('inline constexpr std::size_t ctl_player=0x18, ctl_inventory=0x8, q_equip_rows=0xc0, q_equip_dirty=0x17c, equip_row_stride=0x3a;')
w('inline constexpr std::size_t equip_row_current=0x0, equip_row_slots=0x2, equip_row_nibble=0x36, inv_handles=0x40;')
w('inline constexpr std::uint32_t equip_id_mask=0x7ff, handle_empty_hi=0xffff, handle_type_mask=0x1f, handle_equip_shift=5;')
w('// --- Anim round 1, weapons (docs/anim-round1-weapons.md) --------------------------------------')
w('// rec+0x310: the reload rate (enter 0x10778f0 = provider vt+0x28(id, 6), x2 at 0x1077922; exit 0x10790d9 = 1.0); the')
w('// motion segment requests 0x1059620/0x10596b0 read it by value (op 0 per frame: 0x107a550). Written by the hook only')
w('// inside modes 11/12 (partner_native_reload_rate=adapt).')
w('inline constexpr std::size_t gun_rate=0x310;')
w('// Fire census (read-only): RequestBullet record +0x2e bullet id (u16), +0x30 kind, +0x31 sub, +0x32 flags; the noise')
w('// record {pos, u8 kind +0x10, u8 flags +0x11, u16 owner +0x12}.')
w('inline constexpr std::size_t shot_bullet_id=0x2e, shot_kind=0x30, shot_sub=0x31, shot_flags=0x32, noise_kind=0x10;')
w('// The class-3 projectile request (0x1303450): *(*(svc+0x98)+0x210) vt+0 = 0x1337760 (iface vtable slot 0,')
w('// read-only data, no direct caller); request +0x2e the projectile id, +0x32 the owner (shot record +0x2a), +0x34.')
w('// The factory 0x1336440 stores the live iface vtable 0x23e2d50 at +0x20; 0x23e2bd0 is the base (dtor) iface vtable.')
w('inline constexpr std::uintptr_t proj_slot_live_rva=0x23e2d50, proj_slot_base_rva=0x23e2bd0, proj_request_rva=0x1337760;')
w('inline constexpr std::size_t gm_projectiles=0x210, proj_id=0x2e, proj_owner=0x32, proj_aux=0x34;')
w('// The rocket punch (gun mode 53): the projectile service vt+0x58 = 0x1337920, request +0x32 the owner (id & 0x1ff).')
w('// Thrown objects (gun mode 37, helper 0x1061d90): gm+0x200 vt+8 = 0x15e0230 (live iface vtable 0x2429ad0, base 0x24299a0),')
w('// request +0x32 the owner, +0x3c the equip (low 11 bits), +0x3e flags. Placed objects (gun mode 46): gm+0x208 vt+0x48 =')
w('// 0xfccd80 (live 0x23a9fd0, base 0x23a9d20), request +0x30 the equip, +0x32 the owner, +0x3c flags.')
w('inline constexpr std::uintptr_t rocket_request_rva=0x1337920, thrown_request_rva=0x15e0230, placed_request_rva=0xfccd80;')
w('inline constexpr std::uintptr_t thrown_slot_live_rva=0x2429ad0, thrown_slot_base_rva=0x24299a0;')
w('inline constexpr std::uintptr_t placed_slot_live_rva=0x23a9fd0, placed_slot_base_rva=0x23a9d20;')
w('inline constexpr std::size_t rocket_slot=0x58, thrown_slot=0x8, placed_slot=0x48, gm_thrown=0x200, gm_placed=0x208;')
w('inline constexpr std::size_t req_owner=0x32, rocket_id=0x2c, rocket_aux=0x3e, thrown_id=0x3c, thrown_aux=0x3e, placed_id=0x30, placed_aux=0x3c;')
w('// --- Native thrown support v1: permanent stock guard (partner_support_guard.cpp) ------------')
w('inline constexpr std::uintptr_t support_stock_fn_rva=0x108c8c0, support_stock_load_rva=0x108c8e5;')
w('inline constexpr std::uintptr_t support_stock_pad_rva=0x108c888, support_stock_pad_begin_rva=0x108c888, support_stock_pad_end_rva=0x108c890;')
w('inline constexpr std::uintptr_t support_stock_back_rva=0x108c8ec, support_stock_remote_rva=0x108c8fc;')
w('inline constexpr std::uint32_t support_stock_load_orig=0x04818b41, support_stock_load_new=0xcccca1eb;')
w('inline constexpr std::size_t support_stock_counts=0x614, support_stock_count=46, support_item_handle=0x240;')
w('inline constexpr std::uint8_t equip_type_thrown=12, equip_type_placed=13;')
w('// --- WS3 vehicles (partner_vehicle, partner_player::WriteSeatTarget; docs/vehicle-ws3.md) ---------')
w('// The unit chain: service = *(*(*(*(service_root)+8)+0x98)+0x2a8) (vt 0x22ac8e0, +0x18 = the lookup), object =')
w('// service+0xb0 (vt 0x22ed9f0: +0 count, +0x78 SetPosition forwarder), V = object+0x1a8 (vt+0x70), RB = V+0x98')
w('// (vt+0x78), cat = (RB+0x70)[type >> 4] (vt+0x1f0), body = (cat+0x1c8)[slot].')
w('inline constexpr std::uintptr_t vehicle_service_vt_rva=0x22ac8e0, vehicle_lookup_rva=0xa272a0, vehicle_object_vt_rva=0x22ed9f0;')
w('inline constexpr std::uintptr_t vehicle_count_fn_rva=0xb2dd30, vehicle_setpos_fn_rva=0xb36780, vehicle_v_fn_rva=0x16fbec0;')
w('inline constexpr std::uintptr_t vehicle_rb_fn_rva=0x16fddc0, vehicle_cat_fn_rva=0xba9710;')
w('inline constexpr std::uintptr_t vehicle_v_vts[]={' + ','.join(f'0x{v:x}' for v in VEHICLE_V_VTS) + '};')
w('inline constexpr std::uintptr_t vehicle_rb_vts[]={' + ','.join(f'0x{v:x}' for v in VEHICLE_RB_VTS) + '};')
w('inline constexpr std::uintptr_t vehicle_cat_vts[]={' + ','.join(f'0x{v:x}' for v in VEHICLE_CAT_VTS) + '};')
w('// The contract blocks partner_vehicle re-checks before every tilt write (code_sites rvas) and the')
w('// small-move limits of 0xBA9710 (read-only .edata dwords, little-endian float bits).')
w('inline constexpr std::uintptr_t vehicle_blocks[]={' + ','.join(f'0x{rva:x}' for rva, _, _ in VEHICLE_SITES if rva != 0x9DAAE1) + '};')
w('struct DataWord { std::uintptr_t rva; std::uint32_t value; };')
w('inline constexpr DataWord vehicle_data[]={' + ','.join(f'{{0x{rva:x},0x{value:08x}}}' for rva, value in VEHICLE_DATA) + '};')
w('// The DM record entry arrays (rec = the body-1 DM record, stride 0x250): the named state code rec+0x78+8i,')
w('// the flags rec+0xb8+i (2: target, 8: named state), the target id rec+0xc0+2i, the CNP rec+0xd0+8i, the')
w('// current entry rec+0x1a0. The seat-by-id write stores the ids first, then ORs flag 2 (critic #17).')
w('inline constexpr std::size_t dm_entry_state=0x78, dm_entry_flags=0xb8, dm_entry_target=0xc0, dm_entry_cnp=0xd0, dm_entry_cur=0x1a0;')
w('inline constexpr std::uint8_t dm_entry_flag_target=2, dm_entry_flag_state=8, dm_entry_max=8;')
w('inline constexpr std::uint64_t dm_cnp_ppos_c=0x2ccada28e251ull;  // low 48 bits')
w('inline constexpr std::uintptr_t seat_target_blocks[]={0x9daae1,0x1198770,0x11987b5,0x11987e5,0x1198834,0x1198cc0,0x1198cff,0x1199508};')
w('// --- WS1 handle call: X = [prim+0x60] = [[gm]+0x98]+0x1e8 (the equipment-handle manager, = inventory+0x58); its')
w('// vt+0x160 = 0xdc1740 (row X+0x140[(h>>16)*6] byte 4 bit 7 + the model part), vt+0x38 = 0xdc64e0, vt+0x40 = 0xdc0350;')
w('// rows: u16 equip +0, u16 owner +2, byte 4 bit 7 pin, byte 5 bit 0 model instance; capacity X+0x68.')
w('inline constexpr std::uintptr_t handle_engine_vt_rva=0x234b840, handle_engine_vt_alt_rva=0x234b470, handle_pin_rva=0xdc1740;')
w('inline constexpr std::size_t prim_handle_engine=0x60, handle_engine_capacity=0x68, handle_engine_rows=0x140;')
w('inline constexpr std::size_t handle_row_stride=6, handle_row_equip=0, handle_row_owner=2, handle_vt_pin=0x160, handle_vt_release=0x38;')
w('// --- WS4 step 0 census (read-only; partner_placed_census): the placed iface owner table as the detonation 0xfc54f0')
w('// (iface vt+0x68) reads it: owners u16 iface+0xb0[0..u32 iface+0x6c), slot first + per*index + j (u32 iface+0x68,')
w('// u32 iface+0x70), flags iface+0x90 stride 3 bit 0, equip u16 iface+0x18 stride 4, age u16 iface+0xd8[per*index + j].')
w('// Mode 46 op 1 (gate 0x107bc5b: offline or the local id -> 0x107bc9b) latches the gun plugin: +0x350 bit 3 and')
w('// u16 +0x3e8 = u16 +0x3d8 (0x107bd64..0x107bd8b); its online-remote branch 0x107bc7b folds rec+0x3cc bits 22-23')
w('// into 20-21 and jumps to 0x107bd93. The stun arm (mode 52, 0x1085b50) op 0 reads [P+0xb0]+0x2c bit 0 and the')
w('// float [P+0xb0]+0x10 directly.')
w('inline constexpr std::uintptr_t placed_detonate_rva=0xfc54f0, place_gate_rva=0x107bc5b, place_gate_remote_rva=0x107bc7b, place_shared_rva=0x107bc9b;')
w('inline constexpr std::size_t placed_vt_detonate=0x68, placed_first=0x68, placed_owner_count=0x6c, placed_per_owner=0x70;')
w('inline constexpr std::size_t placed_equips=0x18, placed_equip_stride=4, placed_flags=0x90, placed_flag_stride=3, placed_owners=0xb0, placed_age=0xd8;')
w('inline constexpr std::size_t prim_place_flags=0x350, prim_place_target=0x3d8, prim_place_latched=0x3e8, gun_surface=0x3cc;')
w('inline constexpr std::uint32_t prim_place_bit=0x8;')
w('inline constexpr std::size_t pad_stun_mag=0x10, pad_stun_mask=0x2c;')
w('// --- WS4 v1 (read-only digests; partner_placed_policy): the arm equip ctl vt+0x1f0 = 0x11b1b60 (ChangeEquip type 14)')
w('// and the arm energy ctl vt+0xe8 = 0x11af820 -> 0x11af5e0: index i (0 for 0x203..0x206), movss [Y+0x1c+4i], the word')
w('// u16 [[Y+0x98]+2k] (k = 5, through ctl vt+0xc0; store vt+0x88 0xee9050 reads it), u32 [ctl+0x58+4i] and u32')
w('// vars+0x670 = their sum (vars = [ctl+0x20]); no instance id anywhere. Mode 46 op 1 latches the record\'s own prim.')
w('inline constexpr std::uintptr_t arm_equip_rva=0x11b1b60, arm_energy_rva=0x11af820, store_count_read_rva=0xee9050;')
w('inline constexpr std::size_t ctl_vt_arm_equip=0x1f0, ctl_vt_arm_energy=0xe8, store_vt_count=0x88;')
w('inline constexpr std::size_t ctl_vars=0x20, ctl_arm_frac=0x58, store_arm_energy=0x1c, store_counts=0x98, vars_arm_energy_sum=0x670;')
w('inline constexpr unsigned arm_energy_k=5, arm_energy_slots=4;')
w('inline constexpr std::uint16_t arm_equip_first=0x203, arm_equip_last=0x209;')
w('// --- WS4 v2 (partner_place_mirror): rec+0x3cc bit 18 asks modes 43/44 for 46 (place), bit 19 asks 44 for 47')
w('// (detonate); bits 20-21 the placed surface class (46 op 1: its probe, else bits 22-23), 22-23 the preview class.')
w('inline constexpr std::uint32_t gun3cc_place_req=0x40000, gun3cc_detonate_req=0x80000, gun3cc_surface_mask=0x300000, gun3cc_preview_mask=0xc00000;')
w('inline constexpr std::uint8_t gun_mode_place_hold=43, gun_mode_place_ready=44, gun_mode_place=46, gun_mode_detonate=47;')
w('// The decode reads the press word through PP vt+0x40 (0x1205e80): [PP+8]+0x50, or PP+0x468 when 0, & ~PP+0x28;')
w('// FIRE 0x10 with a class in rec+0x3cc 22-23 reaches placed vt+0x40 (0xfcb1f0 -> 0xfc9c00 claims an owner row).')
w('// rec+0x3c0 bit 26: set by 46 op 1, cleared by 46 op 2 (the decode\'s class-3 place path is off while set).')
w('inline constexpr std::size_t pp_vt_press=0x40, src_press=0x50, pp_press_fallback=0x468;')
w('inline constexpr std::uint32_t pad_press_fire=0x10, gun3c0_place_busy=0x4000000;')
w('inline constexpr std::uintptr_t placed_owner_claim_rva=0xfc9c00, placed_can_place_rva=0xfcb1f0, pp_press_word_rva=0x1205e80;')
w('// --- F1 vehicles (partner_board; docs/native-puppet.md "Vehicle boarding") --------------------------------')
w('// Site A: the ride pre-query 0x1245820 calls the nearest free seat 0x124b3a0(unit, pos, first, last); site B: phase 1')
w('// 0x1251200 calls SearchNearestRideSeatForDriver 0x124b360(unit, pos). Both return a seat or 4 (none).')
w('inline constexpr std::uintptr_t board_a_site=0x12458ac, board_a_next_ip=0x12458b1, board_a_block_rva=0x1245877;')
w('inline constexpr std::uint32_t board_a_rel32_orig=0x00005aef;  // -> 0x124b3a0')
w('inline constexpr std::uintptr_t board_b_site=0x1251479, board_b_next_ip=0x125147e, board_b_block_rva=0x125145c;')
w('inline constexpr std::uint32_t board_b_rel32_orig=0xffff9ee2;  // -> 0x124b360')
w('inline constexpr std::uintptr_t board_nearest_rva=0x124b3a0, board_driver_rva=0x124b360, board_type_rva=0x55e7f0, board_count_rva=0xa27cc0;')
w('// Unit record: u16 rider[seat] at +0x30, u16 index at +0x78 (id = 0x6400 | index). Player: (*(P+0x120))[body] the')
w('// ridden unit, (*(Q+0x18))[body] the act (0x6e: the passenger phase), (*(Q+0xe0))[body] the ridden vehicle id.')
w('inline constexpr std::size_t board_unit_riders=0x30, board_unit_index=0x78, board_p_units=0x120, board_q_vehicle=0xe0;')
w('inline constexpr std::uint16_t board_act_passenger=0x6e;')
w('// The contract blocks partner_board re-checks before the patch (code_sites rvas; the two rel32 operands masked).')
w('inline constexpr std::uintptr_t board_blocks[]={' + ','.join(f'0x{rva:x}' for rva, _, _ in BOARD_SITES) + '};')
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
text = '\n'.join(lines) + '\n'
if CHECK:
    # The committed header must be exactly what this generator writes (a
    # hand-merged or stale header fails here, not only at the code gate).
    committed = OUT.read_text(encoding='utf-8').replace('\r\n', '\n') if OUT.exists() else ''
    if committed != text:
        print(f'{OUT.relative_to(ROOT)} differs from the generator output: regenerate it (python tools/generate_partner_contract.py)')
        raise SystemExit(1)
    print(f'{len(SITES)} code sites, {len(SLOTS)} vtable slots and the native reference/containment assertions match {GAME_SHA256}; '
          f'{OUT.relative_to(ROOT)} is the generator output')
    raise SystemExit(0)
OUT.write_text(text, newline='\n')
print(f'wrote {OUT.relative_to(ROOT)}: {len(SITES)} code sites, {len(SLOTS)} vtable slots')
