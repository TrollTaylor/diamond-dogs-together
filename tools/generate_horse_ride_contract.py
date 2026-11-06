#!/usr/bin/env python3
"""Generate the RideHorse ride-record contract (step S3: MGS_COOP_HORSE_DRIVE_V1 rrec / getoff) from the exact local PE."""
from pathlib import Path
import argparse,hashlib,json,struct,sys
PROJECT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(PROJECT/'tools'))
from inspect_game import PEFile
parser=argparse.ArgumentParser();parser.add_argument('--executable',type=Path,required=True);parser.add_argument('--sites',type=Path,default=Path(__file__).with_name('horse_ride_sites.json'));parser.add_argument('--output',type=Path,default=PROJECT/'native/horse_ride_contract.hpp');parser.add_argument('--check',action='store_true');args=parser.parse_args()
j=json.loads(args.sites.read_text());raw=args.executable.read_bytes()
if hashlib.sha256(raw).hexdigest()!=j['executable_sha256']:raise SystemExit('exact executable hash mismatch')
pe=PEFile(raw);lines=['// Generated from exact retail PE only (tools/generate_horse_ride_contract.py, tools/horse_ride_sites.json).','// MGS_COOP_HORSE_DRIVE_V1 rrec / getoff (step S3): the RideHorse action (vtable 0x1423CB600) found in a player block, its','// per-player ride record read; getoff writes the next state 3 as the stock get-off callers do. Every site and slot is','// compared in memory first (native/horse_ride.cpp PinsReason).','#pragma once','#include <cstddef>','#include <cstdint>','namespace mgscoop::horse_ride_contract {','struct Site {std::uintptr_t rva;std::size_t size;const unsigned char* bytes;};','struct Slot {std::uintptr_t vtable_rva,offset,target_rva;};']
for n,s in enumerate(j['sites']):
 buf=pe.rva_read(s['rva'],len(bytes.fromhex(s['bytes'])))
 if buf.hex()!=s['bytes'] or hashlib.sha256(buf).hexdigest()!=s['sha256'] or len(buf)>8192:raise SystemExit('site mismatch')
 lines.append('// 0x%x: %s'%(s['rva'],s['why']))
 lines.append('inline constexpr unsigned char bytes_%d[]={%s};'%(n,','.join('0x%02x'%v for v in buf)))
lines.append('inline constexpr Site sites[]={'+','.join('{0x%x,sizeof(bytes_%d),bytes_%d}'%(s['rva'],n,n) for n,s in enumerate(j['sites']))+'};')
for s in j['slots']:
 if struct.unpack('<Q',pe.rva_read(s['vtable_rva']+s['offset'],8))[0]!=pe.image_base+s['target_rva']:raise SystemExit('slot mismatch')
lines.append('inline constexpr Slot slots[]={'+','.join('{0x%x,0x%x,0x%x}'%(s['vtable_rva'],s['offset'],s['target_rva']) for s in j['slots'])+'};')
lines.append('// The action object: vtable at +0, the obj+0x10 / obj+0x30 interfaces, +8 its player block, +0x38 the id node (+0x24 base id),')
lines.append('// +0x68 the records; a block keeps its actions in two 0x45-slot arrays at +0x190 and +0x3B8 (the block ctor 0x1409BAC80).')
lines.append('inline constexpr std::uintptr_t ride_vtable=0x23cb600,ride_vtable_10=0x23cb680,ride_vtable_30=0x23cb6b0;')
lines.append('inline constexpr std::size_t obj_block=0x8,obj_node=0x38,node_base_id=0x24,obj_records=0x68,record_size=0x2b0;')
lines.append('inline constexpr std::size_t block_actions=0x190,block_action_slots=0x8a,block_player=0x138;')
lines.append('inline constexpr std::size_t rec_state=0x10,rec_next=0x11,rec_sub=0x18,rec_again=0x235,sub_rider=0x7c,sub_select=0x82,sub_forced=0x16e;')
lines.append('inline constexpr std::uint8_t getoff_state=3,getoff_select=6;')
lines.append('} // namespace mgscoop::horse_ride_contract')
result='\n'.join(lines)+'\n'
if args.check:
 if args.output.read_text(encoding='utf-8')!=result:raise SystemExit('Generated contract is stale')
else:args.output.write_text(result,encoding='utf-8')
print('horse ride contract:',len(j['sites']),'sites,',len(j['slots']),'slots')
