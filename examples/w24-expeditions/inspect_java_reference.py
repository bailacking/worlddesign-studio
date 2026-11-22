"""Measure published Java 1.16.5 data without including game files in the output."""

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path
from zipfile import ZipFile

METADATA_SHA1 = "fba9f7833e858a1257d810d21a3a9e3c967f9077"
REGISTRIES = ("block", "item", "entity_type", "mob_effect", "villager_profession")
STRUCTURES = ("bastion", "end_city", "village")
OBSERVATIONS = {
    "bread_recipe": "recipes/bread.json",
    "coal_ore_loot": "loot_tables/blocks/coal_ore.json",
    "piglin_barter": "loot_tables/gameplay/piglin_bartering.json",
    "dragon_advancement": "advancements/end/kill_dragon.json",
    "end_city_advancement": "advancements/end/find_end_city.json",
    "raider_tag": "tags/entity_types/raiders.json",
}


def sha1(path):
    digest = hashlib.sha1()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def inspect(root):
    metadata = root / "1.16.5.json"
    if sha1(metadata) != METADATA_SHA1:
        raise ValueError("Mojang version metadata SHA-1 mismatch")
    downloads = read_json(metadata)["downloads"]
    versions = {}
    for role in ("client", "server"):
        path = root / f"{role}.jar"
        declared = downloads[role]
        digest = sha1(path)
        if digest != declared["sha1"] or path.stat().st_size != declared["size"]:
            raise ValueError(f"{role} length or SHA-1 mismatch")
        with ZipFile(path) as game:
            versions[role] = json.loads(game.read("version.json"))
            if role == "server":
                paths = set(game.namelist())
    if versions["client"] != versions["server"] or versions["client"]["id"] != "1.16.5":
        raise ValueError("Version metadata inside the client and server does not agree")

    reports = root / "reports" / "reports"
    blocks = read_json(reports / "blocks.json")
    registry = read_json(reports / "registries.json")
    structures = Counter(
        name.split("/")[3] for name in paths
        if name.startswith("data/minecraft/structures/") and name.endswith(".nbt")
        and name.split("/")[3] in STRUCTURES
    )
    return {
        "version": versions["server"]["id"],
        "world_version": versions["server"]["world_version"],
        "protocol": versions["server"]["protocol_version"],
        "sha1": {"metadata": METADATA_SHA1, **{k: downloads[k]["sha1"] for k in versions}},
        "registry_entries": {name: len(registry[f"minecraft:{name}"]["entries"])
                             for name in REGISTRIES},
        "structure_templates": {name: structures[name] for name in STRUCTURES},
        "nether_biome_reports": sorted(
            path.stem for path in (reports / "biomes").glob("*.json")
            if read_json(path).get("category") == "nether"
        ),
        "anchor_charge_states": sorted(
            {int(state["properties"]["charges"])
             for state in blocks["minecraft:respawn_anchor"]["states"]}
        ),
        "archive_indicators": {key: "data/minecraft/" + value in paths
                               for key, value in OBSERVATIONS.items()},
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="W2 reference folder outside the repository")
    args = parser.parse_args()
    try:
        report = inspect(args.root)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
