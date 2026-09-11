"""Map marker loading methods from the pinned executable; no game execution."""
import hashlib
import json
import struct
from pathlib import Path
from analyze_network_bindings import Analysis
from analyze_host_allocations import GAME_SHA256
root=Path(__file__).resolve().parent.parent
path=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
raw=path.read_bytes()
if hashlib.sha256(raw).hexdigest()!=GAME_SHA256: raise SystemExit('Executable mismatch')
a=Analysis(raw)
sites=[('fixed',0x5C53A0,325),('follow',0x5C5530,448),('remove',0x5C6250,60)]
functions=[]
for name,rva,size in sites:
    ins=a.instructions(rva,size)
    if sum(i.size for i in ins)!=size or ins[-1].mnemonic!='ret': raise SystemExit('Incomplete marker decode')
    ranges=[(b,e,u) for b,e,u in a.ranges if rva<=b<rva+size]
    if ranges[0][0]!=rva or ranges[-1][1]!=rva+size or any(x[1]!=y[0] for x,y in zip(ranges,ranges[1:])):
        raise SystemExit('Marker unwind boundaries changed')
    functions.append(dict(name=name,rva=hex(rva),size=size,sha256=hashlib.sha256(a.pe.rva_read(rva,size)).hexdigest(),
        unwind_ranges=[dict(begin=hex(b),end=hex(e),unwind=hex(u)) for b,e,u in ranges],
        instructions=[dict(rva=hex(i.address-a.pe.image_base),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]))
slots={0x21ADE40:0x5C53A0,0x21ADE50:0x5C5530,0x21ADE58:0x5C6250,0x21ADE60:0x5C6190}
for r,t in slots.items():
    if struct.unpack('<Q',a.pe.rva_read(r,8))[0]!=a.pe.image_base+t:raise SystemExit('Marker table changed')
if a.pe.cstring(0x2255EB8)!='RemovedAllUserMarker':raise SystemExit('Named reset binding changed')
support=[a.function(r) for r in [0x8D6090,0x5C6190,0x5C30C0,0x5C32A0,0x5C6D40,0x5C6B90]]
lines=['// Generated marker-loading contract for the fingerprinted executable.','#pragma once','#include <cstdint>',
       'namespace mgscoop::marker_loading_contract {']
for name,rva,size in sites:
    lines += [f'inline constexpr std::uintptr_t {name}_rva={hex(rva)};',
        f'inline constexpr unsigned char {name}_body[]={{'+','.join(f'0x{x:02x}' for x in a.pe.rva_read(rva,size))+'};']
lines += ['struct Slot {std::uintptr_t rva,target;};','inline constexpr Slot slots[]={'+','.join('{'+hex(r)+','+hex(t)+'}' for r,t in slots.items())+'};','}','']
(root/'native/marker_loading_contract.hpp').write_text('\n'.join(lines))
report=dict(executable=str(path),executable_sha256=GAME_SHA256,functions=functions,supporting_functions=support,
    table_rva='0x21addf0',slots={hex(r):hex(t) for r,t in slots.items()},named_reset='RemovedAllUserMarker',
    provenance='Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e marker-loading guards; GPLv3 + Commons Clause 1.0',
    mapping='Named reset wrapper dispatches context+0x98->+0x80 interface slot+0x70; constructor/destructor initialize table 0x21addf0. Reset clears the five marker active bits and count. Fixed/follow methods in the same table copy RDX xyz into 48-byte marker records, manipulate the same count and capacity, and use shared remove/reorder helpers. Follow also consumes R8W object ID and returns Boolean AL; its complete body spans three adjacent unwind ranges. Remove takes EDX marker index, invokes shared removal and decrements the count.',
    limitations=['Static mapping only; no game execution or runtime lifetime verification.',
                 'The loading guard deliberately drops native marker requests and returns true for ignored follow requests, as upstream does.',
                 'The guard does not implement shared marker networking or replay dropped requests.'])
(root/'docs/evidence/marker-loading-contract-2026-09-10.json').write_text(json.dumps(report,indent=2)+'\n')
print('Recorded three complete marker methods with named reset, table and lifecycle evidence.')
