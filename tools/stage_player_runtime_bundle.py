"""Build a local four-player allocation/parts/motion experiment, without installing.

Project-authored packaging tool. Supplemental XML derives from the pinned
Dynamite source under its GPLv3/Commons Clause terms. Game bytes stay in build/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

from compile_player_asset_stage import signature
from stage_four_player_assets import scalar

ROOT = Path(__file__).resolve().parent.parent
CONVERTER_SHA = 'a1ec95c8adab11339b7a16ec2ebe7bd92a521bab428f9238feaa20da3bc64c1e'
SUPPLEMENTAL = {
    'player2_additional_parts_x1.fox2.xml': 'b81f5e22f0e3317f4986ac5f011de044ec528a2d159d1a691632d4866157315c',
    'player2_online_motion.fox2.xml': 'c6b5fa73eeda2e63a440ab01e853672fb00beecb0387d6d4b437219b421180ca',
    'player2_mtbs_motion_loader.fox2.xml': '01aaf463b0e93f32a0efd62403130c3d8864906589038adb4b63457be62fe0f4',
}
ALLOCATION = {
    'mgs_coop_four_players_game_obj.fpk': '60b461fdf13b32b6e35fe67191d3b55e22d6c589468178b7446340a197816293',
    'mgs_coop_four_players_game_obj.fpkd': 'bd6bd3c39cbc8d244d2e14e816e97715f5b518b39ed291323a056caff7887dd1',
}
PACK_PREFIX = '/Assets/tpp/pack/player/game_object/'
FOX_PREFIX = '/Assets/tpp/level_asset/chara/player/game_object/'
MOTION_SOURCE = '/Assets/tpp/pack/location/mtbs/pack_mission/mtbs_mission_fob.fpk'
GAME_SOURCES = {
    MOTION_SOURCE: 'f2092c931361235826346509dd82b1a188c7d18787fb432e7a064a60dc96d279',
    '/Assets/tpp/pack/player/motion/player2_location_motion.fpk': '1cda98ef5e86ecc47075715836278b8ad5b27d1b04382781576aca233749b011',
    '/Assets/tpp/pack/player/motion/player2_location_vram_motion.fpk': '98ede6815a3a6ae126ad5c34d39a8fbfca481b6e0616f0c13d98134f2be86a2c',
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(path, expected):
    if sha(path) != expected:
        raise ValueError(f'Changed or unreviewed input: {path.name}')
    return path


def inside(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError('Path escapes its local input/output root')
    return path


def build(stage, extracted, upstream, converter, output, additional_parts_count=3):
    if additional_parts_count not in (1, 3):
        raise ValueError("Only the original-count control and three-part experiment are supported")
    stage, extracted, upstream = stage.resolve(), extracted.resolve(), upstream.resolve()
    output, converter = output.resolve(), converter.resolve()
    build_root = (ROOT / 'build').resolve()
    if output == build_root or not output.is_relative_to(build_root):
        raise ValueError('Output must be a new directory beneath project build/')
    checked(converter, CONVERTER_SHA)
    for name, digest in ALLOCATION.items():
        checked(stage / 'compiled' / name, digest)
    controller = stage / 'four_player_controller.fox2.xml'
    # Pin the XML as well as the already inspected controller binary.
    source_manifest = json.loads((stage / 'stage.json').read_text())
    controller_items = [f for f in source_manifest['files'] if f['xml'] == controller.name]
    if len(controller_items) != 1:
        raise ValueError('Ambiguous allocation controller')
    checked(controller, controller_items[0]['sha256'])
    checked(stage / 'compiled/files' / (FOX_PREFIX.lstrip('/') + 'mgs_coop_four_player_controller.fox2'),
            'a200dc9fc0588110ef060edeb5c6a84fd8fb49803d581d2edbefff4bb729ee1d')
    supplemental_root = upstream / 'moddata/coop_essentials/coop_essentials_fpkd' / FOX_PREFIX.lstrip('/')
    for name, digest in SUPPLEMENTAL.items():
        checked(supplemental_root / name, digest)
    inventory = json.loads((extracted / 'inventory.json').read_text())
    packages = {}
    for path, digest in GAME_SOURCES.items():
        matches = [p for p in inventory['packages'] if p['path'] == path]
        if len(matches) != 1 or matches[0]['sha256'] != digest:
            raise ValueError(f'Missing, ambiguous or changed game package: {path}')
        packages[path] = (matches[0], checked(inside(extracted, matches[0]['saved_as']), digest))
    motion_meta, motion_source = packages[MOTION_SOURCE]
    expected_motion = {
        '/Assets/tpp/motion/mtar/player2/player2_online.mtar': 'e99b76d35b549f64e2b4678b9771e394b80b78d14a5cea187306bece66b58750',
        '/Assets/tpp/motion/mtar/player2/player2_pipe.mtar': '70e18a4da821491d25e93f56fe09465201c5f01248eb2be669b449c1f84efe5a',
    }
    if {e['path']: e['sha256'] for e in motion_meta['entries']} != expected_motion:
        raise ValueError('Motion inventory differs from inspected package')
    output.mkdir()  # No overwrite or merge with a previous experiment.
    files, xml_dir = output / 'files', output / 'source'
    xml_dir.mkdir()

    def run(*args):
        subprocess.run([str(converter), *map(str, args)], check=True, capture_output=True, text=True, timeout=30)

    def destination(internal):
        path = inside(files, internal.lstrip('/'))
        path.parent.mkdir(parents=True, exist_ok=True)
        return path

    edits, entries, checks = [], [], []
    sources = [('mgs_coop_four_player_controller.fox2', controller)]
    for name in SUPPLEMENTAL:
        tree = ET.parse(supplemental_root / name)
        target_name = name.removesuffix('.xml')
        if name == 'player2_additional_parts_x1.fox2.xml':
            edits.append(scalar(tree, 'TppPlayer2AdditionalPartsBlockData', 'count', 1, additional_parts_count))
            target_name = 'mgs_coop_player2_additional_parts_x3.fox2'
        path = xml_dir / (target_name + '.xml')
        ET.indent(tree, space='  ')
        tree.write(path, encoding='utf-8', xml_declaration=False)
        sources.append((target_name, path))
    shutil.copyfile(controller, xml_dir / controller.name)
    for index, (name, source) in enumerate(sources):
        internal = FOX_PREFIX + name
        binary = destination(internal)
        run(source, binary)
        if index == 0:
            checked(binary, 'a200dc9fc0588110ef060edeb5c6a84fd8fb49803d581d2edbefff4bb729ee1d')
        restored = output / f'roundtrip-{index}.xml'
        run(binary, restored)
        if signature(source) != signature(restored):
            raise ValueError('XML/FOX2 semantic round trip differs')
        entries.append(dict(filePath=internal))
        checks.append(dict(internal_path=internal, sha256=sha(binary), xml_nodes=len(signature(source)), xml_roundtrip_equal=True))
    name = 'mgs_coop_four_player_controller'
    definition = output / 'controller-definition.json'
    definition.write_text(json.dumps(dict(type='fpkd', entries=entries)))
    controller_pair = destination(PACK_PREFIX + name + '.fpkd')
    run(definition, controller_pair, files)
    run(controller_pair)
    unpacked = controller_pair.parent / (name + '_fpkd')
    for entry in checks:
        checked(inside(unpacked, entry['internal_path'].lstrip('/')), entry['sha256'])
        entry['fpkd_embedded_bytes_equal'] = True
    # As in Dynamite createEssentials(), the paired FPK supplies the installed
    # game's online/pipe MTAR data; no unrelated FOB mission definition is loaded.
    motion_pair = destination(PACK_PREFIX + name + '.fpk')
    shutil.copyfile(motion_source, motion_pair)
    run(motion_pair)
    for internal, digest in expected_motion.items():
        checked(inside(motion_pair.parent / (name + '_fpk'), internal.lstrip('/')), digest)
    for filename, digest in ALLOCATION.items():
        path = destination(PACK_PREFIX + filename)
        shutil.copyfile(stage / 'compiled' / filename, path)
        checked(path, digest)
    shutil.copyfile(ROOT / 'third_party/dynamite/LICENSE', output / 'DYNAMITE-LICENSE')
    outputs = [dict(path=PACK_PREFIX + filename, sha256=sha(files / (PACK_PREFIX.lstrip('/') + filename)))
               for filename in [*ALLOCATION, name + '.fpk', name + '.fpkd']]
    probe_path = '/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua'
    probe = destination(probe_path)
    shutil.copyfile(ROOT / 'lua/coop/archive_probe.lua', probe)
    outputs.append(dict(path=probe_path, sha256=sha(probe)))
    # Standalone archive: never rebuild or append to an installed game archive.
    # This version/flags pair is exercised by datfpk's local plain-QAR fixture;
    # successful round-trip reading does not establish a live mount contract.
    archive_definition = output / 'runtime-archive-definition.json'
    archive_definition.write_text(json.dumps(dict(type='qar', version=1, flags=3150304,
                                                  entries=[dict(filePath=f['path']) for f in outputs])))
    archive = output / 'mgs-coop-player-runtime.dat'
    run(archive_definition, archive, files)
    dictionary = output / 'runtime-paths.txt'
    # hashing.Dictionary resolves the extension separately; its lines are stems.
    dictionary.write_text('\n'.join(sorted({f['path'].rsplit('.', 1)[0] for f in outputs})) + '\n')
    archive_extract = output / 'archive-roundtrip'
    run(archive, archive_extract, dictionary)
    archive_inventory = json.loads(Path(str(archive) + '.json').read_text())
    if (archive_inventory['version'] != 1 or archive_inventory['flags'] != 3150304 or
            len(archive_inventory['entries']) != len(outputs) or
            {e['filePath'] for e in archive_inventory['entries']} != {f['path'] for f in outputs}):
        raise ValueError('Standalone archive inventory differs')
    for item in outputs:
        checked(inside(archive_extract, item['path'].lstrip('/')), item['sha256'])
        item['qar_roundtrip_equal'] = True
    report = dict(experiment='four_player_runtime_assets', players=4, additional_parts_count=additional_parts_count, installed=False, runtime_verified=False,
                  mission_pack=PACK_PREFIX + name + '.fpk', outputs=outputs, definitions=checks,
                  archive=dict(file=archive.name, sha256=sha(archive), bytes=archive.stat().st_size,
                               version=1, flags=3150304, entries_verified=len(outputs), live_mount_verified=False),
                  changes=edits, converter_sha256=CONVERTER_SHA,
                  dynamite_commit='c90f252f8200765f66c005d6bf4357fda0c0d98e', supplemental_sources=SUPPLEMENTAL,
                  motion_entries_verified=expected_motion,
                  installed_dependency_packages={k: v for k, v in GAME_SOURCES.items() if k != MOTION_SOURCE},
                  source_allocation_manifest=source_manifest,
                  limitations=[f'Additional parts count {additional_parts_count} is an experiment; count 3 has not been established as a three-guest requirement. The x3 internal filename is retained for a count-only comparison.',
                               'Instance memory budget remains the earlier unverified doubled value.',
                               'Shared motion budgets are preserved; sufficiency for four actors is unverified.',
                               'Archive mounting, precedence, mission selection, session slots and actor control remain unverified.',
                               'Only the controller FPK should be added to the mission list; it owns selection of the game-object package.',
                               'Local game-derived experiment only; not a distributable release.'])
    (output / 'bundle.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--extracted', type=Path, required=True)
    parser.add_argument('--upstream', type=Path, default=ROOT / 'work/upstream-dynamite')
    parser.add_argument('--converter', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--additional-parts-count', type=int, choices=(1, 3), default=3)
    args = parser.parse_args()
    result = build(args.stage, args.extracted, args.upstream, args.converter, args.output, args.additional_parts_count)
    print(f"Verified {len(result['definitions'])} FOX2 definitions and two motion entries; four package files staged, not installed")
