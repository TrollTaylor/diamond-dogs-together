import argparse,json,hashlib,struct
from pathlib import Path
P=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description='Verify the exact read-only carry-query contract; never executes game code.')
parser.add_argument('--exe',type=Path,default=Path(r'D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe'))
parser.add_argument('--sites',type=Path,default=P/'carry_plugin_sites.json')
parser.add_argument('--output',type=Path,default=P/'carry_plugin_contract.hpp')
parser.add_argument('--check',action='store_true')
args=parser.parse_args()
data=json.loads(args.sites.read_text());raw=args.exe.read_bytes()
if hashlib.sha256(raw).hexdigest()!=data['executable_sha256']:raise SystemExit('Executable SHA256 mismatch')
pe=struct.unpack_from('<I',raw,0x3c)[0]
if raw[pe:pe+4]!=b'PE\0\0':raise SystemExit('Not PE')
num,opt=struct.unpack_from('<H',raw,pe+6)[0],pe+24
if struct.unpack_from('<H',raw,opt)[0]!=0x20b:raise SystemExit('Not PE32+')
base=struct.unpack_from('<Q',raw,opt+24)[0]
if base!=0x140000000:raise SystemExit('Unexpected PE image base')
sectionStart=opt+struct.unpack_from('<H',raw,pe+20)[0]
sections=[]
for k in range(num):
 off=sectionStart+k*40;virtualSize,rva,rawSize,ptr=struct.unpack_from('<IIII',raw,off+8)
 sections.append((rva,rawSize,ptr))
def read(rva,size):
 for start,count,off in sections:
  if start<=rva and rva+size<=start+count:return raw[off+rva-start:off+rva-start+size]
 raise SystemExit(f'Unmapped RVA {rva:x}')
lines=['// Generated from carry_plugin_sites.json, exact retail SHA256 '+data['executable_sha256'], '#pragma once','#include <cstddef>','#include <cstdint>','namespace mgscoop::carry_plugin_contract {','inline constexpr bool native_calls=false;']
for k,s in enumerate(data['sites']):
 expected=bytes.fromhex(s['bytes'])
 if not expected or len(expected)>8192:raise SystemExit('Invalid complete site size')
 if hashlib.sha256(expected).hexdigest()!=s['sha256']:raise SystemExit('Evidence bytes SHA mismatch')
 if read(s['rva'],len(expected))!=expected:raise SystemExit(f'Code bytes mismatch {s["rva"]:x}')
 lines.append('inline constexpr unsigned char site'+str(k)+'[]={'+','.join(f'0x{x:02x}' for x in expected)+'};')
lines+=['struct Site { std::uintptr_t rva; std::size_t size; const unsigned char* bytes; };','inline constexpr Site sites[]={']
lines += [f'{{0x{s["rva"]:x},sizeof(site{k}),site{k}}},' for k,s in enumerate(data['sites'])]
lines+=['};','struct Slot { std::uintptr_t vtable_rva; std::size_t offset; std::uintptr_t target_rva; };','inline constexpr Slot slots[]={']
for s in data['slots']:
 if struct.unpack('<Q',read(s['vtable_rva']+s['offset'],8))[0]!=base+s['target_rva']:raise SystemExit('Vtable slot mismatch')
 lines.append(f'{{0x{s["vtable_rva"]:x},0x{s["offset"]:x},0x{s["target_rva"]:x}}},')
lines+=['};','} // namespace mgscoop::carry_plugin_contract','']
output='\n'.join(lines)
if args.check:
 if not args.output.exists() or args.output.read_text()!=output:raise SystemExit('Generated header differs')
 print('Exact carry query contract verified;',len(data['sites']),'code/constant sites and',len(data['slots']),'typed slots')
else:args.output.write_text(output);print('Generated',args.output)
