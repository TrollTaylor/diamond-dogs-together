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
    # Run ddcdec94: 2961503 was driven on after his placement (place_div why=driven); 1.55 m over the old spot is the
    # ground he ran to, not a fall. A newer placement 1.6 s later re-armed his read-back, so the first got no 2 s line.
    'world_lua=event=readback; ent=2961503; src=place; at=500; ms=520; dy_m=1.55; held=0; far_m=70; t_ms=5',
    'world_lua=event=readback; ent=2961503; src=place; at=500; ms=510; dy_m=0.20; held=1; far_m=70; t_ms=6',
    'world_lua=event=readback; ent=2961503; src=place; at=2000; ms=2005; dy_m=0.30; held=1; far_m=70; t_ms=7',
    'world_drive=vert; go=1041; slot=7; gap_cm=330; e_cm=104; r=0; h=1; n=1',
    'world_drive=vert_stop; go=1041; slot=7; n=2; left_ms=8000',
]
guest = [
    'world_lua=event=readback; ent=1799112697; src=vert; at=500; ms=505; dy_m=0.00; held=1; far_m=30; t_ms=5',
    # Runs 90d9b4d1 / ddcdec94: the guest was downed in the ALERT and the host's copies of its guards drifted (median
    # per second up to 29 m / 52 m)
    # (native range refusals, near=host) until the end; the host M3 error before that down is reported apart.
    'world_lua=event=downed; step=entered; role=2; generation=4; cause=1; t_ms=1000',
]
host += [
    'world_lua=event=place_err; n=10; h=10,0,0,0,0,0,0; p50=0.2; p95=0.5; max=0.5; fix=0; q=0; t_ms=900',
    'world_lua=event=place_err; n=10; h=0,0,0,0,0,0,10; p50=40.0; p95=50.0; max=50.0; fix=0; q=0; t_ms=1100',
]
r = rep.analyse(host, guest)
check(r['never']['guard_fell'] == 1, 'guard_fell counts the host copies too')
rb = r['readback']
check(rb['host']['vert'] == {'held': 1, 'under': 1, 'above': 0} and
      rb['host']['place'] == {'held': 2, 'under': 1, 'above': 1} and
      rb['guest']['vert'] == {'held': 1, 'under': 0, 'above': 0}, 'read-backs per side and source, held / under / above')
check(r['readback_under_guards'] == {'host': ['1179373153', '2929558510'], 'guest': []} and
      r['readback_above_guards'] == {'host': ['2961503'], 'guest': []}, 'the guards under / above where they were put')
check(r['readback_2s']['host'] == {'armed': 4, 'got': 2, 'cut': 1, 'open': 1} and
      r['readback_2s']['guest'] == {'armed': 1, 'got': 0, 'cut': 0, 'open': 1},
      'the 2 s read-backs: got, cut by a newer arm of the same copy, still open at the log end')
check(r['readback_fix'] == {'host': 1, 'guest': 0} and r['vert_snaps'] == {'host': 1, 'guest': 0} and
      r['vert_stop'] == {'host': 1, 'guest': 0}, 'put-backs, vert snaps and vert stops per side')
check(r['guest_downed_at'] == 1000 and r['m3_host_err_samples_before_down'] == 10 and
      r['m3_host_err_p90_m_before_down'] is not None and r['m3_host_err_p90_m_before_down'] <= 1.0 and
      r['m3_host_err_p90_m'] > 15.0, 'the host M3 error before the guest\'s first down, apart from the whole run')
# Enemy round 8 track A (range_flat, design 7.22): the host M3 error and placements WHILE the guest is down (entered ..
# abandoned / grant, else the log end), and the guest's quiet moves (quiet_place).
guest2 = guest + [
    'world_lua=event=downed; step=abandoned; role=2; generation=4; next=5; t_ms=1200',
    'world_place=warp; seq=6; kind=3; ent=1926520947; go=1270; age_ms=0; r=0; h=1; leaf=quiet; refused=none',
    'world_place=quiet; go=1270; seq=6; moved=1; bias=0; err_m=0.00; dy_m=0.00',
    'world_place=warp; seq=7; kind=3; ent=1740692850; go=1039; age_ms=0; r=0; h=1; leaf=typed; refused=none',
    'world_lua=event=readback; ent=1926520947; src=quiet; at=500; ms=510; dy_m=0.10; held=1; far_m=56; dxz_m=0.40; t_ms=1300',
    'world_lua=event=readback; ent=1926520947; src=quiet; at=2000; ms=2010; dy_m=0.20; held=1; far_m=56; dxz_m=1.40; t_ms=1310',
]
host2 = host + [
    'world_lua=event=hplace; seq=1; ent=1; kind=correct; result=refused; why=range; native=range; err_m=0.00; frames=1; t_ms=1150',
    'world_lua=event=hplace; seq=2; ent=2; kind=correct; result=applied; why=none; native=none; err_m=0.10; frames=3; t_ms=1160',
    'world_lua=event=place_err; n=10; h=10,0,0,0,0,0,0; p50=0.2; p95=0.5; max=0.5; fix=0; q=0; t_ms=1300',
]
r2 = rep.analyse(host2, guest2)
check(r2['guest_down_windows'] == [[1000, 1200]] and r2['m3_host_err_samples_while_down'] == 10 and
      r2['m3_host_err_p90_m_while_down'] > 15.0 and r2['m3_hplace_while_down'] == {'applied': 1, 'refused': 1},
      'the host M3 error and placements while the guest is down')
check(all(r2['quiet'][k] == v for k, v in {'warps': 1, 'moved': 1, 'bias': 0, 'readback': 2, 'readback_within_1m': 1}.items()),
      'the guest\'s quiet moves and their read-backs (x/z and y within 1 m)')
# Enemy round 9 track A (design 7.24): the reasons a quiet move was not taken (quiet=<why> on the warp line), the 5 s
# read-back (the walk-on row: x/z within 2 m) and the per-move rows (0.5 s and 2 s both within 1 m).
guest3 = guest2 + [
    'world_place=warp; seq=8; kind=3; ent=1904171355; go=1255; age_ms=0; r=0; h=1; leaf=typed; refused=none; quiet=moving',
    'world_place=warp; seq=9; kind=3; ent=1926520947; go=1270; age_ms=0; r=0; h=1; leaf=quiet; refused=none; quiet=ok',
    'world_lua=event=readback; ent=1926520947; src=quiet; at=5000; ms=5010; dy_m=0.20; held=1; far_m=56; dxz_m=1.90; t_ms=1320',
]
r3 = rep.analyse(host2, guest3)
q3 = r3['quiet']
check(q3['why'] == {'moving': 1, 'ok': 1} and q3['readback_5s'] == 1 and q3['walk_on_within_2m_5s'] == 1 and
      q3['moves_both_within_1m'] == 0 and q3['moves_read'] == 1,
      'the quiet reasons, the 5 s walk-on read-back and the per-move read-back rows')
# Enemy round 11 track A (design 7.31): a walker's quiet move (quiet=walk), his re-plan (world_place=quiet_plan) and the
# owner rows: the 2 s and 10 s read-backs after it within 3 m of the owner copy (dho_m).
guest5 = guest3 + [
    'world_place=warp; seq=10; kind=3; ent=667680985; go=1060; age_ms=0; r=0; h=1; leaf=quiet; refused=none; quiet=walk',
    'world_place=quiet; go=1060; seq=10; moved=1; bias=1; err_m=0.00; dy_m=0.00',
    'world_place=quiet_plan; go=1060; seq=10; idx=36; mode=1; r19c=0xffff; r19e=0x006e; slot=0xdf31ba77279e; slot_h=0x0001; '
    'fol=1; step=cancel_plan; started=1; handle=0x0004; p=1.00,2.00,3.00; dest=4.00,2.00,3.00',
    'world_lua=event=readback; ent=667680985; src=quiet; at=500; ms=500; dy_m=0.00; held=1; far_m=80; dxz_m=0.20; dho_m=0.30; oage_ms=200; t_ms=1500',
    'world_lua=event=readback; ent=667680985; src=quiet; at=2000; ms=2000; dy_m=0.00; held=1; far_m=80; dxz_m=2.10; dho_m=1.20; oage_ms=300; t_ms=3000',
    'world_lua=event=readback; ent=667680985; src=quiet; at=5000; ms=5000; dy_m=0.00; held=1; far_m=80; dxz_m=6.00; dho_m=2.50; oage_ms=400; t_ms=6000',
    'world_lua=event=readback; ent=667680985; src=quiet; at=10000; ms=10000; dy_m=0.00; held=1; far_m=80; dxz_m=12.0; dho_m=4.10; oage_ms=900; t_ms=11000',
]
q5 = rep.analyse(host2, guest5)['quiet']
check(q5['why'] == {'moving': 1, 'ok': 1, 'walk': 1} and q5['walk_moves'] == 1 and q5['plans'] == {'cancel_plan': 1} and
      q5['plans_started'] == 1 and q5['walk_owner_2s'] == {'read': 1, 'within_3m': 1, 'walkers': 1} and
      q5['walk_owner_10s'] == {'read': 1, 'within_3m': 0, 'walkers': 1} and q5['plans_lost'] == 0,
      'round 11: quiet moves of walkers, their re-plans and the owner rows at 2 s and 10 s')
# Enemy round 11 track A, as the review checked it: a later quiet=ok move of the same guard is not a walker move (the move's
# own warp line names its kind, not the guard), a cancelled slot whose plan did not start is a lost plan (per guard), and
# the owner rows count the walkers read.
guest6 = guest5 + [
    'world_place=warp; seq=11; kind=3; ent=667680985; go=1061; age_ms=0; r=0; h=1; leaf=quiet; refused=none; quiet=ok',
    'world_place=quiet; go=1061; seq=11; moved=1; bias=1; err_m=0.00; dy_m=0.00',
    'world_lua=event=readback; ent=667680985; src=quiet; at=500; ms=500; dy_m=0.00; held=1; far_m=80; dxz_m=0.20; dho_m=0.30; oage_ms=200; t_ms=21500',
    'world_lua=event=readback; ent=667680985; src=quiet; at=2000; ms=2000; dy_m=0.00; held=1; far_m=80; dxz_m=0.30; dho_m=9.00; oage_ms=300; t_ms=23000',
    'world_place=quiet_plan; go=1062; seq=12; idx=37; mode=1; r19c=0xffff; r19e=0x006e; slot=0xdf31ba77279e; slot_h=0x0002; '
    'fol=1; step=cancel_plan; started=0; handle=0xffff; p=1.00,2.00,3.00; dest=4.00,2.00,3.00',
    'world_place=quiet_plan; go=1063; seq=13; idx=37; mode=1; r19c=0xffff; r19e=0x006e; slot=0xdf31ba77279e; slot_h=0x0003; '
    'fol=1; step=cancel_plan; started=0; handle=0xffff; p=1.00,2.00,3.00; dest=4.00,2.00,3.00',
]
q6 = rep.analyse(host2, guest6)['quiet']
check(q6['walk_moves'] == 1 and q6['walk_owner_2s'] == {'read': 1, 'within_3m': 1, 'walkers': 1} and
      q6['walk_owner_10s'] == {'read': 1, 'within_3m': 0, 'walkers': 1} and q6['plans_lost'] == 2 and
      q6['plans_lost_guards'] == 1,
      'round 11 review: a later quiet=ok move of a walker is not a walker move; lost plans per guard; walkers read')
# Enemy flip A-vert (user decision D-A10, design 7.27): drive_sink is information only; the gate row is "vert left under":
# a copy more than 1.5 m under its owner copy for more than 2 s after the owner copy was level in y for 1 s (per guard,
# with the time), from the native world_drive lines (no t_ms: the last t_ms before each).
host4 = [
    'world_place=warp; seq=1; kind=3; ent=1179373153; go=1050; age_ms=0; r=0; h=1; leaf=typed; refused=none',
    # The no-thrash cap stopped a due snap of 1179373153 (the owner level); his next snap came 6 s later: left 6 s.
    'world_lua=event=drive_sink; ent=1179373153; off_m=-3.00; e_cm=50; low_ms=2100; t_ms=20000',
    'world_drive=vert_stop; go=1050; slot=9; n=2; gap_cm=300; left_ms=6000',
    'world_lua=event=place_combat; seq=2; ent=1179373153; nudge=0; t_ms=26000',
    'world_drive=vert; go=1050; slot=9; gap_cm=300; e_cm=50; r=0; h=1; n=3; rest_ms=7000',
    # A late snap of 667680985 (go 1060, mapped from the guest's lines): the owner level 4 s, the copy low 2 s + 1.5 s.
    'world_lua=event=drive_sink; ent=667680985; off_m=-2.50; e_cm=40; low_ms=2000; t_ms=30000',
    'world_lua=event=place_combat; seq=3; ent=667680985; nudge=0; t_ms=31500',
    'world_drive=vert; go=1060; slot=10; gap_cm=250; e_cm=40; r=0; h=1; n=4; rest_ms=4000',
    # A snap down (the copy above his owner) is not under.
    'world_drive=vert; go=1070; slot=11; gap_cm=-579; e_cm=136; r=0; h=1; n=5; rest_ms=9000',
]
guest4 = [
    'world_lua=event=obs; m=Unconscious; ent=667680985; id=1060; att=none; t_ms=1',
    'world_place=warp; seq=137; kind=3; ent=2929558510; go=1041; age_ms=1; r=0; h=1; leaf=typed; refused=none',
    # Run 948c978a: the owner climbed (vert_wait, rest 0), drive_sink fired, the snap came 178 ms after the owner had been
    # level 1 s: not left under.
    'world_lua=event=place_combat; seq=159; ent=2929558510; nudge=0; t_ms=10000',
    'world_drive=vert_wait; go=1041; slot=124; gap_cm=348; rest_ms=0; span_cm=0',
    'world_lua=event=drive_sink; ent=2929558510; off_m=-3.48; e_cm=106; low_ms=2252; t_ms=10010',
    'world_drive=vert; go=1041; slot=124; gap_cm=444; e_cm=107; r=0; h=1; n=1; rest_ms=1178',
    # A copy that fell while his owner stood level long: snapped after drive_vert's own 1 s (no drive_sink): not left.
    'world_drive=vert; go=1080; slot=12; gap_cm=310; e_cm=20; r=0; h=1; n=2; rest_ms=12000',
    # The owner of 3268415846 went down a ladder (vert_wait) and nothing moved the copy: unresolved, not counted.
    'world_place=warp; seq=140; kind=3; ent=3268415846; go=1090; age_ms=1; r=0; h=1; leaf=typed; refused=none',
    'world_drive=vert_wait; go=1090; slot=13; gap_cm=489; rest_ms=0; span_cm=0',
    'world_lua=event=drive_sink; ent=3268415846; off_m=-4.89; e_cm=10; low_ms=2050; t_ms=40000',
    # A vert_stop with the copy under and no later snap: left until the log end (5 s).
    'world_place=warp; seq=141; kind=3; ent=1799112697; go=1100; age_ms=1; r=0; h=1; leaf=typed; refused=none',
    'world_lua=event=place_combat; seq=160; ent=1799112697; nudge=0; t_ms=50000',
    'world_drive=vert_stop; go=1100; slot=14; n=2; gap_cm=200; left_ms=5000',
    'world_lua=event=place_combat; seq=161; ent=1799112697; nudge=0; t_ms=55000',
]
r4 = rep.analyse(host4, guest4)
check('drive_sank' not in r4['never'] and r4['drive_sank'] == {'host': 2, 'guest': 2},
      'drive_sink is information per copy, not in the never gate')
check(r4['vert_left_under'] == {'host': {'1179373153': [6000], '667680985': [3000]}, 'guest': {'1799112697': [5000]}},
      'vert left under: per guard and copy, the time under after the owner copy was level 1 s, over 2 s only')
check(r4['vert_left_unresolved'] == {'host': [], 'guest': [{'ent': '3268415846', 'vert_wait': True}]} and r4['vert_rest_data'],
      'a drive_sink with only a vert_wait (the owner never level) is unresolved, not left under')
g4 = {n: (res, v) for n, res, v in rep.gates(r4)}
vl = [k for k in g4 if k.startswith('vert left under')]
check(len(vl) == 1 and g4[vl[0]][0] == 'FAIL' and g4[vl[0]][1]['host'] == {'1179373153': [6.0], '667680985': [3.0]},
      'the vert left under gate fails with a guard left under, per guard with seconds')
ok5 = rep.analyse(host[:0] + ['world_drive=vert_wait; go=1041; slot=124; gap_cm=348; rest_ms=0; span_cm=0'], guest4[:6])
g5 = {n: res for n, res, v in rep.gates(ok5)}
check(g5[vl[0]] == 'PASS' and g5['0 latches, faults, game overs, roster mismatches, overflow, guard copies under the ground'] == 'PASS',
      'a climb-time drive_sink resolved by a snap passes both gates')
off6 = rep.analyse(['world_lua=event=drive_sink; ent=2929558510; off_m=-4.54; e_cm=105; low_ms=3470; t_ms=369930697'], [])
check({n: res for n, res, v in rep.gates(off6)}[vl[0]] == 'n/a' and not off6['vert_rest_data'],
      'drive_sink with no world_drive=vert line (drive_vert off): vert left under is n/a')
print('enemy_div_report_check: %d checks passed' % checks)
