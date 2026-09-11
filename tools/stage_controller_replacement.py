"""Stage a renamed Mother Base package with one overlapping controller removed.

Original local packaging tool. The removal strategy follows pinned Dynamite;
all installed archives stay untouched. Supports pinned two- and four-slot controls.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from stage_player_runtime_bundle import ROOT, CONVERTER_SHA, checked, inside, sha

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
    for name in ('runtime', 'mission', 'partner', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--players', type=int, choices=(2, 4), default=2)
    args = parser.parse_args()
    print(json.dumps(build(args.runtime, args.mission, args.partner, args.converter, args.output, args.players), indent=2))
