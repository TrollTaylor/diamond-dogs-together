"""Stage pinned two-instance buddy allocation candidates in scratch, never install.

Project-authored transformation. Retail assets are supplied locally; no game or
upstream implementation is copied into source. The existing datfpk converter
(v0.3.0, unknown321/datfpk; based on Atvaark/FoxTool and GzsTool) handles its
formats. Successful round trips do not establish allocation or ride control.
"""
from __future__ import annotations
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

CONVERTER_SHA = "a1ec95c8adab11339b7a16ec2ebe7bd92a521bab428f9238feaa20da3bc64c1e"
SPECS = {
    "horse": dict(type="TppHorse2", package="/Assets/tpp/pack/buddy/horse/buddy_horse2_00.fpkd",
                  fox2="/Assets/tpp/level_asset/chara/buddy/horse/buddy_horse2_00.fox2",
                  package_sha="d3681d0ec777cf7bcc14a049b29a371f1974996be2a0137bac97547f403f7380",
                  xml_sha="29769ba8a4c233329f29d46d90af44998213c103c7d1b5a565164b7e5efb14fa"),
    "walker": dict(type="TppWalkerGear2", package="/Assets/tpp/pack/buddy/walkergear/buddy_wg2_00.fpkd",
                   fox2="/Assets/tpp/level_asset/chara/buddy/walkergear/buddy_wg2_00.fox2",
                   package_sha="77e966b61cf51bd4e3cc7e71637963706591602a529e0a59052cf6433721a5ee",
                   xml_sha="642c880f531e340eeeee1ff00079d48ba88e9bd4f73e1b2246d39391eee7a7a2"),
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def prop(entity, name):
    rows = entity.findall(f"./staticProperties/property[@name='{name}']")
    if len(rows) != 1:
        raise ValueError(f"Expected one {entity.get('class')}.{name}")
    return rows[0]

def scalar(entity, name):
    rows = prop(entity, name).findall('containerEntry')
    if len(rows) != 1:
        raise ValueError(f"Expected scalar {name}")
    return rows[0]

def signature(path):
    return [(e.tag, sorted(e.attrib.items()), (e.text or '').strip()) for e in ET.parse(path).iter()]

def validate_graph(tree):
    entities = tree.findall('./entities/entity')
    for key in ('addr', 'id'):
        values = [int(e.get(key), 0) for e in entities]
        if len(set(values)) != len(values):
            raise ValueError(f"Duplicate entity {key}")
    addresses = {e.get('addr') for e in entities}
    for e in entities:
        for p in e.findall('./staticProperties/property'):
            if p.get('type') not in ('EntityPtr', 'EntityHandle'):
                continue
            for c in p.findall('containerEntry'):
                text = c.find('data').text if c.find('data') is not None else c.text
                if text and int(text, 0) != 0 and text not in addresses:
                    raise ValueError(f"Dangling {e.get('class')}.{p.get('name')}")

def expand_xml(tree, typ):
    """Keep group/instance zero; author a second locator and shared capacity two."""
    validate_graph(tree)
    entities = tree.find('entities')
    by_addr = {e.get('addr'): e for e in entities}
    games = [e for e in entities if e.get('class') == 'GameObject' and scalar(e, 'typeName').text == typ]
    locators = [e for e in entities if e.get('class') == 'GameObjectLocator' and scalar(e, 'typeName').text == typ]
    if len(games) != 1 or len(locators) != 1:
        raise ValueError('Expected exactly one buddy group and one locator')
    game, loc = games[0], locators[0]
    for name in ('totalCount', 'realizedCount'):
        if scalar(game, name).text != '1':
            raise ValueError('Already modified or unexpected buddy capacity')
        scalar(game, name).text = '2'
    transform = by_addr[scalar(loc, 'transform').text]
    params = by_addr[scalar(loc, 'parameters').text]
    if transform.get('class') != 'TransformEntity' or params.get('class') != typ + 'LocatorParameter':
        raise ValueError('Unexpected locator graph')
    for node in (transform, params):
        if scalar(node, 'owner').text != loc.get('addr'):
            raise ValueError('Unexpected original locator ownership')
    first_addr = (max(int(v, 0) for v in by_addr) + 0x1000 + 0xF) & ~0xF
    first_id = max(int(e.get('id'), 0) for e in entities) + 1
    originals = (loc, transform, params)
    mapping = {e.get('addr'): f"0x{first_addr + i * 0x100:X}" for i, e in enumerate(originals)}
    copies = []
    for i, source in enumerate(originals):
        node = deepcopy(source)
        node.set('addr', mapping[source.get('addr')]); node.set('id', f"0x{first_id + i:X}")
        for p in node.findall('./staticProperties/property'):
            if p.get('type') in ('EntityPtr', 'EntityHandle'):
                for c in p.findall('containerEntry'):
                    if c.text in mapping:
                        c.text = mapping[c.text]
        copies.append(node)
    old_name = scalar(loc, 'name').text
    new_name = old_name + '_coop1'
    scalar(copies[0], 'name').text = new_name
    offset = scalar(copies[1], 'transform_translation')
    offset.set('x', format(float(offset.get('x')) + 4.0, 'g'))
    data_set = by_addr[scalar(loc, 'dataSet').text]
    listing = prop(data_set, 'dataList')
    if listing.get('container') != 'StringMap' or any(c.get('key') == new_name for c in listing):
        raise ValueError('Unexpected dataset locator map')
    entry = ET.SubElement(listing, 'containerEntry', key=new_name)
    ET.SubElement(entry, 'data').text = mapping[loc.get('addr')]
    listing.set('arraySize', str(len(listing)))
    entities.extend(copies)
    validate_graph(tree)
    return dict(type=typ, total=2, realized=2, locators=2, original_locator=old_name,
                second_locator=new_name, second_local_offset_m=[4, 0, 0],
                authored_group=scalar(game, 'groupId').text,
                runtime_index_mapping_verified=False)

def stage(extracted, output, scratch_root, converter, game_dir):
    extracted, output, scratch_root = extracted.resolve(), output.resolve(), scratch_root.resolve()
    converter, game_dir = converter.resolve(), game_dir.resolve()
    if output == scratch_root or not output.is_relative_to(scratch_root) or output.is_relative_to(game_dir):
        raise ValueError('Output must be a new directory beneath scratch root, outside the game')
    if sha(converter) != CONVERTER_SHA:
        raise ValueError('Unreviewed converter binary')
    inventory = json.loads((extracted / 'inventory.json').read_text())
    selected = []
    for name, spec in SPECS.items():
        packages = [p for p in inventory['packages'] if p['path'] == spec['package']]
        if len(packages) != 1:
            raise ValueError('Missing or ambiguous original buddy package')
        package = packages[0]
        package_file = (extracted / package['saved_as']).resolve()
        if package_file.parent != extracted or package['sha256'] != spec['package_sha'] or sha(package_file) != spec['package_sha']:
            raise ValueError('Unexpected original buddy package')
        rows = [e for e in package['entries'] if 'xml' in e and e['path'] == spec['fox2']]
        if len(rows) != 1:
            raise ValueError('Expected one buddy FOX2')
        item = rows[0]; source = (extracted / item['xml']).resolve()
        if source.parent != extracted or sha(source) != spec['xml_sha']:
            raise ValueError('Decoded buddy XML differs from inspected source')
        tree = ET.parse(source)
        change = expand_xml(tree, spec['type'])
        selected.append((name, package, package_file, item, tree, change))
    output.mkdir(parents=True, exist_ok=False)
    def run(*args):
        subprocess.run([str(converter), *map(str, args)], check=True, capture_output=True, text=True, timeout=30)
    checks = []
    for name, package, package_file, item, tree, change in selected:
        original = output / f'source-{name}.fpkd'
        shutil.copyfile(package_file, original); run(original)
        definition = Path(str(original) + '.json')
        files = output / f'source-{name}_fpkd'
        internal = Path(item['path'].lstrip('/'))
        destination = (files / internal).resolve()
        if not destination.is_relative_to(files) or destination.suffix != '.fox2':
            raise ValueError('Unsafe internal FOX2 path')
        # Hash every extracted entry before changing the sole buddy definition.
        before = {p.relative_to(files).as_posix(): sha(p) for p in files.rglob('*') if p.is_file()}
        xml = output / f'{name}-two.fox2.xml'; ET.indent(tree, space='  ')
        tree.write(xml, encoding='utf-8', xml_declaration=False)
        run(xml, destination)
        decoded = output / f'{name}-roundtrip.fox2.xml'; run(destination, decoded)
        if signature(xml) != signature(decoded):
            raise ValueError('FOX2 semantic round trip differs')
        packed = output / f'candidate-{name}.fpkd'; run(definition, packed, files); run(packed)
        restored = output / f'candidate-{name}_fpkd'
        after = {p.relative_to(restored).as_posix(): sha(p) for p in restored.rglob('*') if p.is_file()}
        expected = dict(before); expected[internal.as_posix()] = sha(destination)
        if after != expected:
            raise ValueError('FPKD changed other assets or failed to retain the authored FOX2')
        if json.loads(Path(str(packed)+'.json').read_text()) != json.loads(definition.read_text()):
            raise ValueError('FPKD inventory/reference metadata differs')
        checks.append(dict(name=name, package=package['path'], source_package_sha256=package['sha256'],
                           source_fox2_sha256=item['sha256'], xml_sha256=sha(xml), fox2_sha256=sha(destination),
                           fpkd_sha256=sha(packed), candidate=packed.name, change=change,
                           xml_roundtrip_equal=True, package_entries_equal=True, unchanged_assets=len(before)-1))
    report = dict(experiment='two_instance_buddy_capacity', converter_sha256=CONVERTER_SHA, checks=checks,
                  installed=False, runtime_allocation_verified=False, ride_attachment_verified=False,
                  limitations=['One global buddy controller remains unchanged.',
                               'Second locator allocation/index order and memory budget require hidden runtime observation.',
                               'Two simultaneous player-to-ride attachments and rider/ride replication are not implemented.',
                               'Only inspected variant00 packs are staged; other equipment variants are untouched.'])
    (output / 'verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--extracted', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--scratch-root', type=Path, default=Path(r'D:\mgs-coop-scratch'))
    p.add_argument('--converter', required=True, type=Path)
    p.add_argument('--game-dir', type=Path, default=Path(r'D:\Games\steamapps\common\MGS_TPP'))
    a = p.parse_args()
    r = stage(a.extracted, a.output, a.scratch_root, a.converter, a.game_dir)
    print(f"{len(r['checks'])} buddy capacity candidates compiled and round-trip verified; not installed")
