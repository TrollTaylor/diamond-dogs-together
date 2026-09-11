"""Compile a local allocation experiment and verify XML/package round trips.

Uses an explicitly supplied local datfpk tool. Outputs stay under the stage in
build; this tool has no install operation and never modifies game archives.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent


def signature(path):
    return [(e.tag, sorted(e.attrib.items()), (e.text or '').strip()) for e in ET.parse(path).iter()]


def compile_stage(stage, converter):
    stage, converter = stage.resolve(), converter.resolve()
    if not stage.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('Stage must be in project build directory')
    manifest=json.loads((stage/'stage.json').read_text())
    if manifest['experiment']!='four_actor_allocation_candidate' or manifest['players']!=4 or len(manifest['files'])!=2:
        raise ValueError('Unexpected allocation experiment')
    compiled=stage/'compiled'
    compiled.mkdir()
    def run(*args):
        subprocess.run([str(converter),*map(str,args)],check=True,capture_output=True,text=True,timeout=30)
    checks=[]
    for index,item in enumerate(manifest['files']):
        source=(stage/item['xml']).resolve()
        if source.parent!=stage or hashlib.sha256(source.read_bytes()).hexdigest()!=item['sha256']:
            raise ValueError('Changed or escaped stage source')
        internal=Path(item['internal_path'].lstrip('/'))
        destination=(compiled/'files'/internal).resolve()
        if not destination.is_relative_to(compiled/'files') or internal.suffix!='.fox2':
            raise ValueError('Unsafe internal filename')
        destination.parent.mkdir(parents=True,exist_ok=True)
        run(source,destination)
        roundtrip=compiled/f'roundtrip-{index}.xml'
        run(destination,roundtrip)
        if signature(source)!=signature(roundtrip):
            raise ValueError(f'FOX2 semantic round trip differs: {source.name}')
        name=('mgs_coop_four_players_game_obj','mgs_coop_four_player_controller')[index]
        definition=compiled/f'{name}.definition.json'
        definition.write_text(json.dumps(dict(type='fpkd',entries=[dict(filePath=item['internal_path'])])))
        packed=compiled/f'{name}.fpkd'
        run(definition,packed,compiled/'files')
        # datfpk v0.3.0 ignores a positional output directory for FPK input.
        # Use and verify its documented default sibling directory explicitly.
        unpacked=compiled/f'{name}_fpkd'
        run(packed)
        restored=unpacked/internal
        if restored.read_bytes()!=destination.read_bytes():
            raise ValueError('FPKD round trip changed embedded FOX2')
        pair_definition=compiled/f'{name}.pair-definition.json'
        pair_definition.write_text(json.dumps(dict(type='fpk',references=[dict(filePath='/Assets/tpp/pack/resident/resident00.fpk')]) if index==0 else dict(type='fpk')))
        pair=compiled/f'{name}.fpk'
        run(pair_definition,pair,compiled/'files')
        # Validate the paired container's own inventory, including references.
        run(pair)
        pair_inventory=json.loads(Path(str(pair)+'.json').read_text())
        expected_refs=[dict(filePath='/Assets/tpp/pack/resident/resident00.fpk')] if index==0 else []
        if pair_inventory.get('references',[])!=expected_refs or pair_inventory.get('entries',[]):
            raise ValueError('Paired FPK metadata differs')
        checks.append(dict(source_xml=item['xml'],internal_path=item['internal_path'],
                           semantic_xml_nodes=len(signature(source)),xml_roundtrip_equal=True,
                           embedded_bytes_equal=True,pair_references_equal=True,
                           fox2_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                           fpkd_sha256=hashlib.sha256(packed.read_bytes()).hexdigest(),
                           fpk_sha256=hashlib.sha256(pair.read_bytes()).hexdigest()))
    report=dict(converter_sha256=hashlib.sha256(converter.read_bytes()).hexdigest(),
                checks=checks,installed=False,runtime_allocation_verified=False,
                limitation='Compiled allocation candidates only; motion, engine sessions and mission integration are absent.')
    (compiled/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--converter',type=Path,required=True)
    args=parser.parse_args()
    result=compile_stage(args.stage,args.converter)
    print(f"{len(result['checks'])} definition/container pairs compiled and round-trip checked; not installed")
