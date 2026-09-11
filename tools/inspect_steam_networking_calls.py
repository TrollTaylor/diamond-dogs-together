#!/usr/bin/env python3
"""Independently verify natural SteamNetworking callers in the exact game files.

Static disassembly only. Does not load a DLL, read a process, or call an export.
Requires the existing local Capstone dependency used by engine investigation.
"""
from pathlib import Path
from bisect import bisect_right
from datetime import datetime, timezone
import argparse
import hashlib
import json
import struct
from analyze_network_bindings import Analysis
from inspect_game import PEError

GAME_SHA = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
STEAM_SHA = '08bd0152356e5619ee56a6714ac951b6800dbb21821693a4efaf85278442c60a'
CALLS = (0x1a5f749,0x1a5f758,0x1a5f789,0x1a5f798,0x1a5f8e0,
         0x1a5f8eb,0x1a5fad9,0x1a5fb96,0x1a5fc0f)

def inspect(directory):
    game_data = (directory/'mgsvtpp.exe').read_bytes()
    steam_data = (directory/'steam_api64.dll').read_bytes()
    if hashlib.sha256(game_data).hexdigest() != GAME_SHA or hashlib.sha256(steam_data).hexdigest() != STEAM_SHA:
        raise PEError('Unsupported file fingerprint')
    game, steam = Analysis(game_data), Analysis(steam_data)
    exports = [e for e in steam.pe.exports() if e['name'] == 'SteamNetworking']
    getter_bytes = bytes.fromhex('488b0571b00200c3')
    if len(exports) != 1 or int(exports[0]['rva'],16) != 0x5e30 or exports[0].get('forwarder') or steam.pe.rva_read(0x5e30,8) != getter_bytes:
        raise PEError('Unexpected getter export')
    references = []
    for section in game.pe.sections:
        if not section['characteristics'] & 0x20000000:
            continue
        raw = game_data[section['raw_offset']:section['raw_offset']+section['raw_size']]
        for opcode in (b'\xff\x15', b'\xff\x25'):
            at = 0
            while (at := raw.find(opcode, at)) >= 0:
                rva = section['rva']+at
                if at+6 <= len(raw) and rva+6+struct.unpack_from('<i',raw,at+2)[0] == 0x208e138:
                    references.append(rva)
                at += 1
    if sorted(references) != list(CALLS):
        raise PEError('Direct IAT reference inventory changed')
    functions = {}
    callers = []
    for call in CALLS:
        index = bisect_right(game.starts,call)-1
        if index < 0:
            raise PEError('Missing unwind boundary')
        begin,end,_ = game.ranges[index]
        if not begin <= call < end or end-begin > 2048:
            raise PEError('Unsupported enclosing function')
        instructions = game.instructions(begin,end-begin)
        aligned = [i for i in instructions if i.address-game.pe.image_base == call]
        if len(aligned) != 1 or aligned[0].mnemonic != 'call' or aligned[0].size != 6:
            raise PEError('IAT reference is not an aligned call')
        if not instructions or instructions[-1].address+instructions[-1].size != game.pe.image_base+end:
            raise PEError('Incomplete function disassembly')
        callers.append({'call_rva':hex(call),'return_rva':hex(call+6),'function_rva':hex(begin),
                        'call_bytes':aligned[0].bytes.hex()})
        functions[hex(begin)] = {'end_rva_exclusive':hex(end),
            'bytes':game.pe.rva_read(begin,end-begin).hex(),
            'instructions':[{'rva':hex(i.address-game.pe.image_base),'bytes':i.bytes.hex(),
                             'mnemonic':i.mnemonic,'operands':i.op_str} for i in instructions]}
    return {'utc':datetime.now(timezone.utc).isoformat(),'scope':'static_file_only',
            'executable_sha256':GAME_SHA,'steam_dll_sha256':STEAM_SHA,
            'getter_export_rva':'0x5e30','getter_bytes':getter_bytes.hex(),
            'getter_pointer_storage_rva':'0x30ea8','iat_rva':'0x208e138',
            'callers':callers,'functions':functions,'exports_called':False,
            'runtime_access_authorized':False,
            'limitation':'Direct FF15/FF25 inventory only; indirect/cached/dynamic users and runtime ownership are unverified'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-directory',type=Path,default=Path('D:/Games/steamapps/common/MGS_TPP'))
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    report = inspect(args.game_directory)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f"Verified {len(report['callers'])} aligned calls in {len(report['functions'])} functions; static evidence only")
