"""Generate one bounded natural block-counter observation for the exact game.

Uses the existing CDB guard/detach machinery. No engine calls or counter writes.
The launcher must enforce its external deadline. Hardware breakpoints briefly
stop the process; this is a development observation, not an installed hook.
"""
import argparse
import hashlib
from pathlib import Path

from generate_registration_trace import (
    GAME_SHA256, PeImage, byte_guards, require, notice,
    optional_breakpoint, breakpoint_action, cleanup, call_script,
)

BEGIN, END = 0x6E4E0, 0x6F1F8
BODY_SHA256 = '05ea4e586413f543de990f5803ef203dd839487a0024c822f6b27f7e5e371b9b'
SITES = ((0x6ED89, 11), (0x6EE4B, 12))


def generate(executable, output, entry=False):
    executable = executable.resolve(strict=True)
    data = executable.read_bytes()
    if executable.name.lower() != 'mgsvtpp.exe' or hashlib.sha256(data).hexdigest() != GAME_SHA256:
        raise ValueError('Unsupported executable')
    pe = PeImage(data)
    body = pe.at_rva(BEGIN, END-BEGIN, code=True)
    if hashlib.sha256(body).hexdigest() != BODY_SHA256:
        raise ValueError('Block dispatch body changed')
    output = output.resolve()
    failed = output.with_suffix('.failed.cmd')
    hits = [output.with_suffix(f'.hit{i}.cmd') for i in range(2)]
    for path in (output, failed, *hits):
        call_script(path)
        if path == executable:
            raise ValueError('Output overlaps executable')
    sites = ((BEGIN, None),) if entry else SITES
    checks = [require('@$ptrsize == 8', 'pointer_size'),
              optional_breakpoint(0, 6), optional_breakpoint(1, 7),
              require('(@$t6 == 0) and (@$t7 == 0)', 'breakpoints_occupied'),
              'r @$t1=mgsvtpp',
              require(f'$vvalid(@$t1,0x{pe.nt+88:x})', 'image_unreadable'),
              require('wo(@$t1) == 0x5a4d', 'dos_signature'),
              require(f'dwo(@$t1+0x{pe.nt:x}) == 0x4550', 'pe_signature'),
              require(f'dwo(@$t1+0x{pe.nt+80:x}) == 0x{pe.image_size:x}', 'image_size')]
    # Check the complete dispatch body, including the state jump table, live.
    for offset in range(0, len(body), 16):
        checks.extend(byte_guards(f'(@$t1+0x{BEGIN+offset:x})', body[offset:offset+16], f'body_{offset:x}'))
    for i, (site, _) in enumerate(sites):
        checks.extend([f'r @$t{i+4}=1',
                       f'ba{i} e1 (@$t1+0x{site:x}) {breakpoint_action(hits[i])}',
                       require(f'@$bp{i} == (@$t1+0x{site:x})', 'breakpoint_not_installed')])
    checks.append(notice('MGS_BLOCK_ARMED'))
    initial = ['.expr /s masm', 'r @$t0=1', 'r @$t4=0', 'r @$t5=0']
    initial += ['.if (@$t0 != 0) { r @$t0=0; .catch { '+c+'; r @$t0=1 } }' for c in checks]
    initial += [f'.if (@$t0 == 0) {{ {call_script(failed)}; }} .else {{ g }}']
    output.parent.mkdir(parents=True, exist_ok=True)
    failed.write_text('\n'.join([notice('MGS_ABORT block_setup_failed'), cleanup(),
                                notice('MGS_BLOCK_DETACH'), '.detach /h', 'q'])+'\n', encoding='ascii')
    for i, (site, state) in enumerate(sites):
        receiver = '@rcx' if entry else '@rdi'
        baseline = '0x14c' if state == 11 else '0x148'
        total = f'((dwo({receiver}+0x60)-dwo({receiver}+0x18)+dwo({receiver}+0x40)+dwo({receiver}+0x10)) & 0xffffffff)'
        capture = [require(f'@rip == (@$t1+0x{site:x})', 'unexpected_site'),
                   require(f'$vvalid({receiver},0x150)', 'block_unreadable')]
        if not entry:
            capture += [require(f'dwo({receiver}+0x84) == 0n{state}', 'unexpected_state'),
                        require(f'(@eax & 0xffffffff) == {total}', 'counter_arithmetic')]
        capture += ['.printf "MGS_BLOCK_CAPTURE mode=' + ('entry' if entry else 'comparison') + ' tid=0x%x image=%p site=%p block=%p state=%u total=%u baseline=%u baseline_offset=%x c60=%u c18=%u c40=%u c10=%u\\n", '
                    + f'@$tid, @$t1, @rip, {receiver}, dwo({receiver}+0x84), {total}, dwo({receiver}+{baseline}), {baseline}, dwo({receiver}+0x60), dwo({receiver}+0x18), dwo({receiver}+0x40), dwo({receiver}+0x10)']
        lines = ['r @$t0=1'] + ['.if (@$t0 != 0) { r @$t0=0; .catch { '+c+'; r @$t0=1 } }' for c in capture]
        lines += [cleanup(), '.if (@$t0 == 0) { '+notice('MGS_ABORT block_capture_failed')+' }',
                  notice('MGS_BLOCK_DETACH'), '.detach /h', 'q']
        hits[i].write_text('\n'.join(lines)+'\n', encoding='ascii')
    if any(len(line) >= 4000 for line in initial):
        raise ValueError('Debugger line too long')
    output.write_text('\n'.join(initial)+'\n', encoding='ascii')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--entry', action='store_true')
    args = parser.parse_args()
    generate(args.executable, args.output, args.entry)
