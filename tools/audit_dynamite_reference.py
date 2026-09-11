"""Compare pinned Dynamite reference addresses/patches with a PE file, read only.

This is a compatibility inventory, never a patch installer. Even matching bytes
do not establish instruction alignment, ABI, lifetime or safe hook behavior.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

from inspect_game import PEFile, PEError

ROOT = Path(__file__).resolve().parent.parent
REFERENCE = ROOT / 'third_party/dynamite'
COMMIT = 'c90f252f8200765f66c005d6bf4357fda0c0d98e'
BASE = 0x140000000
# Preserve strings while removing comments: disabled patches must not be audited.
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/')


def uncomment(source):
    return TOKENS.sub(lambda m: m[0] if m[0].startswith('"') else
                      ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def patches(source):
    source = uncomment(source)
    source = source.split('Dynamite::GetPatches()', 1)[1].split(
        'bool Dynamite::PatchMasterServerURL', 1)[0]
    starts = list(re.finditer(r'\.address\s*=\s*(0x[0-9a-fA-F]+)', source))
    rows = []
    for index, match in enumerate(starts):
        block = source[match.end():starts[index+1].start() if index+1 < len(starts) else len(source)]
        values = {}
        for field in ('expected', 'patch'):
            found = re.search(r'\.' + field + r'\s*=\s*\{([^}]*)\}', block)
            if not found:
                raise ValueError(f'Unparsed {field} at {match[1]}')
            literals = found[1].split(',')
            values[field] = bytes(int(v.strip(), 0) for v in literals if v.strip())
            if not values[field]:
                raise ValueError('Empty patch vector')
        description = re.search(r'\.description\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+)', block)
        if not description:
            raise ValueError(f'Unparsed description at {match[1]}')
        label = ''.join(json.loads(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', description[1]))
        rows.append((int(match[1], 16), values['expected'], values['patch'], label))
    if not rows:
        raise ValueError('No active patches parsed')
    return rows


def inspect_location(pe, va, size):
    rva = va - BASE
    if rva < 0 or rva + size > pe.image_size:
        return dict(rva=hex(rva), status='outside_image')
    try:
        actual = pe.rva_read(rva, size)
    except PEError:
        return dict(rva=hex(rva), status='not_file_backed')
    section = next((s for s in pe.sections if s['rva'] <= rva and
                    rva + size <= s['rva'] + max(s['virtual_size'], s['raw_size'])), None)
    return dict(rva=hex(rva), status='file_backed', actual_hex=actual.hex(),
                section=section['name'] if section else 'headers',
                executable_section=bool(section and section['characteristics'] & 0x20000000))


def audit(executable):
    manifest = json.loads((REFERENCE / 'provenance.json').read_text(encoding='utf-8'))
    if manifest['commit'] != COMMIT:
        raise ValueError('Unexpected reference revision')
    for name, digest in manifest['files_sha256'].items():
        if hashlib.sha256((REFERENCE / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Reference changed: {name}')
    data = executable.read_bytes()
    pe = PEFile(data)
    if pe.bits != 64 or pe.machine != 0x8664:
        raise ValueError('AMD64 executable required')
    rows = []
    for va, expected, replacement, label in patches((REFERENCE / 'reference/dynamitePatches.cpp').read_text()):
        row = inspect_location(pe, va, len(expected))
        if row['status'] == 'file_backed':
            row['status'] = 'expected_bytes_match' if row['actual_hex'] == expected.hex() else 'expected_bytes_mismatch'
        row.update(upstream_va=hex(va), expected_hex=expected.hex(), replacement_hex=replacement.hex(),
                   equal_patch_length=len(expected) == len(replacement), description=label,
                   runtime_approved=False)
        rows.append(row)
    mapping = uncomment((REFERENCE / 'reference/mgsvtpp_addresses_1_0_15_3_en.h').read_text())
    symbols = []
    for name, address in re.findall(r'\{\s*"([A-Za-z_][A-Za-z_0-9]*)"\s*,\s*(0x[0-9a-fA-F]+)\s*\}', mapping):
        symbols.append(dict(name=name, upstream_va=address, runtime_approved=False,
                            **inspect_location(pe, int(address, 16), 16)))
    if not symbols or len({s['name'] for s in symbols}) != len(symbols):
        raise ValueError('Empty or duplicate symbol inventory')
    return dict(utc=datetime.now(timezone.utc).isoformat(), upstream_commit=COMMIT,
                upstream_license='GPLv3 with Commons Clause v1.0; see third_party/dynamite/LICENSE',
                executable_sha256=hashlib.sha256(data).hexdigest(), image_size=hex(pe.image_size),
                patch_counts=dict(Counter(r['status'] for r in rows)),
                symbol_counts=dict(Counter(r['status'] for r in symbols)),
                patches=rows, symbols=symbols, runtime_calls=False, process_writes=False,
                limitations=['Upstream symbol names are source claims, not independently verified identities.',
                             'File-backed addresses and short byte matches do not prove callable ABI or lifecycle.',
                             'No relocation guesses, live patches, connection, actor allocation or campaign support.',
                             'Master-server URL patch is outside GetPatches and is not included.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.executable)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('patch_counts', 'symbol_counts', 'image_size')}))
