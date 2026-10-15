# Verifies DM overlay state names (docs/anim-round1-actions.md 4.3): each name's StrCode64 must be
# present in the retail TppPlayer2_layers.mog node pool and absent from the SQ-4 graph 2/3 node list
# (a graph-0 state the DirectMotion named-state branch can request). Read-only; prints one line per name.
# Usage: python -I tools/partner_overlay_mogcheck.py <dir with foxhash.py (sc64 = StrCode64)> <TppPlayer2_layers.mog>
#        <mog-nodes-2_3.json> <stateName>...
# The positive controls stateGunHold / stateThrowHoldEnd must report g23=('2', ...); a made-up name must
# report pool=0.
import json
import struct
import sys

sys.path.insert(0, sys.argv[1])
from foxhash import sc64  # noqa: E402

mog = open(sys.argv[2], 'rb').read()
g23 = json.load(open(sys.argv[3]))
codes23 = {}
for g, nodes in g23.items():
    for n in nodes:
        codes23[int(n['code'], 16)] = (g, n['i'], n['name'])
# The shared node/clip pool (0xa6820..0xb2ec8): 8-byte entries, the low 48 bits are the code.
T0, T1 = 0xa6820, 0xb2ec8
pool = {}
for o in range(T0, T1, 8):
    v = struct.unpack_from('<Q', mog, o)[0]
    pool.setdefault(v & 0xFFFFFFFFFFFF, []).append(o)
print('check', sc64('stateGunHold') == 0xAA0EA22A5E0E, 'stateGunHold', hex(sc64('stateGunHold')), 'in g23',
      sc64('stateGunHold') in codes23)
for name in sys.argv[4:]:
    h = sc64(name)
    b6 = h.to_bytes(8, 'little')[:6]
    print('%-26s %012x mog_bytes=%d pool=%d g23=%s' % (name, h, mog.count(b6), len(pool.get(h, [])), codes23.get(h, 'no')))
