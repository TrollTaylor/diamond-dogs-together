#!/usr/bin/env python3
"""Regenerate the exact C0 preservation contract from retail PE; no game calls."""
from pathlib import Path
import argparse, hashlib, json, struct, sys
PROJECT = Path(__file__).resolve().parent.parent
# Installed in main/tools for integration; scratch runs pass --project explicitly.
p = argparse.ArgumentParser()
p.add_argument('--project', type=Path, default=PROJECT)
p.add_argument('--executable', type=Path, required=True)
p.add_argument('--sites', type=Path, default=Path(__file__).with_name('camera_mode_window_sites.json'))
p.add_argument('--output', type=Path, default=PROJECT/'native/camera_mode_window_contract.hpp')
a = p.parse_args()
sys.path.insert(0, str(a.project/'tools'))
from inspect_game import PEFile
j = json.loads(a.sites.read_text(encoding='utf-8-sig'))
raw = a.executable.read_bytes()
if hashlib.sha256(raw).hexdigest() != j['executable_sha256']:
    raise SystemExit('exact executable hash mismatch')
pe = PEFile(raw)
lines = ['// Generated exact retail C0 contract. No native engine method is called.', '#pragma once', '#include <cstddef>', '#include <cstdint>', 'namespace mgscoop::camera_mode_window_contract {', 'struct Site {std::uintptr_t rva;std::size_t size;const unsigned char* bytes;};', 'struct Slot {std::uintptr_t vtable_rva,offset,target_rva;};']
for n, s in enumerate(j['sites']):
    size = s['end_exclusive']-s['rva']
    buf = pe.rva_read(s['rva'], size)
    if size <= 0 or len(buf) != size or size > 8192 or buf.hex() != s['bytes'] or hashlib.sha256(buf).hexdigest() != s['sha256']:
        raise SystemExit('complete site mismatch: '+s['name'])
    lines.append('inline constexpr unsigned char bytes_%d[]={%s};' % (n, ','.join('0x%02x'%v for v in buf)))
lines.append('inline constexpr Site sites[]={'+','.join('{0x%x,sizeof(bytes_%d),bytes_%d}'%(s['rva'],n,n) for n,s in enumerate(j['sites']))+'};')
for s in j['slots']:
    if struct.unpack('<Q', pe.rva_read(s['vtable_rva']+s['offset'],8))[0] != pe.image_base+s['target_rva']:
        raise SystemExit('slot mismatch')
lines.append('inline constexpr Slot slots[]={'+','.join('{0x%x,0x%x,0x%x}'%(s['vtable_rva'],s['offset'],s['target_rva']) for s in j['slots'])+'};')
lines.append('} // namespace mgscoop::camera_mode_window_contract')
a.output.write_text('\n'.join(lines)+'\n', encoding='utf-8')
print('C0 preservation contract:', len(j['sites']), 'full ranges/explicit leaves,', len(j['slots']), 'slots')
