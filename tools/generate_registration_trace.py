"""Generate original CDB scripts for one natural registration-entry observation.

Does not launch or attach. Use Microsoft CDB with -pd -hd -sins -noshell and an
external observation deadline; do not use -g. The initial script sets one verified
one-shot SOFTWARE breakpoint at the PE entry point: Windows resets hardware
breakpoint contexts after its initial loader break. That temporary in-memory code
byte is restored by CDB before the bootstrap arms a hardware execution breakpoint.
Thus the observation is not entirely hardware-only or free of process writes;
there are no on-disk executable changes, Lua calls, or Lua-state dereferences.

The output script and its sibling command files must stay together. Runtime
script errors and byte mismatches detach; a no-hit run still needs the caller's
tested deadline/cleanup. Optional --capture-return keeps one hardware breakpoint
until the captured thread/frame returns. Neither observation proves VM lifetime.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

GAME_SHA256 = "085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45"
REGISTRAR_RVA = 0xA1B2E0
PROLOGUE = bytes.fromhex("40 55 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00")
REGISTRAR_RETURN_RVA = 0x1A12F54
REGISTRAR_RETURN_BYTES = bytes.fromhex("85 C0")


class PeImage:
    """Small file-only PE reader for bounded entry/export verification."""

    def __init__(self, data: bytes):
        self.data = data
        if self.read(0, 2) != b"MZ":
            raise ValueError("Executable has no DOS signature")
        self.nt = self.u32(0x3C)
        if self.read(self.nt, 4) != b"PE\0\0" or self.u16(self.nt + 4) != 0x8664:
            raise ValueError("Executable is not an AMD64 PE image")
        count = self.u16(self.nt + 6)
        optional_size = self.u16(self.nt + 20)
        optional = self.nt + 24
        if not 1 <= count <= 96 or optional_size < 120 or self.u16(optional) != 0x20B:
            raise ValueError("Unsupported PE section count or optional header")
        self.read(optional, optional_size)
        self.entry_rva = self.u32(optional + 16)
        self.image_size = self.u32(optional + 56)
        self.header_size = self.u32(optional + 60)
        self.export_rva = self.u32(optional + 112) if self.u32(optional + 108) else 0
        self.export_size = self.u32(optional + 116) if self.export_rva else 0
        if not 0 < self.entry_rva < self.image_size or self.header_size > len(data):
            raise ValueError("Invalid PE entry or image/header size")
        self.sections = []
        for index in range(count):
            row = optional + optional_size + index * 40
            self.read(row, 40)
            virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, row + 8)
            flags = self.u32(row + 36)
            if rva + max(virtual_size, raw_size) > self.image_size:
                raise ValueError("Section exceeds image size")
            self.read(raw, raw_size)
            self.sections.append((rva, raw_size, raw, flags))

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise ValueError("PE read exceeds executable file")
        return self.data[offset:offset + size]

    def u16(self, offset: int) -> int:
        return struct.unpack("<H", self.read(offset, 2))[0]

    def u32(self, offset: int) -> int:
        return struct.unpack("<I", self.read(offset, 4))[0]

    def at_rva(self, rva: int, size: int, *, code: bool = False) -> bytes:
        matches = [section for section in self.sections
                   if section[0] <= rva and rva + size <= section[0] + section[1]]
        if len(matches) != 1:
            raise ValueError("RVA is not uniquely backed by a file section")
        start, _, raw, flags = matches[0]
        if code and not flags & 0x20000000:
            raise ValueError("Breakpoint target is not in an executable section")
        return self.read(raw + rva - start, size)

    def rva_u32(self, rva: int) -> int:
        return struct.unpack("<I", self.at_rva(rva, 4))[0]

    def export(self, name: bytes) -> int:
        if not self.export_rva or self.export_size < 40:
            raise ValueError("Fixture has no export directory")
        directory = self.at_rva(self.export_rva, 40)
        functions, names, function_table, name_table, ordinal_table = struct.unpack_from("<IIIII", directory, 20)
        if not 0 < names <= functions <= 65536:
            raise ValueError("Fixture export table has invalid bounds")
        matches = []
        for index in range(names):
            name_rva = self.rva_u32(name_table + index * 4)
            candidate = bytearray()
            for offset in range(256):
                byte = self.at_rva(name_rva + offset, 1)[0]
                if not byte:
                    break
                candidate.append(byte)
            else:
                raise ValueError("Fixture export name is not bounded")
            if candidate != name:
                continue
            ordinal = struct.unpack("<H", self.at_rva(ordinal_table + index * 2, 2))[0]
            if ordinal >= functions:
                raise ValueError("Fixture export ordinal is invalid")
            target = self.rva_u32(function_table + ordinal * 4)
            if self.export_rva <= target < self.export_rva + self.export_size:
                raise ValueError("Fixture registrar cannot be a forwarded export")
            matches.append(target)
        if len(matches) != 1:
            raise ValueError("Fixture registrar export is missing or ambiguous")
        return matches[0]


def notice(message: str) -> str:
    # .printf has explicit string termination; .echo text can consume punctuation.
    return f'.printf "{message}\\n"'


def require(condition: str, reason: str) -> str:
    # Always used inside .catch, so .leave exits the enclosing failed setup.
    return f'.if (not({condition})) {{ {notice("MGS_ABORT " + reason)}; .leave }}'


def byte_guards(target: str, expected: bytes, label: str) -> list[str]:
    # MASM has bitwise AND, not C++ short-circuit &&. Check validity separately.
    condition = " and ".join(f"(by({target}+0x{i:x}) == 0x{byte:02x})"
                             for i, byte in enumerate(expected))
    return [require(f"$vvalid({target},0x{len(expected):x})", label + "_unreadable"),
            require(condition, label + "_bytes_mismatch")]


def call_script(path: Path) -> str:
    name = str(path.resolve())
    # Prevent command/alias injection and ambiguous command-file encoding.
    if not name.isascii() or any(char in name for char in '\";\r\n${}'):
        raise ValueError("CDB command-file paths must be ASCII without quotes, semicolons, dollar signs, or braces")
    # Keep commands on separate lines. The condensed $$>a< form appends a
    # semicolon after qd, which this installed CDB rejects inside a hit action.
    return f'$$< {name}'


def breakpoint_action(path: Path) -> str:
    # Breakpoint CommandString interprets C control escapes; preserve Windows
    # path backslashes before escaping the inner command-file quotes.
    return '"' + call_script(path).replace('\\', '\\\\').replace('"', '\\"') + '"'


def cleanup() -> str:
    # $t4/$t5 record ownership only after verifying that IDs 0/1 are free.
    # /1 may have already removed an ID by the time its action executes.
    return "; ".join([
        '.catch { .if (@$t4 != 0) { bc 0 } }',
        '.catch { .if (@$t5 != 0) { bc 1 } }',
    ])


def optional_breakpoint(identifier: int, temporary: int) -> str:
    # This installed CDB throws for absent $bpN, despite current documentation
    # describing zero. Isolate that expected error from the outer setup catch.
    return f'r @$t{temporary} = 0; .catch {{ r @$t{temporary} = @$bp{identifier} }}'


def return_frame_condition(module: str) -> str:
    condition = "(@$tid == @$t10) and (@rsp == (@$t8+8))"
    if module == "mgsvtpp":
        condition += " and (@rbx == @$t9)"
    return condition


def return_action(path: Path, module: str) -> str:
    # A persistent breakpoint is required: another thread or recursive frame
    # can reach this address before the frame whose entry we captured returns.
    # gc resumes those wrong-frame hits without deleting the owned breakpoint.
    skipped = (
        '.if (@$t13 < 8) { r @$t13 = @$t13+1; '
        '.printf "MGS_RETURN_FRAME_SKIPPED tid=0x%x expected_tid=0x%x rsp=%p expected_rsp=%p\\n", '
        '@$tid, @$t10, @rsp, (@$t8+8) }; gc'
    )
    command = f'.if ({return_frame_condition(module)}) {{ {call_script(path)}; }} .else {{ {skipped} }}'
    return '"' + command.replace('\\', '\\\\').replace('"', '\\"') + '"'


def return_cleanup() -> str:
    return '.catch { .if (@$t12 != 0) { bc 2; r @$t12 = 0 } }'


def command_texts(module: str, pe: PeImage, target_rva: int, expected: bytes,
                  bootstrap: Path, hit: Path,
                  capture_return: Path | None = None) -> tuple[str, ...]:
    entry = pe.at_rva(pe.entry_rva, len(PROLOGUE), code=True)
    initial_checks = [
        require("@$ptrsize == 8", "wrong_pointer_size"),
        optional_breakpoint(0, 6),
        optional_breakpoint(1, 7),
        require("(@$t6 == 0) and (@$t7 == 0)", "owned_breakpoint_ids_occupied"),
        f"r @$t1 = {module}",
        require(f"$vvalid(@$t1,0x{pe.nt + 24 + 64:x})", "image_header_unreadable"),
        require("wo(@$t1) == 0x5a4d", "dos_signature_mismatch"),
        require(f"dwo(@$t1+0x3c) == 0x{pe.nt:x}", "pe_header_offset_mismatch"),
        require(f"dwo(@$t1+0x{pe.nt:x}) == 0x4550", "pe_signature_mismatch"),
        require(f"wo(@$t1+0x{pe.nt + 4:x}) == 0x8664", "machine_mismatch"),
        require(f"wo(@$t1+0x{pe.nt + 24:x}) == 0x20b", "optional_header_mismatch"),
        require(f"dwo(@$t1+0x{pe.nt + 24 + 56:x}) == 0x{pe.image_size:x}", "image_size_mismatch"),
        require(f"dwo(@$t1+0x{pe.nt + 24 + 16:x}) == 0x{pe.entry_rva:x}", "entry_rva_mismatch"),
        f"r @$t2 = @$t1+0x{pe.entry_rva:x}",
        f"r @$t3 = @$t1+0x{target_rva:x}",
        require("@$exentry == @$t2", "wrong_process_entry"),
        *byte_guards("@$t2", entry, "entry"),
        *byte_guards("@$t3", expected, "registrar"),
        "r @$t4 = 1",
        f"bp0 /1 @$exentry {breakpoint_action(bootstrap)}",
        require("@$bp0 == @$t2", "bootstrap_breakpoint_not_installed"),
        notice("MGS_BOOTSTRAP_SOFTWARE_BREAKPOINT_ARMED"),
        "r @$t0 = 1",
    ]
    if capture_return is not None:
        initial_checks[4:4] = [
            optional_breakpoint(2, 14),
            require("@$t14 == 0", "return_breakpoint_id_occupied"),
        ]
    initial = [
        notice("MGS original observation uses one temporary software bootstrap then hardware entry observation"),
        ".expr /s masm",
        "r @$t0 = 0", "r @$t4 = 0", "r @$t5 = 0",
        ".catch { " + "; ".join(initial_checks) + " }",
        f'.if (@$t0 == 0) {{ {cleanup()}; qd }} .else {{ g }}',
    ]
    if capture_return is not None:
        initial.insert(5, "r @$t12 = 0")
    boot_checks = [
        require("@rip == @$t2", "unexpected_bootstrap_address"),
        # Explicit conditional removal also makes the restored entry bytes testable.
        optional_breakpoint(0, 6),
        '.if (@$t6 != 0) { bc 0 }',
        "r @$t4 = 0",
        *byte_guards("@$t2", entry, "restored_entry"),
        optional_breakpoint(1, 7),
        require("@$t7 == 0", "hardware_breakpoint_id_occupied"),
        *byte_guards("@$t3", expected, "registrar"),
        require("(@dr7 & 0xff) == 0", "occupied_debug_registers"),
        "r @$t5 = 1",
        f"ba1 e1 /1 @$t3 {breakpoint_action(hit)}",
        require("@$bp1 == @$t3", "hardware_breakpoint_not_installed"),
        notice("MGS_EXPECTED_BYTES_ACCEPTED"),
        notice("MGS_HARDWARE_REGISTRAR_ARMED"),
        "r @$t0 = 1",
    ]
    boot = [
        "r @$t0 = 0",
        ".catch { " + "; ".join(boot_checks) + " }",
        f'.if (@$t0 == 0) {{ {cleanup()}; qd }} .else {{ g }}',
    ]
    hit_checks = [
        require("@rip == @$t3", "unexpected_registration_address"),
        *byte_guards("@$t3", expected, "registrar_hit"),
        require("$vvalid(@rsp,8)", "return_address_unreadable"),
        '.printf "MGS_REGISTRATION_ENTRY tid=0x%x state_opaque=%p caller=%p entry=%p\\n", '
        '@$tid, @rcx, poi(@rsp), @rip',
        "r @$t0 = 1",
    ]
    if module == "mgsvtpp":
        # Read only known image data at the already paused natural entry. No
        # access through the opaque Lua argument and no game function calls.
        hit_checks.extend([
            require("$vvalid(@$t1+0x208e0e8,8)", "callback_import_unreadable"),
            require("$vvalid(@$t1+0x208e130,8)", "init_import_unreadable"),
            require("$vvalid(@$t1+0x2c16920,8)", "vars_pointer_unreadable"),
            '.printf "MGS_REGISTRATION_DATA image=%p callbacks=%p init=%p vars=%p\\n", '
            '@$t1, poi(@$t1+0x208e0e8), poi(@$t1+0x208e130), poi(@$t1+0x2c16920)',
            'ln poi(@$t1+0x208e0e8)',
            'ln poi(@$t1+0x208e130)',
        ])
    hit_text = [
        # Both /1 breakpoints are removed by CDB before this action. Do not issue
        # bc for an absent breakpoint from its own command program.
        "r @$t0 = 0", "r @$t5 = 0",
        ".catch { " + "; ".join(hit_checks) + " }",
        '.if (@$t0 == 0) { ' + notice("MGS_ABORT registration_capture_failed") + " }",
        ".detach /h",
        "q",
    ]
    if capture_return is not None:
        # Keep all entry-only text generation above unchanged. Return mode only
        # changes the capture action and adds the extra initial ID/ownership gate.
        hit_checks.remove("r @$t0 = 1")
        hit_checks.extend([
            "r @$t8 = @rsp", "r @$t9 = @rcx", "r @$t10 = @$tid",
            "r @$t11 = poi(@rsp)",
            # MASM comparisons are signed; keep the upper bound positive.
            require("(@$t8 > 0) and (@$t8 <= 0x7ffffffffffffff7)", "entry_stack_overflow"),
            require(f"(@$t11 >= @$t1) and (@$t11 < (@$t1+0x{pe.image_size:x}))", "return_outside_image"),
            optional_breakpoint(2, 14),
            require("@$t14 == 0", "return_breakpoint_id_occupied"),
        ])
        if module == "mgsvtpp":
            hit_checks.extend([
                require(f"@$t11 == (@$t1+0x{REGISTRAR_RETURN_RVA:x})", "unexpected_registrar_return"),
                *byte_guards("@$t11", REGISTRAR_RETURN_BYTES, "registrar_return"),
            ])
        else:
            hit_checks.append(require("$vvalid(@$t11,2)", "fixture_return_unreadable"))
        hit_checks.extend([
            "r @$t13 = 0",
            "r @$t12 = 1",
            f"ba2 e1 @$t11 {return_action(capture_return, module)}",
            require("@$bp2 == @$t11", "return_breakpoint_not_installed"),
            notice("MGS_HARDWARE_REGISTRAR_RETURN_ARMED"),
            "r @$t0 = 1",
        ])
        # The return script handles setup failure too: its address/frame checks
        # reject this still-at-entry event, then execute the proven separate
        # .detach /h and q lines. Keep $$< uncondensed in both action paths.
        hit_text = [
            "r @$t0 = 0", "r @$t5 = 0",
            ".catch { " + "; ".join(hit_checks) + " }",
            '.if (@$t0 == 0) { ' + notice("MGS_ABORT return_capture_setup_failed") + " }",
            f'.if (@$t0 == 0) {{ {call_script(capture_return)}; }} .else {{ g }}',
        ]
        return_checks = [
            require("@rip == @$t11", "unexpected_return_address"),
            require(return_frame_condition(module), "unexpected_return_frame"),
        ]
        if module == "mgsvtpp":
            return_checks.extend(byte_guards("@$t11", REGISTRAR_RETURN_BYTES, "registrar_return_hit"))
        return_checks.extend([
            '.printf "MGS_REGISTRATION_RETURN tid=0x%x state_opaque=%p entry_rsp=%p return_rsp=%p return=%p eax=%u\\n", '
            '@$tid, @$t9, @$t8, @rsp, @rip, @eax',
            "r @$t0 = 1",
        ])
        returned = [
            "r @$t0 = 0",
            ".catch { " + "; ".join(return_checks) + " }",
            return_cleanup(),
            '.if (@$t0 == 0) { ' + notice("MGS_ABORT return_capture_failed") + " }",
            ".detach /h",
            "q",
        ]
        return tuple("\n".join(lines) + "\n" for lines in (initial, boot, hit_text, returned))
    return tuple("\n".join(lines) + "\n" for lines in (initial, boot, hit_text))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fixture", action="store_true")
    parser.add_argument("--capture-return", action="store_true",
                        help="Also observe EAX at the matching natural thread/frame return")
    args = parser.parse_args()
    try:
        exe = args.executable.resolve(strict=True)
        data = exe.read_bytes()
        pe = PeImage(data)
        if args.fixture:
            if exe.name.lower() != "registration_fixture.exe":
                raise ValueError("Fixture mode accepts only registration_fixture.exe")
            module = "registration_fixture"
            target_rva = pe.export(b"FixtureRegistrar")
            expected = pe.at_rva(target_rva, len(PROLOGUE), code=True)
        else:
            if exe.name.lower() != "mgsvtpp.exe" or hashlib.sha256(data).hexdigest() != GAME_SHA256:
                raise ValueError("Executable fingerprint is not the independently inspected MGSV build")
            module, target_rva, expected = "mgsvtpp", REGISTRAR_RVA, PROLOGUE
            if pe.at_rva(target_rva, len(expected), code=True) != expected:
                raise ValueError("Independently inspected registrar bytes do not match")
            if args.capture_return and pe.at_rva(REGISTRAR_RETURN_RVA, len(REGISTRAR_RETURN_BYTES), code=True) != REGISTRAR_RETURN_BYTES:
                raise ValueError("Independently inspected registrar return bytes do not match")
        output = args.output.resolve()
        bootstrap = output.with_name(output.stem + ".bootstrap.cmd")
        hit = output.with_name(output.stem + ".hit.cmd")
        returned = output.with_name(output.stem + ".return.cmd") if args.capture_return else None
        paths = [output, bootstrap, hit] + ([returned] if returned is not None else [])
        if len(set(paths)) != len(paths) or exe in paths:
            raise ValueError("Command output paths overlap each other or the executable")
        for path in paths:
            call_script(path)
        texts = command_texts(module, pe, target_rva, expected, bootstrap, hit, returned)
        output.parent.mkdir(parents=True, exist_ok=True)
        # Write referenced files before the initial script is made available.
        for path, text in reversed(list(zip(paths, texts))):
            path.write_text(text, encoding="ascii")
        if returned is None:
            print(f"Created {output}, {bootstrap}, and {hit}")
        else:
            print("Created " + ", ".join(str(path) for path in paths))
        print("No process launched or modified. Running this script uses one temporary software entry breakpoint.")
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
