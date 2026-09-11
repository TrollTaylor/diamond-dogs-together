"""Generate a bounded attach-time observer for setters or player position writes.

File-only generation. Running the result in a dedicated CDB session briefly
stops the target. It observes registers (and a stack return at setter entry).
Optional owner mode reads a bounded pointer/header graph only at the identified
record-store boundary. It does not read source-vector floats or invoke setters.
The caller must supply an external deadline and verified debugger detachment.
"""
import argparse
import hashlib
from pathlib import Path

from generate_registration_trace import (
    GAME_SHA256, PeImage, byte_guards, require, notice,
    optional_breakpoint, breakpoint_action, cleanup, call_script,
)

SETTERS = (
    (0x1E03410, bytes.fromhex('40534883ec208bca498bd8e8f09573fe0f28030f2940104883c4205bc3')),
    (0x1E03450, bytes.fromhex('40534883ec20488bdae8429573fe0f28030f2940104883c4205bc3')),
)


def generate(executable, output, fixture=False, wrong_bytes=False, watch_published=False, skip_bank_copy=False, capture_owner=False):
    if capture_owner and (fixture or not skip_bank_copy):
        raise ValueError('Owner capture requires the filtered game write observer')
    if skip_bank_copy and not watch_published:
        raise ValueError('Copy filtering requires published-write mode')
    exe = executable.resolve(strict=True)
    data = exe.read_bytes()
    pe = PeImage(data)
    if fixture:
        if exe.name.lower() != 'player_vector_fixture.exe':
            raise ValueError('Only player_vector_fixture.exe is accepted as a fixture')
        module = 'player_vector_fixture'
        targets = [(pe.export(name), None) for name in (b'FixtureIndexed', b'FixtureLocal')]
        targets = [(rva, pe.at_rva(rva, 16, code=True)) for rva, _ in targets]
    else:
        if exe.name.lower() != 'mgsvtpp.exe' or hashlib.sha256(data).hexdigest() != GAME_SHA256:
            raise ValueError('Unsupported game fingerprint')
        if wrong_bytes:
            raise ValueError('Wrong-byte negative control is fixture-only')
        module, targets = 'mgsvtpp', SETTERS
    targets = list(targets)
    addresses = [rva for rva, _ in targets]
    if watch_published:
        addresses = ([pe.export(name) for name in (b'FixtureWrite0', b'FixtureWrite1')]
                     if fixture else [0x2BD4000, 0x2BD4E00])
        if any(rva % 4 or rva + 4 > pe.image_size for rva in addresses):
            raise ValueError('Invalid aligned watch range')
        if not fixture:
            targets.append((0x1E02E00, bytes.fromhex(
                '40534883ec20488bdae8c29b73fe0f284010488bc3660f7f034883c4205bc3')))
    for rva, expected in targets:
        if pe.at_rva(rva, len(expected), code=True) != expected:
            raise ValueError('Setter bytes mismatch')
    if wrong_bytes:
        rva, expected = targets[0]
        targets[0] = rva, bytes([expected[0] ^ 0xff]) + expected[1:]
    output = output.resolve()
    hits = [output.with_name(output.stem + f'.hit{i}.cmd') for i in range(2)]
    failed = output.with_name(output.stem + '.failed.cmd')
    captures = [output.with_name(output.stem + f'.capture{i}.cmd') for i in range(2)]
    copy_script = output.with_name(output.stem + '.copy.cmd')
    for path in [output, *hits, *captures, copy_script, failed]:
        call_script(path)
        if path == exe:
            raise ValueError('Output overlaps executable')
    checks = [require('@$ptrsize == 8', 'wrong_pointer_size'),
              optional_breakpoint(0, 6), optional_breakpoint(1, 7),
              require('(@$t6 == 0) and (@$t7 == 0)', 'breakpoint_ids_occupied'),
              f'r @$t1 = {module}',
              require(f'$vvalid(@$t1,0x{pe.nt+88:x})', 'image_header_unreadable'),
              require('wo(@$t1) == 0x5a4d', 'dos_signature'),
              require(f'dwo(@$t1+0x3c) == 0x{pe.nt:x}', 'pe_offset'),
              require(f'dwo(@$t1+0x{pe.nt:x}) == 0x4550', 'pe_signature'),
              require(f'wo(@$t1+0x{pe.nt+4:x}) == 0x8664', 'machine'),
              require(f'dwo(@$t1+0x{pe.nt+80:x}) == 0x{pe.image_size:x}', 'image_size')]
    # Both complete game functions are checked before arming either breakpoint.
    for i, (rva, expected) in enumerate(targets):
        checks.extend(byte_guards(f'(@$t1+0x{rva:x})', expected, f'setter_{i}'))
    owner_guard = []
    if capture_owner:
        # Independently decoded producer span, including the actual store and
        # its post-store boundary. No owner method is called by the observer.
        for offset in range(0, 0x35, 16):
            rva = 0x12CF23A + offset
            expected = pe.at_rva(rva, min(16, 0x35-offset), code=True)
            owner_guard.extend(byte_guards(f'(@$t1+0x{rva:x})', expected, f'owner_span_{offset:x}'))
        checks.extend(owner_guard)
    copy_rips = []
    if skip_bank_copy:
        if fixture:
            copy_rva = pe.export(b'FixtureBaseline')
            copy_bytes = pe.at_rva(copy_rva, 20, code=True)
            if (copy_bytes[:6] != bytes.fromhex('0f57c0f30f11') or
                    copy_bytes[6] != 5 or copy_bytes[11:15] != bytes.fromhex('f30f1105') or copy_bytes[19] != 0xc3):
                raise ValueError('Fixture baseline instructions changed')
            copy_rips = [copy_rva + 11, copy_rva + 19]
        else:
            copy_rva, copy_bytes = 0x53CB10, pe.at_rva(0x53CB10, 0xAF, code=True)
            copy_rips = [0x53CB7D]
        for offset in range(0, len(copy_bytes), 16):
            checks.extend(byte_guards(f'(@$t1+0x{copy_rva+offset:x})', copy_bytes[offset:offset+16], f'copy_{offset:x}'))
    if watch_published:
        for i, rva in enumerate(addresses):
            checks.append(require(f'$vvalid(@$t1+0x{rva:x},4)', f'bank_{i}_unreadable'))
        if not fixture:
            checks.extend([
                require('$vvalid(@$t1+0x2bd3fd0,8)', 'selectors_unreadable'),
                require('dwo(@$t1+0x2bd3fd4) == 0', 'local_index_not_zero'),
                require('dwo(@$t1+0x2bd3fd0) < 2', 'invalid_bank')])
    for i, rva in enumerate(addresses):
        checks.extend([f'r @$t{i+4} = 1',
                       f'ba{i} {"w4" if watch_published else "e1"} (@$t1+0x{rva:x}) {breakpoint_action(hits[i])}',
                       require(f'@$bp{i} == (@$t1+0x{rva:x})', f'breakpoint_{i}_not_installed')])
    checks += [notice('MGS_VECTOR_ARMED'), 'r @$t0 = 1']
    # CDB truncates long physical command lines. Keep each check separate and
    # latch any failure before subsequent checks or breakpoint installation.
    initial = ['.expr /s masm', 'r @$t0=1', 'r @$t4=0', 'r @$t5=0', 'r @$t8=0']
    initial += ['.if (@$t0 != 0) { r @$t0=0; .catch { ' + check + '; r @$t0=1 } }'
                for check in checks]
    initial += [f'.if (@$t0 == 0) {{ {call_script(failed)}; }} .else {{ g }}']
    if any(len(line) >= 4000 for line in initial):
        raise ValueError('Debugger setup line exceeds bounded command length')
    # No queued commands after g: deadline recovery must own its interrupt,
    # inspect that event, and detach without racing a queued script cleanup.
    output.parent.mkdir(parents=True, exist_ok=True)
    failed.write_text('\n'.join([notice('MGS_ABORT vector_observation_failed'), cleanup(), notice('MGS_VECTOR_DETACH'), '.detach /h', 'q'])+'\n', encoding='ascii')
    for i, (rva, expected) in enumerate(targets[:2]):
        source = '@r8' if i == 0 else '@rdx'
        capture = [require(f'@rip == (@$t1+0x{rva:x})', 'unexpected_hit'),
                   *byte_guards(f'(@$t1+0x{rva:x})', expected, 'hit_setter'),
                   require('$vvalid(@rsp,8)', 'stack_return_unreadable'),
                   '.printf "MGS_VECTOR_ENTRY setter=%u tid=0x%x image=%p entry=%p caller=%p receiver_opaque=%p source_opaque=%p index_raw=%u\\n", '
                   f'{i}, @$tid, @$t1, @rip, poi(@rsp), @rcx, {source}, @edx',
                   'r @$t0=1']
        if watch_published:
            # A data trap reports raw RIP. Identify the store offline; do not
            # interpret the stack top as a call return from an arbitrary RIP.
            capture = [
                '.printf "MGS_VECTOR_WRITE bank=%u tid=0x%x image=%p instruction_raw=%p watched=%p rax_opaque=%p rbx_opaque=%p rcx_opaque=%p rdx_opaque=%p r8_opaque=%p r9_opaque=%p rsp_opaque=%p rdi_opaque=%p rsi_raw=%p r13_opaque=%p r14_opaque=%p\\n", '
                f'{i}, @$tid, @$t1, @rip, (@$t1+0x{addresses[i]:x}), @rax, @rbx, @rcx, @rdx, @r8, @r9, @rsp, @rdi, @rsi, @r13, @r14',
                'r @$t0=1']
            if not fixture:
                capture.insert(0, require('dwo(@$t1+0x2bd3fd4) == 0', 'hit_local_index_not_zero'))
            if capture_owner:
                graph = [
                    require('@rip == (@$t1+0x12cf26a)', 'owner_unexpected_writer'),
                    require(f'(@rdi+0x10) == (@$t1+0x{addresses[i]:x})', 'owner_destination_mismatch'),
                    require('@rsi < 0n16', 'owner_source_index_out_of_range'),
                    require('$vvalid(@r14,8)', 'owner_header_unreadable'),
                    require('$vvalid(@r14+0x20,8)', 'owner_secondary_unreadable'),
                    'r @$t9=poi(@r14)', 'r @$t10=poi(@r14+0x20)',
                    require('((@$t9 == (@$t1+0x23d61a8)) and (@$t10 == (@$t1+0x23d61d8))) or ((@$t9 == (@$t1+0x23d6228)) and (@$t10 == (@$t1+0x23d6258)))', 'owner_table_pair_mismatch'),
                    require('$vvalid(@r14+0x50,8)', 'owner_source_link_unreadable'),
                    'r @$t11=poi(@r14+0x50)',
                    require('(@$t11 != 0) and (@$t11 < 0x7fffffffff90)', 'owner_source_link_invalid'),
                    require('$vvalid(@$t11+0x60,8)', 'owner_source_interface_unreadable'),
                    require('poi(@$t11+0x60) == @r13', 'owner_source_interface_mismatch'),
                    require('$vvalid(@r13,8)', 'source_table_unreadable'),
                    'r @$t12=poi(@r13)',
                    require('(@$t12 >= @$t1) and (@$t12 < (@$t1+0xa01d000-0x18))', 'source_table_outside_image'),
                    require('$vvalid(@$t12+0x10,8)', 'source_accessor_unreadable'),
                    'r @$t13=poi(@$t12+0x10)',
                    require('(@$t13 >= @$t1) and (@$t13 < (@$t1+0xa01d000))', 'source_accessor_outside_image'),
                    require('$vvalid(@rax+0x20,8)', 'source_array_link_unreadable'),
                    'r @$t14=poi(@rax+0x20)',
                    require('(@$t14+(@rsi*0x10)) == @rbx', 'source_array_arithmetic_mismatch'),
                    '.printf "MGS_VECTOR_OWNER primary=%p secondary=%p source_owner=%p source_table=%p source_accessor=%p source_array=%p\\n", @$t9, @$t10, @$t11, @$t12, @$t13, @$t14',
                ]
                # Latch each short command independently, as in setup. Graph
                # reads happen only at the identified natural store boundary.
                capture = [*graph, *capture]
        lines = ['r @$t0=0', '.catch { ' + '; '.join(capture) + ' }', cleanup(),
                 '.if (@$t0 == 0) { ' + notice('MGS_ABORT vector_capture_failed') + ' }',
                 notice('MGS_VECTOR_DETACH'), '.detach /h', 'q']
        if capture_owner:
            lines = ['r @$t0=1'] + [
                '.if (@$t0 != 0) { r @$t0=0; .catch { '+command+'; r @$t0=1 } }'
                for command in capture] + lines[2:]
        if any(len(line) >= 4000 for line in lines):
            raise ValueError('Debugger capture line exceeds bounded command length')
        if skip_bank_copy:
            captures[i].write_text('\n'.join(lines)+'\n', encoding='ascii')
            condition = ' or '.join(f'(@rip == (@$t1+0x{site:x}))' for site in copy_rips)
            if not fixture:
                condition = f'({condition}) and ((@rcx-0x70) == (@$t1+0x{addresses[i]:x}))'
            lines = [f'.if ({condition}) {{ {call_script(copy_script)}; }} .else {{ {call_script(captures[i])}; }}']
        hits[i].write_text('\n'.join(lines)+'\n', encoding='ascii')
    if skip_bank_copy:
        copy_script.write_text('\n'.join([
            'r @$t8 = @$t8 + 1', notice('MGS_VECTOR_COPY_SKIPPED'),
            f'.if (@$t8 > 0n120) {{ {notice("MGS_ABORT copy_skip_limit")}; {call_script(failed)}; }} .else {{ g }}'])+'\n', encoding='ascii')
    output.write_text('\n'.join(initial)+'\n', encoding='ascii')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', action='store_true')
    parser.add_argument('--wrong-bytes', action='store_true')
    parser.add_argument('--watch-published', action='store_true')
    parser.add_argument('--skip-bank-copy', action='store_true')
    parser.add_argument('--capture-owner', action='store_true')
    args = parser.parse_args()
    try:
        generate(args.executable, args.output, args.fixture, args.wrong_bytes, args.watch_published, args.skip_bank_copy, args.capture_owner)
    except (OSError, ValueError) as error:
        parser.error(str(error))
