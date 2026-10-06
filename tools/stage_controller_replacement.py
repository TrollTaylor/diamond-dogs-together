"""Stage pinned free-roam packages with their overlapping controllers removed.

Original local packaging tool. The removal strategy follows pinned Dynamite;
all installed archives stay untouched. Supports pinned two- and four-slot controls.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

from stage_player_runtime_bundle import ROOT, CONVERTER_SHA, checked, inside, sha
from compile_player_asset_stage import signature
from stage_four_player_assets import prop

RUNTIME_SHAS = {
    2: 'c6603a58d5a6037cacb4e98bd6ece8dd9e50fc0a325e4aec400f28bdcc26be8f',
    4: '28308ddb53dacb4e76a3980fdc73023f95ad646c15e0ed3dabf4229cf9cd6742',
}
MISSION_SHA = '3b38bdb7cc8d1de21d76fea8064ed097d9164038af548a5d9dc090225fccdb49'
PARTNER_SHA = '05f6e682cfc4670269cbc5b29a923dc98de3b0ca7266d8ee010daabce974ed36'
REMOVE = '/Assets/tpp/level_asset/chara/player/game_object/single_player2_instance.fox2'
REMOVE_SHA = 'dc38ce424736be69765660d077c05cd7329e00bab50bdb58e1817298f5ab4f1d'
MISSION = '/Assets/tpp/pack/mission2/free/f30050/mgs_coop_f30050'
PLAYER = '/Assets/tpp/pack/player/game_object/'
FREE_RUNTIME_SHA = '26ce7c2168d655c35be39e4f17a295206aa0522de8782cd57a36c4feea382f54'
EXE_SHA = '085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45'
FREE_SOURCES = {
    30010: dict(fpk='97c50bee8885f581b54d86f6b42585509a1393436d6225f1f26e5eb5cd402414',
                fpkd='1238d2538db5ba44823cf1703cd0efed5b7b44d6fb6c09c2eaf0ac0df2d6efff',
                count=366, controller='single_pl2_instance_scarf.fox2',
                controller_sha='2eec9aa68f858ee68a75f3ebdb0b52870895568aab257e25ef2e68a18b840a51'),
    30020: dict(fpk='c250778c09f56fcf960476fdeed9520e399b8ecb55b8437712dee5774e0a8f5c',
                fpkd='d1dcf1e0fcb174ae4df994f6317ee7e6b6a1420a59b5d3f2916c00c783248cba',
                count=320, controller='single_player2_instance.fox2',
                controller_sha='61048eaee1bacc9cedfd6c91c10735de73c933baeb4b9f5379828321c8327e8a'),
}
FOX = '/Assets/tpp/level_asset/chara/player/game_object/'


def build_freeroam(runtime, extracted, converter, output, executable):
    """Extend the proven two-slot Mother Base archive; preserve all seven entries.

    Input packs are exact-build local game copies, never distribution payloads.
    Repeated builds with these pins must produce the same output archive hash.
    """
    runtime, extracted, converter, output, executable = [p.resolve() for p in (runtime, extracted, converter, output, executable)]
    root = (ROOT / 'build').resolve()
    if output == root or not output.is_relative_to(root):
        raise ValueError('Output must be new and under build/')
    checked(runtime, FREE_RUNTIME_SHA)
    checked(converter, CONVERTER_SHA)
    checked(executable, EXE_SHA)
    inventory = json.loads((extracted / 'inventory.json').read_text())
    inputs = {}
    for code, spec in FREE_SOURCES.items():
        for ext in ('fpk', 'fpkd'):
            path = f'/Assets/tpp/pack/mission2/free/f{code}/f{code}.{ext}'
            matches = [p for p in inventory['packages'] if p['path'] == path]
            if len(matches) != 1 or matches[0]['sha256'] != spec[ext]:
                raise ValueError(f'Missing, ambiguous or changed game package: {path}')
            inputs[code, ext] = checked(inside(extracted, matches[0]['saved_as']), spec[ext])
    output.mkdir()

    def run(*args):
        result = subprocess.run([str(converter), *map(str, args)], check=True, capture_output=True, text=True, timeout=60)
        # This CLI sometimes catches an error and still exits zero.
        if 'error' in result.stderr.lower() or 'panic' in result.stderr.lower():
            raise ValueError(f'Converter reported an error: {result.stderr[:500]}')

    base_paths = [PLAYER + n for n in ('mgs_coop_four_players_game_obj.fpk', 'mgs_coop_four_players_game_obj.fpkd',
                                     'mgs_coop_four_player_controller.fpk', 'mgs_coop_four_player_controller.fpkd')]
    base_paths += ['/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua', MISSION + '.fpk', MISSION + '.fpkd']
    scarf = PLAYER + 'mgs_coop_two_player_scarf_controller'
    paths = base_paths + [scarf + '.fpk', scarf + '.fpkd']
    for code in FREE_SOURCES:
        paths += [f'/Assets/tpp/pack/mission2/free/f{code}/mgs_coop_f{code}.{ext}' for ext in ('fpk', 'fpkd')]
    dictionary = output / 'paths.txt'
    dictionary.write_text('\n'.join(sorted({p.rsplit('.', 1)[0] for p in paths})) + '\n')
    local_runtime = output / 'source.dat'
    shutil.copyfile(runtime, local_runtime)
    files = output / 'files'
    run(local_runtime, files, dictionary)
    source_inventory = json.loads(Path(str(local_runtime) + '.json').read_text())
    if (source_inventory['version'] != 1 or source_inventory['flags'] != 3150304 or
            len(source_inventory['entries']) != 7 or {e['filePath'] for e in source_inventory['entries']} != set(base_paths)):
        raise ValueError('Unexpected proven Mother Base archive inventory')
    unchanged = {p: sha(inside(files, p.lstrip('/'))) for p in base_paths}

    def verify_package(package, expected, source_root):
        run(package)
        meta = json.loads(Path(str(package) + '.json').read_text())
        if meta.get('entries', []) != expected or meta.get('references'):
            raise ValueError('Repacked definition inventory differs')
        restored = package.parent / (package.stem + '_fpkd')
        hashes = {e['filePath']: sha(inside(source_root, e['filePath'].lstrip('/'))) for e in expected}
        for path, digest in hashes.items():
            checked(inside(restored, path.lstrip('/')), digest)
        return hashes

    # The normal controller remains byte-identical. Afghanistan differs only
    # in its two parts-type entries; same allocation/motion/parts budgets.
    normal = inside(files, (PLAYER + 'mgs_coop_four_player_controller.fpkd').lstrip('/'))
    run(normal)
    normal_root = normal.parent / (normal.stem + '_fpkd')
    meta = json.loads(Path(str(normal) + '.json').read_text())
    target = FOX + 'mgs_coop_four_player_controller.fox2'
    if len(meta['entries']) != 4 or sum(e['filePath'] == target for e in meta['entries']) != 1 or meta.get('references'):
        raise ValueError('Unexpected proven controller inventory')
    original_xml = output / 'normal-controller.fox2.xml'
    run(inside(normal_root, target.lstrip('/')), original_xml)
    tree = ET.parse(original_xml)
    cls = 'TppPlayer2BlockControllerData'
    for name, value in (('configuration_partsBlockCount', '2'), ('instanceSettings_instanceBlockSize', '1126400')):
        node = prop(tree, cls, name)
        if len(node.findall('containerEntry')) != 1 or node.find('containerEntry').text != value:
            raise ValueError('Controller is not the proven two-slot allocation')
    for name in ('instanceSettings_partsTypeNames', 'instanceSettings_partsTypeInitial'):
        node = prop(tree, cls, name)
        children = node.findall('containerEntry')
        if node.get('arraySize') != '2' or len(children) != 2 or any(e.text != 'PLTypeNormal' for e in children):
            raise ValueError('Unexpected normal parts array')
        for child in children:
            child.text = 'PLTypeNormalScarf'
    edited = output / 'scarf-controller.fox2.xml'
    ET.indent(tree, space='  ')
    tree.write(edited, encoding='utf-8')
    compiled = output / 'scarf-controller.fox2'
    run(edited, compiled)
    restored_xml = output / 'scarf-roundtrip.fox2.xml'
    run(compiled, restored_xml)
    if signature(edited) != signature(restored_xml):
        raise ValueError('Scarf FOX2 semantic round trip differs')
    for name in ('instanceSettings_partsTypeNames', 'instanceSettings_partsTypeInitial'):
        for child in prop(tree, cls, name).findall('containerEntry'):
            child.text = 'PLTypeNormal'
    reversed_xml = output / 'scarf-reversed.fox2.xml'
    tree.write(reversed_xml, encoding='utf-8')
    if signature(reversed_xml) != signature(original_xml):
        raise ValueError('Unintended scarf controller edit')
    scarf_root = output / 'scarf-files'
    shutil.copytree(normal_root, scarf_root)
    shutil.copyfile(compiled, inside(scarf_root, target.lstrip('/')))
    scarf_definition = output / 'scarf.json'
    scarf_definition.write_text(json.dumps(dict(type='fpkd', entries=meta['entries'])))
    scarf_package = inside(files, (scarf + '.fpkd').lstrip('/'))
    run(scarf_definition, scarf_package, scarf_root)
    scarf_hashes = verify_package(scarf_package, meta['entries'], scarf_root)
    for e in meta['entries']:
        p = e['filePath']
        if p != target:
            checked(inside(scarf_root, p.lstrip('/')), sha(inside(normal_root, p.lstrip('/'))))
    shutil.copyfile(inside(files, (PLAYER + 'mgs_coop_four_player_controller.fpk').lstrip('/')),
                    inside(files, (scarf + '.fpk').lstrip('/')))

    missions = []
    for code, spec in FREE_SOURCES.items():
        local = output / f'original-{code}.fpkd'
        shutil.copyfile(inputs[code, 'fpkd'], local)
        run(local)
        meta = json.loads(Path(str(local) + '.json').read_text())
        entries = meta['entries']
        removed = FOX + spec['controller']
        if (len(entries) != spec['count'] or len({e['filePath'] for e in entries}) != spec['count'] or meta.get('references') or
                sum(e['filePath'] == removed for e in entries) != 1):
            raise ValueError('Unexpected free-roam mission definitions')
        local_root = local.parent / (local.stem + '_fpkd')
        checked(inside(local_root, removed.lstrip('/')), spec['controller_sha'])
        kept = [e for e in entries if e['filePath'] != removed]
        if any(e['filePath'].endswith(('single_player2_instance.fox2', 'single_pl2_instance_scarf.fox2')) for e in kept):
            raise ValueError('A second single-player controller remains')
        definition = output / f'mission-{code}.json'
        definition.write_text(json.dumps(dict(type='fpkd', entries=kept)))
        replacement = f'/Assets/tpp/pack/mission2/free/f{code}/mgs_coop_f{code}'
        package = inside(files, (replacement + '.fpkd').lstrip('/'))
        package.parent.mkdir(parents=True, exist_ok=True)
        run(definition, package, local_root)
        retained = verify_package(package, kept, local_root)
        resource = inside(files, (replacement + '.fpk').lstrip('/'))
        shutil.copyfile(inputs[code, 'fpk'], resource)
        checked(resource, spec['fpk'])
        missions.append(dict(mission=code, original_fpk_sha256=spec['fpk'], original_fpkd_sha256=spec['fpkd'],
                             removed_definition=dict(path=removed, sha256=spec['controller_sha']),
                             retained_definitions=len(kept), retained_bytes_verified=True, retained_sha256=retained,
                             replacement=replacement + '.fpk', resource_bytes_unchanged=True))

    definition = output / 'archive.json'
    definition.write_text(json.dumps(dict(type='qar', version=1, flags=3150304, entries=[dict(filePath=p) for p in paths])))
    archive = output / 'mgs-coop-player-runtime.dat'
    run(definition, archive, files)
    restored = output / 'archive-roundtrip'
    run(archive, restored, dictionary)
    meta = json.loads(Path(str(archive) + '.json').read_text())
    if (meta['version'] != 1 or meta['flags'] != 3150304 or len(meta['entries']) != len(paths) or
            {e['filePath'] for e in meta['entries']} != set(paths)):
        raise ValueError('Free-roam archive inventory differs')
    final_hashes = {p: sha(inside(files, p.lstrip('/'))) for p in paths}
    for p, digest in final_hashes.items():
        checked(inside(restored, p.lstrip('/')), digest)
    for p, digest in unchanged.items():
        checked(inside(restored, p.lstrip('/')), digest)
    report = dict(experiment='two_player_freeroam_controllers', players=2, missions=missions,
                  inspected_executable_sha256=EXE_SHA, source_archive_sha256=FREE_RUNTIME_SHA,
                  converter_sha256=CONVERTER_SHA, original_runtime_entries_unchanged=7,
                  scarf_controller=dict(parts_type='PLTypeNormalScarf', semantic_roundtrip=True,
                      intended_changes_only=True, unchanged_supplemental=3, embedded_sha256=scarf_hashes),
                  archive=dict(sha256=sha(archive), bytes=archive.stat().st_size, entries_verified=len(paths), entry_sha256=final_hashes),
                  installed=False, runtime_verified=False,
                  limitations=['Normal 30010/30020/30050 only; no 30011, story or helicopter body support.',
                               'A partner outside loaded blocks may remain unrealized; never disable the area policy.',
                               'Static round trips do not verify visibility, animation, memory headroom or teardown.',
                               'Local game-derived output only; never commit or distribute this archive.'])
    shutil.copyfile(ROOT / 'third_party/dynamite/LICENSE', output / 'DYNAMITE-LICENSE')
    (output / 'control.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def build(runtime, mission, partner, converter, output, players=2):
    if players not in RUNTIME_SHAS:
        raise ValueError('Only pinned two- and four-slot runtimes are supported')
    runtime_sha = RUNTIME_SHAS[players]
    runtime, mission, partner, converter, output = [p.resolve() for p in (runtime, mission, partner, converter, output)]
    root = (ROOT / 'build').resolve()
    if output == root or not output.is_relative_to(root):
        raise ValueError('Output must be new and under build/')
    for path, digest in ((runtime, runtime_sha), (mission, MISSION_SHA), (partner, PARTNER_SHA), (converter, CONVERTER_SHA)):
        checked(path, digest)
    output.mkdir()

    def run(*args):
        subprocess.run([str(converter), *map(str, args)], check=True, capture_output=True, text=True, timeout=30)

    paths = [PLAYER + n for n in ('mgs_coop_four_players_game_obj.fpk', 'mgs_coop_four_players_game_obj.fpkd',
                                 'mgs_coop_four_player_controller.fpk', 'mgs_coop_four_player_controller.fpkd')]
    paths.append('/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua')
    dictionary = output / 'paths.txt'
    dictionary.write_text('\n'.join(sorted({p.rsplit('.', 1)[0] for p in paths} | {MISSION})) + '\n')
    local_runtime = output / 'source.dat'
    shutil.copyfile(runtime, local_runtime)
    files = output / 'files'
    run(local_runtime, files, dictionary)
    source_inventory = json.loads(Path(str(local_runtime) + '.json').read_text())
    if len(source_inventory['entries']) != 5 or {e['filePath'] for e in source_inventory['entries']} != set(paths):
        raise ValueError('Unexpected source archive inventory')
    unchanged = {p: sha(inside(files, p.lstrip('/'))) for p in paths}

    local_mission = output / 'original.fpkd'
    shutil.copyfile(mission, local_mission)
    run(local_mission)
    metadata = json.loads(Path(str(local_mission) + '.json').read_text())
    entries = metadata['entries']
    if len(entries) != 45 or len({e['filePath'] for e in entries}) != 45 or metadata.get('references'):
        raise ValueError('Unexpected original mission definitions')
    extracted = output / 'original_fpkd'
    checked(inside(extracted, REMOVE.lstrip('/')), REMOVE_SHA)
    kept = [e for e in entries if e['filePath'] != REMOVE]
    if len(kept) != 44:
        raise ValueError('Controller removal is not unique')
    kept_hashes = {e['filePath']: sha(inside(extracted, e['filePath'].lstrip('/'))) for e in kept}
    definition = output / 'mission.json'
    definition.write_text(json.dumps(dict(type='fpkd', entries=kept)))
    new_mission = inside(files, (MISSION + '.fpkd').lstrip('/'))
    new_mission.parent.mkdir(parents=True, exist_ok=True)
    run(definition, new_mission, extracted)
    run(new_mission)
    roundtrip = new_mission.parent / (new_mission.stem + '_fpkd')
    inventory = json.loads(Path(str(new_mission) + '.json').read_text())
    if inventory['entries'] != kept or inventory.get('references'):
        raise ValueError('Repacked mission inventory differs')
    for p, digest in kept_hashes.items():
        checked(inside(roundtrip, p.lstrip('/')), digest)
    new_partner = inside(files, (MISSION + '.fpk').lstrip('/'))
    shutil.copyfile(partner, new_partner)
    checked(new_partner, PARTNER_SHA)  # Preserve resources and references exactly.
    paths.extend([MISSION + '.fpk', MISSION + '.fpkd'])
    definition = output / 'archive.json'
    definition.write_text(json.dumps(dict(type='qar', version=1, flags=3150304,
                                         entries=[dict(filePath=p) for p in paths])))
    archive = output / 'mgs-coop-player-runtime.dat'
    run(definition, archive, files)
    restored = output / 'archive-roundtrip'
    run(archive, restored, dictionary)
    inventory = json.loads(Path(str(archive) + '.json').read_text())
    if (inventory['version'] != 1 or inventory['flags'] != 3150304 or len(inventory['entries']) != 7 or
            {e['filePath'] for e in inventory['entries']} != set(paths)):
        raise ValueError('Seven-entry archive inventory differs')
    for p in paths:
        checked(inside(restored, p.lstrip('/')), sha(inside(files, p.lstrip('/'))))
    for p, digest in unchanged.items():
        checked(inside(restored, p.lstrip('/')), digest)
    shutil.copyfile(ROOT / 'third_party/dynamite/LICENSE', output / 'DYNAMITE-LICENSE')
    report = dict(experiment='single_controller_mother_base_control', players=players,
                  dynamite_commit='c90f252f8200765f66c005d6bf4357fda0c0d98e',
                  source_archive_sha256=runtime_sha, source_mission_sha256=MISSION_SHA,
                  removed_definition=dict(path=REMOVE, sha256=REMOVE_SHA),
                  retained_definitions=44, retained_bytes_verified=True,
                  original_runtime_entries_unchanged=5, original_partner_sha256=PARTNER_SHA,
                  replacement_mission=MISSION + '.fpk', archive_entries_verified=7,
                  archive=dict(sha256=sha(archive), bytes=archive.stat().st_size), installed=False,
                  limitations=['Only normal f30050 is staged; demo/hangar variants must not select this control.',
                               'Additional-parts overlap remains unchanged to isolate controller removal.',
                               'Local game-derived assets only; no playable multiplayer or verified cleanup yet.'])
    (output / 'control.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('mission', 'partner', 'extracted', 'executable'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--freeroam', action='store_true', help='Extend the proven Mother Base archive with normal 30010/30020 controllers')
    parser.add_argument('--players', type=int, choices=(2, 4), default=2)
    args = parser.parse_args()
    if args.freeroam:
        if args.extracted is None or args.executable is None or args.players != 2 or args.mission is not None or args.partner is not None:
            parser.error('--freeroam requires --extracted, --executable and --players 2; omit --mission/--partner')
        report = build_freeroam(args.runtime, args.extracted, args.converter, args.output, args.executable)
    else:
        if args.mission is None or args.partner is None or args.extracted is not None or args.executable is not None:
            parser.error('Mother Base staging requires --mission/--partner; omit --extracted/--executable')
        report = build(args.runtime, args.mission, args.partner, args.converter, args.output, args.players)
    print(json.dumps(report, indent=2))
