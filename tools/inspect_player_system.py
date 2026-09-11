"""Bounded external player-system header observation. No game calls or writes.

Candidate root comes from pinned tpp-mod symbols.hpp; object fields are raw,
not validated actor counts. Run only against the fingerprinted installed game.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

EXPECTED = "085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45"
CODE_CHECKS = {
    0x9C39F7: "488d51e0488d4c2420e86b082300",
    0xBF4293: "ff50403970047721",
    0xBF42BC: "488b4708897308488b742448488b4828",
    0x9C39B0: "33c041b801000000664489024889420489420c44848124020000741a8b81280200004489420c8942048b8130020000894208488bc2c348894204488bc2c3",
    0x9C3BB0: "488b81300400008bd2488b04d0c3",
    0xBF5250: "4c8b41284d85c07506b801000000c38bc20fb6ca48c1e80383e107420fb60400d3e883e001c3",
}


def observe(pid):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    psapi = c.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    psapi.EnumProcessModules.argtypes = [w.HANDLE, c.POINTER(w.HMODULE), w.DWORD, c.POINTER(w.DWORD)]
    handle = kernel.OpenProcess(0x410, False, pid)  # QUERY_INFORMATION | VM_READ
    if not handle:
        raise OSError(c.get_last_error(), "OpenProcess read-only failed")
    try:
        path = c.create_unicode_buffer(32768)
        length = w.DWORD(len(path))
        if not kernel.QueryFullProcessImageNameW(handle, 0, path, c.byref(length)):
            raise OSError(c.get_last_error(), "process path unavailable")
        executable = Path(path.value)
        if executable.name.lower() != "mgsvtpp.exe" or hashlib.sha256(executable.read_bytes()).hexdigest() != EXPECTED:
            raise ValueError("unexpected process identity")
        modules = (w.HMODULE * 2048)()
        needed = w.DWORD()
        if not psapi.EnumProcessModules(handle, modules, c.sizeof(modules), c.byref(needed)) or not 0 < needed.value <= c.sizeof(modules):
            raise ValueError("module inventory unavailable")
        base = modules[0]

        def read(address, size):
            if not 0x10000 <= address <= 0x7FFFFFFFFFFF - size or not 0 < size <= 256:
                raise ValueError("out-of-bounds header request")
            buffer = c.create_string_buffer(size)
            count = c.c_size_t()
            if not kernel.ReadProcessMemory(handle, address, buffer, size, c.byref(count)) or count.value != size:
                raise OSError(c.get_last_error(), "header unreadable")
            return buffer.raw

        if read(base, 2) != b"MZ":
            raise ValueError("module base mismatch")
        for rva, expected in CODE_CHECKS.items():
            expected_bytes = bytes.fromhex(expected)
            if read(base + rva, len(expected_bytes)) != expected_bytes:
                raise ValueError(f"loaded allocation path differs at {rva:#x}")
        root_address = base + 0x2A82C28
        root = read(root_address, 16)
        table, impl = struct.unpack("<QQ", root)
        report = {"utc": datetime.now(timezone.utc).isoformat(), "pid": pid,
                  "executable_sha256": EXPECTED, "base": hex(base),
                  "candidate_source": "tpp-mod 301bf797b7f8032941b21ed56a3570338de6ead4 symbols.hpp player2System",
                  "root_rva": "0x2a82c28", "root_table_raw": hex(table), "implementation_raw": hex(impl),
                  "calls_or_writes": False, "allocation_verified": False, "snapshot_atomic": False,
                  "loaded_code_checks": len(CODE_CHECKS)}
        if impl:
            header = read(impl, 256)
            vtable = struct.unpack_from("<Q", header)[0]
            report["implementation_header_qwords_raw"] = [hex(v) for v in struct.unpack("<32Q", header)]
            report["implementation_header_u32_raw"] = list(struct.unpack("<64I", header))
            report["implementation_table_raw"] = hex(vtable)
            if base <= vtable < base + 0xA01D000 - 128:
                report["implementation_vtable_entries_raw"] = [hex(v) for v in struct.unpack("<16Q", read(vtable, 128))]
            report["candidate_local_index_214_raw"] = struct.unpack("<I", read(impl + 0x214, 4))[0]
            # Live secondary methods subtract 0x20 before calling the generic
            # indexed resolver. Observe that enclosing header, never invoke it.
            enclosing = read(impl - 0x20, 32)
            enclosing_table = struct.unpack_from("<Q", enclosing)[0]
            if table != base + 0x22E51C8 or vtable != base + 0x2295490 or enclosing_table != base + 0x22953D0:
                raise ValueError("observed player-system type differs; fields suppressed")
            for address, target in ((vtable + 16, base + 0x9C39F0),
                                    (vtable + 88, base + 0x9C3BB0),
                                    (enclosing_table + 64, base + 0x9C39B0)):
                if struct.unpack("<Q", read(address, 8))[0] != target:
                    raise ValueError("loaded player method slot differs")
            report["enclosing_header_qwords_raw"] = [hex(v) for v in struct.unpack("<4Q", enclosing)]
            if base <= enclosing_table < base + 0xA01D000 - 128:
                report["enclosing_vtable_entries_raw"] = [hex(v) for v in struct.unpack("<16Q", read(enclosing_table, 128))]
            # Descriptor method 0x9c39b0 gates output with enclosing+0x224 bit 0,
            # copies +0x228 to descriptor+4 and +0x230 to descriptor+8.
            # Resolver 0xbf4270 compares descriptor+4 against the unsigned index.
            state = read(impl + 0x204, 20)
            report["descriptor_source_u32_raw"] = list(struct.unpack("<5I", state))
            report["descriptor_enabled_raw"] = bool(state[0] & 1)
            report["descriptor_index_limit_raw"] = struct.unpack_from("<I", state, 4)[0]
            report["descriptor_secondary_value_raw"] = struct.unpack_from("<I", state, 12)[0]
            limit = report["descriptor_index_limit_raw"]
            if report["descriptor_enabled_raw"] and 0 < limit <= 4:
                internal = struct.unpack_from("<Q", enclosing, 8)[0]
                executor = struct.unpack("<Q", read(internal + 0x28, 8))[0]
                executor_table = struct.unpack("<Q", read(executor, 8))[0]
                report["indexed_executor_raw"] = hex(executor)
                report["indexed_executor_table_raw"] = hex(executor_table)
                report["indexed_executor_header_u32_raw"] = list(struct.unpack("<16I", read(executor, 64)))
                if executor_table != base + 0x23063F8:
                    raise ValueError("indexed executor type differs")
                if struct.unpack("<Q", read(executor_table + 8, 8))[0] != base + 0xBF5250:
                    raise ValueError("loaded executor method slot differs")
                executor_state = read(executor + 8, 48)
                executor_limit = struct.unpack_from("<I", executor_state, 4)[0]
                bitset = struct.unpack_from("<Q", executor_state, 32)[0]
                report["executor_index_limit_raw"] = executor_limit
                if executor_limit == limit:
                    bits = read(bitset, (limit + 7) // 8) if bitset else None
                    report["executor_bitset_raw"] = bits.hex() if bits else None
                    report["executor_index_flags_raw"] = [bool(bits[i // 8] & (1 << (i % 8))) if bits else True for i in range(limit)]
                    report["executor_bits_unchanged_after_read"] = bits == (read(bitset, (limit + 7) // 8) if bitset else None)
                report["executor_state_unchanged_after_read"] = executor_state == read(executor + 8, 48)
                if base <= executor_table < base + 0xA01D000 - 64:
                    report["indexed_executor_methods_raw"] = [hex(v) for v in struct.unpack("<8Q", read(executor_table, 64))]
                storage = struct.unpack("<Q", read(impl + 0x430, 8))[0]
                report["indexed_storage_pointer_raw"] = hex(storage)
                if storage:
                    entries = struct.unpack(f"<{limit}Q", read(storage, 8 * limit))
                    report["indexed_storage_entries_raw"] = [hex(v) for v in entries]
                    report["indexed_storage_headers_raw"] = [
                        {"index": i, "qwords": [hex(v) for v in struct.unpack("<8Q", read(entry, 64))]}
                        for i, entry in enumerate(entries) if entry]
                    report["indexed_storage_unchanged_after_read"] = (
                        storage == struct.unpack("<Q", read(impl + 0x430, 8))[0]
                        and entries == struct.unpack(f"<{limit}Q", read(storage, 8 * limit)))
                report["descriptor_unchanged_after_read"] = state == read(impl + 0x204, 20)
        report["root_unchanged_after_read"] = root == read(root_address, 16)
        entries = report.get("indexed_storage_entries_raw", [])
        report["consistent_indexed_storage_observed"] = (
            bool(entries) and len(set(entries)) == len(entries) and "0x0" not in entries
            and report.get("executor_index_limit_raw") == report.get("descriptor_index_limit_raw")
            and all(report.get(key, False) for key in (
                "root_unchanged_after_read", "descriptor_unchanged_after_read", "executor_state_unchanged_after_read",
                "executor_bits_unchanged_after_read", "indexed_storage_unchanged_after_read")))
        report["limitation"] = "Source-derived root; unvalidated raw headers. Equal root rereads do not establish ownership, lifetime, allocation or four actors."
        return report
    finally:
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = observe(args.pid)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if "header" not in k}))
