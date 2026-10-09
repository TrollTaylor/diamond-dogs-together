#!/usr/bin/env python3
"""Generate native/steam_shim_contract.hpp from the exact installed binaries.

Read-only static inspection of mgsvtpp.exe 1.0.15.4 and the shipped
steam_api64.dll. Every byte range the co-op loader gates on (DllMain-early
patches, loopback networking emulator, synthetic callback dispatch, and the
Steam online path of docs/steam-online.md: the SteamAPI_Init wrapper and the
flat User/Friends/Client exports the owner context calls) is copied verbatim
from the files after their SHA-256 fingerprints are verified, and the decoded
instruction meaning is asserted with Capstone so a wrong RVA fails here rather
than in the game. The executable census asserts that the game never registers
the callbacks we add (337, 1251, 1252, 1221, 1281), that SteamAPI_Init and
SteamAPI_RunCallbacks each have exactly one call site, the 13 Steam imports,
the game's ISteamFriends slot use and the absence of any Steam networking
sockets/messages/utils interface string.

--steamclient PATH is a structural mode for Valve's steamclient64.dll (never
pinned: Valve updates it). By RTTI names, name strings and instruction shapes,
never by RVA, it asserts what the loader's runtime gates (docs/steam-online.md
section 5) and the transport rely on, prints the file's SHA-256 and writes
nothing. The coordinator runs it before any hidden or live Steam run. It also
asserts the function-pointer dispatch of docs/steam-online.md section 20:
Sockets012 slot 41 RunCallbacks (lock, swap, unlock, then call [entry+8] with
rcx = the payload for exactly 1221/1222/1223/1251/1252/1281), Utils004 slot 16
SetConfigValue (Ptr: *(void**)pArg, null resets to the default), the 201/204/
205 config entries, the single readers of the 204/205 values (queue 1251/1252
through InternalQueueCallback on the Messages object's own sockets object),
InternalQueueCallback (queues only a non-null function, then always posts to
the pipe), the 1221 payload and the connection-info end reason/debug offsets.

Usage:
  python tools/generate_steam_shim_contract.py [--game DIR] [--output PATH] [--check]
  python tools/generate_steam_shim_contract.py --steamclient PATH
  --check  verify only; exit 1 on any mismatch, including a committed header
           that differs from the generator output; nothing is written.
"""
import argparse
import hashlib
import pathlib
import re
import struct
import sys

import capstone

EXE_SHA256 = "085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45"
STEAM_SHA256 = "08bd0152356e5619ee56a6714ac951b6800dbb21821693a4efaf85278442c60a"
IMAGE_BASE = 0x140000000


class Image:
    def __init__(self, path=None, data=None):
        self.data = data if data is not None else path.read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        self.pe = pe
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional = struct.unpack_from("<H", self.data, pe + 20)[0]
        table = pe + 24 + optional
        self.sections = []
        self.section_info = []
        for i in range(count):
            off = table + 40 * i
            name = self.data[off:off + 8].rstrip(b"\0")
            vsize, va, rsize, raw = struct.unpack_from("<IIII", self.data, off + 8)
            chars = struct.unpack_from("<I", self.data, off + 36)[0]
            self.sections.append((va, max(vsize, rsize), raw, rsize))
            self.section_info.append((name, va, max(vsize, rsize), raw, rsize, chars))
        self.timestamp = struct.unpack_from("<I", self.data, pe + 8)[0]
        self.size_of_image = struct.unpack_from("<I", self.data, pe + 24 + 56)[0]
        self.image_base = struct.unpack_from("<Q", self.data, pe + 24 + 24)[0]
        self.dirs = pe + 24 + 112  # PE32+ data directories

    def read(self, rva, size):
        for va, span, raw, rsize in self.sections:
            if va <= rva and rva + size <= va + span:
                start = raw + rva - va
                chunk = self.data[start:start + size]
                if len(chunk) != size or rva + size > va + rsize:
                    raise ValueError(f"rva {rva:#x} not backed by file data")
                return chunk
        raise ValueError(f"rva {rva:#x} outside sections")

    def rva_of_offset(self, off):
        for va, span, raw, rsize in self.sections:
            if raw <= off < raw + rsize:
                return va + off - raw
        return None

    def section_of(self, rva):
        for name, va, span, raw, rsize, chars in self.section_info:
            if va <= rva < va + span:
                return name, chars
        return None, 0

    def is_code(self, rva):
        _, chars = self.section_of(rva)
        return bool(chars & 0x20000000)

    def is_rdata(self, rva):
        _, chars = self.section_of(rva)
        return bool(chars & 0x40000000) and not (chars & 0x80000000) and not (chars & 0x20000000)

    def cstring(self, rva, limit=256):
        try:
            data = self.read(rva, limit)
        except ValueError:
            data = b""
            for n in range(limit, 0, -1):
                try:
                    data = self.read(rva, n)
                    break
                except ValueError:
                    continue
        return data.split(b"\0")[0]

    def exports(self):
        rva, size = struct.unpack_from("<II", self.data, self.dirs + 0 * 8)
        if not rva:
            return {}
        d = self.read(rva, 40)
        count_funcs, count_names, funcs, names, ords = struct.unpack_from("<IIIII", d, 20)
        out = {}
        for i in range(count_names):
            name_rva = struct.unpack("<I", self.read(names + 4 * i, 4))[0]
            ordinal = struct.unpack("<H", self.read(ords + 2 * i, 2))[0]
            out[self.cstring(name_rva).decode()] = struct.unpack("<I", self.read(funcs + 4 * ordinal, 4))[0]
        return out

    def imports(self, dll):
        rva, size = struct.unpack_from("<II", self.data, self.dirs + 1 * 8)
        out = {}
        at = rva
        while True:
            entry = self.read(at, 20)
            original, _, _, name_rva, first = struct.unpack("<IIIII", entry)
            if not name_rva and not first:
                break
            if self.cstring(name_rva).decode().lower() == dll.lower():
                k = 0
                while True:
                    thunk = struct.unpack("<Q", self.read((original or first) + 8 * k, 8))[0]
                    if not thunk:
                        break
                    if thunk >> 63:
                        name = f"#{thunk & 0xFFFF}"
                    else:
                        name = self.cstring((thunk & 0x7FFFFFFF) + 2).decode()
                    out[first + 8 * k] = name
                    k += 1
            at += 20
        return out


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True


def disasm(image, rva, size):
    return [(i.address, i.mnemonic, i.op_str) for i in MD.disasm(image.read(rva, size), rva)]


def expect(condition, message):
    if not condition:
        raise SystemExit(f"contract check failed: {message}")


def rip_target(ins_rva, ins_len, disp):
    return ins_rva + ins_len + disp


def check_exe(exe):
    # Mutex block: lea r8 title; call [CreateMutexW]; call [GetLastError]; cmp eax,0B7h; je exit.
    lea = exe.read(0x13D6, 7)
    expect(lea[:3] == b"\x4c\x8d\x05", "lea r8 at 0x13D6")
    expect(rip_target(0x13D6, 7, struct.unpack("<i", lea[3:])[0]) == 0x20A0980, "lea r8 -> 0x20A0980")
    call = exe.read(0x13F9, 6)
    expect(call[:2] == b"\xff\x15" and rip_target(0x13F9, 6, struct.unpack("<i", call[2:])[0]) == 0x208D170,
           "call [CreateMutexW IAT 0x208D170] at 0x13F9")
    expect(exe.read(0x1415, 5) == b"\x3d\xb7\x00\x00\x00", "cmp eax,0B7h at 0x1415")
    je = exe.read(0x141A, 6)
    expect(je[:2] == b"\x0f\x84" and rip_target(0x141A, 6, struct.unpack("<i", je[2:])[0]) == 0x1595, "je 0x1595")
    title = exe.read(0x20A0980, 74)
    expect(title == "METAL GEAR SOLID V: THE PHANTOM PAIN\0".encode("utf-16-le"), "title string")
    # Early identity: SteamAPI_Init, SteamUser getter, GetSteamID slot 2 with hidden return pointer.
    init = exe.read(0x1428, 6)
    expect(rip_target(0x1428, 6, struct.unpack("<i", init[2:])[0]) == 0x208E130, "SteamAPI_Init IAT")
    expect(exe.read(0x142E, 4) == bytes.fromhex("84c0750c"), "test al,al; jne after SteamAPI_Init (bool in AL)")
    user = exe.read(0x143E, 6)
    expect(rip_target(0x143E, 6, struct.unpack("<i", user[2:])[0]) == 0x208E128, "SteamUser IAT")
    expect(exe.read(0x1444, 18) == bytes.fromhex("488d9424a00000004c8b00488bc841ff5010"),
           "lea rdx,[rsp+0A0h]; mov r8,[rax]; mov rcx,rax; call [r8+10h]")
    # XInputGetState import thunk.
    thunk = exe.read(0x32892C, 6)
    expect(thunk[:2] == b"\xff\x25" and rip_target(0x32892C, 6, struct.unpack("<i", thunk[2:])[0]) == 0x208E070,
           "jmp [XInputGetState IAT]")
    # CCallback vtables: [Run(p,bIOFailure,h), Run(p), GetCallbackSizeBytes] (MSVC reverse overload order).
    v1202 = [struct.unpack("<Q", exe.read(0x24BF400 + 8 * i, 8))[0] - IMAGE_BASE for i in range(3)]
    v1203 = [struct.unpack("<Q", exe.read(0x24BF418 + 8 * i, 8))[0] - IMAGE_BASE for i in range(3)]
    expect(v1202 == [0x1A5FB50, 0x1A5FB50, 0x1A5F830], f"1202 vtable {v1202}")
    expect(v1203 == [0x1A5FB50, 0x1A5FB50, 0x1D38940], f"1203 vtable {v1203}")
    expect(exe.read(0x1A5FB50, 11) == bytes.fromhex("488bc1488b491048ff6018"), "Run thunk: this=m_pObj; jmp m_Func")
    expect(exe.read(0x1A5F830, 6) == bytes.fromhex("b808000000c3"), "1202 size 8")
    expect(exe.read(0x1D38940, 6) == bytes.fromhex("b809000000c3"), "1203 size 9")
    ctor = exe.read(0x1A5F4C3, 0x1A5F536 - 0x1A5F4C3)
    ins = disasm(exe, 0x1A5F4C3, len(ctor))
    text = "\n".join(f"{a:x} {m} {o}" for a, m, o in ins)
    expect("mov edx, 0x4b2" in text and "mov edx, 0x4b3" in text, "ctor registers 1202/1203")
    expect(rip_target(0x1A5F4C3, 7, struct.unpack("<i", ctor[3:7])[0]) == 0x24BF400, "1202 vptr")
    expect(rip_target(0x1A5F504, 7, struct.unpack("<i", exe.read(0x1A5F507, 4))[0]) == 0x24BF418, "1203 vptr")
    # Networking call shapes (slots 0,1,2,3,4,6; channel 0; mode 0).
    expect(exe.read(0x1A5F76C, 4) == bytes.fromhex("49ff6018"), "Accept slot 3")
    expect(exe.read(0x1A5F7AC, 4) == bytes.fromhex("49ff6020"), "Close slot 4")
    expect(exe.read(0x1A5F8FF, 4) == bytes.fromhex("41ff5130"), "GetP2PSessionState slot 6")
    expect(exe.read(0x1A5FB02, 4) == bytes.fromhex("41ff5210"), "Read slot 2")
    expect(exe.read(0x1A5FBB5, 3) == bytes.fromhex("41ff12"), "Send slot 0")
    expect(exe.read(0x1A5FC2F, 4) == bytes.fromhex("41ff5108"), "Available slot 1")
    expect(exe.read(0x1A5FCA0, 3) == bytes.fromhex("ff5008"), "Available slot 1 (loop)")
    return {
        "mutex_block": (0x13D1, exe.read(0x13D1, 0x1420 - 0x13D1)),
        "title": (0x20A0980, title),
        "identity_block": (0x1428, exe.read(0x1428, 0x1456 - 0x1428)),
        "xinput_thunk": (0x32892C, thunk),
        "run_thunk": (0x1A5FB50, exe.read(0x1A5FB50, 11)),
        "callback_ctor": (0x1A5F4C3, ctor),
        "net_accept": (0x1A5F740, exe.read(0x1A5F740, 0x1A5F776 - 0x1A5F740)),
        "net_close": (0x1A5F780, exe.read(0x1A5F780, 0x1A5F7B6 - 0x1A5F780)),
        "net_state": (0x1A5F8C0, exe.read(0x1A5F8C0, 0x1A5F958 - 0x1A5F8C0)),
        "net_read": (0x1A5FAC0, exe.read(0x1A5FAC0, 0x1A5FB44 - 0x1A5FAC0)),
        "net_send": (0x1A5FB60, exe.read(0x1A5FB60, 0x1A5FBD2 - 0x1A5FB60)),
        "net_poll": (0x1A5FBF0, exe.read(0x1A5FBF0, 0x1A5FCBA - 0x1A5FBF0)),
    }


# ---- Steam online: the executable census (docs/steam-online.md 1.4).
STEAM_IMPORTS = {
    0x208E0D8: "SteamAPI_UnregisterCallResult", 0x208E0E0: "SteamAPI_RegisterCallResult",
    0x208E0E8: "SteamAPI_RunCallbacks", 0x208E0F0: "SteamAPI_UnregisterCallback",
    0x208E0F8: "SteamAPI_RegisterCallback", 0x208E100: "SteamUserStats", 0x208E108: "SteamUtils",
    0x208E110: "SteamFriends", 0x208E118: "SteamRemoteStorage", 0x208E120: "SteamApps",
    0x208E128: "SteamUser", 0x208E130: "SteamAPI_Init", 0x208E138: "SteamNetworking",
}
GAME_CALLBACK_SITES = {0x7D8D9A: 1101, 0x7D8DD7: 1102, 0x7D8E14: 1103, 0x80C3F3: 333, 0x80D705: 152,
                       0x1A5F4EE: 1202, 0x1A5F52F: 1203, 0x1DFE9CA: 163, 0x1DFEC4C: 331}
OUR_CALLBACKS = {337, 1251, 1252, 1221, 1222, 1281}
GAME_FRIENDS_SLOTS = {0, 3, 4, 7, 29, 37}


def iat_call_sites(exe, iat):
    sites = {}
    for name, va, span, raw, rsize, chars in exe.section_info:
        if not chars & 0x20000000:
            continue
        data = exe.data[raw:raw + rsize]
        for m in re.finditer(rb"\xff[\x15\x25]", data):
            off = m.start()
            if off + 6 > len(data):
                continue
            target = va + off + 6 + struct.unpack_from("<i", data, off + 2)[0]
            if target in iat:
                sites.setdefault(iat[target], []).append(va + off)
    return sites


def edx_before(exe, site):
    window = exe.read(site - 0x40, 0x46)
    for k in range(0, 0x30):
        ins = list(MD.disasm(window[k:], site - 0x40 + k))
        if ins and ins[-1].address == site:
            values = [i.op_str for i in ins if i.mnemonic == "mov" and i.op_str.startswith("edx, ")]
            if values:
                return int(values[-1].split(", ")[1], 0)
    return None


def vtable_slot_after(exe, site):
    """The first non-rip indirect call/jmp through a register after the getter call: its slot."""
    for i in MD.disasm(exe.read(site, 0x80), site):
        if i.mnemonic in ("call", "jmp") and i.address != site and "qword ptr [" in i.op_str and "rip" not in i.op_str:
            op = i.operands[0]
            if op.type == capstone.x86.X86_OP_MEM and op.mem.index == 0:
                return op.mem.disp // 8
    return None


def check_exe_online(exe):
    imports = exe.imports("steam_api64.dll")
    expect(imports == STEAM_IMPORTS, f"the 13 Steam imports {sorted((hex(k), v) for k, v in imports.items())}")
    sites = iat_call_sites(exe, imports)
    expect(sites.get("SteamAPI_Init") == [0x1428], f"one SteamAPI_Init site {sites.get('SteamAPI_Init')}")
    expect(sites.get("SteamAPI_RunCallbacks") == [0x7DED5D], f"one SteamAPI_RunCallbacks site {sites.get('SteamAPI_RunCallbacks')}")
    expect("SteamClient" not in imports.values() and "SteamMatchmaking" not in imports.values()
           and "SteamAPI_Shutdown" not in imports.values(), "no SteamClient/SteamMatchmaking/Shutdown import")
    register = sites.get("SteamAPI_RegisterCallback", [])
    ids = {site: edx_before(exe, site) for site in register}
    expect(ids == GAME_CALLBACK_SITES, f"RegisterCallback sites and ids {sorted((hex(k), v) for k, v in ids.items())}")
    expect(not (set(ids.values()) & OUR_CALLBACKS), "the game registers none of 337/1221/1222/1251/1252/1281")
    friends = {vtable_slot_after(exe, site) for site in sites.get("SteamFriends", [])}
    expect(None not in friends and friends <= GAME_FRIENDS_SLOTS, f"game ISteamFriends slots {sorted(friends, key=str)}")
    for needle in (b"SteamNetworkingMessages", b"SteamNetworkingSockets", b"SteamNetworkingUtils"):
        expect(needle not in exe.data, f"no {needle.decode()} string in the executable")
    expect(exe.data.find(b"+connect_lobby") == 0x222DD50, "+connect_lobby string (the game's lobby join; never used)")
    return len(register), sorted(friends)


# ---- steam_api64 (hash-pinned): the flat exports the online path calls.
ONLINE_EXPORTS = [
    # name, rva, size, (assertion kind, value)
    ("SteamAPI_Init", 0x5420, 7, ("init", 0x4970)),
    ("SteamClient", 0x5D90, 8, ("global_q", 0x30D38)),
    ("SteamAPI_GetHSteamUser", 0x5270, 7, ("global_d", 0x30E50)),
    ("SteamAPI_GetHSteamPipe", 0x5260, 7, ("global_d", 0x30E44)),
    ("SteamAPI_ISteamClient_GetISteamGenericInterface", 0x1450, 7, ("tail", 12)),
    ("SteamAPI_ISteamUser_BLoggedOn", 0x1950, 7, ("tail", 1)),
    ("SteamAPI_ISteamUser_GetSteamID", 0x2030, 23, ("hidden", 2)),
    ("SteamAPI_ISteamFriends_GetFriendCount", 0x10A0, 7, ("tail", 3)),
    ("SteamAPI_ISteamFriends_GetFriendByIndex", 0x1300, 29, ("hidden", 4)),
    ("SteamAPI_ISteamFriends_GetFriendRelationship", 0x1C80, 15, ("call", 5)),
    ("SteamAPI_ISteamFriends_GetFriendPersonaName", 0x16C0, 15, ("call", 7)),
    ("SteamAPI_ISteamFriends_GetFriendGamePlayed", 0x1680, 15, ("call", 8)),
    ("SteamAPI_ISteamFriends_SetRichPresence", 0x1D90, 10, ("tail", 43)),
    ("SteamAPI_ISteamFriends_GetFriendRichPresence", 0x13E0, 18, ("call", 45)),
    ("SteamAPI_ISteamFriends_RequestFriendRichPresence", 0x15D0, 18, ("call", 48)),
    ("SteamAPI_ISteamFriends_InviteUserToGame", 0x14B0, 18, ("call", 49)),
]


def check_online_export(dll, name, rva, size, kind):
    data = dll.read(rva, size)
    ins = list(MD.disasm(data, rva))
    expect(sum(i.size for i in ins) == size, f"{name}: {size} bytes decode exactly")
    what, value = kind
    if what == "init":
        expect(data[:2] == b"\x33\xc9" and data[2] == 0xE9 and rip_target(rva + 2, 5, struct.unpack("<i", data[3:7])[0]) == value,
               f"{name}: xor ecx,ecx; jmp {value:#x}")
    elif what == "global_q":
        expect(data[:3] == b"\x48\x8b\x05" and rip_target(rva, 7, struct.unpack("<i", data[3:7])[0]) == value and data[7] == 0xC3,
               f"{name}: mov rax,[{value:#x}]; ret")
    elif what == "global_d":
        expect(data[:2] == b"\x8b\x05" and rip_target(rva, 6, struct.unpack("<i", data[2:6])[0]) == value and data[6] == 0xC3,
               f"{name}: mov eax,[{value:#x}]; ret")
    elif what == "tail":
        expect(data[:3] == b"\x48\x8b\x01", f"{name}: mov rax,[rcx]")
        last = ins[-1]
        expect(last.mnemonic == "jmp" and last.operands[0].type == capstone.x86.X86_OP_MEM and last.operands[0].mem.disp == 8 * value,
               f"{name}: tail jump to slot {value}")
    elif what in ("call", "hidden"):
        expect(data[:4] == b"\x48\x83\xec\x28" and data[4:7] == b"\x48\x8b\x01", f"{name}: sub rsp,28h; mov rax,[rcx]")
        calls = [i for i in ins if i.mnemonic == "call"]
        expect(len(calls) == 1 and calls[0].operands[0].type == capstone.x86.X86_OP_MEM and calls[0].operands[0].mem.disp == 8 * value,
               f"{name}: one call through slot {value}")
        if what == "hidden":
            text = " | ".join(f"{i.mnemonic} {i.op_str}" for i in ins)
            expect("lea rdx, [rsp + 0x30]" in text and "mov rax, qword ptr [rax]" in text,
                   f"{name}: hidden-return CSteamID read back as uint64")
        expect(ins[-1].mnemonic == "ret", f"{name}: ret")
    return data


def check_steam(dll):
    expect(dll.read(0x5E80, 8) == bytes.fromhex("488b05e9af0200c3"), "SteamUser getter")
    expect(dll.read(0x5DB0, 8) == bytes.fromhex("488b05c1b00200c3"), "SteamFriends getter")
    expect(dll.read(0x5E40, 8) == bytes.fromhex("488b0569b00200c3"), "SteamRemoteStorage getter")
    expect(dll.read(0x5E30, 8) == bytes.fromhex("488b0571b00200c3"), "SteamNetworking getter")
    expect(dll.read(0x5530, 5) == bytes.fromhex("e9dbd5ffff"), "RegisterCallback -> 0x2B10")
    expect(dll.read(0x5B10, 5) == bytes.fromhex("e9fbd0ffff"), "UnregisterCallback -> 0x2C10")
    reg = dll.read(0x2B10, 0x2B72 - 0x2B10)
    expect(reg[0x19:0x1D] == bytes.fromhex("804f0801"), "Register: or byte [cb+8],1")
    expect(reg[0x28:0x2B] == bytes.fromhex("895f0c"), "Register: mov [cb+0Ch],id")
    unreg = dll.read(0x3670, 0x368B - 0x3670)
    expect(unreg[4:8] == bytes.fromhex("0fb64208") and unreg[0x16:0x1B] == bytes.fromhex("24fe884208"),
           "Unregister clears flag bit 0 at +8")
    dispatch = dll.read(0x2DE0, 0x2DFA - 0x2DE0)
    expect(dispatch.endswith(bytes.fromhex("ff5008")), "RunCallbacks dispatches vtable slot 1 (+8)")
    expect(dispatch[:11] == bytes.fromhex("0fb64108d0e82401403ae8"), "dispatch filters on the GameServer bit (+8 >> 1 & 1)")
    expect(b"\xff\x50\x10" not in dispatch, "dispatch never calls GetCallbackSizeBytes ([vt+10h])")
    # Exports by name at their pinned RVAs.
    exports = dll.exports()
    expect(len(exports) == 747, f"747 named exports ({len(exports)})")
    for name, rva in (("SteamUser", 0x5E80), ("SteamFriends", 0x5DB0), ("SteamAPI_RegisterCallback", 0x5530),
                      ("SteamAPI_RunCallbacks", 0x58A0), ("SteamAPI_ISteamFriends_ClearRichPresence", 0x1E20)):
        expect(exports.get(name) == rva, f"export {name} at {rva:#x}")
    online = {}
    for name, rva, size, kind in ONLINE_EXPORTS:
        expect(exports.get(name) == rva, f"export {name} at {rva:#x} ({exports.get(name)})")
        online[name] = (rva, check_online_export(dll, name, rva, size, kind))
    # ClearRichPresence is slot 44 (never called; only key "mgscoop_join" is ever set).
    clear = dll.read(0x1E20, 10)
    expect(clear == bytes.fromhex("488b0148ffa060010000"), "ClearRichPresence: tail jump to slot 44 (never called)")
    # RunCallbacks -> 0x2BA0 -> 0x3320 and its non-atomic reentrancy byte 0x30C45 (never called by us).
    run = dll.read(0x58A0, 0x5920 - 0x58A0)
    run_ins = list(MD.disasm(run, 0x58A0))
    text = "\n".join(f"{i.address:x} {i.mnemonic} {i.op_str}" for i in run_ins)
    expect(rip_target(0x58A4, 7, struct.unpack("<i", run[7:11])[0]) == 0x30D38, "RunCallbacks reads the SteamClient global 0x30D38")
    expect("call qword ptr [rax + 0x48]" in text and "call qword ptr [rax + 0x70]" in text, "GetISteamUtils (+48h) and RunFrame (+70h)")
    jumps = [i for i in run_ins if i.mnemonic == "jmp" and i.op_str.startswith("0x")]
    expect(len(jumps) == 1 and int(jumps[0].op_str, 16) == 0x2BA0, "RunCallbacks tail-jumps to 0x2BA0")
    expect(rip_target(0x5906, 6, struct.unpack("<i", run[0x5906 - 0x58A0 + 2:0x5906 - 0x58A0 + 6])[0]) == 0x30E44,
           "RunCallbacks uses the pipe global 0x30E44")
    inner = dll.read(0x2BA0, 0x2BCC - 0x2BA0)
    inner_ins = list(MD.disasm(inner, 0x2BA0))
    expect(inner_ins[-1].mnemonic == "jmp" and int(inner_ins[-1].op_str, 16) == 0x3320, "0x2BA0 tail-jumps to 0x3320")
    guard = dll.read(0x3358, 0x3374 - 0x3358)
    expect(guard[:2] == b"\x80\x3d" and rip_target(0x3358, 7, struct.unpack("<i", guard[2:6])[0]) == 0x30C45 and guard[6] == 0,
           "cmp byte [0x30C45],0 (reentrancy byte)")
    expect(guard[0x15:0x17] == b"\xc6\x05" and rip_target(0x336D, 7, struct.unpack("<i", guard[0x17:0x1B])[0]) == 0x30C45 and guard[0x1B] == 1,
           "mov byte [0x30C45],1 (non-atomic)")
    gates = {
        "user_getter": (0x5E80, dll.read(0x5E80, 8)),
        "friends_getter": (0x5DB0, dll.read(0x5DB0, 8)),
        "storage_getter": (0x5E40, dll.read(0x5E40, 8)),
        "networking_getter": (0x5E30, dll.read(0x5E30, 8)),
        "register_export": (0x5530, dll.read(0x5530, 5)),
        "unregister_export": (0x5B10, dll.read(0x5B10, 5)),
        "register_body": (0x2B10, reg),
        "unregister_body": (0x3670, unreg),
        "dispatch": (0x2DE0, dispatch),
    }
    online_gates = {
        "init_export": online["SteamAPI_Init"],
        "client_export": online["SteamClient"],
        "hsteamuser_export": online["SteamAPI_GetHSteamUser"],
        "hsteampipe_export": online["SteamAPI_GetHSteamPipe"],
        "generic_interface_export": online["SteamAPI_ISteamClient_GetISteamGenericInterface"],
        "blogged_on_export": online["SteamAPI_ISteamUser_BLoggedOn"],
        "get_steam_id_export": online["SteamAPI_ISteamUser_GetSteamID"],
        "friend_count_export": online["SteamAPI_ISteamFriends_GetFriendCount"],
        "friend_by_index_export": online["SteamAPI_ISteamFriends_GetFriendByIndex"],
        "friend_relationship_export": online["SteamAPI_ISteamFriends_GetFriendRelationship"],
        "friend_persona_export": online["SteamAPI_ISteamFriends_GetFriendPersonaName"],
        "friend_game_played_export": online["SteamAPI_ISteamFriends_GetFriendGamePlayed"],
        "set_rich_presence_export": online["SteamAPI_ISteamFriends_SetRichPresence"],
        "friend_rich_presence_export": online["SteamAPI_ISteamFriends_GetFriendRichPresence"],
        "request_rich_presence_export": online["SteamAPI_ISteamFriends_RequestFriendRichPresence"],
        "invite_user_export": online["SteamAPI_ISteamFriends_InviteUserToGame"],
        "run_callbacks": (0x58A0, run),
        "run_callbacks_inner": (0x2BA0, inner),
        "run_reentrancy": (0x3358, guard),
    }
    return gates, online_gates


# ---- steamclient64 structural mode (never pinned; by RTTI, strings and shapes).
ADAPTERS = {
    ".?AVCAdapterSteamNetworkingMessages002@@": 6,
    ".?AVCAdapterSteamNetworkingSockets012@@": 47,
    ".?AVCAdapterSteamNetworkingUtils004@@": 25,
    ".?AVCAdapterSteamClient017@@": 37,
}
MESSAGES_METHODS = ["SendMessageToUser", "ReceiveMessagesOnChannel", "AcceptSessionWithUser", "CloseSessionWithUser",
                    "CloseChannelWithUser", "GetSessionConnectionInfo"]
SOCKETS_METHODS = {5: "CloseConnection", 11: "SendMessageToConnection", 14: "ReceiveMessagesOnConnection", 19: "CreateSocketPair"}


def vtables_by_rtti(img, name, this_offset=None):
    """[(vtable_rva, this_offset, slots)] for every COL naming `name`."""
    out = []
    needle = name.encode() + b"\0"
    at = img.data.find(needle)
    while at >= 0:
        td = img.rva_of_offset(at)
        if td is not None:
            td -= 0x10
            pat = struct.pack("<I", td)
            j = img.data.find(pat)
            while j >= 0:
                col_off = j - 12
                if col_off >= 0:
                    sig, off, cd = struct.unpack_from("<III", img.data, col_off)
                    col = img.rva_of_offset(col_off)
                    if sig == 1 and col is not None and struct.unpack_from("<I", img.data, col_off + 20)[0] == col and \
                            (this_offset is None or off == this_offset):
                        va = struct.pack("<Q", img.image_base + col)
                        k = img.data.find(va)
                        while k >= 0:
                            vt = img.rva_of_offset(k)
                            if vt is not None and img.is_rdata(vt):
                                vt += 8
                                slots = []
                                for n in range(400):
                                    q = struct.unpack("<Q", img.read(vt + 8 * n, 8))[0] - img.image_base
                                    if not (0 <= q < img.size_of_image) or not img.is_code(q):
                                        break
                                    slots.append(q)
                                out.append((vt, off, slots))
                            k = img.data.find(va, k + 1)
                j = img.data.find(pat, j + 1)
        at = img.data.find(needle, at + 1)
    return out


def forwarder_slot(code):
    """The inner slot an adapter forwarder jumps to, or None."""
    if code[:4] != b"\x48\x8b\x49\x08":
        return None
    p = code[4:]
    if p[:3] == b"\x48\x8b\x01":
        q = p[3:]
        if q[:3] == b"\x48\xff\x20":
            return 0
        if q[:3] == b"\x48\xff\x60":
            return q[3] // 8
        if q[:3] == b"\x48\xff\xa0":
            return struct.unpack_from("<I", q, 3)[0] // 8
    if p[:9] == b"\x0f\xb6\x44\x24\x28\x88\x44\x24\x28":  # Sockets CloseConnection: normalizes the bool, then forwards
        p = p[9:]
    if p[:3] == b"\x4c\x8b\x11":
        q = p[3:]
        if q[:3] == b"\x49\xff\x22":
            return 0
        if q[:3] == b"\x49\xff\x62":
            return q[3] // 8
        if q[:3] == b"\x49\xff\xa2":
            return struct.unpack_from("<I", q, 3)[0] // 8
    return None


def messages_forwarder(code, slot):
    """The exact Messages002 forwarder shapes of the runtime gate (steam_net_gates.cpp IsForwarder)."""
    if code[:4] != b"\x48\x8b\x49\x08":
        return False
    p = code[4:]
    if p[:6] == b"\x48\x8b\x01\x48\xff\x60":
        return p[6] == 8 * slot
    if p[:5] == b"\x4c\x8b\x11\x49\xff":
        if p[5] == 0x22:
            return slot == 0
        if p[5] == 0x62:
            return p[6] == 8 * slot
    return False


def lea_strings(img, rva, size=0x200):
    names = []
    for i in MD.disasm(img.read(rva, size), rva):
        if i.mnemonic == "lea" and "rip" in i.op_str:
            target = i.address + i.size + i.operands[1].mem.disp
            try:
                names.append(img.cstring(target, 64).decode("ascii", "replace"))
            except ValueError:
                pass
        if i.mnemonic == "int3":  # the padding after the function (early returns come before the name)
            break
    return names


# ---- Section 20: function-pointer dispatch (config 204/205 + Sockets012::RunCallbacks).
# (id, config name, data type Ptr = 5, scope: 1 Global, 4 Connection), as steam_net_abi.hpp kConfig*.
CONFIG_CALLBACKS = ((201, "Callback_ConnectionStatusChanged", 5, 4),
                    (204, "Callback_MessagesSessionRequest", 5, 1),
                    (205, "Callback_MessagesSessionFailed", 5, 1))
# steam_net_abi.hpp kRunCallbacksIds: the ids RunCallbacks dispatches (anything else asserts).
RUN_CALLBACKS_IDS = {1221, 1222, 1223, 1251, 1252, 1281}
QUEUE_SLOT = 0x220 // 8  # InternalQueueCallback(id, cb, pv, fn): call [vtbl+0x220]


def gate_forwarder(code, slot):
    """The runtime gate's IsForwarder (steam_net_gates.cpp): disp8 below slot 16, disp32 from slot 16."""
    if code[:4] != b"\x48\x8b\x49\x08":
        return False
    p = code[4:]
    off = 8 * slot
    small = off < 0x80
    if p[:5] == b"\x48\x8b\x01\x48\xff":
        if p[5] == 0x60:
            return small and p[6] == off
        return p[5] == 0xa0 and not small and struct.unpack_from("<I", p, 6)[0] == off
    if p[:5] == b"\x4c\x8b\x11\x49\xff":
        if p[5] == 0x22:
            return slot == 0
        if p[5] == 0x62:
            return small and p[6] == off
        return p[5] == 0xa2 and not small and struct.unpack_from("<I", p, 6)[0] == off
    return False


RIP_REX = re.compile(rb"[\x48\x4c][\x8b\x89\x8d\x39\x3b\x85\xc7\x83\x81\xff][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", re.S)
RIP_PLAIN = re.compile(rb"[\x8b\x89\x8d\x39\x3b\x85\xc7\x83\x81\xff][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", re.S)


def rip_refs(img, target):
    """Every rip-relative instruction in code that addresses `target`: [(rva, mnemonic, op_str)]."""
    out = {}
    for _, va, _, raw, rsize, chars in img.section_info:
        if not chars & 0x20000000:
            continue
        buf = img.data[raw:raw + rsize]
        for pattern, opcode_at in ((RIP_REX, 1), (RIP_PLAIN, 0)):
            for m in pattern.finditer(buf):
                off = m.start()
                if opcode_at == 0 and off and 0x40 <= buf[off - 1] <= 0x4f:
                    continue  # the opcode of a REX instruction (decoded by RIP_REX)
                if off + opcode_at + 6 > len(buf):
                    continue
                disp = struct.unpack_from("<i", buf, off + opcode_at + 2)[0]
                end = va + off + opcode_at + 6
                if not any(end + extra + disp == target for extra in (0, 1, 4)):
                    continue
                ins = next(MD.disasm(buf[off:off + 16], va + off), None)
                mem = [o for o in ins.operands if o.type == capstone.x86.X86_OP_MEM] if ins is not None else []
                if mem and mem[0].mem.base == capstone.x86.X86_REG_RIP and ins.address + ins.size + mem[0].mem.disp == target:
                    out[ins.address] = (ins.address, ins.mnemonic, ins.op_str)
    return sorted(out.values())


def string_rva(img, data, text):
    at = data.find(text.encode() + b"\0")
    expect(at >= 0 and data.find(text.encode() + b"\0", at + 1) < 0, f"string {text!r} exactly once")
    return img.rva_of_offset(at)


def run_to(img, start, stop, limit=0x600):
    """Linear disassembly from `start` that reaches the instruction at `stop` (one instruction stream)."""
    out = []
    for i in MD.disasm(img.read(start, limit), start):
        out.append(i)
        if i.address >= stop:
            break
    expect(out and out[-1].address == stop, f"linear disassembly 0x{start:x} reaches 0x{stop:x}")
    return out


def config_entry(img, data, value_id, name, data_type, scope):
    """The static config table entry {i32 id; char* name; i32 type; i32 scope; ...} in .data; value m_data at +0x40."""
    name_rva = string_rva(img, data, name)
    ptr = struct.pack("<Q", img.image_base + name_rva)
    refs = [img.rva_of_offset(m.start()) for m in re.finditer(re.escape(ptr), data)]
    refs = [r for r in refs if r is not None and img.section_of(r)[0] == b".data"]
    expect(len(refs) == 1, f"config {value_id} {name}: one .data entry ({len(refs)})")
    entry = refs[0] - 8
    got = struct.unpack("<i4xQii", img.read(entry, 24))
    expect(got[0] == value_id and got[2] == data_type and got[3] == scope,
           f"config entry {name}: id {value_id}, type {data_type} (Ptr), scope {scope} ({got[0]}, {got[2]}, {got[3]})")
    return entry


def check_value_reader(img, data, entry, anchor, callback_id, size, what):
    """The value (entry+0x40, SetConfigValue's Global m_data) has one 64-bit load: the queue call of `callback_id`."""
    value = entry + 0x40
    refs = rip_refs(img, value)
    loads = [r for r in refs if r[1] == "mov" and re.fullmatch(r"r\w+, qword ptr \[rip \+ 0x[0-9a-f]+\]", r[2])]
    stores = [r for r in refs if r[1] == "mov" and re.fullmatch(r"qword ptr \[rip \+ 0x[0-9a-f]+\], r\w+", r[2])]
    expect(len(loads) == 1 and len(stores) + len(loads) == len(refs),
           f"{what}: exactly one load of the config value, the rest initializer stores ({[hex(r[0]) for r in refs]})")
    load = loads[0][0]
    # The function's own name string precedes the load in one instruction stream.
    anchor_rva = string_rva(img, data, anchor)
    leas = [r[0] for r in rip_refs(img, anchor_rva) if r[1] == "lea" and r[0] < load and load - r[0] < 0x700]
    expect(len(leas) >= 1, f"{what}: the load follows the {anchor.strip()!r} reference")
    stream = run_to(img, leas[-1], load, 0x700)
    # Up to the first call after the load: that call is InternalQueueCallback with the value as its 5th argument.
    tail = []
    for i in MD.disasm(img.read(load, 0x100), load):
        tail.append(i)
        if i.mnemonic == "call":
            break
    text = [f"{i.mnemonic} {i.op_str}" for i in stream[:-1] + tail]
    expect(text[-1] == f"call qword ptr [r10 + 0x{QUEUE_SLOT * 8:x}]", f"{what}: the next call is InternalQueueCallback [r10+0x220]")
    expect(not any(s.startswith("call ") for s in text[len(stream) - 1:-1]), f"{what}: no call between the load and the queue call")

    def last_write(pattern):
        # The last instruction before the call whose destination (first operand) matches `pattern`.
        return next((s for s in reversed(text[:-1]) if not s.startswith(("cmp ", "test ", "push ")) and
                     re.match(r"\w+ (?:" + pattern + r")(?:,|$)", s)), None)
    load_at = len(stream) - 1
    expect(text[load_at].startswith("mov rax, qword ptr [rip") and
           last_write(r"qword ptr \[rsp \+ 0x20\]") == "mov qword ptr [rsp + 0x20], rax" and
           not any(re.match(r"\w+ (rax|eax|ax|al),", s) for s in text[load_at + 1:text.index("mov qword ptr [rsp + 0x20], rax", load_at)]),
           f"{what}: the loaded value is the 5th argument")
    expect(last_write(r"edx|rdx|dx|dl") == f"mov edx, 0x{callback_id:x}" and last_write(r"r8d|r8|r8w|r8b") == f"mov r8d, 0x{size:x}",
           f"{what}: queues id {callback_id} with size 0x{size:x}")
    return stream


def check_steamclient_dispatch(img, data, found, s_inner, u_inner):
    # Adapters: the runtime gate's forwarder shapes (disp32) for Sockets012 41 and Utils004 16.
    _, s_slots = found[".?AVCAdapterSteamNetworkingSockets012@@"]
    _, u_slots = found[".?AVCAdapterSteamNetworkingUtils004@@"]
    expect(gate_forwarder(img.read(s_slots[41], 14), 41) and forwarder_slot(img.read(s_slots[41], 20)) == 41,
           "Sockets012 slot 41: the gate's disp32 forwarder to inner slot 41")
    expect(gate_forwarder(img.read(u_slots[16], 14), 16) and forwarder_slot(img.read(u_slots[16], 20)) == 16,
           "Utils004 slot 16: the gate's disp32 forwarder to inner slot 16")
    base = vtables_by_rtti(img, ".?AVCSteamNetworkingSockets@SteamNetworkingSocketsLib@@", 0)
    expect(len(base) == 1 and len(base[0][2]) > QUEUE_SLOT, "CSteamNetworkingSockets base vtable")
    inner = s_inner[0][2]
    expect(len(inner) > QUEUE_SLOT and inner[41] == base[0][2][41], "RunCallbacks: the steamclient class keeps the base slot 41")

    # RunCallbacks (inner slot 41): lock, swap the queue vector, unlock, then dispatch outside the lock.
    rc = inner[41]
    expect("Unknown callback type %d!" in lea_strings(img, rc, 0x300), "inner Sockets slot 41 is RunCallbacks (its assert string)")
    ins = list(MD.disasm(img.read(rc, 0x1b0), rc))
    text = [f"{i.mnemonic} {i.op_str}" for i in ins]
    first = {}
    for n, s in enumerate(text):
        first.setdefault(s, n)
    lock, swap = first.get("lea rsi, [rcx + 0x878]"), first.get("lea rax, [rdi + 0x860]")
    entry_data, dispatch = first.get("lea rsi, [rdi + 0x10]"), first.get("call qword ptr [rbx + 8]")
    expect(None not in (lock, swap, entry_data, dispatch) and lock < swap < entry_data < dispatch,
           "RunCallbacks: lock (this+0x878), swap of the queue (this+0x860), then the dispatch loop")
    between = [i for i in ins[swap:entry_data] if i.mnemonic == "call"]
    expect(len(between) == 2 and all(i.op_str.startswith("0x") for i in between),
           "RunCallbacks: exactly the two unlock calls between the swap and the loop (no lock held while dispatching)")
    expect(text[dispatch - 1] == "mov rcx, rsi" and "add rbx, 0x2d8" in text[dispatch:] and "add rsi, 0x2d8" in text[dispatch:],
           "RunCallbacks: call [entry+8] with rcx = entry+0x10 (the payload), 0x2D8-byte entries")
    # The jump table: id - 1221 <= 60 (r15 = the image base); only the six ids reach the call.
    base_reg = next((i for i in ins if i.mnemonic == "lea" and "rip" in i.op_str and
                     i.address + i.size + i.operands[1].mem.disp == 0), None)
    idx = next((n for n, s in enumerate(text) if s == "lea eax, [r9 - 0x4c5]"), None)
    expect(base_reg is not None and idx is not None and text[idx + 1] == "cmp eax, 0x3c", "RunCallbacks: switch on id - 1221 (<= 60)")
    expect(ins[idx + 4].mnemonic == "movzx" and ins[idx + 5].mnemonic == "mov", "RunCallbacks: two-level jump table")
    byte_table = ins[idx + 4].operands[1].mem.disp
    dword_table = ins[idx + 5].operands[1].mem.disp
    call_block = ins[dispatch - 1].address
    reached = set()
    for cid in range(1221, 1282):
        k = img.read(byte_table + cid - 1221, 1)[0]
        if struct.unpack("<I", img.read(dword_table + 4 * k, 4))[0] == call_block:
            reached.add(cid)
    expect(reached == RUN_CALLBACKS_IDS, f"RunCallbacks dispatches exactly {sorted(RUN_CALLBACKS_IDS)} ({sorted(reached)})")

    # InternalQueueCallback (base slot 68): queues only a non-null function, into the vector RunCallbacks swaps.
    q = base[0][2][QUEUE_SLOT]
    expect("Callback doesn't fit!" in lea_strings(img, q, 0x200), "base slot 68 is InternalQueueCallback (its assert string)")
    qtext = [f"{i.mnemonic} {i.op_str}" for i in MD.disasm(img.read(q, 0x120), q)]
    fn = next((n for n, s in enumerate(qtext) if re.fullmatch(r"mov r\w+, qword ptr \[rsp \+ 0x70\]", s)), None)
    reg = qtext[fn].split()[1].rstrip(",") if fn is not None else ""
    expect(fn is not None and qtext[fn + 1] == f"test {reg}, {reg}" and qtext[fn + 2].startswith("je "),
           "InternalQueueCallback: a null function (5th argument) queues nothing")
    expect(any(re.fullmatch(r"cmp r\w+, 0x2c8", s) for s in qtext) and any(re.fullmatch(r"lea r\w+, \[rdi \+ 0x878\]", s) for s in qtext)
           and any(re.fullmatch(r"lea r\w+, \[rdi \+ 0x860\]", s) for s in qtext),
           "InternalQueueCallback: payload <= 0x2C8, the RunCallbacks lock (+0x878) and vector (+0x860)")
    d = inner[QUEUE_SLOT]
    dins = list(MD.disasm(img.read(d, 0x80), d))
    end = next((n for n, i in enumerate(dins) if i.mnemonic == "ret"), len(dins))
    calls = [i for i in dins[:end] if i.mnemonic == "call"]
    first_base = next((n for n, i in enumerate(calls) if i.op_str == f"0x{q:x}"), None)
    branches = [i for i in dins[:end] if i.mnemonic.startswith("j")]
    expect(first_base is not None and len(calls) - first_base >= 2 and not branches,
           "the steamclient InternalQueueCallback calls the base, then always posts to the pipe (no branch)")

    # SetConfigValue (Utils inner slot 16): Ptr stores *(void**)pArg; pArg null resets to the default.
    sc = u_inner[0][2][16]
    expect("SetConfigValue" in lea_strings(img, sc, 0x200), "inner Utils slot 16 is SetConfigValue (name string)")
    stext = "\n".join(f"{i.mnemonic} {i.op_str}" for i in MD.disasm(img.read(sc, 0x420), sc))
    for pattern, what in ((r"cmp dword ptr \[rsp \+ 0x70\], 5\n.*\n.*\nmov rax, qword ptr \[rax\]\nmov qword ptr \[rbx \+ 0x10\], rax\n"
                           r"mov dword ptr \[rbx \+ 8\], 1", "Ptr: type 5 (5th argument), m_data = *(void**)pArg (6th), set"),
                          (r"mov rax, qword ptr \[rsp \+ 0x78\]\ntest rax, rax", "pArg is the 6th argument; null takes the reset path"),
                          (r"mov rax, qword ptr \[rbx \+ 0x18\]\nmov rcx, qword ptr \[rsp \+ 0x30\]\nmov qword ptr \[rbx \+ 0x10\], rax",
                           "Global reset: m_data = the default")):
        expect(re.search(pattern, stext) is not None, f"SetConfigValue: {what}")

    # The config entries and the single readers of 204 / 205.
    entries = {cid: config_entry(img, data, cid, name, t, s) for cid, name, t, s in CONFIG_CALLBACKS}
    stream = check_value_reader(img, data, entries[204], "CSteamNetworkingMessages::BHandleNewIncomingConnection", 1251, 0x88, "204")
    # rcx = [this+8]: the sockets object owning the Messages object (kMessagesOwnerSockets); this = rbp from entry.
    prologue = next(MD.disasm(img.read(stream[0].address - 3, 3), stream[0].address - 3), None)
    expect(prologue is not None and f"{prologue.mnemonic} {prologue.op_str}" == "mov rbp, rcx" and
           any(f"{i.mnemonic} {i.op_str}" == "mov rcx, qword ptr [rbp + 8]" for i in stream[-8:]) and
           not any(i.op_str.startswith("rbp,") for i in stream[1:] if i.mnemonic not in ("cmp", "test")),
           "204: queued on [this+8], the sockets object owning the Messages object")
    check_value_reader(img, data, entries[205], "[%s] Posting SteamNetworkingMessagesSessionFailed_t\n", 1252, 0x2B8, "205")

    # 1221: {hConn @0; SteamNetConnectionInfo_t @8; old state @0x2C0}, 0x2C8 bytes; the info filler.
    sites = [img.rva_of_offset(m.start()) for m in re.finditer(re.escape(b"\xba\xc5\x04\x00\x00\x41\xb8\xc8\x02\x00\x00"), data)]
    sites = [s for s in sites if s is not None and img.is_code(s)]
    expect(len(sites) >= 1, "1221 posts (mov edx,0x4C5; mov r8d,0x2C8)")
    filler = None
    for site in sites:
        window = list(MD.disasm(img.read(site - 0x80, 0xa0), site - 0x80))
        aligned = [i for i in window if i.address == site]
        expect(len(aligned) == 1, f"1221 post 0x{site:x}: decodes in one stream with its setup")
        wtext = [f"{i.mnemonic} {i.op_str}" for i in window]
        expect(any(re.fullmatch(r"mov dword ptr \[rsp \+ 0x2f0\], e\w+", s) for s in wtext) and "lea r9, [rsp + 0x30]" in wtext and
               "call qword ptr [r10 + 0x220]" in wtext and "mov dword ptr [rsp + 0x30], eax" in wtext,
               f"1221 post 0x{site:x}: payload rsp+0x30 (hConn), old state at +0x2C0")
        fill = [window[n + 1] if window[n + 1].mnemonic == "call" else window[n + 2] for n, i in enumerate(window[:-2])
                if f"{i.mnemonic} {i.op_str}" == "lea rdx, [rsp + 0x38]"]
        expect(fill and fill[0].mnemonic == "call", f"1221 post 0x{site:x}: the info is filled at payload+8")
        target = int(fill[0].op_str, 16)
        expect(filler in (None, target), "both 1221 posts use one connection-info filler")
        filler = target
    ftext = "\n".join(f"{i.mnemonic} {i.op_str}" for i in MD.disasm(img.read(filler, 0x160), filler))
    for pattern, what in ((r"mov r8d, 0x2b8\nmov rcx, rbx\ncall", "memset(info, 0, 0x2B8)"),
                          (r"mov dword ptr \[rbx \+ 0xb0\], eax", "state +0xB0"),
                          (r"mov dword ptr \[rbx \+ 0xb4\], eax", "end reason +0xB4"),
                          (r"lea rcx, \[rbx \+ 0xb8\]\n(?:.*\n){0,2}mov r8d, 0x80", "end debug +0xB8, 0x80 bytes")):
        expect(re.search(pattern, ftext) is not None, f"connection-info filler: {what}")
    return ("dispatch: RunCallbacks ids 1221/1222/1223/1251/1252/1281 outside the lock, SetConfigValue Ptr, config 201/204/205, "
            "one reader each of 204/205, InternalQueueCallback null check + pipe post, 1221 payload, info end reason/debug")


def check_steamclient(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    img = Image(data=data)
    expect(struct.unpack_from("<H", data, img.pe + 4)[0] == 0x8664, "steamclient64 is x64")
    found = {}
    for name, count in ADAPTERS.items():
        tables = [t for t in vtables_by_rtti(img, name, 0)]
        expect(len(tables) == 1, f"{name}: exactly one vtable ({len(tables)})")
        vt, off, slots = tables[0]
        expect(len(slots) == count, f"{name}: {count} slots ({len(slots)})")
        expect(img.is_rdata(vt), f"{name}: vtable in a read-only non-executable section")
        found[name] = (vt, slots)
    m_vt, m_slots = found[".?AVCAdapterSteamNetworkingMessages002@@"]
    for n, target in enumerate(m_slots):
        expect(messages_forwarder(img.read(target, 11), n), f"Messages002 slot {n}: forwarder to inner slot {n}")
    # The inner implementation: CSteamNetworkingMessages' IClientNetworkingMessages subobject (this +0xD0).
    inner = vtables_by_rtti(img, ".?AVCSteamNetworkingMessages@SteamNetworkingSocketsLib@@", 0xD0)
    expect(len(inner) == 1 and len(inner[0][2]) >= 6, "CSteamNetworkingMessages subobject vtable (this+0xD0)")
    inner_slots = inner[0][2]
    for n, method in enumerate(MESSAGES_METHODS):
        expect(method in lea_strings(img, inner_slots[n], 0x300), f"inner Messages slot {n} is {method} (name string)")
    send = "\n".join(f"{i.mnemonic} {i.op_str}" for i in MD.disasm(img.read(inner_slots[0], 0x200), inner_slots[0]))
    expect("cmp dword ptr [rdx], 0" in send, "SendMessageToUser: identity type 0 refused (rdx = identity)")
    expect(re.search(r"cmp r\d+d, 0x80000", send) is not None, "SendMessageToUser: cb <= 0x80000 (r9d)")
    expect("test byte ptr [rsp + 0x120], 0x20" in send, "SendMessageToUser: flags (5th argument) AutoRestartBrokenSession 0x20")
    info_ins = list(MD.disasm(img.read(inner_slots[5], 0x200), inner_slots[5]))
    info = "\n".join(f"{i.mnemonic} {i.op_str}" for i in info_ins)
    expect("mov r8d, 0x2b8" in info, "GetSessionConnectionInfo: memset(info, 0, 0x2B8)")
    expect(re.search(r"mov qword ptr \[r\w+ \+ 0x70\], r\w+", info) is not None, "GetSessionConnectionInfo: quick status zeroed (0x78 bytes)")
    base = re.search(r"lea rax, \[(r\w+) \+ 0x170\]", info)
    expect(base is not None and f"[{base.group(1)} + 0x220]" in "\n".join(f"{i.op_str}" for i in MD.disasm(img.read(inner_slots[5], 0x260), inner_slots[5])),
           "GetSessionConnectionInfo: info copied from conn+0x170, state returned from conn+0x220 (info+0xB0)")
    # Sockets012: forwarders to the CSteamNetworkingSocketsSteamClient slots, by name string.
    s_vt, s_slots = found[".?AVCAdapterSteamNetworkingSockets012@@"]
    s_inner = vtables_by_rtti(img, ".?AVCSteamNetworkingSocketsSteamClient@SteamNetworkingSocketsLib@@", 0)
    expect(len(s_inner) == 1, "CSteamNetworkingSocketsSteamClient vtable")
    for slot, method in SOCKETS_METHODS.items():
        target = forwarder_slot(img.read(s_slots[slot], 20))
        expect(target == slot, f"Sockets012 slot {slot}: forwarder to inner slot {slot} ({target})")
        expect(method in lea_strings(img, s_inner[0][2][slot], 0x300), f"inner Sockets slot {slot} is {method}")
    pair = "\n".join(f"{i.mnemonic} {i.op_str}" for i in MD.disasm(img.read(s_inner[0][2][19], 0x200), s_inner[0][2][19]))
    expect(re.search(r"movzx r\d+d, r9b", pair) is not None, "CreateSocketPair: bUseNetworkLoopback in r9b")
    expect(re.search(r"mov dword ptr \[rsp \+ (0x[0-9a-f]+)\], 1\nmov dword ptr \[rsp \+ 0x[0-9a-f]+\], 0x12", pair) is not None,
           "CreateSocketPair: a null identity becomes IP localhost (type 1, cbSize 0x12)")
    # Utils004: slot 0 AllocateMessage (+0xB0 / +0xB8 / +0xC0 stores), slot 1 / 7 forwarders.
    u_vt, u_slots = found[".?AVCAdapterSteamNetworkingUtils004@@"]
    u_inner = vtables_by_rtti(img, ".?AVCSteamNetworkingUtilsSteamClient@SteamNetworkingSocketsLib@@", 0)
    expect(len(u_inner) == 1, "CSteamNetworkingUtilsSteamClient vtable")
    for slot in (0, 1, 7):
        expect(forwarder_slot(img.read(u_slots[slot], 20)) == slot, f"Utils004 slot {slot}: forwarder to inner slot {slot}")
    alloc_thunk = list(MD.disasm(img.read(u_inner[0][2][0], 16), u_inner[0][2][0]))
    expect(alloc_thunk[0].mnemonic == "mov" and alloc_thunk[0].op_str == "ecx, edx" and alloc_thunk[1].mnemonic == "jmp",
           "Utils inner slot 0: mov ecx,edx; jmp AllocateMessage")
    alloc = int(alloc_thunk[1].op_str, 16) - img.image_base if int(alloc_thunk[1].op_str, 16) > img.image_base else int(alloc_thunk[1].op_str, 16)
    text = "\n".join(f"{i.mnemonic} {i.op_str}" for i in MD.disasm(img.read(alloc, 0x120), alloc))
    for pattern, what in ((r"mov ecx, 0x108", "0x108-byte message object"),
                          (r"mov qword ptr \[rbx \+ 0xb0\], rax", "+0xB0 pfnFreeData"),
                          (r"mov qword ptr \[rbx \+ 0xb8\], rax", "+0xB8 pfnRelease"),
                          (r"mov dword ptr \[rbx \+ 0xc0\], 0xffffffff", "+0xC0 channel = -1"),
                          (r"mov dword ptr \[rbx \+ 8\], e\w+", "+0x08 cbSize")):
        expect(re.search(pattern, text) is not None, f"AllocateMessage: {what}")
    # The interface table entry and its factory: a 16-byte adapter {vptr Messages002, inner}.
    name_rva = img.rva_of_offset(data.find(b"SteamNetworkingMessages002\0"))
    expect(name_rva is not None, "the interface name string")
    ref = data.find(struct.pack("<Q", img.image_base + name_rva))
    expect(ref >= 0, "interface table entry")
    factory = struct.unpack_from("<Q", data, ref - 8)[0] - img.image_base
    expect(img.is_code(factory), "interface table: the factory precedes the name")
    fac = list(MD.disasm(img.read(factory, 0x80), factory))
    fac_text = "\n".join(f"{i.mnemonic} {i.op_str}" for i in fac)
    expect("mov edx, 0x10" in fac_text, "factory allocates a 16-byte adapter per call (acquire once, cache)")
    leas = [i.address + i.size + i.operands[1].mem.disp for i in fac if i.mnemonic == "lea" and "rip" in i.op_str]
    expect(m_vt in leas, "factory stores the Messages002 adapter vtable")
    expect("mov qword ptr [rax + 8], rbx" in fac_text, "factory stores the inner object at +8")
    # The client adapter's GetISteamGenericInterface slot.
    c_vt, c_slots = found[".?AVCAdapterSteamClient017@@"]
    expect(len(c_slots) > 12 and img.is_code(c_slots[12]), "SteamClient017 slot 12 in code")
    for needle in (b"k_nVirtualPort_Messages", b"Callback_MessagesSessionRequest"):
        expect(needle in data, f"string {needle.decode()}")
    dispatch = check_steamclient_dispatch(img, data, found, s_inner, u_inner)
    print(f"steamclient64.dll SHA-256 {digest}: Messages002 6 forwarders, Sockets012 47, Utils004 25, Client017 37, "
          f"inner names, GetSessionConnectionInfo 0x2B8/0x78/+0xB0, AllocateMessage stores, CreateSocketPair, factory; {dispatch}: ok")
    return 0


def emit(name, rva, data):
    body = ",".join(f"0x{b:02x}" for b in data)
    return f"inline constexpr ByteGate {name}{{0x{rva:x},{len(data)},{{{body}}}}};\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="D:/Games/steamapps/common/MGS_TPP")
    parser.add_argument("--output", default=str(pathlib.Path(__file__).resolve().parents[1] / "native/steam_shim_contract.hpp"))
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--steamclient")
    args = parser.parse_args()
    if args.steamclient:
        return check_steamclient(pathlib.Path(args.steamclient))
    game = pathlib.Path(args.game)
    exe_path, dll_path = game / "mgsvtpp.exe", game / "steam_api64.dll"
    exe_bytes = exe_path.read_bytes()
    dll_bytes = dll_path.read_bytes()
    expect(hashlib.sha256(exe_bytes).hexdigest() == EXE_SHA256, "mgsvtpp.exe SHA-256")
    expect(hashlib.sha256(dll_bytes).hexdigest() == STEAM_SHA256, "steam_api64.dll SHA-256")
    exe, dll = Image(data=exe_bytes), Image(data=dll_bytes)
    gates = check_exe(exe)
    sites, friends = check_exe_online(exe)
    steam, online = check_steam(dll)
    longest = max(len(d) for _, d in list(gates.values()) + list(steam.values()) + list(online.values()))
    out = [
        "// Generated by tools/generate_steam_shim_contract.py; exact game build.\n",
        f"// mgsvtpp.exe SHA-256 {EXE_SHA256} (TimeDateStamp 0x{exe.timestamp:08x}, SizeOfImage 0x{exe.size_of_image:x})\n",
        f"// steam_api64.dll SHA-256 {STEAM_SHA256} (TimeDateStamp 0x{dll.timestamp:08x}, SizeOfImage 0x{dll.size_of_image:x})\n",
        "#pragma once\n#include <cstddef>\n#include <cstdint>\n\nnamespace mgscoop::steam_shim_contract {\n",
        f"constexpr std::size_t kMaxGateBytes = {longest};\n",
        "struct ByteGate { std::uint32_t rva; std::uint32_t size; std::uint8_t bytes[kMaxGateBytes]; };\n",
        f"constexpr std::uint32_t kExeTimeDateStamp = 0x{exe.timestamp:08x};\n",
        f"constexpr std::uint32_t kExeSizeOfImage = 0x{exe.size_of_image:x};\n",
        f"constexpr std::uint32_t kSteamTimeDateStamp = 0x{dll.timestamp:08x};\n",
        f"constexpr std::uint32_t kSteamSizeOfImage = 0x{dll.size_of_image:x};\n",
        "// mgsvtpp.exe RVAs (fixed image base 0x140000000; no DYNAMIC_BASE).\n",
    ]
    for name, (rva, data) in gates.items():
        out.append(emit("exe_" + name, rva, data))
    out.append("// steam_api64.dll module-relative RVAs.\n")
    for name, (rva, data) in steam.items():
        out.append(emit("steam_" + name, rva, data))
    out.append("// Steam online (docs/steam-online.md 1.2): the flat exports the owner context calls, the\n"
               "// SteamAPI_Init wrapper target and the RunCallbacks chain (never called by the loader).\n")
    for name, (rva, data) in online.items():
        out.append(emit("steam_" + name, rva, data))
    out.append("struct NamedGate { const char* name; const ByteGate* gate; };\n")
    out.append("// Every flat export the online path resolves by name (GetProcAddress must equal base + rva).\n")
    out.append("inline constexpr NamedGate kOnlineExports[] = {\n")
    for name, _, _, _ in ONLINE_EXPORTS:
        key = {"SteamAPI_Init": "init_export", "SteamClient": "client_export", "SteamAPI_GetHSteamUser": "hsteamuser_export",
               "SteamAPI_GetHSteamPipe": "hsteampipe_export",
               "SteamAPI_ISteamClient_GetISteamGenericInterface": "generic_interface_export",
               "SteamAPI_ISteamUser_BLoggedOn": "blogged_on_export", "SteamAPI_ISteamUser_GetSteamID": "get_steam_id_export",
               "SteamAPI_ISteamFriends_GetFriendCount": "friend_count_export",
               "SteamAPI_ISteamFriends_GetFriendByIndex": "friend_by_index_export",
               "SteamAPI_ISteamFriends_GetFriendRelationship": "friend_relationship_export",
               "SteamAPI_ISteamFriends_GetFriendPersonaName": "friend_persona_export",
               "SteamAPI_ISteamFriends_GetFriendGamePlayed": "friend_game_played_export",
               "SteamAPI_ISteamFriends_SetRichPresence": "set_rich_presence_export",
               "SteamAPI_ISteamFriends_GetFriendRichPresence": "friend_rich_presence_export",
               "SteamAPI_ISteamFriends_RequestFriendRichPresence": "request_rich_presence_export",
               "SteamAPI_ISteamFriends_InviteUserToGame": "invite_user_export"}[name]
        out.append(f"    {{\"{name}\", &steam_{key}}},\n")
    out.append("};\n")
    out.append(f"// Executable census: {sites} RegisterCallback sites (none of 337/1221/1222/1251/1252/1281), one SteamAPI_Init\n"
               f"// site 0x1428, one SteamAPI_RunCallbacks site 0x7DED5D, 13 Steam imports, ISteamFriends slots {friends}.\n")
    out.append(f"constexpr unsigned kGameRegisterCallbackSites = {sites};\n")
    out.append("}  // namespace mgscoop::steam_shim_contract\n")
    text = "".join(out)
    target = pathlib.Path(args.output)
    if args.check:
        committed = target.read_text(encoding="utf-8").replace("\r\n", "\n") if target.exists() else ""
        if committed != text:
            print(f"{target} differs from the generator output: regenerate it (python tools/generate_steam_shim_contract.py)")
            return 1
        print(f"{len(gates)} exe gates, {len(steam) + len(online)} steam gates and the online census match; {target.name} is the generator output")
        return 0
    target.write_text(text, newline="\n")
    print(f"wrote {args.output}: {len(gates)} exe gates, {len(steam)} + {len(online)} steam gates, max {longest} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
