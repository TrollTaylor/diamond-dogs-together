"""Aim parity contract (docs/native-puppet.md "Facing", the aim-target mirror).

Imported by tools/generate_partner_contract.py, which appends SITES and SLOTS
to its own lists, emits HEADER under "// --- Aim parity ---" in
native/partner_player_contract.hpp and adds assertions(ctx) to --check.
Disassembly only (the fingerprinted mgsvtpp.exe 1.0.15.4, read-only).

What it pins (RVAs):
- the aim-turn predicate 0x10A0AB0 (|(0.5 - r60) * pi| > 80 deg) and its two
  callers (stand aim idle 0x10B2699, crouch aim idle 0x10B0DE2);
- the per-frame aim parameter update 0x108E7D0: r5c/r60 (rate record
  +0x5C/+0x60, the record of *(PS+0x260) vt+0) from 0x1042F80 over the gun
  aim point gunRec+0x150;
- the aim point builder 0x1096F90 (gun phase 5 via 0x10407A0): offline (and
  for the local id) a ray from the viewer's camera *(P+0xB8)+0xE0 through
  Q+0x10[id]; online remote ids take Q+0x10[id] directly;
- the network aim slot Q+0x10[id] (16-byte stride) and its sample records
  *(Q+0x1F0) + id*0x30 (current, previous, timers +0x24/+0x28/+0x2C): the Q
  methods vt+0x18 0xFE0080 (push), vt+0x20 0xFE02F0 (interpolate every id
  but the argument), vt+0x28 0xFDEDD0 (snap), the fraction helper 0xFDECB0
  (duration <= 0 -> 1.0), the Q allocator 0xFDF010 (array n*16, 16-aligned;
  records n*0x30 by 0xFDE2F0, 16-aligned, initialized cur/prev 0, +0x24 -1.0);
- the stand/crouch aim turn acts (states 11/12, 26/27) and the static
  handler table built by 0x20262A0 (simulated);
- the range globals (pi/2, 75 deg) and their initializer order;
- a census of every 16-byte indexed store through an [object+0x10] array
  pointer: exactly four write Q+0x10 (the two Q methods and the two
  state-specific writers 0x12755AA, 0x1200FC2); a new one fails --check;
- the gun direction (input frame v6, partner_aim::GunDir): the class and
  stance branches of 0x108E7D0, the forward mapping and the game's own
  inverse (0x108EC55), the prone yaw (2 pi) and pitch (rate record vt+0x30
  0xFDD580), the camera-relative bit 13 and the angle convention of
  0x1042F80, the pivots (0x10499D0 kind 0, prone 0x211C640), the static
  initializers of the class ranges and a census of every rip-relative store
  to the ten range globals (exactly their initializers).
"""
import re
import struct

# (rva, expected hex, description). Long sites are split into <= 64-byte chunks below.
_SITES = [
    (0x10A0AB0, '4883ec28488b41088bca4c8b4878412b4908498b818002000048c1e105448b44010c41c1e80c41f6c0017558498b8960020000488b01ff10'
                'f30f1015d4670001f30f100d18f6a201f30f5c5060f30f590d73750001f30f1005f7f5a201f30f5915af4fbb01f30f5cc1f30f59152f930001'
                '0f5415087600010f2fd07607b0014883c428c332c04883c428c3',
     'aim-turn predicate 0x10A0AB0: |(0.5 - rec88+0x60) * [0x2C55AC4] * 2| > pi/2 - 1 deg * 10'),
    (0x10B2691, '4c8bc68bd5498bcee812e4feff8bcbbb0b000000', 'stand aim idle (state 10): predicate true -> state 11 (act 12 turn)'),
    (0x10B0DE2, 'e8c9fcfeff', 'crouch aim idle (state 25): calls the predicate'),
    (0x108E85B, '4c8bcb488bcf89742420e81647fbff', 'aim parameter update 0x108E7D0: call 0x1042F80 (relative pitch/yaw of the aim point)'),
    (0x108E985, '488b45788bd6488b8860020000488b01ff10', '0x108E7D0: rec88 = *(PS+0x260) vt+0 (id) (the rate record)'),
    (0x108EC17, 'f30f117f60f30f11475c', '0x108E7D0: stores r60 (+0x60) and r5c (+0x5C)'),
    (0x1042FBB, '450f288150010000', '0x1042F80 reads the aim point gunRec+0x150'),
    (0x1096FBE, '458bc0488b41604d03c0488b4010420f283cc08b8104020000c1e808a801740e443b8918020000740540b601eb034032f6',
     'aim point builder 0x1096F90: Q+0x10[id] read, S_IS_ONLINE && id != P+0x218 selects the remote branch'),
    (0x109705C, '488b81b8000000440f2890e0000000', '0x1096F90 offline: the ray starts at the viewer camera *(P+0xB8)+0xE0'),
    (0x1097679, '0f298350010000', '0x1096F90: stores the aim point gunRec+0x150 (offline/local ray)'),
    (0x10976E0, '0f298350010000eb070f29bb50010000', '0x1096F90: stores the aim point gunRec+0x150 (remote branch)'),
    (0x10407C8, 'f783d003000000020000741680bbd503000001740d448bc2488bd3e8286f0500eb0b448bc2488bd3e89b670500',
     'gun phase 5 0x10407A0: rec+0x3D0 bit 9 and b3d5 != 1 -> bone path 0x1097710, else 0x1096F90'),
    (0x1041057, '4883c1d0418bd0e93df7ffff', 'gun node phase 5 tail: jmp 0x10407A0 (prim = iface - 0x30)'),
    (0xFE0080, '8bc2488d144048c1e204480391f00100000f28020f294210410f28080f57c00f290a0f2f42247608410f28000f294210'
               'f30f1005dc3ac70133c0f30f11422448894228c3', 'Q vt+0x18 0xFE0080: push a sample (prev <- cur <- point; timers)'),
    (0xFDEDD0, '448bc233c04b8d14404d03c048c1e204480391f00100004889422489422c0f28020f294210488b41100f2802420f2904c0c3',
     'Q vt+0x28 0xFDEDD0: snap (timers 0, prev <- cur, Q+0x10[id] <- cur)'),
    (0xFE0349, '498b45384d8bbc24f00100004e8d343f', 'Q vt+0x20 0xFE02F0: the record of each id but the argument'),
    (0xFE0389, 'e822e9fffff3410f11462c420f28143f498b442410448b7424780fc6c000420f5c543f100f59d0420f58543f100f291428',
     'Q vt+0x20: Q+0x10[i] = prev + (cur - prev) * 0xFDECB0(record+0x20)'),
    (0xFDECB0, 'f30f1041040f57c90f2fc17709f30f100507860c01c3f30f1051080f2fd073ed',
     'interpolation fraction 0xFDECB0: duration +0x24 <= 0 or elapsed >= duration -> 1.0'),
    (0xFDF02C, '448be2', 'Q allocator 0xFDF010: r12 = n'),
    (0xFDF043, '49c1e404', 'Q allocator: r12 = n * 16 (the Q+0x10 array size)'),
    (0xFDF0E1, 'e80af2ffff48898610020000', 'Q allocator: Q+0x1F0 (obj+0x210) = the records of 0xFDE2F0(n, 0x10)'),
    (0xFDF107, '4883c00f66894b204883e0f04903c4', 'Q allocator: the Q+0x10 array is 16-aligned, n*16 bytes'),
    (0xFDF138, '48894630', 'Q allocator: stores the Q+0x10 array pointer (obj+0x30)'),
    (0xFDE3A8, '4d89591049c7411830000000', 'record allocator 0xFDE2F0: record stride 0x30'),
    (0xFDE3E0, '488d0c5248c1e104', 'record allocator: record i at i*0x30'),
    (0xFDE3F2, '0f57c00f29010f29411048894120c74124000080bf', 'record allocator: cur = prev = 0, +0x20 = 0, +0x24 = -1.0'),
    (0xFFACC5, '488b88380100008bd3488b4160488bcb488b40104803c90f283cc8', 'online remote look 0xFFAC00: Q+0x10[id] - body'),
    (0x10B27C7, '488d4860e830db13fff30f1074fb04440f28c00f28c6f3410f5cc0e8096c02fff30f11b6c40200000f287424600f2fc7720f'
                'f30f100d03d9a101f3410f58c8eb0c410f28c8f30f5c0df0d8a101440f2844244033db0f2fc7f30f118ed4020000',
     'act 12 enter: rec+0x2C4 = look, rec+0x2D4 = body yaw +- pi/2 by sign(look - body)'),
    (0x10B28A3, '488b4818b80c00000066890469', 'act 12 enter: Q+0x18[id] = 0xC'),
    (0xFDD6E2, '48c741540000803fc7415c0000003f48c741600000003f4c89416844894170', 'rate record init: +0x5C = +0x60 = 0.5'),
    (0x108F744, 'f682c0030000014c8bc90f84fd00000080bad5030000010f', 'camera aim writer 0x108F740: local gun record only (rec+0x3C0 bit 0)'),
    (0x10540A2, '3b90180200008bcd0f94c18bfa338bc003000083e101318bc0030000', 'rec+0x3C0 bit 0 = (id == P+0x218), every frame'),
    (0x2026C20, 'f30f1005dc94aa00f30f110594eec200c3', 'static init: 0x2C55AC4 = pi/2 (0x2AD0104)'),
    (0x2025B70, 'f30f100510a1aa00f30f1105fcfac200', 'static init: 0x2C5567C = pi/2 (0x2ACFC88)'),
    (0x2025A30, 'f30f100544fcc200f30f110524fcc200', 'static init: 0x2C55664 = 0x2C5567C (the yaw range)'),
    (0x2025B50, 'f30f10053ca1aa00f30f590524fe2600f30f1105e4fac200c3', 'static init: 0x2C5564C = 1 deg * 75'),
    (0x20259F0, 'f30f100554fcc200f30f110580fcc200c3', 'static init: 0x2C55680 = 0x2C5564C (the pitch range)'),
    (0x108EB11, 'f30f100d676bbc01f30f1025436bbc01', '0x108E7D0: the default ranges 0x2C55680 (pitch) and 0x2C55664 (yaw)'),
    (0x109EFF4, '488d15e56abb01488bcd4803c9488b04ca48', 'ChangeState 0x109EFA0: the handler table 0x2C55AE0 (16-byte entries)'),
    (0x109E2DE, '440fb68ba10200004585c9741a0fb683a0020000443bc8740e448bc6488bd7488bcbe89b0c0000',
     'phase 1: ChangeState when next != 0 && next != state'),
]

# Gun direction (input frame v6, docs/native-puppet.md "The gun direction"): the sender inverts its own aim
# pose r5c/r60 with the game's own mapping and adds its body yaw. Every branch the inverse relies on:
_SITES += [
    (0x108E8E0, '80bbd4030000030f8419040000', '0x108E7D0: class rec+0x3D4 == 3 takes 0x108ED30 (refused: class)'),
    (0x108E997, '80bbeb03000003488bf80f8528010000', '0x108E7D0: stance rec+0x3EB == 3 (prone) takes the rate record vt+0x20/+0x30 path'),
    (0x108EA5D, '488b07f30f104f5c488bcfff5030f30f100d51880101f30f118344030000f30f5c4f60f30f590dfc11a401f30f118b48030000',
     '0x108E7D0 prone: rec+0x344 = vt+0x30(r5c); rec+0x348 = (0.5 - r60) * 2 pi (0x2ACFC84)'),
    (0x108EACF, '0fb683d4030000440f29942480000000440f296424603c027512f30f100da36bbc01f30f1025476abc01eb263c067512'
                'f30f100d3d6abc01f30f10253d6abc01eb10f30f100d676bbc01f30f1025436bbc01',
     '0x108E7D0: the ranges by class: 2 -> 0x2C55694/0x2C55540, 6 -> 0x2C55544/0x2C5554C, else 0x2C55680/0x2C55664'),
    (0x108EB21, 'f3440f102526b30101440f28d10f28d1f3450f58d3f3410f59d4440f2fd67304440f28d60f28c2f3410f5cc20f2fc67304'
                '440f28d2f3440f100565870101f3440f5ed1f3450f59d0440f2fd67304440f28d6f30f1015518701010f28c2f3410f5cc2'
                '0f2fc67304440f28d20f28cc0f28dcf3410f58c9f3410f59dc0f2fce73030f28ce0f28c3f30f5cc10f2fc673030f28cbf3'
                '0f5ecc0f28e2f3410f59c8f30f5ce10f2fe673030f28e60f28c2f30f5cc40f2fc673030f28e2',
     '0x108E7D0: r5c target = clamp((pitch + Rp) / Rp * 0.5, 0, 1), r60 target = 1 - clamp((yaw + Ry) / Ry * 0.5, 0, 1)'),
    (0x108EC55, '0fb683d4030000f3410f5cc083f802741983f806740af30f5905d969bc01eb12f30f5905c768bc01eb08f30f59050d6abc01'
                'f3410f59c4f30f118344030000f30f104f60f3440f5cc183f802741b83f806740bf3440f5905cb69bc01eb14f3440f5905'
                '9068bc01eb09f3440f59057968bc01f3450f59c4f3440f118348030000',
     '0x108E7D0, the game\'s inverse: rec+0x344 = (r5c - 0.5) * Rp * 2, rec+0x348 = (0.5 - r60) * Ry * 2 by class'),
    (0x108ECD5, 'f783c003000000200000440f286424600f84a5fdffff3c050f859dfdffffc7475c0000003fc747600000003fe98afdffff',
     '0x108E7D0: rec+0x3C0 bit 13 with class 5 resets r5c = r60 = 0.5 (refused: camera)'),
    (0xFDD580, 'f30f10056c2b1e010f2fc87324f30f590d43d51101f30f10052f9d0c01f30f5cc1f30f5905c365c7010f570590ab0c01c3'
               'f30f5cc8f30f590dd3fd3c01f30f590d9f65c7010f28c1c3',
     'rate record vt+0x30 0xFDD580 (prone pitch): r < 2/3 -> -(1 - 1.5 r) * 0x2C53B6C, else (r - 2/3) * 3 * 0x2C53B64'),
    (0x10540CD, '488b01ff10448bc50fb64864fec1888beb030000', 'gun phase: rec+0x3EB = rate record +0x64 + 1 (1 stand, 2 crouch, 3 prone)'),
    (0x10540EB, '418b470cc1e80c0fb6c8c1e10d338bc003000081e100200000318bc0030000',
     'gun phase: rec+0x3C0 bit 13 = PS record +0xC bit 12 (camera-relative aim)'),
    (0x1042FDF, '0fbae10d0f82c30200004c8b4608498b90380100008b8204020000c1e808a80174083bba180200007550418b917c0200008b'
                'c2c1e80683e00783f801752281e20018000081fa0008000074144183b94c02000000740a4180b9d503000004751a83c0fb'
                '83f80176128bc1c1e912c1e8130ac1a8010f8553020000',
     '0x1042F80: bit 13 (or a non-body kind aiming) measures from the camera; a gun kind 1 / 5 / 6 from the body'),
    (0x1043383, '0f57c9410f28d0410f28f0410f28c0410fc6f0aaf3410f59d0410fc6c0550f28de0f5705954d0601f30f59def30f58daf3'
                '0f51cbe84e2c9c000f28ce0f28f8410f28c0e83f2c9c00f783c003000000200000440f284c2460488bbc24b0000000488b'
                'b424b8000000440f284424700f28b42490000000743e8b8b7c0200008bc125c001000083f840752c81e10018000081f900'
                '080000741e83bb4c02000000741580bbd503000004740c80bbeb0300000375030f57fff30f117d00f3410f1106',
     '0x1042F80: pitch = atan2f(-y, |xz|), yaw = atan2f(x, z) of (aim point - origin - pivot) in the supplied frame; prone adds a separate unqualified rotation'),
    (0x104324C, '80bbeb030000037428488d542440448bcf4c8bc3488bcef30f117c2428f30f11442420e85c670000440f2808e902010000'
                '440f280dbb930d01e9f5000000',
     '0x1042F80: the pivot: prone 0x211C640 (0, 0, 0.5), else the table 0x10499D0'),
    (0x1049BAD, '41ffca743241ffca0f85f5090000453890ec03000075100f280595a53601410f2903e9dc0900000f280595a53601410f29'
                '03e9cc0900000f2805a5a53601410f2903e9bc090000',
     'pivot table 0x10499D0 kind 0: stand 0x23B4190 (0.17, 0.6, 0), crouch 0x23B4160 (0.17, 0.2, 0) / 0x23B4170'),
    (0x20228D0, 'f30f1005b8a3aa00f30f5905cc630d00f30f11057c12c300c3', 'static init: 0x2C53B64 = 1 deg (0x2ACCC90) * 30 (prone down)'),
    (0x20228F0, 'f30f100598a3aa00f30f590584302700f30f11056412c300c3', 'static init: 0x2C53B6C = 1 deg (0x2ACCC90) * 75 (prone up)'),
    (0x20258E0, 'f30f1005a4a3aa00f30f1105a4fdc200c3', 'static init: 0x2C55694 = pi/4 (0x2ACFC8C) (class 2 pitch range)'),
    (0x2025900, 'f30f100584a3aa00f30f110530fcc200c3', 'static init: 0x2C55540 = pi/4 (0x2ACFC8C) (class 2 yaw range)'),
    (0x2026060, 'f30f10052c9caa00f30f590548c70c00f30f1105ccf4c200c3', 'static init: 0x2C55544 = 1 deg (0x2ACFC94) * 40 (class 6 pitch)'),
    (0x2026080, 'f30f10050c9caa00f30f5905f4f82600f30f1105b4f4c200c3', 'static init: 0x2C5554C = 1 deg (0x2ACFC94) * 75 (class 6 yaw)'),
]


def _chunks():
    out = []
    for rva, hexstr, what in _SITES:
        n = len(hexstr) // 2
        if n <= 64:
            out.append((rva, hexstr, what))
            continue
        parts = (n + 63) // 64
        for k in range(parts):
            lo = k * 64
            out.append((rva + lo, hexstr[2 * lo:2 * min(n, lo + 64)], f'{what} ({k + 1}/{parts})'))
    return out


SITES = _chunks()

# (vtable rva, slot offset, expected target rva). Q vt+0x18 is pinned by the generator itself.
SLOTS = [
    (0x23AD970, 0x20, 0xFE02F0, 'Q vtable +0x20 = aim interpolate (every id but the argument; never called by us)'),
    (0x23AD970, 0x28, 0xFDEDD0, 'Q vtable +0x28 = aim snap (never called by us)'),
    (0x23AD2B0, 0x20, 0xFDDF20, 'rate record vtable +0x20 = the prone aim setter (r60 = 0.5 * (1 - yaw / pi); never called by us)'),
    (0x23AD2B0, 0x30, 0xFDD580, 'rate record vtable +0x30 = the prone pitch of r5c (inverted by partner_aim::GunDir; never called by us)'),
]

HEADER = [
    '// --- Aim parity (docs/native-puppet.md "Facing"; tools/partner_aim_sites.py) ------------',
    '// The network aim slot Q+0x10[id] (16-byte stride; the gun aim point builder 0x1096F90 reads it every frame) and its',
    '// sample records *(Q+0x1F0) + id*0x30 (current, previous at +0x10, timers +0x24/+0x28/+0x2C), written only by the Q',
    '// synchronizer methods vt+0x18 0xfe0080 / vt+0x20 0xfe02f0 / vt+0x28 0xfdedd0 (and two state-specific writers).',
    'inline constexpr std::size_t q_aim=0x10, q_aim_stride=0x10, q_aim_rec=0x1f0, q_aim_rec_stride=0x30, q_aim_rec_prev=0x10;',
    'inline constexpr std::size_t q_aim_rec_timers=0x24, q_aim_rec_timers_end=0x30;',
    'inline constexpr std::size_t q_vt_aim_push=0x18, q_vt_aim_lerp=0x20, q_vt_aim_snap=0x28;',
    'inline constexpr std::uintptr_t q_aim_push_rva=0xfe0080, q_aim_lerp_rva=0xfe02f0, q_aim_snap_rva=0xfdedd0;',
    '// Gun record: the aim point +0x150 (16 bytes), the path flags +0x3d0 (bit 9 with b3d5 != 1: the bone path 0x1097710,',
    '// which ignores Q+0x10). Rate record (*(PS+0x260) vt+0): the aim pitch/yaw parameters r5c +0x5c and r60 +0x60.',
    'inline constexpr std::size_t gun_aim_point=0x150, gun_path_flags=0x3d0, rate_aim_pitch=0x5c, rate_aim_yaw=0x60;',
    'inline constexpr std::uint32_t gun3d0_bone_path=0x200;',
    'inline constexpr std::uint8_t gun_b3d5_sight=1;',
    '// Gun direction (input frame v6; partner_aim::GunDir inverts the aim pose like 0x108ec55): the class +0x3d4 (2: 45/45 deg,',
    '// 6: 40/75 deg, else 75/90 deg pitch/yaw ranges; 3: another path, refused), the stance +0x3eb (rate record +0x64 + 1:',
    '// 1 stand, 2 crouch, 3 prone: yaw (0.5 - r60) * 2 pi, pitch rate record vt+0x30 0xfdd580: 75 deg up, 30 deg down),',
    '// Prone world conversion is refused: the extra orientation in 0x1043c80 is not a qualified read.',
    '// +0x3c0 bit 13 (camera-relative angles, refused), b3d5 1 (the local sight writer 0x108f740, refused).',
    'inline constexpr std::size_t gun_class=0x3d4, gun_stance=0x3eb;',
    'inline constexpr std::uint32_t gun3c0_camera_rel=0x2000;',
    'inline constexpr std::uint8_t gun_class_other_path=3, gun_class_narrow=2, gun_class_wide=6, gun_stance_prone=3;',
    'inline constexpr float aim_range_deg[3][2]={{75.0f,90.0f},{45.0f,45.0f},{40.0f,75.0f}};  // {pitch, yaw}: default, class 2, class 6',
    'inline constexpr float prone_up_deg=75.0f, prone_down_deg=30.0f, prone_split=0.6666666865348816f, prone_slope=1.5f, prone_gain=3.000000238418579f;',
    '// The pivot heights of 0x10499d0 kind 0 (stand 0.6 m, crouch 0.2 m; 0.17 m to the side) and prone 0x211c640 (0.5 m ahead).',
    'inline constexpr float pivot_stand_m=0.6f, pivot_crouch_m=0.2f, pivot_side_m=0.17f, pivot_prone_ahead_m=0.5f;',
]

# The handler table entries the aim acts use (state -> handler), from the static initializer 0x20262A0.
HANDLERS = {10: 0x10B1F30, 11: 0x10B2720, 12: 0x10B2720, 25: 0x10B0660, 26: 0x10B0E60, 27: 0x10B0E60}
HANDLER_TABLE = 0x2C55AE0

# Every 16-byte indexed store through an array pointer loaded from [object+0x10]
# within 12 instructions (SIB addressing; .xtext/.text, pdata functions and leaves),
# with the store and the provenance of that object register. Exactly the 'q'
# entries write Q+0x10 (the object comes from [X+0x60] = Q, or is the this of a Q
# vtable method); the rest belong to other classes.
Q10_CENSUS = {
    0x38630A: 'other', 0x38630F: 'other', 0x386414: 'other', 0x386419: 'other', 0x38663E: 'other', 0x386642: 'other',
    0x3866FA: 'other', 0x3866FF: 'other', 0x7A024C: 'other', 0xB7CF48: 'other', 0xB7CF4C: 'other', 0xB7CF51: 'other',
    0xCEE028: 'other', 0xD82EEF: 'other', 0xD82EF8: 'other', 0xD83157: 'other', 0xD8315F: 'other', 0xDC4F82: 'other',
    0xFC0DCC: 'other', 0xFDEDFC: 'q', 0xFE03B6: 'q', 0x1200FC2: 'q', 0x12755AA: 'q', 0x137A4DA: 'other', 0x137A4E5: 'other',
    0x18C3D36: 'other', 0x1B6AE63: 'other', 0x1B6AE8B: 'other',
}


def _norm(r):
    t = {'eax': 'rax', 'ebx': 'rbx', 'ecx': 'rcx', 'edx': 'rdx', 'esi': 'rsi', 'edi': 'rdi', 'ebp': 'rbp', 'esp': 'rsp'}
    if r in t:
        return t[r]
    m = re.fullmatch(r'(r\d+)[dwb]?', r)
    return m.group(1) if m else r


def q10_census(ctx):
    """{store rva: 'q' | 'other'} for every 16-byte SIB store whose base or index
    register was loaded by `mov R, qword ptr [S + 0x10]` within the previous 12
    instructions; 'q' when S was loaded from [X + 0x60] (P+0x60 = Q) or is the
    this of a Q vtable method."""
    import bisect
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    funcs = sorted(ctx.runtime_function_entries())
    starts = [f[0] for f in funcs]

    def func_of(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i >= 0 and funcs[i][0] <= rva < funcs[i][1]:
            return funcs[i]
        return None

    def primary(f):
        s, e, u = f
        for _ in range(20):
            hdr = ctx.rd(u, 4)
            if (hdr[0] >> 3) & 4:  # UNW_FLAG_CHAININFO
                s, e, u = struct.unpack('<III', ctx.rd(u + 4 + ((hdr[2] + 1) & ~1) * 2, 12))
            else:
                break
        return s

    def decode(lo, hi):
        return [(a - ctx.IB, sz, m, o) for a, sz, m, o in md.disasm_lite(ctx.rd(lo, hi - lo), ctx.IB + lo)]
    qm = set()
    for k in range(64):
        v = struct.unpack('<Q', ctx.rd(0x23AD970 + 8 * k, 8))[0] - ctx.IB
        if not 0x1000 <= v < 0x208D000:
            break
        qm.add(v)
    store = re.compile(rb'(?:[\x40-\x4f])?\x0f[\x29\x11][\x04\x0c\x14\x1c\x24\x2c\x34\x3c\x44\x4c\x54\x5c\x64\x6c\x74\x7c'
                       rb'\x84\x8c\x94\x9c\xa4\xac\xb4\xbc]', re.S)
    load10 = re.compile(rb'[\x48-\x4f]\x8b[\x40-\x7f]\x24?\x10', re.S)
    memre = re.compile(r'xmmword ptr \[(\w+)(?: \+ (\w+)(?:\*\d)?)?(?: [+-] 0x[0-9a-f]+)?\], xmm\d+$')
    movq = re.compile(r'^(\w+), qword ptr \[(\w+) \+ 0x10\]$')
    cache = {}
    out = {}
    for s in ctx.pe.sections:
        if s.Name.rstrip(b'\0').decode() not in ('.xtext', '.text'):
            continue
        data, va = s.get_data(), s.VirtualAddress
        for m in store.finditer(data):
            i = m.start()
            if not load10.search(data, max(0, i - 96), i):
                continue
            c = va + i
            f = func_of(c)
            if f:
                if f[0] not in cache:
                    cache[f[0]] = decode(f[0], f[1])
                ins = cache[f[0]]
                ps = primary(f)
            else:
                ins, ps = None, None
                for back in range(96, 0, -1):
                    cand = decode(c - back, c + 16)
                    if any(x[0] == c for x in cand) and all(x[2] not in ('ret', 'int3') or x[0] >= c for x in cand):
                        ins = cand
                        break
                if ins is None:
                    continue
            idx = next((k for k, x in enumerate(ins) if x[0] == c), None)
            if idx is None or ins[idx][2] not in ('movaps', 'movups', 'movdqa', 'movdqu'):
                continue
            mm = memre.match(ins[idx][3])
            if not mm:
                continue
            regs = {_norm(mm.group(1))} | ({_norm(mm.group(2))} if mm.group(2) else set())
            src = None
            for k in range(idx - 1, max(-1, idx - 13), -1):
                mv = movq.match(ins[k][3])
                if ins[k][2] == 'mov' and mv and _norm(mv.group(1)) in regs:
                    src = (ins[k][0], _norm(mv.group(2)))
                    break
            if not src:
                continue
            # The provenance of the object register S: its last definition before the load
            # (decoded linearly from the primary function's start), or a Q method's this.
            kind = 'other'
            owner = ps
            if owner is None:
                owner = max((q for q in qm if q <= c < q + 0x80), default=None)
                if owner in qm and src[1] == 'rcx':
                    kind = 'q'
            else:
                full = decode(owner, f[1]) if owner != f[0] else ins
                j = next((k for k, x in enumerate(full) if x[0] == src[0]), None)
                last = None
                for k in range((j or 0) - 1, -1, -1):
                    ops = full[k][3].split(', ')
                    if ops and _norm(ops[0]) == src[1] and full[k][2] in ('mov', 'lea', 'pop', 'xchg', 'movzx'):
                        last = full[k]
                        break
                if last and last[2] == 'mov' and re.fullmatch(r'\w+, qword ptr \[\w+ \+ 0x60\]', last[3]):
                    kind = 'q'
                elif owner in qm and (last is None and src[1] == 'rcx' or last and last[3].endswith(', rcx')):
                    kind = 'q'
            out[c] = kind
    return out


def _f32(ctx, rva):
    return struct.unpack('<f', ctx.rd(rva, 4))[0]


# The aim range globals the inverse uses (runtime-initialized .data2) and their only writers (the static initializers).
RANGE_GLOBALS = {
    0x2C53B64: 0x20228E0, 0x2C53B6C: 0x2022900, 0x2C55694: 0x20258E8, 0x2C55540: 0x2025908, 0x2C55544: 0x2026070,
    0x2C5554C: 0x2026090, 0x2C5564C: 0x2025B60, 0x2C5567C: 0x2025B78, 0x2C55680: 0x20259F8, 0x2C55664: 0x2025A38,
}


def rip_store_census(ctx, targets):
    """{store rva: target} for every instruction in .xtext/.text whose first (written) operand is a rip-relative
    memory reference overlapping one of the 4-byte `targets` (SSE stores, mov r/m stores, mov imm stores)."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    rip = b'[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]'
    pats = [re.compile(rb'[\xf3\xf2\x66]?(?:[\x40-\x4f])?\x0f[\x11\x29\x7f\xd6\x7e\x13\x17]' + rip, re.S),
            re.compile(rb'(?:[\x40-\x4f])?[\x88\x89]' + rip, re.S),
            re.compile(rb'(?:\x66)?(?:[\x40-\x4f])?[\xc6\xc7]\x05', re.S)]
    memre = re.compile(r'(?:(\w+) ptr )?\[rip ([+-]) (0x[0-9a-f]+)\]')
    widths = {'byte': 1, 'word': 2, 'dword': 4, 'qword': 8, 'xmmword': 16}
    out = {}
    for s in ctx.pe.sections:
        if s.Name.rstrip(b'\0').decode() not in ('.xtext', '.text'):
            continue
        data, va = s.get_data(), s.VirtualAddress
        for p in pats:
            for m in p.finditer(data):
                at = m.start()
                ins = next(md.disasm_lite(data[at:at + 16], ctx.IB + va + at), None)
                if not ins:
                    continue
                mm = memre.match(ins[3])
                if not mm:
                    continue
                t = va + at + ins[1] + (int(mm.group(3), 16) if mm.group(2) == '+' else -int(mm.group(3), 16))
                width = widths.get(mm.group(1) or 'xmmword', 16)
                for g in targets:
                    if t < g + 4 and g < t + width:
                        out[va + at] = g
    return out


def handler_table(ctx):
    """Simulates the static initializer 0x20262A0: each entry stored to the
    table is a 16-byte member pointer built on the stack from a lea of the
    handler (rbp-0x10) and loaded with movups before the movaps store."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    funcs = ctx.runtime_function_entries()
    f = next((x for x in funcs if x[0] == 0x20262A0), None)
    if not f:
        return {}
    regs, mem, xmm, table = {}, {}, None, {}
    for a, sz, m, o in md.disasm_lite(ctx.rd(f[0], f[1] - f[0]), ctx.IB + f[0]):
        nxt = a - ctx.IB + sz
        if m == 'lea':
            mm = re.match(r'(\w+), \[rip ([+-]) (0x[0-9a-f]+)\]', o)
            if mm:
                regs[mm.group(1)[-2:]] = nxt + (int(mm.group(3), 16) if mm.group(2) == '+' else -int(mm.group(3), 16))
        elif m == 'mov':
            mm = re.match(r'qword ptr \[rbp - 0x10\], (\w+)', o)
            if mm:
                mem['f'] = regs.get(mm.group(1)[-2:])
        elif m == 'movups' and 'rbp - 0x10' in o:
            xmm = mem.get('f')
        elif m == 'movaps':
            mm = re.match(r'xmmword ptr \[rip ([+-]) (0x[0-9a-f]+)\], xmm0', o)
            if mm:
                table[nxt + (int(mm.group(2), 16) if mm.group(1) == '+' else -int(mm.group(2), 16))] = xmm
    return table


def assertions(ctx):
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    import math
    # Constants (read-only image data) and the predicate's operands.
    for rva, value, what in ((0x20A72C4, 0.5, '0.5'), (0x20A72CC, 1.0, '1.0 (fraction helper)'), (0x20A8078, 10.0, '10'),
                             (0x20A9E50, 2.0, '2'), (0x2295984, 75.0, '75'), (0x2AD04BC, 0.1, '0.1 (aim-walk stick)'),
                             (0x2AD0104, math.pi / 2, 'pi/2'), (0x2ACFC88, math.pi / 2, 'pi/2'), (0x2AD0110, math.pi / 180, '1 deg'),
                             (0x2ACFC94, math.pi / 180, '1 deg'),
                             (0x2AD0100, 2 * math.pi, '2 pi')):
        need(abs(_f32(ctx, rva) - value) < 1e-6 and ctx.section_of(rva) in ('.edata', '.data2'), f'{rva:#x} = {what} (image data)')
    for site, length, target in ((0x10A0AE8, 8, 0x20A72C4), (0x10A0AF0, 8, 0x2AD0110), (0x10A0AFD, 8, 0x20A8078),
                                 (0x10A0B05, 8, 0x2AD0104), (0x10A0B0D, 8, 0x2C55AC4), (0x10A0B19, 8, 0x20A9E50),
                                 (0x2026C20, 8, 0x2AD0104), (0x2026C28, 8, 0x2C55AC4), (0x2025B70, 8, 0x2ACFC88),
                                 (0x2025B78, 8, 0x2C5567C), (0x2025A30, 8, 0x2C5567C), (0x2025A38, 8, 0x2C55664),
                                 (0x2025B50, 8, 0x2ACFC94), (0x2025B58, 8, 0x2295984), (0x2025B60, 8, 0x2C5564C),
                                 (0x20259F0, 8, 0x2C5564C), (0x20259F8, 8, 0x2C55680), (0x108EB11, 8, 0x2C55680),
                                 (0x108EB19, 8, 0x2C55664), (0xFDECBD, 8, 0x20A72CC), (0x109EFF4, 7, 0x2C55AE0)):
        need(ctx.rip_target(site, length) == target, f'{site:#x} references {target:#x}')
    # Initializer order: the pi/2 source before its copy, 75 deg before its copy.
    tab = {struct.unpack('<Q', ctx.rd(a, 8))[0] - ctx.IB: a for a in (0x2097848, 0x2097850, 0x20978C8, 0x20978D0)}
    need(tab.get(0x2025B70, 0) == 0x2097850 and tab.get(0x2025A30, 0) == 0x20978D0 and 0x2097850 < 0x20978D0,
         'initializer table: 0x2025B70 (0x2097850) runs before 0x2025A30 (0x20978D0)')
    need(tab.get(0x2025B50, 0) == 0x2097848 and tab.get(0x20259F0, 0) == 0x20978C8 and 0x2097848 < 0x20978C8,
         'initializer table: 0x2025B50 (0x2097848) runs before 0x20259F0 (0x20978C8)')
    for g in (0x2C55AC4, 0x2C55664, 0x2C55680, 0x2C5567C, 0x2C5564C):
        need(ctx.section_of(g) == '.data2', f'{g:#x} is writable image data')
    # Callers and tables.
    need(ctx.rel_refs(0x10A0AB0) == [0x10B0DE2, 0x10B2699] and ctx.qword_hits(0x10A0AB0) == [],
         f'the aim-turn predicate has exactly the two aim idle callers: {[hex(x) for x in ctx.rel_refs(0x10A0AB0)]}')
    need(ctx.rel_refs(0x109EFA0) == [0x109E300], f'ChangeState has one caller: {[hex(x) for x in ctx.rel_refs(0x109EFA0)]}')
    need(ctx.rel_refs(0x1096F90) == [0x10407F0] and ctx.rel_refs(0x1097710) == [0x10407E3],
         f'the aim point builders are called only by gun phase 5: {[hex(x) for x in ctx.rel_refs(0x1096F90)]} '
         f'{[hex(x) for x in ctx.rel_refs(0x1097710)]}')
    need(0x108E865 in ctx.rel_refs(0x1042F80), '0x108E7D0 calls 0x1042F80')
    # The synchronizer methods are reached only through the live Q vtable 0x23AD970 (stored once, by 0xFDEDB4)
    # and its sibling 0x23AD8A0 (same slots +0..+0x28; never the live Q: the hook requires 0x23AD970).
    for fn, slots in ((0xFE0080, [0x23AD8B8, 0x23AD988]), (0xFE02F0, [0x23AD8C0, 0x23AD990]), (0xFDEDD0, [0x23AD8C8, 0x23AD998])):
        need(ctx.rel_refs(fn) == [] and ctx.qword_hits(fn) == slots,
             f'{fn:#x} is reached only through the Q vtables: {[hex(x) for x in ctx.qword_hits(fn)]}')
    need(ctx.lea_rip_refs(0x23AD970) == [0xFDEDB4], f'the live Q vtable is stored once: {[hex(x) for x in ctx.lea_rip_refs(0x23AD970)]}')
    need(0xFE0389 in ctx.rel_refs(0xFDECB0), 'the interpolation calls the fraction helper 0xFDECB0')
    need(0xFDF0E1 in ctx.rel_refs(0xFDE2F0), 'the Q allocator builds the aim records with 0xFDE2F0')
    # The static handler table: the aim idle and turn states.
    table = handler_table(ctx)
    for state, handler in HANDLERS.items():
        need(table.get(HANDLER_TABLE + 16 * state) == handler,
             f'handler table state {state}: {hex(table.get(HANDLER_TABLE + 16 * state) or 0)} (expected {handler:#x})')
    # The Q+0x10 store census.
    census = q10_census(ctx)
    need(census == Q10_CENSUS, 'Q+0x10 store census changed: ' +
         str(sorted((hex(k), v) for k, v in census.items() if Q10_CENSUS.get(k) != v)) +
         ' missing ' + str(sorted(hex(k) for k in Q10_CENSUS if k not in census)))
    out += gun_dir_assertions(ctx)
    return out


def gun_dir_assertions(ctx):
    """Input frame v6, the gun direction (docs/native-puppet.md "The gun direction"): the constants, references,
    initializers and the only writers of the ranges the sender's inverse (partner_aim::GunDir) relies on."""
    out = []

    def need(cond, what):
        if not cond:
            out.append(what)
    import math
    f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]  # noqa: E731
    for rva, value, what in ((0x2ACFC84, 2 * math.pi, '2 pi (prone yaw)'), (0x21C00F4, f32(2.0 / 3.0), '2/3'), (0x20FAAD8, 1.5, '1.5'),
                             (0x23AD390, 3.000000238418579, '3.0000002'), (0x2ACCC90, math.pi / 180, '1 deg'), (0x20F8CAC, 30.0, '30'),
                             (0x20F27B8, 40.0, '40'), (0x2ACFC8C, math.pi / 4, 'pi/4')):
        need(abs(_f32(ctx, rva) - value) < 1e-6 and ctx.section_of(rva) in ('.edata', '.data2'), f'{rva:#x} = {what} (image data)')
    for rva, vec, what in ((0x23B4190, (0.17, 0.6, 0.0, 0.0), 'stand pivot'), (0x23B4160, (0.17, 0.2, 0.0, 0.0), 'crouch pivot'),
                           (0x23B4170, (0.17, 0.4, 0.0, 0.0), 'crouch pivot (+0x3ec)'), (0x211C640, (0.0, 0.0, 0.5, 0.0), 'prone pivot')):
        got = struct.unpack('<4f', ctx.rd(rva, 16))
        need(all(abs(a - b) < 1e-6 for a, b in zip(got, vec)) and ctx.section_of(rva) == '.edata', f'{rva:#x} = {what} {vec}')
    for site, length, target in ((0x108EA6B, 8, 0x20A72C4), (0x108EA80, 8, 0x2ACFC84), (0x108EAE9, 8, 0x2C55694),
                                 (0x108EAF1, 8, 0x2C55540), (0x108EAFF, 8, 0x2C55544), (0x108EB07, 8, 0x2C5554C),
                                 (0x108EB21, 9, 0x20A9E50), (0x108EB56, 9, 0x20A72C4), (0x108EB73, 8, 0x20A72CC),
                                 (0x108EC6B, 8, 0x2C5564C), (0x108EC75, 8, 0x2C55544), (0x108EC7F, 8, 0x2C55694),
                                 (0x108ECA8, 9, 0x2C5567C), (0x108ECB3, 9, 0x2C5554C), (0x108ECBE, 9, 0x2C55540),
                                 (0xFDD580, 8, 0x21C00F4), (0xFDD58D, 8, 0x20FAAD8), (0xFDD595, 8, 0x20A72CC), (0xFDD5A1, 8, 0x2C53B6C),
                                 (0xFDD5A9, 7, 0x20A8140), (0xFDD5B5, 8, 0x23AD390), (0xFDD5BD, 8, 0x2C53B64), (0x10433A4, 7, 0x20A8140),
                                 (0x104327D, 8, 0x211C640), (0x1049BC4, 7, 0x23B4160), (0x1049BD4, 7, 0x23B4170), (0x1049BE4, 7, 0x23B4190),
                                 (0x20228D0, 8, 0x2ACCC90), (0x20228D8, 8, 0x20F8CAC), (0x20228E0, 8, 0x2C53B64),
                                 (0x20228F0, 8, 0x2ACCC90), (0x20228F8, 8, 0x2295984), (0x2022900, 8, 0x2C53B6C),
                                 (0x20258E0, 8, 0x2ACFC8C), (0x20258E8, 8, 0x2C55694), (0x2025900, 8, 0x2ACFC8C), (0x2025908, 8, 0x2C55540),
                                 (0x2026060, 8, 0x2ACFC94), (0x2026068, 8, 0x20F27B8), (0x2026070, 8, 0x2C55544),
                                 (0x2026080, 8, 0x2ACFC94), (0x2026088, 8, 0x2295984), (0x2026090, 8, 0x2C5554C)):
        need(ctx.rip_target(site, length) == target, f'{site:#x} references {target:#x}')
    # The atan2f pair and the pivot table call (0x1A0600A is the atan2f IAT stub, pinned with the PP update).
    need(0x10433B7 + 5 + struct.unpack('<i', ctx.rd(0x10433B8, 4))[0] == 0x1A0600A and
         0x10433C6 + 5 + struct.unpack('<i', ctx.rd(0x10433C7, 4))[0] == 0x1A0600A, '0x1042F80 calls atan2f twice (0x1A0600A)')
    need(ctx.rel_refs(0x10499D0) == [0x104326F], f'the pivot table 0x10499D0 has one caller: {[hex(x) for x in ctx.rel_refs(0x10499D0)]}')
    need(0x108E865 in ctx.rel_refs(0x1042F80), '0x108E7D0 computes the relative angles with 0x1042F80')
    # The prone methods are reached only through the rate record vtable.
    for fn, slot in ((0xFDD580, 0x23AD2E0), (0xFDDF20, 0x23AD2D0)):
        need(ctx.rel_refs(fn) == [] and ctx.qword_hits(fn) == [slot], f'{fn:#x} is reached only through 0x23AD2B0: {[hex(x) for x in ctx.qword_hits(fn)]}')
    # The range globals: writable image data, written only by their static initializers (each in the initializer
    # table once), never address-taken.
    census = rip_store_census(ctx, RANGE_GLOBALS)
    need(census == {store: g for g, store in RANGE_GLOBALS.items()},
         'aim range global writers changed: ' + str(sorted((hex(k), hex(v)) for k, v in census.items())))
    for g in RANGE_GLOBALS:
        need(ctx.section_of(g) == '.data2' and ctx.lea_rip_refs(g) == [], f'{g:#x}: .data2, no lea of it')
    for init, entry in ((0x20228D0, 0x2097228), (0x20228F0, 0x2097220), (0x20258E0, 0x20978D8), (0x2025900, 0x20978E0),
                        (0x2026060, 0x20978E8), (0x2026080, 0x20978F0)):
        need(ctx.qword_hits(init) == [entry] and ctx.rel_refs(init) == [], f'initializer {init:#x} runs once from the table entry {entry:#x}')
    return out
