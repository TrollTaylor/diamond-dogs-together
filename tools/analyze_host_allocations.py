"""Exact-build host/client allocation analysis and optional read-only baseline.

Source hints: pinned Dynamite issue_7 and pinned tpp-mod NetworkInfo symbol.
No upstream address is executed, no allocator is patched, and no session is made.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

from analyze_network_bindings import Analysis

GAME_SHA256 = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
RANGES = {
    'host_create': (0x7EE590, 0x7EE758),
    'client_create': (0x7EE380, 0x7EE588),
    'network_info_accessor': (0x7CFDB0, 0x7CFDEE),
    'bandwidth_lazy_create': (0x7CFA70, 0x7CFA95),
    'bandwidth_allocate': (0x7E1300, 0x7E133A),
    'bandwidth_construct': (0x7E0C10, 0x7E0DCD),
    'bandwidth_destroy': (0x7E0DD0, 0x7E0E65),
    'bandwidth_delete': (0x7E0E70, 0x7E0E9F),
    'job_allocate': (0x7E1340, 0x7E13B3),
    'job_delete': (0x5918C0, 0x5918FD),
    'job_lock': (0x323E0, 0x3240C),
    'job_unlock': (0x32730, 0x3273D),
    'job_request_cancel': (0x326F0, 0x3272C),
    'atomic_compare_exchange': (0x21E70, 0x21E78),
    'atomic_or': (0x21ED0, 0x21EE9),
    'job_update_wrapper': (0x7E13C0, 0x7E13D2),
    'block_heap_alloc': (0xDCB50, 0xDCBAF),
    'current_block': (0xE63E0, 0xE63FD),
    'heap_allocate': (0xDBAB0, 0xDBB18),
    'tail_allocate': (0xDBC20, 0xDBDC0),
    'block_heap_free': (0xDCBC0, 0xDCBD6),
    'free_dispatch': (0xDBFD0, 0xDC11D),
}
# These predicates establish dataflow, not merely that a named candidate exists.
CHECKS = {
    0x7EE65B: ('call', '0x1407cfa70'),
    0x7EE45E: ('call', '0x1407cfa70'),
    0x7CFDCC: ('mov', 'rdx, qword ptr [rip + 0x241fa6d]'),
    0x7CFA76: ('cmp', 'qword ptr [rcx + 0xd00], 0'),
    0x7CFA81: ('jne', '0x1407cfa8f'),
    0x7CFA83: ('call', '0x1407e1300'),
    0x7CFA88: ('mov', 'qword ptr [rbx + 0xd00], rax'),
    0x7E130D: ('mov', 'edx, 8'),
    0x7E1312: ('lea', 'r8d, [rdx + 0x6b]'),
    0x7E1316: ('lea', 'ecx, [rdx + 0x50]'),
    0x7E1319: ('call', '0x1400dcb50'),
    0x7E132B: ('call', '0x1407e0c10'),
    0x7E0CDC: ('call', '0x1407e1340'),
    0x7E0D53: ('mov', 'qword ptr [rbx + 0x88], rdi'),
    0x7E0D5A: ('mov', 'rdx, rbx'),
    0x7E0D5D: ('mov', 'rcx, rsi'),
    0x7E0D60: ('call', '0x140016090'),
    0x7E134F: ('mov', 'edx, 8'),
    0x7E1354: ('mov', 'ecx, 0x90'),
    0x7E1359: ('lea', 'r8d, [rdx + 0x6b]'),
    0x7E135D: ('call', '0x1400dcb50'),
    0xDCB5F: ('mov', 'ebx, r8d'),
    0xDCB62: ('mov', 'rdi, rdx'),
    0xDCB65: ('mov', 'rsi, rcx'),
    0xDCB68: ('call', '0x1400e63e0'),
    0xDCB6D: ('test', 'rax, rax'),
    0xDCB70: ('je', '0x1400dcb92'),
    0xDCB72: ('mov', 'r9d, ebx'),
    0xDCB75: ('mov', 'r8, rdi'),
    0xDCB78: ('mov', 'rdx, rsi'),
    0xDCB7B: ('mov', 'rcx, rax'),
    0xDCB8D: ('jmp', '0x1400dbc20'),
    0xDCB92: ('mov', 'r8d, ebx'),
    0xDCB95: ('mov', 'rdx, rdi'),
    0xDCB98: ('mov', 'rcx, rsi'),
    0xDCBAA: ('jmp', '0x1400dbab0'),
    0xE63E0: ('mov', 'ecx, dword ptr [rip + 0x2b8568a]'),
    0xE63E6: ('mov', 'rax, qword ptr gs:[0x58]'),
    0xE63EF: ('mov', 'edx, 0x48'),
    0xE63F4: ('mov', 'rax, qword ptr [rax + rcx*8]'),
    0xE63F8: ('mov', 'rax, qword ptr [rdx + rax]'),
    0xDBAC2: ('mov', 'ecx, 0x10'),
    0xDBACC: ('mov', 'edi, r8d'),
    0xDBACF: ('cmovb', 'eax, ecx'),
    0xDBAD8: ('call', '0x140015fc0'),
    0xDBAFD: ('mov', 'dword ptr [rax - 0x10], ebx'),
    0xDBB05: ('mov', 'dword ptr [rax - 8], ecx'),
    0xDBB08: ('mov', 'dword ptr [rax - 4], 0xdead'),
    0xDBB0F: ('mov', 'dword ptr [rax - 0xc], edi'),
    0x7E0E09: ('mov', 'qword ptr [rbx + 0x88], 0'),
    0x7E0DFA: ('lea', 'rdi, [rcx + 8]'),
    0x7E0DFE: ('mov', 'rbx, qword ptr [rdi]'),
    0x7E0E01: ('mov', 'rcx, rbx'),
    0x7E0E04: ('call', '0x1400323e0'),
    0x7E0E14: ('mov', 'rcx, rbx'),
    0x7E0E17: ('call', '0x140032730'),
    0x7E0E1C: ('lea', 'rdx, [rsp + 0x48]'),
    0x7E0E21: ('mov', 'rcx, rbx'),
    0x7E0E24: ('call', '0x1400326f0'),
    0x323E6: ('lea', 'rbx, [rcx + 0x2c]'),
    0x323F0: ('xor', 'edx, edx'),
    0x323F2: ('mov', 'rcx, rbx'),
    0x323F5: ('lea', 'r8d, [rdx + 1]'),
    0x323F9: ('call', '0x140021e70'),
    0x323FE: ('test', 'eax, eax'),
    0x32400: ('jne', '0x1400323f0'),
    0x32402: ('lock or', 'dword ptr [rsp], eax'),
    0x21E70: ('mov', 'eax, edx'),
    0x21E72: ('lock cmpxchg', 'dword ptr [rcx], r8d'),
    0x32730: ('lock or', 'dword ptr [rsp], 0'),
    0x32735: ('mov', 'dword ptr [rcx + 0x2c], 0'),
    0x326F6: ('cmp', 'qword ptr [rcx + 0x30], 0'),
    0x326FB: ('mov', 'rbx, rdx'),
    0x326FE: ('jne', '0x14003270f'),
    0x32700: ('mov', 'dword ptr [rdx], 0x94970407'),
    0x3270F: ('add', 'rcx, 0x28'),
    0x32713: ('mov', 'edx, 1'),
    0x32718: ('call', '0x140021ed0'),
    0x3271D: ('mov', 'dword ptr [rbx], 0'),
    0x21ED0: ('mov', 'eax, dword ptr [rcx]'),
    0x21ED2: ('mov', 'r8d, eax'),
    0x21ED5: ('mov', 'r9d, eax'),
    0x21ED8: ('or', 'r8d, edx'),
    0x21EDB: ('lock cmpxchg', 'dword ptr [rcx], r8d'),
    0x21EE0: ('cmp', 'r9d, eax'),
    0x21EE3: ('jne', '0x140021ed2'),
    0x7E13C0: ('mov', 'rcx, qword ptr [rcx + 0x88]'),
    0x7E13C7: ('test', 'rcx, rcx'),
    0x7E13CA: ('jne', '0x1407e1430'),
    0x7E0E45: ('call', '0x140016090'),
    0x7E0E84: ('test', 'bl, 1'),
    0x7E0E8C: ('call', '0x1400dcbc0'),
    0x5918E2: ('test', 'bl, 1'),
    0x5918EA: ('call', '0x1400dcbc0'),
    0xDCBCC: ('call', '0x1400dbfd0'),
    0xDC0BA: ('lea', 'rcx, [rdi - 0x10]'),
    0xDC0C1: ('cmp', 'eax, 0xdead'),
    0xDC0C6: ('jne', '0x1400dc0e0'),
    0xDC0C8: ('mov', 'eax, dword ptr [rcx]'),
    0xDC0CA: ('sub', 'rdi, rax'),
    0xDC0CD: ('mov', 'edx, dword ptr [rcx + 4]'),
    0xDC0D3: ('call', '0x140015f90'),
}


def analyze(executable):
    data = executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != GAME_SHA256:
        raise ValueError('Unsupported executable')
    a = Analysis(data)
    bodies, decoded = {}, {}
    for name, (begin, end) in RANGES.items():
        f = a.function(begin)
        if int(f['boundary']['end_rva_exclusive'], 16) != end or not f['complete_declared_range_decoded']:
            raise ValueError(f'Function boundary differs: {name}')
        bodies[name] = f
        for i in f['instructions']:
            decoded[int(i['rva'], 16)] = i
    for rva, expected in CHECKS.items():
        row = decoded.get(rva)
        if row is None or (row['mnemonic'], row['operands']) != expected:
            raise ValueError(f'Allocation dataflow check failed at {rva:#x}')
    # Constructor tables and matching deletion dispatch slots.
    table_checks = {0x221C5B8: 0x7E0E70, 0x221C578: 0x5918C0, 0x221C588: 0x7E13C0}
    for slot, target in table_checks.items():
        if struct.unpack('<Q', a.pe.rva_read(slot, 8))[0] != a.pe.image_base + target:
            raise ValueError('Deletion table differs')
    report = dict(utc=datetime.now(timezone.utc).isoformat(), executable_sha256=GAME_SHA256,
        instruction_checks=len(CHECKS), table_checks=len(table_checks), bodies=bodies,
        network_info_root_rva='0x2bef840', bandwidth_member_offset='0xd00',
        allocator_abi='Windows x64: RCX=size, RDX=alignment, R8D=tag; RAX=pointer',
        tls_index_rva=hex(0xE63E6 + 0x2B8568A), tls_vector_teb_offset='0x58',
        current_block_tls_offset='0x48',
        persistent_allocation_candidates=[
            dict(role='bandwidth_manager', call_rva='0x7e1319', return_rva='0x7e131e', size=88, alignment=8, tag=115),
            dict(role='bandwidth_job', call_rva='0x7e135d', return_rva='0x7e1362', size=144, alignment=8, tag=115)],
        heap_header=dict(size=16, marker_offset=-4, marker='0xdead', tag_offset=-12, original_base_delta_offset=-16),
        job_shutdown=dict(lock_offset='0x2c', manager_backpointer_offset='0x88',
            request_flags_offset='0x28', request_precondition_offset='0x30',
            order=['lock job', 'clear manager backpointer', 'unlock job', 'set request bit 0 if job+0x30 is nonnull', 'release manager references'],
            request_success_status='0x0', request_missing_status='0x94970407',
            callback='Method-table slot 2 returns for a null backpointer, otherwise tail-dispatches to 0x7e1430.',
            completion_verified=False,
            limitation='Cancellation meaning is inferred from destructor context. Scheduler handling, callback locking and final reference release are not established; no explicit join appears in this destructor.'),
        runtime_calls_or_writes=False, hooks_enabled=False,
        source_hints=['Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e docs/issue_7.md (GPLv3 + Commons Clause 1.0)',
                      'tpp-mod 301bf797b7f8032941b21ed56a3570338de6ead4 NetworkInfo_::m_instance (GPLv3)'],
        limitation='Static dataflow only. Asynchronous job completion, singleton lifetime, natural allocation context, transport creation and full cleanup remain unverified. Two mapped sites are not an exhaustive allocation inventory.')
    return report, a


def observe(pid, a, ranges=None):
    """Read mapped fields only after validating the process and every code body."""
    ranges = RANGES if ranges is None else ranges
    kernel = c.WinDLL('kernel32', use_last_error=True)
    psapi = c.WinDLL('psapi', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    psapi.EnumProcessModules.argtypes = [w.HANDLE, c.POINTER(w.HMODULE), w.DWORD, c.POINTER(w.DWORD)]
    handle = kernel.OpenProcess(0x410, False, pid)
    if not handle:
        raise OSError(c.get_last_error(), 'Read-only OpenProcess failed')
    try:
        path, length = c.create_unicode_buffer(32768), w.DWORD(32768)
        if not kernel.QueryFullProcessImageNameW(handle, 0, path, c.byref(length)):
            raise ValueError('Process path unavailable')
        if Path(path.value).name.lower() != 'mgsvtpp.exe' or hashlib.sha256(Path(path.value).read_bytes()).hexdigest() != GAME_SHA256:
            raise ValueError('Unexpected process executable')
        modules, needed = (w.HMODULE * 2048)(), w.DWORD()
        if not psapi.EnumProcessModules(handle, modules, c.sizeof(modules), c.byref(needed)) or not 0 < needed.value <= c.sizeof(modules):
            raise ValueError('Module list unavailable')
        base = modules[0]

        def read(address, size):
            if not 0 < size <= 256 or not 0x10000 <= address <= 0x7FFFFFFFFFFF - size:
                raise ValueError('Unbounded memory request')
            buffer, count = c.create_string_buffer(size), c.c_size_t()
            if not kernel.ReadProcessMemory(handle, address, buffer, size, c.byref(count)) or count.value != size:
                raise OSError(c.get_last_error(), 'Mapped field unreadable')
            return buffer.raw

        for begin, end in ranges.values():
            for rva in range(begin, end, 256):
                size = min(256, end-rva)
                if read(base+rva, size) != a.pe.rva_read(rva, size):
                    raise ValueError(f'Live allocation code differs at {rva:#x}')
        root = read(base+0x2BEF840, 8)
        owner = struct.unpack('<Q', root)[0]
        fields = read(owner+0xCC8, 8) + read(owner+0xD00, 8) if owner else None
        session, manager = struct.unpack('<QQ', fields) if fields else (0, 0)
        result = dict(utc=datetime.now(timezone.utc).isoformat(), pid=pid, base=hex(base),
            loaded_code_bodies_matched=len(ranges), network_info_raw=hex(owner),
            session_member_raw=hex(session), bandwidth_manager_raw=hex(manager),
            calls_or_writes=False, snapshot_atomic=False)
        if manager:
            header = read(manager-16, 32)
            table, job = struct.unpack_from('<QQ', header, 16)
            if table != base+0x221C5A8:
                raise ValueError('Bandwidth manager type differs')
            result['manager_allocation_header_u32_raw'] = list(struct.unpack('<4I', header[:16]))
            result['manager_heap_marker_observed'] = struct.unpack_from('<I', header, 12)[0] == 0xDEAD
            result['job_raw'] = hex(job)
            if job:
                job_header = read(job-16, 24)
                if struct.unpack_from('<Q', job_header, 16)[0] != base+0x221C578:
                    raise ValueError('Bandwidth job type differs')
                result['job_allocation_header_u32_raw'] = list(struct.unpack('<4I', job_header[:16]))
                result['job_header_unchanged'] = job_header == read(job-16, 24)
            result['manager_header_unchanged'] = header == read(manager-16, 32)
        result['owner_fields_unchanged'] = fields == (read(owner+0xCC8, 8) + read(owner+0xD00, 8) if owner else None)
        result['root_unchanged'] = root == read(base+0x2BEF840, 8)
        result['limitation'] = 'Raw field baseline, not a live session test. Equal rereads are not an atomic snapshot or lifetime guarantee.'
        return result
    finally:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pid', type=int)
    args = parser.parse_args()
    report, analysis = analyze(args.executable)
    if args.pid is not None:
        report['live_baseline'] = observe(args.pid, analysis)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k != 'bodies'}, indent=2))
