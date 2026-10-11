#!/usr/bin/env python3
"""Generate the pure read-only ordinary Vehicle contract from exact local PE."""
from pathlib import Path
import argparse,hashlib,json,struct,sys
PROJECT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(PROJECT/'tools'))
from inspect_game import PEFile
parser=argparse.ArgumentParser();parser.add_argument('--executable',type=Path,required=True);parser.add_argument('--sites',type=Path,default=Path(__file__).with_name('vehicle_controller_sites.json'));parser.add_argument('--output',type=Path,default=PROJECT/'native/vehicle_controller_contract.hpp');parser.add_argument('--check',action='store_true');args=parser.parse_args()
j=json.loads(args.sites.read_text());raw=args.executable.read_bytes()
if hashlib.sha256(raw).hexdigest()!=j['executable_sha256']:raise SystemExit('exact executable hash mismatch')
pe=PEFile(raw);lines=['// Generated from exact retail PE only. Pure observer: no engine methods are called.','#pragma once','#include <cstddef>','#include <cstdint>','namespace mgscoop::vehicle_controller_contract {','struct Site {std::uintptr_t rva;std::size_t size;const unsigned char* bytes;};','struct Slot {std::uintptr_t vtable_rva,offset,target_rva;};']
for n,s in enumerate(j['sites']):
 buf=pe.rva_read(s['rva'],len(bytes.fromhex(s['bytes'])))
 if buf.hex()!=s['bytes'] or hashlib.sha256(buf).hexdigest()!=s['sha256'] or len(buf)>8192:raise SystemExit('site mismatch')
 lines.append('inline constexpr unsigned char bytes_%d[]={%s};'%(n,','.join('0x%02x'%v for v in buf)))
lines.append('inline constexpr Site sites[]={'+','.join('{0x%x,sizeof(bytes_%d),bytes_%d}'%(s['rva'],n,n) for n,s in enumerate(j['sites']))+'};')
for s in j['slots']:
 if struct.unpack('<Q',pe.rva_read(s['vtable_rva']+s['offset'],8))[0]!=pe.image_base+s['target_rva']:raise SystemExit('slot mismatch')
lines.append('inline constexpr Slot slots[]={'+','.join('{0x%x,0x%x,0x%x}'%(s['vtable_rva'],s['offset'],s['target_rva']) for s in j['slots'])+'};')
lines.append('inline constexpr std::uintptr_t phase_callbacks[]={'+','.join(j['phase_callbacks'])+'};');lines.append('} // namespace mgscoop::vehicle_controller_contract')
result='\n'.join(lines)+'\n'
if args.check:
 if args.output.read_text(encoding='utf-8')!=result:raise SystemExit('Generated contract is stale')
else:args.output.write_text(result,encoding='utf-8')
print('vehicle controller contract:',len(j['sites']),'sites,',len(j['slots']),'slots')
