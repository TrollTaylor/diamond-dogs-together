"""Generate an original, bounded natural Steam Init call/return CDB observation.

Generation reads files only; it never launches or attaches to a process. The
generated command uses the existing temporary software PE-entry bootstrap,
followed by two hardware execution breakpoints. Use a fresh desktop-user launch
with the existing external deadline/verified stop helper. No SDK calls are made.

The separate original steam_init_fixture validates call-site pairing and clean
detach. It does not establish the game's complete Steam lifecycle or authorize
SDK queries. A live launcher must retain the external verified deadline path.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

from generate_registration_trace import (
    GAME_SHA256, PeImage, breakpoint_action, byte_guards, call_script,
    notice, optional_breakpoint, require,
)

STEAM_SHA256 = "08bd0152356e5619ee56a6714ac951b6800dbb21821693a4efaf85278442c60a"
INIT_CALL_RVA = 0x1428
INIT_RETURN_RVA = 0x142E
INIT_IAT_RVA = 0x208E130
INIT_EXPORT_RVA = 0x5420
CALL_BYTES = bytes.fromhex("FF 15 02 CD 08 02")
RETURN_BYTES = bytes.fromhex("84 C0")
EXPORT_BYTES = bytes.fromhex("33 C9 E9 49 F5 FF FF")


@dataclass(frozen=True)
class TraceTarget:
    module: str = "mgsvtpp"
    api_module: str = "steam_api64"
    api_name: str = "SteamAPI_Init"
    call_rva: int = INIT_CALL_RVA
    return_rva: int = INIT_RETURN_RVA
    iat_rva: int = INIT_IAT_RVA
    export_rva: int = INIT_EXPORT_RVA
    call_bytes: bytes = CALL_BYTES
    return_bytes: bytes = RETURN_BYTES
    export_bytes: bytes = EXPORT_BYTES


def string_at(pe: PeImage, rva: int) -> bytes:
    value = bytearray()
    for index in range(256):
        byte = pe.at_rva(rva + index, 1)[0]
        if not byte:
            return bytes(value)
        value.append(byte)
    raise ValueError("Import string exceeds bounded length")


def unique_init_import(pe: PeImage, dll: bytes = b"steam_api64.dll", symbol: bytes = b"SteamAPI_Init") -> int:
    optional = pe.nt + 24
    if pe.u16(pe.nt + 20) < 128 or pe.u32(optional + 108) < 2:
        raise ValueError("Import directory missing")
    directory, size = pe.u32(optional + 120), pe.u32(optional + 124)
    if not directory or size < 20 or size > 20 * 4096:
        raise ValueError("Invalid import-directory bounds")
    matches = []
    terminated = False
    for index in range(size // 20):
        fields = struct.unpack("<IIIII", pe.at_rva(directory + index * 20, 20))
        if not any(fields):
            terminated = True
            break
        original, _, _, name, first = fields
        if string_at(pe, name).lower() != dll.lower():
            continue
        for item in range(4096):
            value = struct.unpack("<Q", pe.at_rva((original or first) + item * 8, 8))[0]
            if not value:
                break
            if not value & (1 << 63) and string_at(pe, value + 2) == symbol:
                pe.at_rva(first + item * 8, 8)
                matches.append(first + item * 8)
        else:
            raise ValueError("Steam import table has no bounded terminator")
    if not terminated or len(matches) != 1:
        raise ValueError("Steam Init import missing, ambiguous, or malformed")
    return matches[0]


def action(command: str) -> str:
    return '"' + command.replace('\\', '\\\\').replace('"', '\\"') + '"'


def ownership_guards(target: TraceTarget) -> list[str]:
    # Symbol resolution and pointer/byte comparisons are debugger reads, not
    # GetProcAddress or Steam calls in the target. No interface is dereferenced.
    return [
        require(f"$vvalid(@$t1+0x{target.iat_rva:x},8)", "steam_init_iat_unreadable"),
        require(f"@$t11 == {target.api_module}!{target.api_name}", "steam_init_export_changed"),
        require(f"poi(@$t1+0x{target.iat_rva:x}) == @$t11", "steam_init_iat_not_original_export"),
        *byte_guards("@$t11", target.export_bytes, "steam_init_export"),
    ]


def command_texts(pe: PeImage, boot: Path, entered: Path, returned: Path, stopped: Path,
                  target: TraceTarget = TraceTarget()) -> tuple[str, ...]:
    entry = pe.at_rva(pe.entry_rva, 17, code=True)
    # IDs 0/1/2 and debugger-local temporary registers belong exclusively to
    # this dedicated session. $t14 is phase (0 awaiting call, 1 awaiting return).
    # $t8/$t10 retain the first call site's stack/thread identity permanently.
    initial_checks = [
        require("@$ptrsize == 8", "wrong_pointer_size"),
        optional_breakpoint(0, 6), optional_breakpoint(1, 7), optional_breakpoint(2, 15),
        require("(@$t6 == 0) and (@$t7 == 0) and (@$t15 == 0)", "owned_breakpoint_ids_occupied"),
        f"r @$t1 = {target.module}",
        require(f"$vvalid(@$t1,0x{pe.nt+24+64:x})", "image_header_unreadable"),
        require("wo(@$t1) == 0x5a4d", "dos_signature_mismatch"),
        require(f"dwo(@$t1+0x3c) == 0x{pe.nt:x}", "pe_header_offset_mismatch"),
        require(f"dwo(@$t1+0x{pe.nt:x}) == 0x4550", "pe_signature_mismatch"),
        require(f"wo(@$t1+0x{pe.nt+4:x}) == 0x8664", "machine_mismatch"),
        require(f"wo(@$t1+0x{pe.nt+24:x}) == 0x20b", "optional_header_mismatch"),
        require(f"dwo(@$t1+0x{pe.nt+24+56:x}) == 0x{pe.image_size:x}", "image_size_mismatch"),
        require(f"dwo(@$t1+0x{pe.nt+24+16:x}) == 0x{pe.entry_rva:x}", "entry_rva_mismatch"),
        f"r @$t2 = @$t1+0x{pe.entry_rva:x}",
        f"r @$t3 = @$t1+0x{target.call_rva:x}",
        f"r @$t9 = @$t1+0x{target.return_rva:x}",
        require("@$exentry == @$t2", "wrong_process_entry"),
        *byte_guards("@$t2", entry, "entry"),
        *byte_guards("@$t3", target.call_bytes, "steam_init_call"),
        *byte_guards("@$t9", target.return_bytes, "steam_init_return"),
        "r @$t4 = 1",
        f"bp0 /1 @$exentry {breakpoint_action(boot)}",
        require("@$bp0 == @$t2", "bootstrap_breakpoint_not_installed"),
        notice("MGS_STEAM_INIT_BOOTSTRAP_ARMED"),
        "r @$t0 = 1",
    ]
    initial = [
        notice("MGS original Steam Init observation: temporary software bootstrap then hardware call/return"),
        ".expr /s masm",
        "r @$t0 = 0", "r @$t4 = 0", "r @$t5 = 0", "r @$t12 = 0",
        "r @$t13 = 0", "r @$t14 = 0",
        ".catch { " + "; ".join(initial_checks) + " }",
        f'.if (@$t0 == 0) {{ {call_script(stopped)}; }} .else {{ g }}',
    ]
    skip_call = (
        '.if (@$t13 < 8) { r @$t13 = @$t13+1; '
        '.printf "MGS_STEAM_INIT_OTHER_CALL_SKIPPED tid=0x%x rsp=%p\\n", @$tid, @rsp }; gc'
    )
    repeated = notice("MGS_ABORT repeated_steam_init_call_frame") + "; " + call_script(stopped) + ";"
    call_action = action(
        f'.if (@$t14 == 0) {{ {call_script(entered)}; }} .else {{ '
        f'.if ((@$tid == @$t10) and (@rsp == @$t8)) {{ {repeated} }} .else {{ {skip_call} }} }}'
    )
    boot_checks = [
        require("@rip == @$t2", "unexpected_bootstrap_address"),
        optional_breakpoint(0, 6), '.if (@$t6 != 0) { bc 0 }', "r @$t4 = 0",
        *byte_guards("@$t2", entry, "restored_entry"),
        *byte_guards("@$t3", target.call_bytes, "steam_init_call"),
        *byte_guards("@$t9", target.return_bytes, "steam_init_return"),
        f"r @$t11 = {target.api_module}!{target.api_name}",
        require(f"@$t11 == ({target.api_module}+0x{target.export_rva:x})", "unexpected_steam_init_export_rva"),
        *ownership_guards(target),
        optional_breakpoint(1, 7), optional_breakpoint(2, 15),
        require("(@$t7 == 0) and (@$t15 == 0)", "hardware_breakpoint_ids_occupied"),
        require("(@dr7 & 0xff) == 0", "occupied_debug_registers"),
        "r @$t5 = 1", f"ba1 e1 @$t3 {call_action}",
        require("@$bp1 == @$t3", "call_breakpoint_not_installed"),
        notice("MGS_STEAM_INIT_CALL_ARMED"), "r @$t0 = 1",
    ]
    bootstrap = ["r @$t0 = 0", ".catch { " + "; ".join(boot_checks) + " }",
                 f'.if (@$t0 == 0) {{ {call_script(stopped)}; }} .else {{ g }}']
    frame = "(@$t14 == 1) and (@$tid == @$t10) and (@rsp == @$t8)"
    skipped = (
        '.if (@$t13 < 8) { r @$t13 = @$t13+1; '
        '.printf "MGS_STEAM_INIT_RETURN_FRAME_SKIPPED tid=0x%x rsp=%p expected_rsp=%p\\n", '
        '@$tid, @rsp, @$t8 }; gc'
    )
    return_action = action(f'.if ({frame}) {{ {call_script(returned)}; }} .else {{ {skipped} }}')
    call_checks = [
        require("(@rip == @$t3) and (@$t14 == 0)", "unexpected_init_call_or_phase"),
        *byte_guards("@$t3", target.call_bytes, "steam_init_call_hit"),
        *byte_guards("@$t9", target.return_bytes, "steam_init_return"),
        *ownership_guards(target),
        require("@rsp > 0", "invalid_call_stack"),
        "r @$t8 = @rsp", "r @$t10 = @$tid",
        optional_breakpoint(2, 15), require("@$t15 == 0", "return_breakpoint_id_occupied"),
        "r @$t12 = 1", f"ba2 e1 @$t9 {return_action}",
        require("@$bp2 == @$t9", "return_breakpoint_not_installed"),
        "r @$t14 = 1",
        '.printf "MGS_STEAM_INIT_CALL tid=0x%x call=%p call_rsp=%p original_export=%p\\n", '
        '@$tid, @rip, @$t8, @$t11',
        notice("MGS_STEAM_INIT_RETURN_ARMED"), "r @$t0 = 1",
    ]
    called = ["r @$t0 = 0", ".catch { " + "; ".join(call_checks) + " }",
              f'.if (@$t0 == 0) {{ {call_script(stopped)}; }} .else {{ g }}']
    return_checks = [
        require("@rip == @$t9", "unexpected_init_return_address"), require(frame, "unexpected_init_return_frame"),
        *byte_guards("@$t3", target.call_bytes, "steam_init_call_at_return"),
        *byte_guards("@$t9", target.return_bytes, "steam_init_return_hit"), *ownership_guards(target),
        '.printf "MGS_STEAM_INIT_RETURN tid=0x%x call_rsp=%p return_rsp=%p al=%u\\n", '
        '@$tid, @$t8, @rsp, @al', "r @$t0 = 1",
    ]
    matched = ["r @$t0 = 0", ".catch { " + "; ".join(return_checks) + " }",
               '.if (@$t0 == 0) { ' + notice("MGS_ABORT steam_init_return_capture_failed") + " }",
               call_script(stopped)]
    cleanup = [
        '.catch { .if (@$t4 != 0) { bc 0; r @$t4 = 0 } }',
        '.catch { .if (@$t5 != 0) { bc 1; r @$t5 = 0 } }',
        '.catch { .if (@$t12 != 0) { bc 2; r @$t12 = 0 } }',
        ".detach /h", "q",
    ]
    return tuple("\n".join(lines) + "\n" for lines in (initial, bootstrap, called, matched, cleanup))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fixture", action="store_true", help="Accept only the separate original steam_init_fixture.exe and its inert API DLL")
    args = parser.parse_args()
    try:
        executable = args.executable.resolve(strict=True)
        data = executable.read_bytes()
        if not args.fixture and (executable.name.lower() != "mgsvtpp.exe" or hashlib.sha256(data).hexdigest() != GAME_SHA256):
            raise ValueError("Executable fingerprint does not match the independently inspected game")
        pe = PeImage(data)
        if args.fixture and executable.name.lower() != "steam_init_fixture.exe":
            raise ValueError("Fixture mode accepts only steam_init_fixture.exe")
        steam_path = executable.parent / ("steam_init_fixture_api.dll" if args.fixture else "steam_api64.dll")
        steam_data = steam_path.read_bytes()
        steam = PeImage(steam_data)
        if not args.fixture and hashlib.sha256(steam_data).hexdigest() != STEAM_SHA256:
            raise ValueError("Shipped Steam DLL fingerprint mismatch")
        target = TraceTarget()
        if args.fixture:
            call_rva, return_rva = pe.export(b"FixtureBeforeCall"), pe.export(b"FixtureAfterCall")
            call_bytes = pe.at_rva(call_rva, 6, code=True)
            iat_rva = unique_init_import(pe, b"steam_init_fixture_api.dll", b"FixtureSteamInit")
            if return_rva != call_rva + 6 or call_bytes[:2] != b"\xff\x15" or call_rva + 6 + struct.unpack("<i", call_bytes[2:])[0] != iat_rva:
                raise ValueError("Fixture labels must bound one direct RIP-relative IAT call")
            if pe.at_rva(return_rva, 2, code=True) != RETURN_BYTES:
                raise ValueError("Fixture return must begin with test al,al")
            export_rva = steam.export(b"FixtureSteamInit")
            target = TraceTarget("steam_init_fixture", "steam_init_fixture_api", "FixtureSteamInit",
                                 call_rva, return_rva, iat_rva, export_rva,
                                 call_bytes, RETURN_BYTES, steam.at_rva(export_rva, 17, code=True))
        if not args.fixture and unique_init_import(pe) != INIT_IAT_RVA:
            raise ValueError("Steam Init import slot changed")
        if pe.at_rva(target.call_rva, len(target.call_bytes), code=True) != target.call_bytes or pe.at_rva(target.return_rva, len(target.return_bytes), code=True) != target.return_bytes:
            raise ValueError("Independently decoded Init call or return bytes changed")
        if steam.export(target.api_name.encode("ascii")) != target.export_rva or steam.at_rva(target.export_rva, len(target.export_bytes), code=True) != target.export_bytes:
            raise ValueError("Shipped Steam Init export changed")
        output = args.output.resolve()
        paths = [output] + [output.with_name(output.stem + suffix + ".cmd") for suffix in (".bootstrap", ".call", ".return", ".stop")]
        if len(set(paths)) != len(paths) or any(p in (executable, steam_path.resolve()) for p in paths):
            raise ValueError("Output paths overlap inputs or each other")
        for path in paths:
            call_script(path)  # Validate every nested command path before writing.
        texts = command_texts(pe, *paths[1:], target=target)
        output.parent.mkdir(parents=True, exist_ok=True)
        for index in (4, 3, 2, 1, 0):
            paths[index].write_text(texts[index], encoding="ascii")
        print(f"Created {output} and four sibling command files")
        print("Generation only. No process launched or modified; execution requires a separate bounded observer.")
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
