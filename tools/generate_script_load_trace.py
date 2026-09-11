"""Prepare one natural luaL_loadbuffer entry/parser-return observation.

Uses the existing proven startup bootstrap; no game calls or Lua stack writes.
Return is observed at the loader's own epilogue with the original thread/frame.
"""
import argparse
import hashlib
from pathlib import Path
from generate_registration_trace import (PeImage, GAME_SHA256, command_texts,
    byte_guards, require, notice, optional_breakpoint, breakpoint_action, call_script)

RVA = 0x1A178E0
BODY = bytes.fromhex('4883ec3848895424204c894424284c8d442420488d15a6f2ffffe8019cffff4883c438c3')


def generate(executable, output):
    data=executable.read_bytes()
    if executable.name.lower()!='mgsvtpp.exe' or hashlib.sha256(data).hexdigest()!=GAME_SHA256:
        raise ValueError('Unsupported executable')
    pe=PeImage(data)
    if pe.at_rva(RVA,len(BODY),code=True)!=BODY:
        raise ValueError('Script-loader body changed')
    bootstrap=output.with_suffix('.bootstrap.cmd')
    hit=output.with_suffix('.hit.cmd')
    returned=output.with_suffix('.return.cmd')
    initial,boot,_=command_texts('mgsvtpp',pe,RVA,BODY[:14],bootstrap,hit)
    initial=initial.replace('MGS_HARDWARE_REGISTRAR','MGS_HARDWARE_SCRIPT_LOADER')
    boot=boot.replace('MGS_HARDWARE_REGISTRAR','MGS_HARDWARE_SCRIPT_LOADER')
    checks=[require('@rip == @$t3','script_entry_address'),*byte_guards('@$t3',BODY[:14],'script_entry'),
            require('(@rsp > 0x38) and (@rsp < 0x7fffffffffffffff)','script_stack'),
            'r @$t8 = @rsp','r @$t9 = @rcx','r @$t10 = @$tid',f'r @$t11 = @$t1+0x{RVA+31:x}',
            *byte_guards('@$t11',BODY[31:],'script_epilogue'),
            optional_breakpoint(2,14),require('@$t14 == 0','return_breakpoint_occupied'),
            '.printf "MGS_SCRIPT_LOAD_ENTRY tid=0x%x state_opaque=%p entry_rsp=%p buffer=%p size=%I64u name=%p\\n", @$tid,@rcx,@rsp,@rdx,@r8,@r9',
            '.if ($vvalid(@r9,0x80)) { da @r9 L80 }',
            '.if ((@r8 >= 0x10) and (@r8 <= 0x400000)) { .if ($vvalid(@rdx,0x10)) { db @rdx L10 } }',
            'r @$t12 = 1']
    condition='(@$tid == @$t10) and (@rsp == (@$t8-0x38))'
    action=f'.if ({condition}) {{ {call_script(returned)}; }} .else {{ gc }}'
    escaped='"'+action.replace('\\','\\\\').replace('"','\\"')+'"'
    checks += [f'ba2 e1 @$t11 {escaped}',require('@$bp2 == @$t11','script_return_not_armed'),notice('MGS_SCRIPT_LOAD_RETURN_ARMED')]
    # Independent short catches avoid CDB's command-line length limit. Each
    # failure is latched and no later setup command may run after it.
    lines=['r @$t0 = 1','r @$t5 = 0','r @$t12 = 0']
    for check in checks:
        lines += ['r @$t15 = 0',f'.if (@$t0 != 0) {{ .catch {{ {check}; r @$t15 = 1 }} }}',
                  '.if (@$t15 == 0) { r @$t0 = 0 }']
    lines += [f'.if (@$t0 == 0) {{ {call_script(returned)}; }} .else {{ g }}']
    return_checks=[require('@rip == @$t11','script_return_address'),require(condition,'script_return_frame'),
                   *byte_guards('@$t11',BODY[31:],'script_return_bytes'),
                   '.printf "MGS_SCRIPT_LOAD_RETURN tid=0x%x state_opaque=%p entry_rsp=%p epilogue_rsp=%p eax=%u\\n", @$tid,@$t9,@$t8,@rsp,@eax']
    return_lines=['r @$t0 = 1']
    for check in return_checks:
        return_lines += ['r @$t15 = 0',f'.if (@$t0 != 0) {{ .catch {{ {check}; r @$t15 = 1 }} }}',
                         '.if (@$t15 == 0) { r @$t0 = 0 }']
    return_lines += ['.catch { .if (@$t12 != 0) { bc 2 } }',
                     '.if (@$t0 == 0) { '+notice('MGS_ABORT script_load_capture_failed')+' }',
                     '.detach /h','q']
    texts=[initial,boot,'\n'.join(lines)+'\n','\n'.join(return_lines)+'\n']
    paths=[output,bootstrap,hit,returned]
    for path,text in reversed(list(zip(paths,texts))):
        call_script(path)
        path.write_text(text,encoding='ascii')
    print('Prepared script-loader entry/epilogue capture; no process launched')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    generate(args.executable,args.output.resolve())
