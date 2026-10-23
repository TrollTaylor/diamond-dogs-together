#!/usr/bin/env python3
"""Independently decode MGSV network Lua bindings from the installed PE only.

Consumes this project's hash-bound candidate report. Capstone is a disassembler,
not an execution engine; no game code, process memory or save data is accessed.
"""
from __future__ import annotations

import argparse
from bisect import bisect_right
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any

PROJECT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT / "work" / "python"))
try:
    import capstone
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RIP
except ImportError as error:
    raise SystemExit("Capstone is required; make the project's work/python dependency readable or install capstone. " + str(error))

try:
    from .inspect_game import DEFAULT_GAME_DIR, PEError, PEFile
    from .find_lua_bindings import exception_ranges, section_at_rva
except ImportError:
    from inspect_game import DEFAULT_GAME_DIR, PEError, PEFile
    from find_lua_bindings import exception_ranges, section_at_rva

NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]{0,127}\Z")
VOLATILE = ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11")
ALIASES = {"eax": "rax", "ecx": "rcx", "edx": "rdx", "ebx": "rbx", "esp": "rsp", "ebp": "rbp", "esi": "rsi", "edi": "rdi"}
ALIASES.update({f"r{i}d": f"r{i}" for i in range(8, 16)})


class Analysis:
    def __init__(self, data: bytes) -> None:
        self.pe = PEFile(data)
        if self.pe.bits != 64 or self.pe.machine != 0x8664:
            raise PEError("AMD64 PE required")
        self.disassembler = Cs(CS_ARCH_X86, CS_MODE_64)
        self.disassembler.detail = True
        self.ranges, self.starts, _ = exception_ranges(self.pe)
        self.cache: dict[int, dict[str, Any]] = {}

    def instructions(self, begin: int, size: int) -> list[Any]:
        return list(self.disassembler.disasm(self.pe.rva_read(begin, size), self.pe.image_base + begin))

    def name_at(self, value: tuple[str, int] | None) -> str | None:
        if value is None or value[0] != "image_va":
            return None
        try:
            name = self.pe.cstring(value[1] - self.pe.image_base)
            return name if NAME.fullmatch(name) else None
        except PEError:
            return None

    def function(self, target: int) -> dict[str, Any]:
        if target in self.cache:
            return self.cache[target]
        index = bisect_right(self.starts, target) - 1
        known_range = index >= 0 and self.ranges[index][0] <= target < self.ranges[index][1]
        if known_range:
            begin, end, unwind = self.ranges[index]
            if begin != target:
                raise PEError(f"target {target:#x} is inside another declared function, not at its start")
            size = min(end - begin, 4096)
            instructions = self.instructions(begin, size)
            boundary = {"kind": "PE_exception_range", "begin_rva": hex(begin), "end_rva_exclusive": hex(end), "unwind_info_rva": hex(unwind)}
        else:
            begin = target
            instructions = []
            for instruction in self.instructions(begin, 128):
                instructions.append(instruction)
                if instruction.mnemonic in ("ret", "repz ret", "jmp", "int3"):
                    break
            if not instructions or instructions[-1].mnemonic == "int3":
                raise PEError(f"bounded leaf/thunk decode did not establish a terminator at {target:#x}")
            size = sum(instruction.size for instruction in instructions)
            end = begin + size
            boundary = {"kind": "bounded_leaf_or_tail_thunk_decode", "begin_rva": hex(begin), "end_rva_exclusive": hex(end), "exception_range_absent": True}
        rows = []
        calls = []
        for instruction in instructions:
            row = {
                "rva": hex(instruction.address - self.pe.image_base), "bytes_hex": instruction.bytes.hex(" "),
                "mnemonic": instruction.mnemonic, "operands": instruction.op_str,
            }
            if instruction.mnemonic in ("call", "jmp") and instruction.operands[0].type == X86_OP_IMM:
                target_rva = instruction.operands[0].imm - self.pe.image_base
                row["direct_target_rva"] = hex(target_rva)
                if instruction.mnemonic == "call":
                    calls.append({"call_rva": row["rva"], "target_rva": hex(target_rva)})
            rows.append(row)
        decoded_size = sum(instruction.size for instruction in instructions)
        result = {
            "entry_rva": hex(target), "boundary": boundary, "decoded_bytes": decoded_size,
            "complete_declared_range_decoded": decoded_size == size and (not known_range or size == end - begin),
            "decoded_bytes_sha256": hashlib.sha256(self.pe.rva_read(begin, decoded_size)).hexdigest().upper(),
            "entry_bytes_first_17_hex": self.pe.rva_read(begin, min(17, decoded_size)).hex(" "),
            "direct_calls": calls, "instructions": rows, "runtime_verified": False,
        }
        self.cache[target] = result
        return result

    def trace_registration(self, begin: int, end: int) -> dict[str, Any]:
        """Bounded straight-line symbolic tracing of LEA/MOV-built stack pairs."""
        registers: dict[str, tuple[str, int] | None] = {"rcx": ("incoming_argument", 0), "rsp": ("stack", 0)}
        stack: dict[int, tuple[tuple[str, int] | None, int]] = {}
        unsupported = []

        def regname(instruction: Any, operand: Any) -> str:
            name = instruction.reg_name(operand.reg)
            return ALIASES.get(name, name)

        def address(instruction: Any, operand: Any) -> tuple[str, int] | None:
            memory = operand.mem
            if memory.index:
                return None
            if memory.base == X86_REG_RIP:
                return ("image_va", instruction.address + instruction.size + memory.disp)
            base = registers.get(ALIASES.get(instruction.reg_name(memory.base), instruction.reg_name(memory.base)))
            if base and base[0] == "stack":
                return ("stack", base[1] + memory.disp)
            return None

        def value(instruction: Any, operand: Any) -> tuple[str, int] | None:
            if operand.type == X86_OP_IMM:
                return ("constant", operand.imm)
            if operand.type == X86_OP_REG:
                return registers.get(regname(instruction, operand))
            return None

        for instruction in self.instructions(begin, end - begin):
            operands = instruction.operands
            mnemonic = instruction.mnemonic
            if mnemonic == "push":
                current = registers.get("rsp")
                registers["rsp"] = ("stack", current[1] - 8) if current else None
            elif mnemonic in ("sub", "add") and operands[0].type == X86_OP_REG and regname(instruction, operands[0]) == "rsp" and operands[1].type == X86_OP_IMM:
                current = registers.get("rsp")
                delta = operands[1].imm * (-1 if mnemonic == "sub" else 1)
                registers["rsp"] = ("stack", current[1] + delta) if current else None
            elif mnemonic == "lea" and operands[0].type == X86_OP_REG and operands[1].type == X86_OP_MEM:
                registers[regname(instruction, operands[0])] = address(instruction, operands[1])
            elif mnemonic == "mov" and operands[0].type == X86_OP_REG:
                registers[regname(instruction, operands[0])] = value(instruction, operands[1])
            elif mnemonic == "mov" and operands[0].type == X86_OP_MEM:
                destination = address(instruction, operands[0])
                if destination and destination[0] == "stack" and operands[0].size == 8:
                    stack[destination[1]] = (value(instruction, operands[1]), instruction.address - self.pe.image_base)
            elif mnemonic == "xor" and operands[0].type == X86_OP_REG:
                registers[regname(instruction, operands[0])] = ("constant", 0) if operands[1].type == X86_OP_REG and operands[0].reg == operands[1].reg else None
            elif mnemonic == "call":
                module_name = self.name_at(registers.get("rdx"))
                table = registers.get("r8")
                if module_name == "TppNetworkUtil" and table and table[0] == "stack" and operands[0].type == X86_OP_IMM:
                    entries = []
                    terminated = False
                    for index in range(256):
                        name_value, name_store = stack.get(table[1] + index * 16, (None, None))
                        function_value, function_store = stack.get(table[1] + index * 16 + 8, (None, None))
                        if name_value == ("constant", 0) and function_value == ("constant", 0):
                            terminated = True
                            break
                        name = self.name_at(name_value)
                        if name is None or function_value is None or function_value[0] != "image_va":
                            raise PEError("registration stack table contains an unresolved pair")
                        function_rva = function_value[1] - self.pe.image_base
                        section = section_at_rva(self.pe, function_rva)
                        if section is None or not section["characteristics"] & 0x20000000:
                            raise PEError("registration table destination is not executable file data")
                        entries.append({"name": name, "function_rva": hex(function_rva), "name_store_rva": hex(name_store), "function_store_rva": hex(function_store), "runtime_verified": False})
                    if not terminated:
                        raise PEError("registration table has no resolved null pair within the inspection bound")
                    return {
                        "status": "statically_decoded_stack_registration", "module_name": module_name,
                        "registration_call_rva": hex(instruction.address - self.pe.image_base),
                        "registration_helper_rva": hex(operands[0].imm - self.pe.image_base),
                        "win64_arguments": {
                            "rcx": "unchanged incoming first argument" if registers.get("rcx") == ("incoming_argument", 0) else "unresolved",
                            "rdx": "address of TppNetworkUtil string", "r8": "address of temporary stack table", "r9": "not established by this caller",
                        },
                        "entry_count": len(entries), "null_pair_terminator": True, "entries": entries,
                        "unsupported_pre_call_instructions": unsupported, "runtime_verified": False,
                    }
                for name in VOLATILE:
                    registers[name] = None
            elif mnemonic not in ("nop",):
                unsupported.append({"rva": hex(instruction.address - self.pe.image_base), "mnemonic": mnemonic, "operands": instruction.op_str})
                for written in instruction.regs_access()[1]:
                    name = instruction.reg_name(written)
                    registers[ALIASES.get(name, name)] = None
        raise PEError("no resolved TppNetworkUtil registration call found")


def has(function: dict[str, Any], mnemonic: str, operands: str) -> bool:
    return any(row["mnemonic"] == mnemonic and row["operands"] == operands for row in function["instructions"])


def generate(data: bytes, candidate_report: dict[str, Any]) -> dict[str, Any]:
    digest = hashlib.sha256(data).hexdigest().upper()
    if candidate_report.get("sha256") != digest:
        raise PEError("candidate report SHA-256 does not match the inspected executable")
    groups = candidate_report.get("rip_relative_lea_scan", {}).get("shared_code_region_candidates", [])
    groups = [group for group in groups if set(("TppNetworkUtil", "IsSessionConnect", "GetSessionMemberCount")).issubset(group["anchors"])]
    if len(groups) != 1:
        raise PEError("expected exactly one independently derived shared anchor range")
    begin, end = int(groups[0]["begin_rva"], 16), int(groups[0]["end_rva_exclusive"], 16)
    analysis = Analysis(data)
    registration = analysis.trace_registration(begin, end)
    registrar = analysis.function(begin)
    if registration["win64_arguments"]["rcx"] != "unchanged incoming first argument":
        raise PEError("unable to establish registration first-argument provenance")
    bindings = {row["name"]: int(row["function_rva"], 16) for row in registration["entries"]}
    connected = analysis.function(bindings["IsSessionConnect"])
    count = analysis.function(bindings["GetSessionMemberCount"])
    connected_calls = [int(row["target_rva"], 16) for row in connected["direct_calls"]]
    count_calls = [int(row["target_rva"], 16) for row in count["direct_calls"]]
    if len(connected_calls) != 2 or len(count_calls) != 2 or connected_calls[0] != count_calls[0]:
        raise PEError("unexpected network-wrapper direct-call structure")
    accessor = analysis.function(connected_calls[0])
    boolean_push = analysis.function(connected_calls[-1])
    integer_push = analysis.function(count_calls[-1])
    helper = analysis.function(int(registration["registration_helper_rva"], 16))
    thunks = []
    for call in helper["direct_calls"]:
        callee = analysis.function(int(call["target_rva"], 16))
        if len(callee["instructions"]) == 2 and has(callee, "xor", "r9d, r9d") and callee["instructions"][-1]["mnemonic"] == "jmp":
            target = int(callee["instructions"][-1]["direct_target_rva"], 16)
            thunks.append({"thunk": callee, "table_iteration_helper": analysis.function(target)})
    checks = {
        "connection_state_mask": has(connected, "test", "eax, 0xfffffff6"),
        "connection_status_virtual_slot": has(connected, "call", "qword ptr [rdx + 0x10]"),
        "connection_vm_argument_preserved": has(connected, "mov", "rdi, rcx") and has(connected, "mov", "rcx, rdi"),
        "connection_one_result": has(connected, "mov", "esi, 1") and has(connected, "mov", "eax, esi"),
        "count_session_subobject": has(count, "lea", "rcx, [rax + 0x20]"),
        "count_virtual_slot": has(count, "call", "qword ptr [rax + 0x18]"),
        "count_signed_integer_argument": has(count, "movsxd", "rdx, ebx"),
        "count_one_result": has(count, "mov", "eax, 1"),
        "boolean_slot_tag_and_advance": has(boolean_push, "mov", "dword ptr [r8 + 8], 1") and has(boolean_push, "add", "qword ptr [rcx + 0x10], 0x10"),
        "integer_double_conversion_tag_and_advance": has(integer_push, "cvtsi2sd", "xmm0, rdx") and has(integer_push, "mov", "dword ptr [rax + 8], 3") and has(integer_push, "add", "qword ptr [rcx + 0x10], 0x10"),
    }
    if not all(checks.values()):
        raise PEError("static semantic checks did not match: " + ", ".join(name for name, passed in checks.items() if not passed))
    return {
        "status": "static_bindings_identified_runtime_disabled", "sha256": digest,
        "sha1": hashlib.sha1(data).hexdigest().upper(), "bytes": len(data),
        "registration": registration, "registration_function": registrar,
        "binding_functions": {"IsSessionConnect": connected, "GetSessionMemberCount": count},
        "helpers": {"common_accessor": accessor, "push_boolean_shape": boolean_push, "push_integer_shape": integer_push, "registration_wrapper": helper, "zero_upvalue_registration_thunks": thunks},
        "static_semantic_checks": checks,
        "supported_static_contracts": {
            "registration": "Win64 RCX is preserved incoming first argument, RDX is library name, R8 is a temporary null-terminated array of 16-byte name/function pairs; caller returns EAX=1 after registration.",
            "IsSessionConnect": "Win64 RCX is preserved as an opaque VM-state argument. It calls the shared accessor, defaults false on null, otherwise tests virtual-slot result with mask 0xFFFFFFF6, pushes one boolean-shaped value and returns EAX=1. The native return is the Lua result count, not the connection boolean.",
            "GetSessionMemberCount": "Win64 RCX is preserved as an opaque VM-state argument. It defaults a signed 32-bit count to zero, calls the shared accessor, then calls slot +0x18 on the subobject at +0x20 when nonnull. It sign-extends the count into RDX, pushes one number-shaped value and returns EAX=1. The native return is not the session count.",
            "VM_slot_shapes": "The boolean helper writes tag 1 plus normalized boolean; the integer helper converts signed RDX to double and writes tag 3. Both obtain the top slot from RCX+0x10 and advance it by 16 bytes. These observations do not authorize direct structure writes.",
        },
        "critical_limitations": [
            "Bindings and ABI shapes are statically associated, not runtime-approved hook targets.",
            "Both named queries call a common accessor with conditional initialization/helper calls; active polling is not established as side-effect-free.",
            "The original registration wrapper changes/restores additional VM flags and records library metadata; calling a bare standard Lua registration function would bypass observed behavior.",
            "The game state argument, stack availability, owning thread, state destruction/replacement and registration timing relative to DirectInput remain unobserved.",
            "The temporary registration table belongs to the registrar's stack and cannot be retained after it returns.",
            "A session count does not prove allocated remote player characters or four-player gameplay.",
        ],
        "next_minimal_observation": {
            "candidate_boundary_rva": hex(begin),
            "scope": "observe naturally occurring network-library registration and its return, preserving all arguments/results; no query calls",
            "before_any_hook": [
                "Verify exact executable fingerprint and instruction bytes against this report.",
                "Observe natural arrival, owning thread and registration timing using a controlled debugger or equivalent lifecycle evidence.",
                "Establish a safe hook installation/removal window and instruction relocation; static entry bytes alone are insufficient.",
                "Record that the original registration completed and identify state lifetime before accessing or extending the VM.",
            ],
            "active_calls_allowed_by_this_report": False,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-exe", type=Path, default=DEFAULT_GAME_DIR / "mgsvtpp.exe")
    parser.add_argument("--candidates", type=Path, default=PROJECT / "build/analysis/native-binding-candidates.json")
    parser.add_argument("--output", type=Path, default=PROJECT / "build/analysis/network-binding-analysis.json")
    args = parser.parse_args()
    if args.output.resolve() in (args.game_exe.resolve(), args.candidates.resolve()):
        parser.error("output must not overwrite input files")
    report: dict[str, Any] = {
        "schema_version": 1, "inspected_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_executable": str(args.game_exe.resolve()), "candidate_report": str(args.candidates.resolve()),
        "method": "original bounded static instruction decoding and stack-value tracing; no game execution",
        "capstone_version": capstone.__version__,
        "generic_contract_references": ["https://www.lua.org/manual/5.1/manual.html#luaL_Reg", "https://www.lua.org/manual/5.1/manual.html#lua_CFunction"],
        "runtime_calls_or_hooks_performed": False,
    }
    code = 0
    try:
        data = args.game_exe.read_bytes()
        candidates = json.loads(args.candidates.read_text(encoding="utf-8"))
        report.update(generate(data, candidates))
    except (OSError, PEError, ValueError, KeyError) as error:
        report.update({"status": "analysis_error", "error": str(error)})
        code = 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.output.resolve()}: {report['status']}")
    if "registration" in report:
        print(f"Decoded {report['registration']['entry_count']} network bindings; runtime calls remain disabled.")
    return code


if __name__ == "__main__":
    sys.exit(main())
