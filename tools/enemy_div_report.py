#!/usr/bin/env python3
"""Enemy follow divergence report (docs/world-sync-design.md 7.8, docs/STATUS.md "Enemy follow").

Reads the host's and the guest's script-observer.log (a whole log or a run slice) and computes the enemy follow pass
gates: refused corrections, the applied share, the place error of the guards near the guest, place-first and apply
distances, crossfire outcomes, commit/apply parity and the never-lines. Log analysis only: it reads two text files,
runs no game and touches no game file.

Usage:
  python tools/enemy_div_report.py --host <host script-observer.log> --guest <guest script-observer.log> [--json out.json]

Limitations (printed with every report): every number comes from the mod's own log lines of the inspected build (the
build id is the first `probe=` / `build=` value found, else unknown); the place error is the error at sample time of
the host's samples the guest received (guards near either player only, the host's scan rate), bin-interpolated from
the 1 Hz event=place_err histograms (bins 1, 2, 3, 5, 8, 15 m); a log cut by a collector shortens every count; the
gates do not judge what the screens showed (frames are a separate check).
"""
import argparse
import hashlib
import json
import re
import sys

EDGES = [0.0, 1.0, 2.0, 3.0, 5.0, 8.0, 15.0, 30.0]  # the last bin (>= 15 m) is interpolated up to 30 m


def read(path):
    with open(path, 'rb') as f:
        data = f.read()
    return data.decode('utf-8', 'replace').splitlines(), hashlib.sha256(data).hexdigest()


def fields(line):
    """The key=value pairs of one log line (`; ` separated, the world_lua prefix removed)."""
    out = {}
    body = line.split('world_lua=', 1)[-1]
    for part in re.split(r';\s*', body):
        if '=' in part:
            k, v = part.split('=', 1)
            out.setdefault(k.strip(), v.strip())
    return out


def quantile(bins, q):
    """A bin-interpolated quantile of the place error histogram (metres), or None."""
    total = sum(bins)
    if not total:
        return None
    target = q * total
    seen = 0.0
    for i, n in enumerate(bins):
        if n and seen + n >= target:
            lo, hi = EDGES[i], EDGES[i + 1]
            return lo + (hi - lo) * ((target - seen) / n)
        seen += n
    return EDGES[-1]


def build_id(lines):
    for l in lines:
        m = re.search(r'build=([0-9]+\.[0-9]+\.[0-9]+\+[0-9a-f]+)', l)
        if m:
            return m.group(1)
    return 'unknown'


def walk_owner(lines, prefix):
    """Enemy round 11 / 12 track A (design 7.31): per walker quiet move (its own quiet warp line, `prefix` ... leaf=quiet,
    says quiet=walk; its next at=500 read-back starts it) the 2 s / 10 s read-backs' x/z distance to the owner copy
    (dho_m) within 3 m: {'walk_owner_2s': {read, within_3m, walkers}, 'walk_owner_10s': {...}}."""
    kind, wm, wcur = {}, [], {}
    for l in lines:
        if l.startswith(prefix) and 'leaf=quiet' in l:
            f = fields(l)
            kind[f.get('ent')] = f.get('quiet')
        elif 'event=readback' in l and 'src=quiet' in l:
            f = fields(l)
            e, at = f.get('ent'), f.get('at')
            if at == '500':
                wcur.pop(e, None)
                if kind.get(e) == 'walk':
                    wcur[e] = {'500': f}
                    wm.append(wcur[e])
            elif e in wcur:
                wcur[e][at] = f

    def owner3(f):
        try:
            return float(f.get('dho_m', 'x')) <= 3.0
        except ValueError:
            return False
    o2 = [m['2000'] for m in wm if '2000' in m and 'dho_m' in m['2000']]
    o10 = [m['10000'] for m in wm if '10000' in m and 'dho_m' in m['10000']]
    return {'walk_owner_2s': {'read': len(o2), 'within_3m': sum(1 for f in o2 if owner3(f)),
                              'walkers': len(set(f.get('ent') for f in o2))},
            'walk_owner_10s': {'read': len(o10), 'within_3m': sum(1 for f in o10 if owner3(f)),
                               'walkers': len(set(f.get('ent') for f in o10))}}


def analyse(host, guest):
    r = {}
    # Corrections on the guest.
    places = [fields(l) for l in guest if 'event=place; ' in l or 'event=place;seq' in l]
    corr = [p for p in places if p.get('kind') == 'correct']
    applied = [p for p in corr if p.get('result') == 'applied']
    why = {}
    for p in corr:
        if p.get('result') != 'applied':
            k = '%s/%s' % (p.get('why'), p.get('native'))
            why[k] = why.get(k, 0) + 1
    r['corrections'] = len(corr)
    r['corrections_applied'] = len(applied)
    r['corrections_refused'] = why
    r['native_role'] = sum(1 for p in corr if p.get('native') == 'role')
    errs = sorted(float(p.get('err_m', 0)) for p in applied)
    r['landing_err_m_max'] = errs[-1] if errs else None
    # The place error (event=place_err, guards within 60 m of the guest).
    bins = [0] * 7
    maxes, samples = [], 0
    for l in guest:
        if 'event=place_err;' not in l and 'event=place_err; ' not in l:
            continue
        f = fields(l)
        try:
            h = [int(x) for x in f.get('h', '').split(',')]
        except ValueError:
            continue
        if len(h) != 7:
            continue
        bins = [a + b for a, b in zip(bins, h)]
        samples += int(f.get('n', 0))
        maxes.append(float(f.get('max', 0)))
    r['place_err_samples'] = samples
    r['place_err_bins'] = bins
    r['place_err_p50_m'] = quantile(bins, 0.5)
    r['place_err_p90_m'] = quantile(bins, 0.9)
    r['place_err_max_m'] = max(maxes) if maxes else None
    # Place first and the apply distances.
    # A place-first either moved the guest's copy (step=done) or found it already there (step=hold, div_m, fix=0).
    firsts = [fields(l) for l in guest if 'event=place_first' in l and ('step=done' in l or ('step=hold' in l and 'fix=0' in l))]
    r['place_first'] = len(firsts)
    r['place_first_ok'] = sum(1 for f in firsts if (f.get('step') == 'done' and f.get('result') == 'applied' and
                                                   float(f.get('err_m', 99)) <= 2.0) or
                              (f.get('step') == 'hold' and float(f.get('div_m', 99)) <= 2.0))
    applies = [fields(l) for l in guest if 'event=apply' in l and 'src=outcome' in l]
    # Only standing outcomes (hold-up .. dead): a Fulton's OUTCOME carries the host copy's position under the balloon.
    # result=terminal_local is the guest's own kill coming back (its copy was already dead): its d_m is where the host's
    # copy died, the two-way follow of M3, reported apart and not gated (run 51f8e90f: 2.9 m, a crossfire death 13.3 m).
    dms = [float(a['d_m']) for a in applies if 'd_m' in a and a.get('state') in ('1', '2', '3', '4', '5') and
           a.get('result') == 'ok']
    echoes = [float(a['d_m']) for a in applies if 'd_m' in a and a.get('result') == 'terminal_local']
    r['guest_echo_d_m'] = echoes
    r['guest_applies'] = len(applies)
    r['guest_applies_with_d'] = len(dms)
    r['guest_applies_d_le_2m'] = sum(1 for d in dms if d <= 2.0)
    r['guest_apply_d_m_max'] = max(dms) if dms else None
    # Commit / apply parity: every host commit (its own acts, the guest's requests, the start snapshot) reaches the guest
    # as an OUTCOME or a RESYNC record of the same entity and version.
    commits = {(f.get('ent'), f.get('ver')) for f in (fields(l) for l in host if 'event=commit' in l)}
    gapplied = {(a.get('ent'), a.get('ver')) for a in (fields(l) for l in guest if 'event=apply' in l)
                if a.get('src') in ('outcome', 'resync')}
    r['host_commits'] = len(commits)
    r['commits_not_applied'] = len(commits - gapplied)
    # Crossfire.
    r['not_attributed'] = sum(1 for l in guest if 'why=not_attributed' in l)
    r['terminal_win'] = sum(1 for l in host if 'event=terminal_win' in l)
    r['reject_12'] = sum(1 for l in host if 'event=req_rx' in l and 'reason=12' in l)
    # Never-lines.
    never = {
        'soldier_warp_disabled': sum(1 for l in host + guest if 'soldier_warp=disabled' in l),
        'soldier_warp_soldier_latched': sum(1 for l in host + guest if 'soldier_warp=soldier_latched' in l),
        'fault': sum(1 for l in host + guest if 'refused=fault' in l or 'reason=fault' in l),
        # The mod's shared game over (world_recovery.lua: both players downed) logs step=shared_game_over on the host and
        # step=game_over_applied on the guest (run 617311eb: missed by the old patterns).
        'game_over': sum(1 for l in host + guest if 'GameOver' in l or 'reason=game_over' in l or
                         'step=shared_game_over' in l or 'step=host_game_over' in l or 'step=game_over_applied' in l),
        'roster_mismatch': sum(1 for l in host + guest if 'roster_mismatch' in l),
        'overflow': 0,
        # Guard copies under the ground (runs 51f8e90f / 6e22bed0): the place error compares x, z only. Both copies (run
        # 35d4074f: the host's copy of 1179373153 10.5 m under; this row counted guest lines only).
        'guard_fell': sum(1 for l in host + guest if 'event=place_div' in l and 'why=fall' in l),
        # place_sink (enemy gaps): both roles check their placed copies; a copy level with its owner is place_drop.
        'guard_sank': sum(1 for l in host + guest if 'event=place_sink' in l),
        'body_fell': sum(1 for l in host + guest if 'event=body_fall' in l),
    }
    # drive_sink (a driven copy more than 1.5 m under its target for 2 s, also while the owner climbs): information only
    # since the enemy flip A-vert (D-A10); the gate is vert_left_under (below).
    r['drive_sank'] = {side: sum(1 for l in lines if 'event=drive_sink' in l) for side, lines in (('host', host), ('guest', guest))}
    for l in host + guest:
        m = re.search(r'\boverflow=(\d+)', l)
        if m and 'world=stats' in l:
            never['overflow'] = max(never['overflow'], int(m.group(1)))
    r['never'] = never
    r['place_drop'] = sum(1 for l in host + guest if 'event=place_drop' in l)
    # No landing (dy_m) histogram: its pattern never matched the logged lines (it printed 0 for e583ee62 and the gaps runs),
    # and this tool has no test to pin a fixed one; count dy_m by hand until it has (docs/STATUS.md 2026-11-04 landing).
    # The host's scan cost (event=place_scan, follow only).
    scans = [fields(l) for l in host if 'event=place_scan' in l]
    us = [float(s.get('avg_us', 0)) for s in scans]
    r['scan_avg_us'] = sum(us) / len(us) if us else None
    r['scan_max_us'] = max((float(s.get('max_us', 0)) for s in scans), default=None)
    # Enemy sync M3 (two-way): the host's own placements of guest-owned guards, its place error window for them, the
    # owner changes (at most one per guard per 10 s), the guest's place-firsts, the calm-down.
    hpl = [fields(l) for l in host if 'event=hplace' in l]
    r['m3_hplace'] = len(hpl)
    r['m3_hplace_applied'] = sum(1 for p in hpl if p.get('result') == 'applied')
    hwhy = {}
    for p in hpl:
        if p.get('result') != 'applied':
            hwhy[p.get('why')] = hwhy.get(p.get('why'), 0) + 1
    r['m3_hplace_refused'] = hwhy
    hb, hs = [0] * 7, 0
    for l in host:
        if 'event=place_err' not in l:
            continue
        f = fields(l)
        try:
            h = [int(x) for x in f.get('h', '').split(',')]
        except ValueError:
            continue
        if len(h) == 7:
            hb, hs = [a + b for a, b in zip(hb, h)], hs + int(f.get('n', 0))
    r['m3_host_err_samples'] = hs
    r['m3_host_err_p50_m'] = quantile(hb, 0.5)
    r['m3_host_err_p90_m'] = quantile(hb, 0.9)
    # Enemy round 7 (runs 90d9b4d1 / ddcdec94): after the guest is downed the host refuses its M3 placements as out of
    # range (near=host) and its copies of the guest's guards drift 40-60 m; the error before the guest's first down apart.
    down = None
    for l in guest:
        if 'event=downed' in l and 'step=entered' in l:
            m = re.search(r't_ms=(\d+)', l)
            if m:
                down = int(m.group(1))
                break
    r['guest_downed_at'] = down
    db, ds = [0] * 7, 0
    for l in host:
        if 'event=place_err' not in l:
            continue
        f = fields(l)
        try:
            h = [int(x) for x in f.get('h', '').split(',')]
            t = int(f.get('t_ms', 0) or 0)
        except ValueError:
            continue
        if len(h) == 7 and (down is None or t < down):
            db, ds = [a + b for a, b in zip(db, h)], ds + int(f.get('n', 0))
    r['m3_host_err_samples_before_down'] = ds
    r['m3_host_err_p50_m_before_down'] = quantile(db, 0.5)
    r['m3_host_err_p90_m_before_down'] = quantile(db, 0.9)
    # Enemy round 8 track A (range_flat, design 7.22): the windows the guest is down (entered .. abandoned / grant, else
    # the log end) and the host's M3 error and placements in them (runs ddcdec94 / 90d9b4d1: 110 range refusals).
    wins, open_at = [], None
    for l in guest:
        if 'event=downed' not in l:
            continue
        m = re.search(r't_ms=(\d+)', l)
        if not m:
            continue
        if 'step=entered' in l and open_at is None:
            open_at = int(m.group(1))
        elif open_at is not None and ('step=abandoned' in l or 'step=grant' in l):
            wins.append([open_at, int(m.group(1))])
            open_at = None
    if open_at is not None:
        wins.append([open_at, None])
    r['guest_down_windows'] = wins

    def down_at(t):
        return any(a <= t and (b is None or t <= b) for a, b in wins)

    wb, ws, wp = [0] * 7, 0, {'applied': 0, 'refused': 0}
    for l in host:
        if 'event=place_err' not in l and 'event=hplace' not in l:
            continue
        f = fields(l)
        try:
            t = int(f.get('t_ms', 0) or 0)
        except ValueError:
            continue
        if not down_at(t):
            continue
        if 'event=hplace' in l:
            k = 'applied' if f.get('result') == 'applied' else 'refused'
            wp[k] += 1
            continue
        try:
            h = [int(x) for x in f.get('h', '').split(',')]
        except ValueError:
            continue
        if len(h) == 7:
            wb, ws = [a + b for a, b in zip(wb, h)], ws + int(f.get('n', 0))
    r['m3_host_err_samples_while_down'] = ws
    r['m3_host_err_p50_m_while_down'] = quantile(wb, 0.5)
    r['m3_host_err_p90_m_while_down'] = quantile(wb, 0.9)
    r['m3_hplace_while_down'] = wp
    # quiet_place: the guest's corrections moved by the quiet warp and their read-backs (x/z and y within 1 m).
    qw = [fields(l) for l in guest if l.startswith('world_place=warp') and 'leaf=quiet' in l]
    ql = [fields(l) for l in guest if l.startswith('world_place=quiet;') or l.startswith('world_place=quiet; ')]
    qr = [fields(l) for l in guest if 'event=readback' in l and 'src=quiet' in l]

    def within(f):
        try:
            return abs(float(f.get('dy_m', 'x'))) <= 1.0 and float(f.get('dxz_m', 'x')) <= 1.0
        except ValueError:
            return False
    r['quiet'] = {'warps': len(qw), 'moved': sum(1 for f in ql if f.get('moved') == '1'),
                  'bias': sum(1 for f in ql if f.get('bias') == '1'), 'readback': len(qr),
                  'readback_within_1m': sum(1 for f in qr if within(f))}
    # Enemy round 9 track A (design 7.24): why a quiet move was (not) taken (quiet=<why> on the warp line), the walk-on row
    # (the 5 s read-back x/z within 2 m) and per move (an at=500 read-back starts one) both 0.5 s and 2 s within 1 m.
    why = {}
    for l in guest:
        if l.startswith('world_place=warp') and 'quiet=' in l:
            k = fields(l).get('quiet')
            why[k] = why.get(k, 0) + 1
    r5 = [f for f in qr if f.get('at') == '5000']

    def near2(f):
        try:
            return float(f.get('dxz_m', 'x')) <= 2.0
        except ValueError:
            return False
    moves, cur = [], {}
    for f in qr:
        e, at = f.get('ent'), f.get('at')
        if at == '500':
            cur[e] = {'500': f}
            moves.append(cur[e])
        elif e in cur:
            cur[e][at] = f
    read = [m for m in moves if '500' in m and '2000' in m]
    r['quiet'].update({'why': why, 'readback_5s': len(r5), 'walk_on_within_2m_5s': sum(1 for f in r5 if near2(f)),
                       'moves_read': len(read), 'moves_both_within_1m': sum(1 for m in read if within(m['500']) and within(m['2000']))})
    # Enemy round 11 track A (design 7.31): walkers' quiet moves (quiet=walk), their re-plans (world_place=quiet_plan step,
    # started) and the owner rows: per walker move (its next at=500 read-back starts it) the 2 s / 10 s read-backs' x/z
    # distance to the owner copy (dho_m) within 3 m.
    # As the review checked it: a move is a walker's when its own quiet warp line (the guard's last one before its at=500
    # read-back) says quiet=walk, so a later quiet=ok move of the same guard is not counted; a cancelled slot whose plan
    # did not start (step=cancel_plan, started=0) is a lost plan (his brain's walk cancelled, none in its place).
    plans = {}
    started, lost, lost_idx = 0, 0, set()
    for l in guest:
        if l.startswith('world_place=quiet_plan'):
            f = fields(l)
            plans[f.get('step')] = plans.get(f.get('step'), 0) + 1
            started += 1 if f.get('started') == '1' else 0
            if f.get('step') == 'cancel_plan' and f.get('started') != '1':
                lost += 1
                lost_idx.add(f.get('idx'))
    r['quiet'].update({'walk_moves': sum(1 for f in qw if f.get('quiet') == 'walk'), 'plans': plans, 'plans_started': started,
                       'plans_lost': lost, 'plans_lost_guards': len(lost_idx)})
    r['quiet'].update(walk_owner(guest, 'world_place=warp'))
    # Enemy round 12 track A (design 7.31): the host's M3 placements quiet too (world_place=host_warp leaf=quiet), their
    # reasons and the walkers' owner rows from the host's own read-backs (dho_m against the guest's copy).
    hw = [fields(l) for l in host if l.startswith('world_place=host_warp') and 'leaf=quiet' in l]
    hwhy = {}
    for l in host:
        if l.startswith('world_place=host_warp') and 'quiet=' in l:
            k = fields(l).get('quiet')
            hwhy[k] = hwhy.get(k, 0) + 1
    r['host_quiet'] = {'warps': len(hw), 'why': hwhy, 'walk_moves': sum(1 for f in hw if f.get('quiet') == 'walk'),
                       'moved': sum(1 for l in host if l.startswith('world_place=quiet;') and fields(l).get('moved') == '1'),
                       'readback': sum(1 for l in host if 'event=readback' in l and 'src=quiet' in l)}
    r['host_quiet'].update(walk_owner(host, 'world_place=host_warp'))
    owners = [fields(l) for l in host if 'event=owner;' in l or 'event=owner; ' in l]
    r['m3_owner_changes'] = len(owners)
    last, fast = {}, 0
    for o in owners:
        e, t = o.get('ent'), int(o.get('t_ms', 0) or 0)
        if e in last and t - last[e] < 10000:
            fast += 1
        last[e] = t
    r['m3_owner_fast_flips'] = fast
    r['m3_guest_place_first'] = sum(1 for l in guest if 'event=place_first' in l and 'side=guest' in l)
    r['m3_engage'] = sum(1 for l in guest if 'event=engage_tx' in l)
    r['m3_phase_down'] = sum(1 for l in host if 'src=down' in l)
    r['m3_phase_down_applied'] = sum(1 for l in guest if 'src=down' in l and 'applied=1' in l)
    # Enemy sync M4 (the stream and the soft follow): the last stats lines of each side.
    def last(lines, needle):
        found = [fields(l) for l in lines if needle in l]
        return found[-1] if found else {}
    for side, lines in (('host', host), ('guest', guest)):
        s, d = last(lines, 'event=snap_stats'), last(lines, 'world_drive=stats')
        r['m4_' + side] = {k: int(s.get(k, 0) or 0) for k in ('tx', 'calls', 'steps', 'hard', 'rest', 'stale', 'refused')}
        r['m4_' + side + '_native'] = {k: int(d.get(k, 0) or 0) for k in ('calls', 'steps', 'hard', 'rest', 'stale', 'refused', 'anomalies')}
    # Enemy latch (docs/world-sync-design.md 7.14): the typed dispatch's relays (out=0 handled=0, the guard not realized
    # on that copy) are no anomaly; each backs only that guard off (native refused=backoff, Lua why=unrealized). Per copy,
    # per path: grant (the host's M3 copies, the guest's M1 corrections) or drive (M4, id=0).
    for side, lines in (('host', host), ('guest', guest)):
        paths = {'grant': 0, 'drive': 0}
        for l in lines:
            if 'soldier_warp=relayed' in l:
                paths['drive' if re.search(r'\bid=0;', l) else 'grant'] += 1
        r['relayed_' + side] = paths
        r['backoff_' + side] = sum(1 for l in lines if 'refused=backoff' in l)
        r['unrealized_' + side] = sum(1 for l in lines if 'why=unrealized' in l and re.search(r'event=(hplace|place);', l))
    # M6: body_tx per body and copy (run e583ee62's ping-pong: 384 / 383 for one dead guard), the refused body placements.
    tx = {}
    for side, lines in (('host', host), ('guest', guest)):
        for l in lines:
            if 'event=body_tx' in l:
                key = (side, fields(l).get('ent'))
                tx[key] = tx.get(key, 0) + 1
    r['body_tx'] = sum(tx.values())
    r['body_tx_max'] = max(tx.values()) if tx else 0
    r['place_body_refused'] = sum(1 for l in host + guest if 'event=place_body' in l and 'fix=0' in l and 'why=' in l and
                                  'why=none' not in l)
    # Enemy round 7 track A (design 7.20, lua/coop/world_readback.lua): the y read-backs 0.5 s / 2 s after a vert snap,
    # a held landing, a far placement or a put-back, per copy and source (held = within 1 m of where he was put; a
    # not-held line is under or above that spot: a copy driven on after it was put measures the ground he ran to, not
    # a fall); the guards under / above; the 2 s read-backs (one slot per copy: a newer arm within 2 s cuts the older
    # one's 2 s line); the far put-backs; the drive_vert snaps and the no-thrash stops (native world_drive lines).
    r['readback'], r['readback_fix'], r['vert_snaps'], r['vert_stop'] = {}, {}, {}, {}
    r['readback_under_guards'], r['readback_above_guards'], r['readback_2s'] = {}, {}, {}
    for side, lines in (('host', host), ('guest', guest)):
        per, under, above, pend = {}, set(), set(), set()
        two = {'armed': 0, 'got': 0, 'cut': 0, 'open': 0}
        for l in lines:
            if 'event=readback;' in l or 'event=readback; ' in l:
                f = fields(l)
                row = per.setdefault(f.get('src', '?'), {'held': 0, 'under': 0, 'above': 0})
                try:
                    dy = float(f.get('dy_m', '0'))
                except ValueError:
                    dy = 0.0
                if f.get('held') == '1':
                    row['held'] += 1
                elif dy < 0:
                    row['under'] += 1
                    under.add(f.get('ent'))
                else:
                    row['above'] += 1
                    above.add(f.get('ent'))
                ent = f.get('ent')
                if f.get('at') == '500':
                    two['armed'] += 1
                    if ent in pend:
                        two['cut'] += 1
                    pend.add(ent)
                elif f.get('at') == '2000':
                    two['got'] += 1
                    pend.discard(ent)
        two['open'] = len(pend)
        r['readback'][side] = per
        r['readback_under_guards'][side] = sorted(under)
        r['readback_above_guards'][side] = sorted(above)
        r['readback_2s'][side] = two
        r['readback_fix'][side] = sum(1 for l in lines if 'event=readback_fix' in l and 'put=1' in l)
        r['vert_snaps'][side] = sum(1 for l in lines if 'world_drive=vert;' in l)
        r['vert_stop'][side] = sum(1 for l in lines if 'world_drive=vert_stop' in l)
    r['vert_left_under'], r['vert_left_unresolved'], r['vert_rest_data'] = vert_left(host, guest)
    return r


# Enemy flip A-vert (user decision D-A10, design 7.27): the drive_sink row read as "left under after the owner copy's
# height has stayed level for 1 s". A copy is left under when it stays more than 1.5 m under its owner copy for more than
# VERT_LEFT_MS (drive_sink's own 2 s window) counted from the moment the owner copy had been level in y for 1 s; a sink
# while the owner climbs (vert_wait, rest_ms under 1000) does not count. From the native world_drive lines (drive_vert
# on): a vert snap up (gap_cm > 150) came rest_ms - 1000 after that moment (capped by the time he was known to be under:
# an open drive_sink spell's low_ms plus the time since it, or 2 s without one); a vert_stop up (the no-thrash cap: the
# owner level, the copy due) leaves him under until his next snap or the log end. Native lines carry no t_ms: each takes
# the last t_ms before it on that copy.
VERT_LEFT_MS = 2000
VERT_REST_MS = 1000


def vert_left(host, guest):
    """Per copy: {ent: [ms left under, ...]} over VERT_LEFT_MS, the drive_sink spells no vert line resolved, and whether
    any vert line (the owner-rest data) was seen."""
    go_ent = {}
    for l in host + guest:
        m = re.search(r'\bent=(\d+)', l)
        g = re.search(r'\bgo=(\d+)', l) or ('event=obs' in l and re.search(r'\bid=(\d+)', l))
        if m and g:
            go_ent.setdefault(g.group(1), m.group(1))
    left, unresolved, seen = {}, {}, False
    for side, lines in (('host', host), ('guest', guest)):
        t, spell, stop, waited = 0, {}, {}, set()
        out, unres = {}, []
        for l in lines:
            m = re.search(r'(?:^|[;\s])t_ms=(\d+)', l)
            if m:
                t = int(m.group(1))
            if 'event=drive_sink' in l:
                f = fields(l)
                spell[f.get('ent')] = (t, int(f.get('low_ms', 0) or 0))
                continue
            if not l.startswith('world_drive=vert'):
                continue
            seen = True
            f = fields(l.replace('world_drive=', 'kind=', 1))
            ent = go_ent.get(f.get('go'), 'go' + str(f.get('go')))
            try:
                gap = int(f.get('gap_cm', '0'))
            except ValueError:
                gap = 0
            if f.get('kind') == 'vert_wait':
                waited.add(ent)
            elif f.get('kind') == 'vert_stop':
                if gap > 150:
                    stop.setdefault(ent, t)
            elif f.get('kind') == 'vert':
                if gap > 150:
                    try:
                        ms = max(0, int(f.get('rest_ms', '0')) - VERT_REST_MS)
                    except ValueError:
                        ms = 0
                    if ent in stop:
                        ms = max(ms, t - stop.pop(ent))
                    elif ent in spell:
                        s_t, low = spell[ent]
                        ms = min(ms, low + max(0, t - s_t))
                    else:
                        ms = min(ms, VERT_LEFT_MS)
                    if ms > VERT_LEFT_MS:
                        out.setdefault(ent, []).append(ms)
                spell.pop(ent, None)
                waited.discard(ent)
        for ent, s_t in stop.items():
            out.setdefault(ent, []).append(max(0, t - s_t))
            spell.pop(ent, None)
        for ent in spell:
            unres.append({'ent': ent, 'vert_wait': ent in waited})
        left[side], unresolved[side] = out, unres
    return left, unresolved, seen


def gates(r):
    g = []
    def gate(name, ok, value):
        g.append((name, 'n/a' if ok is None else ('PASS' if ok else 'FAIL'), value))
    gate('0 native=role refusals', r['native_role'] == 0, r['native_role'])
    share = r['corrections_applied'] / r['corrections'] if r['corrections'] else None
    gate('>= 90 % of queued corrections applied', None if share is None else share >= 0.9,
         '%d/%d' % (r['corrections_applied'], r['corrections']))
    p50, p90 = r['place_err_p50_m'], r['place_err_p90_m']
    gate('place error median <= 2 m (guards within 60 m of the guest)', None if p50 is None else p50 <= 2.0,
         None if p50 is None else round(p50, 2))
    gate('place error p90 <= 5 m', None if p90 is None else p90 <= 5.0, None if p90 is None else round(p90, 2))
    gate('every place-first landed within 2 m', None if not r['place_first'] else r['place_first_ok'] == r['place_first'],
         '%d/%d' % (r['place_first_ok'], r['place_first']))
    gate('every guest apply of a host outcome d_m <= 2', None if not r['guest_applies_with_d'] else
         r['guest_applies_d_le_2m'] == r['guest_applies_with_d'], '%d/%d' % (r['guest_applies_d_le_2m'], r['guest_applies_with_d']))
    gate('0 unresolved why=not_attributed', r['not_attributed'] == 0, r['not_attributed'])
    gate('every host commit applied on the guest', None if not r['host_commits'] else r['commits_not_applied'] == 0,
         '%d missing of %d' % (r['commits_not_applied'], r['host_commits']))
    if r['m3_hplace'] or r['m3_owner_changes']:
        gate('M3: >= 90 % of the host placements applied', None if not r['m3_hplace'] else r['m3_hplace_applied'] >= 0.9 * r['m3_hplace'],
             '%d/%d' % (r['m3_hplace_applied'], r['m3_hplace']))
        hp50, hp90 = r['m3_host_err_p50_m'], r['m3_host_err_p90_m']
        gate('M3: host place error for guest-owned guards median <= 2 m, p90 <= 5 m',
             None if hp50 is None else (hp50 <= 2.0 and hp90 <= 5.0), None if hp50 is None else (round(hp50, 2), round(hp90, 2)))
        gate('M3: <= 1 owner change per guard per 10 s', r['m3_owner_fast_flips'] == 0, r['m3_owner_fast_flips'])
    if r['body_tx']:
        gate('M6: <= 2 body_tx per body per copy', r['body_tx_max'] <= 2, '%d sent, max %d' % (r['body_tx'], r['body_tx_max']))
    n = r['never']
    gate('0 latches, faults, game overs, roster mismatches, overflow, guard copies under the ground', all(v == 0 for v in n.values()), n)
    # D-A10: n/a when copies sank (drive_sink) but no world_drive=vert line gives the owner's rest (drive_vert off).
    lu = r['vert_left_under']
    sank = sum(r['drive_sank'].values())
    gate('vert left under 0 (a copy > 1.5 m under its owner copy for > 2 s after the owner copy was level in y for 1 s)',
         None if sank and not r['vert_rest_data'] else not any(lu.values()),
         {side: {e: [round(ms / 1000, 2) for ms in v] for e, v in d.items()} for side, d in lu.items()})
    return g


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--host', required=True)
    ap.add_argument('--guest', required=True)
    ap.add_argument('--json')
    a = ap.parse_args(argv)
    host, hsha = read(a.host)
    guest, gsha = read(a.guest)
    r = analyse(host, guest)
    r['inputs'] = {'host': {'path': a.host, 'sha256': hsha, 'lines': len(host), 'build': build_id(host)},
                   'guest': {'path': a.guest, 'sha256': gsha, 'lines': len(guest), 'build': build_id(guest)}}
    g = gates(r)
    r['gates'] = [{'gate': n, 'result': res, 'value': v} for n, res, v in g]
    print('Enemy follow report (log analysis only; no game run, no game file read)')
    for role in ('host', 'guest'):
        i = r['inputs'][role]
        print('  %-5s %s (%d lines, sha256 %s, build %s)' % (role, i['path'], i['lines'], i['sha256'], i['build']))
    print('  corrections %d, applied %d, refused %s; landing error max %s m' % (r['corrections'], r['corrections_applied'],
          r['corrections_refused'] or '{}', r['landing_err_m_max']))
    print('  place error: %d samples, bins %s, p50 %s m, p90 %s m, max %s m' % (r['place_err_samples'], r['place_err_bins'],
          None if r['place_err_p50_m'] is None else round(r['place_err_p50_m'], 2),
          None if r['place_err_p90_m'] is None else round(r['place_err_p90_m'], 2), r['place_err_max_m']))
    print('  place-first %d (within 2 m %d); guest applies %d (with d_m %d, <= 2 m %d, max %s m); the guest side kills committed back, '
          'd_m %s' % (
        r['place_first'], r['place_first_ok'], r['guest_applies'], r['guest_applies_with_d'], r['guest_applies_d_le_2m'],
        r['guest_apply_d_m_max'], r['guest_echo_d_m'] or 'none'))
    if r['m3_hplace'] or r['m3_owner_changes']:
        print('  M3: owner changes %d (fast flips %d), host placements %d applied %d refused %s, host place error %d samples '
              'p50 %s m p90 %s m, guest place-firsts %d, engages %d, calm-downs %d (applied on the guest %d)' % (
                  r['m3_owner_changes'], r['m3_owner_fast_flips'], r['m3_hplace'], r['m3_hplace_applied'], r['m3_hplace_refused'],
                  r['m3_host_err_samples'], r['m3_host_err_p50_m'] and round(r['m3_host_err_p50_m'], 2),
                  r['m3_host_err_p90_m'] and round(r['m3_host_err_p90_m'], 2), r['m3_guest_place_first'], r['m3_engage'],
                  r['m3_phase_down'], r['m3_phase_down_applied']))
        if r['guest_down_windows']:
            print('  M3 while the guest is down %s: host place error %d samples p50 %s m p90 %s m; placements %s' % (
                r['guest_down_windows'], r['m3_host_err_samples_while_down'],
                r['m3_host_err_p50_m_while_down'] and round(r['m3_host_err_p50_m_while_down'], 2),
                r['m3_host_err_p90_m_while_down'] and round(r['m3_host_err_p90_m_while_down'], 2), r['m3_hplace_while_down']))
        print('  quiet moves (guest): %s' % r['quiet'])
        if r['guest_downed_at'] is not None:
            print('  M3: the guest was first downed at t_ms %d; host place error before it %d samples p50 %s m p90 %s m' % (
                r['guest_downed_at'], r['m3_host_err_samples_before_down'],
                r['m3_host_err_p50_m_before_down'] and round(r['m3_host_err_p50_m_before_down'], 2),
                r['m3_host_err_p90_m_before_down'] and round(r['m3_host_err_p90_m_before_down'], 2)))
    if r['m4_host']['tx'] or r['m4_guest']['calls']:
        print('  M4: host snap %s / drive %s; guest snap %s / drive %s' % (r['m4_host'], r['m4_host_native'], r['m4_guest'],
                                                                          r['m4_guest_native']))
    print('  relays (not realized on that copy): host %s backoff %d unrealized %d; guest %s backoff %d unrealized %d; '
          'body_tx %d (max per body %d), body placements refused here %d' % (
              r['relayed_host'], r['backoff_host'], r['unrealized_host'], r['relayed_guest'], r['backoff_guest'],
              r['unrealized_guest'], r['body_tx'], r['body_tx_max'], r['place_body_refused']))
    print('  read-backs (held / under / above per source): host %s guest %s; under: host %s guest %s; above: host %s '
          'guest %s; 2 s read-backs (armed / got / cut by a newer arm / open): host %s guest %s; far put-backs %s; '
          'vert snaps %s, vert stops %s' % (
              r['readback']['host'], r['readback']['guest'], r['readback_under_guards']['host'] or 'none',
              r['readback_under_guards']['guest'] or 'none', r['readback_above_guards']['host'] or 'none',
              r['readback_above_guards']['guest'] or 'none', r['readback_2s']['host'], r['readback_2s']['guest'],
              r['readback_fix'], r['vert_snaps'], r['vert_stop']))
    print('  drive_sink (information since D-A10: also while the owner climbs) %s; vert left under (s) %s; drive_sink spells '
          'no vert line resolved %s; owner-rest data (world_drive=vert lines) %s' % (
              r['drive_sank'], r['vert_left_under'], r['vert_left_unresolved'], 'yes' if r['vert_rest_data'] else 'none'))
    print('  crossfire: terminal_win %d, REJECT 12 %d, not_attributed %d; host scan avg %s us, max %s us' % (
        r['terminal_win'], r['reject_12'], r['not_attributed'],
        None if r['scan_avg_us'] is None else round(r['scan_avg_us']), r['scan_max_us']))
    for n, res, v in g:
        print('  [%s] %s: %s' % (res, n, v))
    print('Limitations: mod log lines only (the build named above); the place error is at sample time, bin-interpolated; '
          'a cut log shortens every count; frames are judged separately.')
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(r, f, indent=2)
    return 0 if all(res != 'FAIL' for _, res, _ in g) else 1


if __name__ == '__main__':
    sys.exit(main())
