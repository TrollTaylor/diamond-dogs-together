"""Observe one natural archive mount and its common return path in exact MGSV.

Uses the existing bounded startup debugger and detach protocol. No mount calls,
argument edits, on-disk game writes or retained engine objects are introduced.
"""
import argparse
import hashlib
from pathlib import Path
from generate_registration_trace import (PeImage, GAME_SHA256, command_texts,
    byte_guards, require, notice, optional_breakpoint, call_script)

RVA = 0x48E20
END = 0x48F8C
RESULT_RVA = 0x48F6B
BODY_SHA = 'dc556b49319c37495c86d14dbb6eb93f22251aa4eaa4fb7a54deae4bd0fffea7'
PROLOGUE = bytes.fromhex('40555657415641574883ec7048c7442430feffffff')


def guarded(checks):
    lines = []
    for check in checks:
        lines += ['r @$t15 = 0', f'.if (@$t0 != 0) {{ .catch {{ {check}; r @$t15 = 1 }} }}',
                  '.if (@$t15 == 0) { r @$t0 = 0 }']
    return lines


def generate(executable, output):
    data = executable.read_bytes()
    if executable.name.lower() != 'mgsvtpp.exe' or hashlib.sha256(data).hexdigest() != GAME_SHA256:
        raise ValueError('Unsupported executable')
    pe = PeImage(data)
    body = pe.at_rva(RVA, END-RVA, code=True)
    if hashlib.sha256(body).hexdigest() != BODY_SHA or not body.startswith(PROLOGUE):
        raise ValueError('Archive creation body changed')
    epilogue = body[RESULT_RVA-RVA:]
    bootstrap, hit, returned = [output.with_suffix(s) for s in ('.bootstrap.cmd', '.hit.cmd', '.return.cmd')]
    initial, boot, _ = command_texts('mgsvtpp', pe, RVA, PROLOGUE, bootstrap, hit)
    initial = initial.replace('MGS_HARDWARE_REGISTRAR', 'MGS_HARDWARE_ARCHIVE_MOUNT')
    boot = boot.replace('MGS_HARDWARE_REGISTRAR', 'MGS_HARDWARE_ARCHIVE_MOUNT')
    checks = [require('@rip == @$t3', 'mount_entry_address'), *byte_guards('@$t3', PROLOGUE, 'mount_entry'),
              require('(@rsp > 0x98) and (@rsp < 0x7fffffffffffffff)', 'mount_stack'),
              'r @$t8 = @rsp', 'r @$t9 = @rcx', 'r @$t10 = @$tid', f'r @$t11 = @$t1+0x{RESULT_RVA:x}',
              *byte_guards('@$t11', epilogue, 'mount_result_path'),
              optional_breakpoint(2, 14), require('@$t14 == 0', 'return_breakpoint_occupied'),
              '.printf "MGS_MOUNT_ENTRY tid=0x%x entry_rsp=%p name=%p path=%p type_tag=0x%I64x flags=0x%x caller=%p\\n", @$tid,@rsp,@rcx,@rdx,@r8,@r9d,poi(@rsp)',
              '.if ($vvalid(@rcx,0x100)) { .echo MGS_MOUNT_NAME; da @rcx L100 }',
              '.if ($vvalid(@rdx,0x100)) { .echo MGS_MOUNT_PATH; da @rdx L100 }',
              'r @$t12 = 1']
    # Five nonvolatile pushes (0x28) and sub rsp,0x70 precede both return paths.
    condition = '(@$tid == @$t10) and (@rsp == (@$t8-0x98))'
    action = f'.if ({condition}) {{ {call_script(returned)}; }} .else {{ gc }}'
    escaped = '"' + action.replace('\\', '\\\\').replace('"', '\\"') + '"'
    checks += [f'ba2 e1 @$t11 {escaped}', require('@$bp2 == @$t11', 'mount_return_not_armed'), notice('MGS_MOUNT_RETURN_ARMED')]
    hit_lines = ['r @$t0 = 1', 'r @$t5 = 0', 'r @$t12 = 0'] + guarded(checks)
    hit_lines += [f'.if (@$t0 == 0) {{ {call_script(returned)}; }} .else {{ g }}']
    return_checks = [require('@rip == @$t11', 'mount_return_address'), require(condition, 'mount_return_frame'),
                     *byte_guards('@$t11', epilogue, 'mount_return_bytes'),
                     '.printf "MGS_MOUNT_RETURN tid=0x%x entry_rsp=%p result_rsp=%p handle=%p\\n", @$tid,@$t8,@rsp,@rax',
                     '.if (@rax != 0) { .if ($vvalid(@rax,0x30)) { .printf "MGS_MOUNT_RAW type_offset10=%u value_offset18=%p tag_offset28=0x%I64x\\n", dwo(@rax+0x10),poi(@rax+0x18),poi(@rax+0x28) } }']
    return_lines = ['r @$t0 = 1'] + guarded(return_checks)
    return_lines += ['.catch { .if (@$t12 != 0) { bc 2 } }',
                     '.if (@$t0 == 0) { ' + notice('MGS_ABORT mount_capture_failed') + ' }', '.detach /h', 'q']
    for path, text in reversed(list(zip([output, bootstrap, hit, returned],
                                       [initial, boot, '\n'.join(hit_lines)+'\n', '\n'.join(return_lines)+'\n']))):
        call_script(path)
        path.write_text(text, encoding='ascii')
    print('Prepared natural archive mount entry/result observation; no process launched')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.executable, args.output.resolve())
