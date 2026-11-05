#!/usr/bin/env python3
"""Checks tools/enemy_div_report.py on a few fixed log lines (enemy round 7 track A, design 7.20).

Usage: python -I tests/enemy_div_report_check.py   (exit 0 when every check passes; no game, no file written)
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('enemy_div_report', os.path.join(HERE, '..', 'tools', 'enemy_div_report.py'))
rep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rep)

checks = 0


def check(ok, why):
    global checks
    checks += 1
    if not ok:
        print('FAIL %d: %s' % (checks, why))
        sys.exit(1)


host = [
    # Run 35d4074f: the host's copy of 1179373153 fell; the report counted guard_fell on guest lines only.
    'world_lua=event=place_div; ent=1179373153; d_m=82.5; dy_m=-10.5; why=fall; t_ms=329630983',
    'world_lua=event=readback; ent=2929558510; src=vert; at=500; ms=512; dy_m=0.10; held=1; far_m=48; t_ms=1',
    'world_lua=event=readback; ent=2929558510; src=vert; at=2000; ms=2010; dy_m=-3.20; held=0; far_m=48; t_ms=2',
    'world_lua=event=readback; ent=1179373153; src=place; at=500; ms=530; dy_m=-3.70; held=0; far_m=150; t_ms=3',
    'world_lua=event=readback_fix; ent=1179373153; src=place; far_m=150; dy_m=-3.70; put=1; t_ms=4',
    'world_drive=vert; go=1041; slot=7; gap_cm=330; e_cm=104; r=0; h=1; n=1',
    'world_drive=vert_stop; go=1041; slot=7; n=2; left_ms=8000',
]
guest = [
    'world_lua=event=readback; ent=1799112697; src=vert; at=500; ms=505; dy_m=0.00; held=1; far_m=30; t_ms=5',
]
r = rep.analyse(host, guest)
check(r['never']['guard_fell'] == 1, 'guard_fell counts the host copies too')
rb = r['readback']
check(rb['host']['vert'] == {'held': 1, 'fell': 1} and rb['host']['place'] == {'held': 0, 'fell': 1} and
      rb['guest']['vert'] == {'held': 1, 'fell': 0}, 'read-backs per side and source, held / fell')
check(r['readback_fell_guards'] == {'host': ['1179373153', '2929558510'], 'guest': []}, 'the guards that fell, per side')
check(r['readback_fix'] == {'host': 1, 'guest': 0} and r['vert_snaps'] == {'host': 1, 'guest': 0} and
      r['vert_stop'] == {'host': 1, 'guest': 0}, 'put-backs, vert snaps and vert stops per side')
print('enemy_div_report_check: %d checks passed' % checks)
