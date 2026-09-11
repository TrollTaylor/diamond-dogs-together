"""Stage an experimental four-player allocation profile from local source files.

Does not modify installed archives. Extracted game assets remain local; this
generator describes changes without checking proprietary game data into source.
"""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
GAME_XML_SHA = 'a85d0a81527151b6e30ff455ff065632c95b97a8b75d140c3b64267f6cf46900'


def prop(tree, cls, name):
    values = tree.findall(f"./entities/entity[@class='{cls}']/staticProperties/property[@name='{name}']")
    if len(values) != 1:
        raise ValueError(f'Expected exactly one {cls}.{name}')
    return values[0]


def scalar(tree, cls, name, old, new):
    node = prop(tree, cls, name)
    entries = node.findall('containerEntry')
    if len(entries) != 1 or entries[0].text != str(old):
        raise ValueError(f'Unexpected original {cls}.{name}')
    entries[0].text = str(new)
    return dict(entity=cls, property=name, before=str(old), after=str(new))


def stage(extracted, upstream, output):
    inventory = json.loads((extracted / 'inventory.json').read_text())
    packages = [p for p in inventory['packages'] if p['path'].endswith('/two_players_game_obj.fpkd')]
    if len(packages) != 1 or packages[0]['sha256'] != '8c8e557bf2d8ccaec0450b27e6bfd81d53fd02c8e7f62d5d507b8f4b040b1efa':
        raise ValueError('Unexpected or ambiguous original player package')
    package = packages[0]
    package_path = extracted / package['saved_as']
    if hashlib.sha256(package_path.read_bytes()).hexdigest() != package['sha256']:
        raise ValueError('Extracted package changed')
    source = extracted / package['entries'][0]['xml']
    if hashlib.sha256(source.read_bytes()).hexdigest() != GAME_XML_SHA:
        raise ValueError('Decoded player XML changed; re-extract and inspect')
    relative = 'moddata/coop_essentials/coop_essentials_fpkd/Assets/tpp/level_asset/chara/player/game_object/two_player2_instances.fox2.xml'
    controller = upstream / relative
    reference_manifest = json.loads((ROOT / 'third_party/dynamite/player-asset-source.json').read_text())
    if hashlib.sha256(controller.read_bytes()).hexdigest() != reference_manifest['controller_sha256']:
        raise ValueError('Unreviewed Dynamite controller source')
    game_tree, controller_tree = ET.parse(source), ET.parse(controller)
    changes = [scalar(game_tree, 'GameObject', name, 2, 4) for name in ('totalCount','realizedCount')]
    cls = 'TppPlayer2BlockControllerData'
    changes.append(scalar(controller_tree,cls,'configuration_partsBlockCount',2,4))
    # Per-profile hypothesis: double the two-actor instance budget. Runtime
    # allocation measurements must establish whether this is sufficient.
    changes.append(scalar(controller_tree,cls,'instanceSettings_instanceBlockSize',1126400,2252800))
    changes.append(scalar(controller_tree,cls,'instanceSettings_instancePackagePath',
                          '/Assets/tpp/pack/player/game_object/two_players_game_obj.fpk',
                          '/Assets/tpp/pack/player/game_object/mgs_coop_four_players_game_obj.fpk'))
    for name in ('instanceSettings_partsTypeNames','instanceSettings_partsTypeInitial'):
        node = prop(controller_tree,cls,name)
        entries = node.findall('containerEntry')
        if node.get('arraySize') != '2' or len(entries) != 2 or any(e.text != 'PLTypeNormal' for e in entries):
            raise ValueError(f'Unreviewed {name} array')
        node.extend([deepcopy(entries[0]),deepcopy(entries[0])])
        node.set('arraySize','4')
        changes.append(dict(entity=cls,property=name,before=2,after=4))
    output = output.resolve()
    build = (ROOT / 'build').resolve()
    if not output.is_relative_to(build) or output == build:
        raise ValueError('Stage directory must be beneath project build directory')
    output.mkdir()  # Refuse to replace an existing experiment.
    files = []
    for filename, tree, internal in (
        ('four_players_game_obj.fox2.xml',game_tree,'/Assets/tpp/level_asset/chara/player/game_object/mgs_coop_four_players_game_obj.fox2'),
        ('four_player_controller.fox2.xml',controller_tree,'/Assets/tpp/level_asset/chara/player/game_object/mgs_coop_four_player_controller.fox2')):
        ET.indent(tree,space='  ')
        path=output/filename
        tree.write(path,encoding='utf-8',xml_declaration=False)
        files.append(dict(xml=filename,internal_path=internal,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    shutil.copyfile(ROOT / 'third_party/dynamite/LICENSE',output / 'DYNAMITE-LICENSE')
    manifest=dict(experiment='four_actor_allocation_candidate',players=4,files=files,changes=changes,
                  source_package_sha256=package['sha256'],dynamite=reference_manifest,
                  installed=False,runtime_verified=False,
                  limitations=['Instance memory budget scaling is an unverified hypothesis.',
                               'Additional parts, motion loading, engine session slots and mission integration remain unverified.',
                               'Binary round-trip validation does not establish live actor allocation.',
                               'Generated game-derived assets are local experimental data, not a release.'])
    (output/'stage.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return manifest


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extracted',type=Path,required=True)
    parser.add_argument('--upstream',type=Path,default=ROOT/'work/upstream-dynamite')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=stage(args.extracted,args.upstream,args.output)
    print(f"Staged {len(result['changes'])} allocation edits; not installed or runtime verified")

