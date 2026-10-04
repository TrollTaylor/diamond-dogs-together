#!/usr/bin/env python3
"""Generate native/steam_shim_contract.hpp from the exact installed binaries.

Read-only static inspection of mgsvtpp.exe 1.0.15.4 and the shipped
steam_api64.dll. Every byte range the co-op lab shim gates on (DllMain-early
patches, loopback networking emulator, synthetic callback dispatch) is copied
verbatim from the files after their SHA-256 fingerprints are verified, and the
decoded instruction meaning is asserted with Capstone so a wrong RVA fails
here rather than in the game.

Usage:
  python tools/generate_steam_shim_contract.py [--game DIR] [--output PATH]
"""
import argparse
import hashlib
import pathlib
import struct
import sys

import capstone

EXE_SHA256 = "085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45"
STEAM_SHA256 = "08bd0152356e5619ee56a6714ac951b6800dbb21821693a4efaf85278442c60a"
IMAGE_BASE = 0x140000000


class Image:
    def __init__(self, path):
        self.data = path.read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional = struct.unpack_from("<H", self.data, pe + 20)[0]
        table = pe + 24 + optional
        self.sections = []
        for i in range(count):
            off = table + 40 * i
            vsize, va, rsize, raw = struct.unpack_from("<IIII", self.data, off + 8)
            self.sections.append((va, max(vsize, rsize), raw, rsize))
        self.timestamp = struct.unpack_from("<I", self.data, pe + 8)[0]
        self.size_of_image = struct.unpack_from("<I", self.data, pe + 24 + 56)[0]

    def read(self, rva, size):
        for va, span, raw, rsize in self.sections:
            if va <= rva and rva + size <= va + span:
                start = raw + rva - va
                chunk = self.data[start:start + size]
                if len(chunk) != size or rva + size > va + rsize:
                    raise ValueError(f"rva {rva:#x} not backed by file data")
                return chunk
        raise ValueError(f"rva {rva:#x} outside sections")


def disasm(image, rva, size):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    return [(i.address, i.mnemonic, i.op_str) for i in md.disasm(image.read(rva, size), rva)]


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
    return {
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


def emit(name, rva, data):
    body = ",".join(f"0x{b:02x}" for b in data)
    return f"inline constexpr ByteGate {name}{{0x{rva:x},{len(data)},{{{body}}}}};\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="D:/Games/steamapps/common/MGS_TPP")
    parser.add_argument("--output", default=str(pathlib.Path(__file__).resolve().parents[1] / "native/steam_shim_contract.hpp"))
    args = parser.parse_args()
    game = pathlib.Path(args.game)
    exe_path, dll_path = game / "mgsvtpp.exe", game / "steam_api64.dll"
    exe_bytes = exe_path.read_bytes()
    dll_bytes = dll_path.read_bytes()
    expect(hashlib.sha256(exe_bytes).hexdigest() == EXE_SHA256, "mgsvtpp.exe SHA-256")
    expect(hashlib.sha256(dll_bytes).hexdigest() == STEAM_SHA256, "steam_api64.dll SHA-256")
    exe, dll = Image(exe_path), Image(dll_path)
    gates = check_exe(exe)
    steam = check_steam(dll)
    longest = max(len(d) for _, d in list(gates.values()) + list(steam.values()))
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
    out.append("}  // namespace mgscoop::steam_shim_contract\n")
    pathlib.Path(args.output).write_text("".join(out), newline="\n")
    print(f"wrote {args.output}: {len(gates)} exe gates, {len(steam)} steam gates, max {longest} bytes")


if __name__ == "__main__":
    sys.exit(main())
