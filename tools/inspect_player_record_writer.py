"""Reproduce the exact-build static path for the naturally observed record write.

Reads only the installed executable. The live receipt is separate evidence;
this tool does not attach, call game code, or infer actor ownership.
"""
import argparse
from bisect import bisect_right
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

from analyze_network_bindings import Analysis
from generate_registration_trace import GAME_SHA256

CHECKS = {
    0x12CC578: '488d4be0',       # caller passes enclosing object minus 0x20
    0x12CC57C: 'e8ef220000',     # direct call to updater
    0x12CE8E6: '488b4150',
    0x12CE8EA: '4c8bf1',         # preserve updater receiver in R14
    0x12CE8ED: '4c8b6860',
    0x12CE8F1: '498bcd',
    0x12CE8F9: '498b4500',
    0x12CE8FD: 'ff5010',         # source collection virtual accessor
    0x12CE90B: '48894580',
    0x12CEA00: '498b4e58',       # destination interface
    0x12CEA04: '8bd3',
    0x12CEA09: 'ff5008',
    0x12CEA0F: '488bf8',
    0x12CF23A: '488bde',
    0x12CF23D: '48c1e304',
    0x12CF247: '488b4580',
    0x12CF24B: '48035820',       # source array + index * 16
    0x12CF24F: 'f30f104308',
    0x12CF254: 'f30f105304',
    0x12CF259: 'f30f101b',
    0x12CF266: '0f295f10',       # actual observed vector store
    0x12CF26A: 'f30f106b0c',     # raw RIP reported after store
    0x53CB79: '0f294990',        # baseline copy store
    0x53CB7D: '0f2842a0',
}
SPANS = [(0x12CC550, 0x12CC581), (0x12CE8E6, 0x12CE931),
         (0x12CEA00, 0x12CEA2D), (0x12CF232, 0x12CF26F),
         (0x53CB10, 0x53CBBF)]


def inspect(executable):
    data = executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != GAME_SHA256:
        raise ValueError('Unsupported executable fingerprint')
    analysis = Analysis(data)
    index = bisect_right(analysis.starts, 0x12CF26A) - 1
    begin, end, unwind = analysis.ranges[index]
    if (begin, end) != (0x12CE870, 0x12D0C24):
        raise ValueError('Updater exception range changed')
    # Decode from the declared entry to establish the observed store boundary.
    aligned = {i.address-analysis.pe.image_base: i
               for i in analysis.instructions(begin, 0x12CF26F-begin)}
    spans, found = [], {}
    for start, stop in SPANS:
        instructions = analysis.instructions(start, stop-start)
        if sum(i.size for i in instructions) != stop-start:
            raise ValueError('Span does not decode to its exact end')
        rows = []
        for instruction in instructions:
            rva = instruction.address-analysis.pe.image_base
            found[rva] = instruction.bytes.hex()
            rows.append(dict(rva=hex(rva), bytes_hex=instruction.bytes.hex(),
                             mnemonic=instruction.mnemonic, operands=instruction.op_str))
        spans.append(dict(begin_rva=hex(start), end_rva_exclusive=hex(stop),
                          sha256=hashlib.sha256(analysis.pe.rva_read(start,stop-start)).hexdigest(),
                          instructions=rows))
    for rva, expected in CHECKS.items():
        if found.get(rva) != expected:
            raise ValueError(f'Instruction check failed at {rva:#x}')
        if begin <= rva < 0x12CF26F and rva not in aligned:
            raise ValueError('Instruction not aligned from declared updater entry')
    return dict(utc=datetime.now(timezone.utc).isoformat(), executable_sha256=GAME_SHA256,
                instruction_checks=len(CHECKS), updater_range=[hex(begin),hex(end)],
                unwind_rva=hex(unwind), spans=spans, runtime_calls=False,
                limitations=['Static dataflow only; live receipts are separate.',
                             'Source virtual accessor and actor allocation/lifetime unresolved.',
                             'Record write is not a complete or atomic actor snapshot.',
                             'No second actor or campaign support established.'])


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    result=inspect(args.executable)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(f"Verified {result['instruction_checks']} instructions across {len(SPANS)} spans; actor ownership unresolved")
