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
           a.get('result') != 'terminal_local']
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
        'game_over': sum(1 for l in host + guest if 'GameOver' in l or 'reason=game_over' in l),
        'roster_mismatch': sum(1 for l in host + guest if 'roster_mismatch' in l),
        'overflow': 0,
        # Guard copies under the ground (runs 51f8e90f / 6e22bed0): the place error compares x, z only.
        'guard_fell': sum(1 for l in guest if 'event=place_div' in l and 'why=fall' in l),
        'guard_sank': sum(1 for l in guest if 'event=place_sink' in l),
        'body_fell': sum(1 for l in host + guest if 'event=body_fall' in l),
    }
    for l in host + guest:
        m = re.search(r'\boverflow=(\d+)', l)
        if m and 'world=stats' in l:
            never['overflow'] = max(never['overflow'], int(m.group(1)))
    r['never'] = never
    # The host's scan cost (event=place_scan, follow only).
    scans = [fields(l) for l in host if 'event=place_scan' in l]
    us = [float(s.get('avg_us', 0)) for s in scans]
    r['scan_avg_us'] = sum(us) / len(us) if us else None
    r['scan_max_us'] = max((float(s.get('max_us', 0)) for s in scans), default=None)
    return r


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
    n = r['never']
    gate('0 latches, faults, game overs, roster mismatches, overflow, guard copies under the ground', all(v == 0 for v in n.values()), n)
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
