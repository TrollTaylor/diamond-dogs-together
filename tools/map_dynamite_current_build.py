"""Join two attributed source maps into an inactive, exact-build porting report.

Name matches are candidates only. No table produced by this tool is consumed by
the loader. Full instruction snapshots are review evidence, not detour approval.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

from analyze_network_bindings import Analysis
from audit_dynamite_reference import ROOT, uncomment, audit
from generate_registration_trace import GAME_SHA256

ALIASES = {'FoxNtSessionGetMainSession': 'GetMainSession'}
EXTRAS = ('GetFobTarget',)


def map_current(executable):
    old = audit(executable)
    if old['executable_sha256'] != GAME_SHA256:
        raise ValueError('Unsupported installed executable')
    reference = ROOT / 'third_party/tpp-mod'
    provenance = json.loads((reference / 'provenance.json').read_text(encoding='utf-8'))
    if provenance['commit'] != '301bf797b7f8032941b21ed56a3570338de6ead4':
        raise ValueError('Unreviewed current-build source revision')
    for name, digest in provenance['files_sha256'].items():
        if hashlib.sha256((reference / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Changed source reference: {name}')
    source = uncomment((reference / 'reference/symbols.hpp').read_text())
    declarations = defaultdict(list)
    # The first address is English TPP (game.hpp mode_tpp_eng=0).
    pattern = r'WEAK\s+symbol<([^;]+?)>\s+(\w+)\s*\{\s*(0x[0-9A-Fa-f]+)\s*,[^}]+\};'
    for match in re.finditer(pattern, source):
        declarations[match[2]].append((int(match[3], 16), match[1], source.count('\n', 0, match.start()) + 1))
    analysis = Analysis(executable.read_bytes())
    rows, unresolved = [], []
    requested = [(s['name'], s['upstream_va']) for s in old['symbols']]
    requested.extend((name, None) for name in EXTRAS)
    for old_name, old_va in requested:
        new_name = ALIASES.get(old_name, old_name)
        matches = declarations.get(new_name, [])
        if len(matches) != 1 or not matches[0][0]:
            unresolved.append(dict(name=old_name, reason='ambiguous' if len(matches) > 1 else 'no TPP mapping'))
            continue
        va, signature, line = matches[0]
        rva = va - analysis.pe.image_base
        body = analysis.function(rva)
        # Analysis.function also decodes unnamed leaf functions heuristically.
        # Preserve a bounded 16-byte snapshot but do not treat it as a full body.
        rows.append(dict(dynamite_name=old_name, dynamite_va=old_va,
                         current_source_name=new_name, current_rva=hex(rva),
                         current_source_signature=signature, source_line=line,
                         first_16_bytes=analysis.pe.rva_read(rva, 16).hex(),
                         match_kind='explicit_alias' if old_name in ALIASES else 'same_unqualified_name',
                         source_function_analysis=body, runtime_approved=False,
                         abi_verified=False, lifetime_verified=False))
    return dict(executable_sha256=GAME_SHA256, dynamite_commit=old['upstream_commit'],
                current_map_commit=provenance['commit'], candidates=rows, unresolved=unresolved,
                runtime_calls=False, process_writes=False,
                limitations=['Candidate names can identify different overloads or wrappers; review each call graph.',
                             'Source signatures are not ABI guarantees. Host/client creation returns AL in inspected code.',
                             'No live hook or installation. Full four-player allocation and campaign remain unimplemented.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = map_current(args.executable)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"{len(result['candidates'])} current-build candidates, {len(result['unresolved'])} unresolved; none enabled")
