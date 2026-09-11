"""Exact-build bandwidth job dispatch, cancellation and reference ownership.

File disassembly plus optional bounded read-only process comparison. No game
function is called, no job is cancelled, and no allocation hook is installed.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import analyze_host_allocations as host

RANGES = {
    'job_base_construct': (0x32070, 0x3210E),
    'job_derived_construct': (0x1A3BD20, 0x1A3BD51),
    'job_execute_locked': (0x32370, 0x323DA),
    'worker_execute': (0x32BB0, 0x32D55),
    'worker_finished': (0x2E1F0, 0x2E493),
    'scheduler_cancel_processing': (0x2E660, 0x2EC0E),
    'job_register_wrapper': (0x3AF70, 0x3AF87),
    'job_register': (0x2C530, 0x2C5E4),
    'reference_assign': (0x16090, 0x160E3),
    'reference_decrement': (0x21EA0, 0x21EA8),
    'reference_increment': (0x21EB0, 0x21EBA),
    'reference_exchange': (0x21F00, 0x21F07),
    'job_release_nonnull': (0x96EAD0, 0x96EAE0),
    'job_release_null': (0x96EAE0, 0x96EAE2),
    'job_registration_callback': (0x4FF9E0, 0x4FF9EA),
    'job_retirement_callback': (0x645940, 0x645943),
}
CHECKS = {
    0x7E1372: ('call', '0x141a3bd20'),
    0x1A3BD38: ('call', '0x140032070'),
    0x3208E: ('mov', 'dword ptr [rcx + 8], r8d'),
    0x320B9: ('mov', 'dword ptr [rcx + 0x2c], r8d'),
    0x320BD: ('mov', 'qword ptr [rcx + 0x30], r8'),
    0x7E0D82: ('mov', 'rcx, qword ptr [rip + 0x2379557]'),
    0x7E0D89: ('call', '0x14003af70'),
    0x3AF79: ('call', '0x14002c530'),
    0x2C53F: ('mov', 'rdi, qword ptr [r8]'),
    0x2C545: ('mov', 'rsi, rcx'),
    0x2C548: ('cmp', 'qword ptr [rdi + 0x30], 0'),
    0x2C54D: ('je', '0x14002c568'),
    0x2C54F: ('mov', 'dword ptr [rdx], 0x94970401'),
    0x2C573: ('call', 'qword ptr [rax + 0x18]'),
    0x2C5A7: ('lea', 'rcx, [rdi + 8]'),
    0x2C5AB: ('mov', 'dword ptr [rdi + 0x1c], 1'),
    0x2C5B2: ('mov', 'qword ptr [rdi + 0x30], rsi'),
    0x2C5B6: ('call', '0x140021eb0'),
    0x2C5BB: ('lea', 'rdx, [rdi + 0x38]'),
    0x2C5BF: ('lea', 'rcx, [rsi + 0x200]'),
    0x2C5C6: ('call', '0x14002dcf0'),
    0x32382: ('mov', 'rbx, rcx'),
    0x32390: ('xor', 'edx, edx'),
    0x32392: ('lea', 'rcx, [rbx + 0x2c]'),
    0x32396: ('lea', 'r8d, [rdx + 1]'),
    0x3239A: ('call', '0x140021e70'),
    0x3239F: ('test', 'eax, eax'),
    0x323A1: ('jne', '0x140032390'),
    0x323A7: ('mov', 'rax, qword ptr [rbx]'),
    0x323AD: ('mov', 'rcx, rbx'),
    0x323B7: ('call', 'qword ptr [rax + 0x10]'),
    0x323BA: ('xor', 'eax, eax'),
    0x323BC: ('mov', 'qword ptr [rbx + 0x80], rax'),
    0x323C3: ('lock or', 'dword ptr [rsp], eax'),
    0x323CC: ('mov', 'dword ptr [rbx + 0x2c], eax'),
    0x32C84: ('call', '0x14002dbb0'),
    0x32C89: ('mov', 'rdi, rax'),
    0x32CCF: ('mov', 'rcx, rdi'),
    0x32CD2: ('call', '0x140032370'),
    0x32CEE: ('mov', 'rdx, rdi'),
    0x32CF4: ('call', '0x14002e1f0'),
    0x2E413: ('mov', 'dword ptr [rdx + 0x1c], 5'),
    0x2E41A: ('lea', 'rsi, [rdx + 0x38]'),
    0x2E41E: ('lea', 'rbx, [r13 + 0x140]'),
    0x2E438: ('lea', 'rdi, [rbx + 0x20]'),
    0x2E449: ('mov', 'qword ptr [rdi + 8], rsi'),
    0x2E44D: ('mov', 'qword ptr [rax + 0x10], rsi'),
    0x2E451: ('mov', 'qword ptr [rsi + 8], rax'),
    0x2E455: ('mov', 'qword ptr [rsi + 0x10], rdi'),
    0x2E694: ('call', '0x140032bb0'),
    0x2E6B2: ('lea', 'r15, [r14 + 0x160]'),
    0x2E6C0: ('mov', 'rsi, qword ptr [r14 + 0x158]'),
    0x2E6F4: ('mov', 'rdi, qword ptr [rsi]'),
    0x2E6F7: ('test', 'byte ptr [rdi + 0x24], 1'),
    0x2E6FB: ('jne', '0x14002e733'),
    0x2E6FD: ('test', 'byte ptr [rdi + 0x28], 1'),
    0x2E701: ('jne', '0x14002e733'),
    0x2E703: ('lea', 'rbx, [r14 + 0x120]'),
    0x2E72A: ('mov', 'dword ptr [rdi + 0x1c], 2'),
    0x2E74B: ('call', '0x14002df20'),
    0x2E75C: ('and', 'dword ptr [rdi + 0x28], 0xfffffffe'),
    0x2E760: ('mov', 'dword ptr [rdi + 0x1c], r12d'),
    0x2E764: ('mov', 'qword ptr [rdi + 0x30], r12'),
    0x2E76E: ('call', 'qword ptr [rax + 0x20]'),
    0x2E771: ('lea', 'rcx, [rdi + 8]'),
    0x2E775: ('call', '0x140021ea0'),
    0x2E77A: ('cmp', 'eax, 1'),
    0x2E77D: ('jg', '0x14002e788'),
    0x2E785: ('call', 'qword ptr [rax + 8]'),
    0x2E7F5: ('lea', 'r13, [r14 + 0x200]'),
    0x2E821: ('mov', 'rsi, qword ptr [r13 + 0x18]'),
    0x2E86F: ('mov', 'r15, qword ptr [rsi]'),
    0x2E872: ('test', 'byte ptr [r15 + 0x28], 1'),
    0x2E877: ('je', '0x14002e905'),
    0x2E8CF: ('and', 'dword ptr [r15 + 0x28], 0xfffffffe'),
    0x2E8D4: ('mov', 'dword ptr [r15 + 0x1c], ebx'),
    0x2E8D8: ('mov', 'qword ptr [r15 + 0x30], rbx'),
    0x2E8E2: ('call', 'qword ptr [rax + 0x20]'),
    0x2E8E5: ('lea', 'rcx, [r15 + 8]'),
    0x2E8E9: ('call', '0x140021ea0'),
    0x2E8EE: ('cmp', 'eax, 1'),
    0x2E8F1: ('jg', '0x14002e810'),
    0x2E8FD: ('call', 'qword ptr [rax + 8]'),
    0x160A5: ('lea', 'rcx, [rdx + 8]'),
    0x160A9: ('call', '0x140021eb0'),
    0x160B4: ('call', '0x140021f00'),
    0x160C1: ('lea', 'rcx, [rax + 8]'),
    0x160C5: ('call', '0x140021ea0'),
    0x160CA: ('cmp', 'eax, 1'),
    0x160CD: ('jg', '0x1400160d8'),
    0x160D5: ('call', 'qword ptr [rdx + 8]'),
    0x21EA0: ('or', 'eax, 0xffffffff'),
    0x21EA3: ('lock xadd', 'dword ptr [rcx], eax'),
    0x21EB0: ('mov', 'eax, 1'),
    0x21EB5: ('lock xadd', 'dword ptr [rcx], eax'),
    0x21F00: ('xchg', 'qword ptr [rcx], rdx'),
    0x96EAD0: ('test', 'rcx, rcx'),
    0x96EAD3: ('je', '0x14096eae0'),
    0x96EAD8: ('mov', 'edx, 1'),
    0x96EADD: ('jmp', 'qword ptr [rax]'),
    0x96EAE0: ('repz ret', ''),
    0x4FF9E0: ('mov', 'dword ptr [rdx], 0'),
    0x645940: ('ret', '0'),
}
TABLES = {0x221C580: 0x96EAD0, 0x221C590: 0x4FF9E0, 0x221C598: 0x645940}


def analyze(executable):
    parent, a = host.analyze(executable)  # Includes whole-executable SHA-256.
    bodies = dict(parent['bodies'])
    for name, (begin, end) in RANGES.items():
        f = a.function(begin)
        if int(f['boundary']['end_rva_exclusive'], 16) != end or not f['complete_declared_range_decoded']:
            raise ValueError(f'Job boundary differs: {name}')
        bodies[name] = f
    decoded = {int(i['rva'], 16): i for f in bodies.values() for i in f['instructions']}
    for rva, expected in CHECKS.items():
        row = decoded.get(rva)
        if row is None or (row['mnemonic'], row['operands']) != expected:
            raise ValueError(f'Job dataflow differs at {rva:#x}')
    for slot, target in TABLES.items():
        if struct.unpack('<Q', a.pe.rva_read(slot, 8))[0] != a.pe.image_base + target:
            raise ValueError(f'Job table differs at {slot:#x}')
    return dict(utc=datetime.now(timezone.utc).isoformat(), executable_sha256=host.GAME_SHA256,
        job_instruction_checks=len(CHECKS), inherited_instruction_checks=parent['instruction_checks'],
        job_table_checks=len(TABLES), inherited_table_checks=parent['table_checks'], bodies=bodies,
        result=dict(callback_exclusion='Normal-return dispatcher holds job+0x2c across slot-2 callback, matching manager destruction lock.',
            scheduler_ownership='Registration increments job+8 before queue insertion; normal cancellation processing decrements after clearing job+0x30.',
            cancelled_completed_job='Finished queue at scheduler+0x140 consumes job+0x28 bit 0; selected removal clears request/state/owner and invokes slot 4 then drops scheduler reference.',
            cancelled_pending_job='Pending queue at scheduler+0x200 also consumes bit 0 before promotion to waiting state.',
            last_reference='Atomic decrement returns prior count; prior count <=1 invokes slot 1, which dispatches slot 0 with deletion flag 1.',
            registration_callback='Concrete slot 3 returns success zero.', retirement_callback='Concrete slot 4 immediately returns.'),
        runtime_calls_or_writes=False, runtime_job_lifecycle_verified=False,
        limitations=['Static normal-control-flow ownership, not live completion or bounded cancellation latency.',
            'Queue locking helpers, dependency teardown internals, scheduler shutdown and exceptional exits are not exhaustively verified.',
            'Additional session allocations and live heap/free behavior remain separate integration requirements.'],
        source_hints=parent['source_hints']), a


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pid', type=int)
    args = parser.parse_args()
    report, a = analyze(args.executable)
    if args.pid is not None:
        report['live_baseline'] = host.observe(args.pid, a, {**host.RANGES, **RANGES})
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k != 'bodies'}, indent=2))
