"""Revert only allocation scaling in the pinned parts-one runtime experiment.

Project-authored, local-only control. Keeps all package paths and supplemental
definitions identical so it can distinguish four-slot sizing from load lifetime.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

from compile_player_asset_stage import signature
from stage_four_player_assets import prop, scalar
from stage_player_runtime_bundle import ROOT, CONVERTER_SHA, checked, inside, sha

SOURCE_SHA = '28308ddb53dacb4e76a3980fdc73023f95ad646c15e0ed3dabf4229cf9cd6742'
PACK = '/Assets/tpp/pack/player/game_object/'
FOX = '/Assets/tpp/level_asset/chara/player/game_object/'


def build(source, converter, output):
    source, converter, output = source.resolve(), converter.resolve(), output.resolve()
    build_root = (ROOT / 'build').resolve()
    if output == build_root or not output.is_relative_to(build_root):
        raise ValueError('Output must be a new directory under build/')
    archive = checked(source / 'mgs-coop-player-runtime.dat', SOURCE_SHA)
    checked(converter, CONVERTER_SHA)
    output.mkdir()  # Refuse existing directories.

    def run(*args):
        subprocess.run([str(converter), *map(str, args)], check=True,
                       capture_output=True, text=True, timeout=30)

    names = ['mgs_coop_four_players_game_obj.fpk', 'mgs_coop_four_players_game_obj.fpkd',
             'mgs_coop_four_player_controller.fpk', 'mgs_coop_four_player_controller.fpkd']
    paths = [PACK + n for n in names] + ['/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua']
    dictionary = output / 'paths.txt'
    dictionary.write_text('\n'.join(sorted({p.rsplit('.', 1)[0] for p in paths})) + '\n')
    # Extract the fingerprinted archive, rather than trusting loose input files.
    local_archive = output / 'source.dat'
    shutil.copyfile(archive, local_archive)
    files = output / 'files'
    run(local_archive, files, dictionary)
    inventory = json.loads(Path(str(local_archive) + '.json').read_text())
    if (inventory['version'] != 1 or inventory['flags'] != 3150304 or
            len(inventory['entries']) != 5 or {e['filePath'] for e in inventory['entries']} != set(paths)):
        raise ValueError('Unexpected pinned archive inventory')
    before = {p: sha(inside(files, p.lstrip('/'))) for p in paths}
    edits, checks = [], []
    for index, name in enumerate((names[1], names[3])):
        package = inside(files, (PACK + name).lstrip('/'))
        run(package)
        package_root = package.parent / (package.stem + '_fpkd')
        metadata = json.loads(Path(str(package) + '.json').read_text())
        expected_count = 1 if index == 0 else 4
        if len(metadata['entries']) != expected_count:
            raise ValueError('Unexpected definition count')
        target = FOX + ('mgs_coop_four_players_game_obj.fox2' if index == 0 else 'mgs_coop_four_player_controller.fox2')
        originals = {e['filePath']: sha(inside(package_root, e['filePath'].lstrip('/'))) for e in metadata['entries']}
        binary = inside(package_root, target.lstrip('/'))
        xml = output / f'original-{index}.xml'
        run(binary, xml)
        tree = ET.parse(xml)
        if index == 0:
            edits.extend(scalar(tree, 'GameObject', n, 4, 2) for n in ('totalCount', 'realizedCount'))
        else:
            cls = 'TppPlayer2BlockControllerData'
            edits.append(scalar(tree, cls, 'configuration_partsBlockCount', 4, 2))
            edits.append(scalar(tree, cls, 'instanceSettings_instanceBlockSize', 2252800, 1126400))
            for n in ('instanceSettings_partsTypeNames', 'instanceSettings_partsTypeInitial'):
                node = prop(tree, cls, n)
                children = node.findall('containerEntry')
                if node.get('arraySize') != '4' or len(children) != 4 or any(e.text != 'PLTypeNormal' for e in children):
                    raise ValueError('Unexpected four-entry parts array')
                for child in children[2:]:
                    node.remove(child)
                node.set('arraySize', '2')
                edits.append(dict(entity=cls, property=n, before=4, after=2))
        # The converter dispatches XML compilation by the full .fox2.xml suffix.
        edited = output / f'control-{index}.fox2.xml'
        ET.indent(tree, space='  ')
        tree.write(edited, encoding='utf-8')
        # Compile to a new path and validate before replacing our local copy.
        compiled = output / f'control-{index}.fox2'
        run(edited, compiled)
        restored = output / f'roundtrip-{index}.xml'
        run(compiled, restored)
        if signature(edited) != signature(restored):
            raise ValueError('FOX2 semantic round trip differs')
        shutil.copyfile(compiled, binary)
        # Verify the complete XML delta by reversing exactly the intended edits.
        original_tree = ET.parse(xml)
        if index == 0:
            for n in ('totalCount', 'realizedCount'):
                scalar(tree, 'GameObject', n, 2, 4)
        else:
            for n in ('configuration_partsBlockCount', 'instanceSettings_instanceBlockSize',
                      'instanceSettings_partsTypeNames', 'instanceSettings_partsTypeInitial'):
                current = prop(tree, cls, n)
                original = prop(original_tree, cls, n)
                current.attrib.clear(); current.attrib.update(original.attrib)
                for child in list(current):
                    current.remove(child)
                for child in list(original):
                    current.append(child)
        normalized = output / f'reversed-{index}.xml'
        tree.write(normalized, encoding='utf-8')
        if signature(normalized) != signature(xml):
            raise ValueError('Unintended XML edit')
        expected = {p: sha(inside(package_root, p.lstrip('/'))) for p in originals}
        if any(expected[p] != digest for p, digest in originals.items() if p != target):
            raise ValueError('Supplemental definition changed')
        packed = output / name
        definition = output / f'package-{index}.json'
        definition.write_text(json.dumps(dict(type='fpkd', entries=metadata['entries'])))
        run(definition, packed, package_root)
        run(packed)
        for p, digest in expected.items():
            checked(inside(output / (packed.stem + '_fpkd'), p.lstrip('/')), digest)
        shutil.copyfile(packed, package)
        checks.append(dict(target=target, semantic_roundtrip=True, intended_changes_only=True,
                           embedded_bytes_equal=True, unchanged_supplemental=len(originals)-1))
    definition = output / 'archive.json'
    definition.write_text(json.dumps(dict(type='qar', version=1, flags=3150304,
                                         entries=[dict(filePath=p) for p in paths])))
    result_archive = output / 'mgs-coop-player-runtime.dat'
    run(definition, result_archive, files)
    restored = output / 'archive-roundtrip'
    run(result_archive, restored, dictionary)
    after = {p: sha(inside(files, p.lstrip('/'))) for p in paths}
    for p, digest in after.items():
        checked(inside(restored, p.lstrip('/')), digest)
    unchanged = [p for p in paths if before[p] == after[p]]
    if unchanged != [paths[0], paths[2], paths[4]]:
        raise ValueError('Unexpected package delta')
    shutil.copyfile(ROOT / 'third_party/dynamite/LICENSE', output / 'DYNAMITE-LICENSE')
    report = dict(experiment='two_player_allocation_control', players=2, additional_parts_count=1,
                  source_archive_sha256=SOURCE_SHA, converter_sha256=CONVERTER_SHA,
                  changes=edits, checks=checks, unchanged_archive_entries=unchanged,
                  archive=dict(sha256=sha(result_archive), bytes=result_archive.stat().st_size, entries_verified=5),
                  installed=False, runtime_verified=False,
                  limitations=['Diagnostic two-slot control only; the project target remains four players.',
                               'All internal paths retain four/x3 names to isolate allocation scaling.',
                               'Round trips do not validate actor initialization, cleanup or multiplayer.'])
    (output / 'control.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.source, args.converter, args.output), indent=2))
