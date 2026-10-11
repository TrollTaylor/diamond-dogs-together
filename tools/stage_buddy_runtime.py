"""Create a scratch-only baseline21 QAR that overrides only horse 00 and walker wg2.

This is not the worker capacity pin. Start-WorkerSession and the native mount
refuse its 16,659,456-byte result. The pinned archive is the seven-horse
variants file recorded in native/include/mgscoop/buddy_capacity_catalog.hpp.

Original project tool. No build, install, game launch or baseline mutation.
Uses the existing local datfpk v0.3.0 format converter (unknown321/datfpk,
based on Atvaark FoxTool/GzsTool); its distribution rights remain unresolved.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

BASELINE_SHA = "87bbe82bb1e907cf405200155da2794bf7e1f5a3d45bf66426a38cdd31a91f96"
CONVERTER_SHA = "a1ec95c8adab11339b7a16ec2ebe7bd92a521bab428f9238feaa20da3bc64c1e"
PLAYER = "/Assets/tpp/pack/player/game_object/"
BASELINE_PATHS = [PLAYER + n + ext for n in (
    "mgs_coop_four_players_game_obj", "mgs_coop_four_player_controller",
    "mgs_coop_two_player_scarf_controller") for ext in (".fpk", ".fpkd")]
BASELINE_PATHS += ["/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua"]
BASELINE_PATHS += [f"/Assets/tpp/pack/mission2/free/f{n}/mgs_coop_f{n}{ext}"
                   for n in (30050, 30010, 30020) for ext in (".fpk", ".fpkd")]
BUDDIES = {
    "horse": ("/Assets/tpp/pack/buddy/horse/buddy_horse2_00.fpkd",
              "a6e55eb4db2a67e10a9b03a9ef956894e89280bc917f3fb237ef876db15d1699"),
    "walker": ("/Assets/tpp/pack/buddy/walkergear/buddy_wg2_00.fpkd",
               "40c73721b719b9f15790325ed5f37e83ded0c8f9c11d352805158a1e68b01652"),
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def check(path, expected):
    if not path.is_file() or sha(path) != expected:
        raise ValueError(f"Missing or changed pinned input: {path}")

def safe(root, internal):
    if not internal.startswith("/Assets/") or "\\" in internal:
        raise ValueError("Unexpected internal archive path")
    result = (root / internal.lstrip("/")).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError("Archive path escaped output")
    return result

def build(baseline, stage, converter, scratch, output, game):
    baseline, stage, converter, scratch, output, game = [x.resolve() for x in
        (baseline, stage, converter, scratch, output, game)]
    if not output.is_relative_to(scratch) or output == scratch or output.is_relative_to(game):
        raise ValueError("Output must be a new descendant of scratch, outside game")
    if output.exists():
        raise ValueError("Output must be new; no overwrite or merge")
    check(baseline, BASELINE_SHA); check(converter, CONVERTER_SHA)
    source_hash = sha(baseline)
    for name, (_, digest) in BUDDIES.items():
        check(stage / f"candidate-{name}.fpkd", digest)
    output.mkdir(parents=True)
    def run(*args):
        result = subprocess.run([str(converter), *map(str, args)], capture_output=True,
                                text=True, check=True, timeout=60)
        if "error" in result.stderr.lower() or "panic" in result.stderr.lower():
            raise ValueError(f"Converter error: {result.stderr[:500]}")
    paths = BASELINE_PATHS + [p for p, _ in BUDDIES.values()]
    dictionary = output / "paths.txt"
    dictionary.write_text("\n".join(sorted({p.rsplit(".", 1)[0] for p in paths})) + "\n")
    source = output / "source-baseline.dat"
    shutil.copyfile(baseline, source); check(source, BASELINE_SHA)
    files = output / "files"
    run(source, files, dictionary)
    source_meta = json.loads(Path(str(source) + ".json").read_text())
    source_paths = [e["filePath"] for e in source_meta["entries"]]
    if source_meta["version"] != 1 or source_meta["flags"] != 3150304 or len(source_paths) != 13 or set(source_paths) != set(BASELINE_PATHS):
        raise ValueError("Unexpected baseline QAR inventory")
    entries = [dict(path=p, sha256=sha(safe(files, p)), origin="baseline") for p in source_paths]
    for name, (path, digest) in BUDDIES.items():
        target = safe(files, path); target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(stage / f"candidate-{name}.fpkd", target); check(target, digest)
        entries.append(dict(path=path, sha256=digest, origin="buddy_capacity_two", kind=name,
                            total_count=2, realized_count=2, locators=2, variant=0))
    definition = output / "runtime-definition.json"
    definition.write_text(json.dumps(dict(type="qar", version=1, flags=3150304,
                                        entries=[dict(filePath=e["path"]) for e in entries])))
    archive = output / "mgs-coop-buddy-capacity-two.dat"
    run(definition, archive, files)
    restored = output / "archive-roundtrip"
    run(archive, restored, dictionary)
    meta = json.loads(Path(str(archive) + ".json").read_text())
    actual = [e["filePath"] for e in meta["entries"]]
    if meta["version"] != 1 or meta["flags"] != 3150304 or len(actual) != 15 or set(actual) != set(paths):
        raise ValueError("Unexpected candidate QAR inventory")
    for entry in entries:
        check(safe(restored, entry["path"]), entry["sha256"])
        entry["qar_roundtrip_equal"] = True
    check(baseline, source_hash)
    report = dict(schema=1, experiment="buddy_capacity_two", profile="buddy_capacity_variant00_rejected",
                  worker_pin=False,
                  baseline_sha256=source_hash, converter_sha256=CONVERTER_SHA,
                  archive=dict(path=archive.name, sha256=sha(archive), bytes=archive.stat().st_size,
                               version=1, flags=3150304, entries=15),
                  catalog=dict(player_motion_count=21, buddy_total_count=2,
                               buddy_kinds=["horse", "walker"], buddy_variants=[0]),
                  entries=entries, baseline_entries_preserved=13, installed=False,
                  live_mount_verified=False, runtime_index_mapping_verified=False,
                  ride_attachment_verified=False, production_pins_changed=False,
                  limits=["This helper still emits only horse 00 and walker wg2. The worker pin is the separate seven-horse variants archive, not this file.",
                          "Exact variant-00 overrides only; other variants unchanged.",
                          "No native controller or force-mount operation added.",
                          "Archive precedence and actual two-controller realization need private-worker proof.",
                          "Requires separate default-off background-only worker admission; normal worker refuses.",
                          "All game-derived bytes stay local; not a distribution payload."])
    (output / "catalog.json").write_text(json.dumps(report, indent=2) + "\n")
    return report

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "stage", "converter", "scratch", "output", "game"):
        p.add_argument("--" + name, type=Path, required=True)
    args = p.parse_args(); report = build(**vars(args))
    print(json.dumps(dict(archive=report["archive"], preserved=13, added=2, installed=False)))
