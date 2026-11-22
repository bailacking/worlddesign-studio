"""Generate two editable WDS expeditions and their original map palettes."""

import argparse
import colorsys
import json
import struct
import time
import zlib
from pathlib import Path


def site(key, kind, x, y, **fields):
    return {"id": key, "kind": kind, "at": [x, y], **fields}


def region(key, habitat, *sites):
    return {
        "id": key, "habitat": habitat, "width": 18, "height": 11,
        "spawn": [1, 1], "sites": list(sites),
    }


def stack(item, count=1):
    return {"item": item, "count": count}


def catalog(regions, recipe_inputs):
    outputs = set(recipe_inputs)
    raw = outputs | {item for values in recipe_inputs.values() for item in values}
    sources = {}
    for area in regions:
        for spot in area["sites"]:
            raw.update(spot[key] for key in ("item", "reward") if key in spot)
            if spot["kind"] == "source":
                sources[spot["id"]] = {"yield": stack(spot["reward"]), "uses": 12}
    return {
        "version": 1, "bag_slots": 32, "chest_slots": 1,
        "items": {key: {"tags": ["food"] if key == "ration" else [], "limit": 32}
                  for key in sorted(raw)},
        "recipes": {key: {"method": "craft", "output": stack(key),
                          "ingredients": [stack(value) for value in values]}
                    for key, values in recipe_inputs.items()},
        "sources": sources,
    }


def copper_regions():
    return [
        region("meadow", "surface",
               site("copper-vein", "source", 2, 1, reward="ore"),
               site("field-store", "source", 1, 2, reward="ration"),
               site("mason", "trade", 3, 1, item="ore", reward="bead"),
               site("keeper", "trade", 4, 1, item="bead", reward="feed"),
               site("pens", "breed", 5, 1, item="feed"),
               site("burrow", "tame", 6, 1, item="ration"),
               site("settlement-raid", "raid", 7, 1, power=5, reward="badge"),
               site("kiln-gate", "portal", 9, 1, item="spear", target="slag-field"),
               site("return", "chapter", 10, 1, item="spire-emblem", target="spire-found")),
        region("slag-field", "ember",
               site("ember-market", "trade", 2, 1, item="ore", reward="charge"),
               site("kiln-anchor", "anchor", 1, 2, item="charge"),
               site("fortress-ruin", "chapter", 3, 1, item="badge", target="departure"),
               site("cooling-vault", "rest", 3, 2, power=8),
               site("sky-gate", "portal", 5, 1, item="spear", target="cloud-ring"),
               site("meadow-gate", "portal", 1, 0, target="meadow")),
        region("cloud-ring", "sky",
               site("ring-warden", "boss", 2, 1, power=9, reward="spire-emblem"),
               site("spire-found", "chapter", 4, 1, item="spire-emblem", target="fortress-ruin"),
               site("city-cache", "source", 5, 1, reward="ration"),
               site("land-gate", "portal", 1, 0, item="spire-emblem", target="meadow")),
    ]


def drift_regions():
    return [
        region("reed-bank", "shore",
               site("driftwood", "source", 2, 1, reward="reed"),
               site("beach-food", "source", 1, 2, reward="ration"),
               site("camp", "rest", 2, 2, power=20),
               site("rising", "chapter", 3, 1, target="departure"),
               site("cave-mouth", "portal", 4, 1, target="blue-cave"),
               site("shipyard", "dock", 7, 1, item="skiff"),
               site("to-current", "portal", 8, 1, item="skiff", target="open-current"),
               site("island-signal", "portal", 10, 1, item="chart", target="heron-isle")),
        region("blue-cave", "cavern",
               site("cave-creature", "enemy", 2, 1, power=3, reward="spore"),
               site("to-vault", "portal", 3, 1, target="echo-vault"),
               site("reed-mouth", "portal", 1, 0, target="reed-bank")),
        region("echo-vault", "ruins",
               site("inscription", "chapter", 2, 1, target="rising", reward="chart"),
               site("spark", "source", 1, 2, reward="lifeline"),
               site("to-cave", "portal", 1, 0, target="blue-cave")),
        region("open-current", "ocean",
               site("current-raider", "enemy", 2, 1, power=5, reward="salvage"),
               site("repair-deck", "repair", 1, 2, item="reed", power=5),
               site("landfall", "portal", 4, 1, item="chart", target="shoal-isle")),
        region("shoal-isle", "shore",
               site("shoal-giant", "boss", 2, 1, power=6, reward="salt-brooch"),
               site("shoal-crossing", "portal", 4, 1, item="salt-brooch", target="heron-isle"),
               site("shoal-return", "portal", 1, 0, target="open-current")),
        region("heron-isle", "shore",
               site("storm-giant", "boss", 2, 1, power=9, reward="crest"),
               site("spring-haven", "rest", 1, 0, power=20),
               site("return", "chapter", 4, 1, target="inscription", item="crest",
                    reward="naturalist"),
               site("to-mainland", "portal", 1, 2, target="reed-bank")),
    ]


def rules(plan, seed, scale):
    if plan == "copper-relay":
        result = copper_regions()
        recipes = {"spear": ["ore"], "lantern": ["ore"]}
    else:
        result = drift_regions()
        recipes = {"spear": ["reed"], "lantern": ["reed"], "skiff": ["reed", "reed"]}
    for index, area in enumerate(result):
        if not scale:
            continue
        # Add optional, runtime-visible sites away from the acceptance route.
        for n in range(12 * scale):
            area["sites"].append(site(f"camp-{index}-{n}", "rest", 3 + n % 12,
                                      4 + n // 12, power=3 + n % 5))
    return {
        "version": 1, "view": "voxel" if plan == "copper-relay" else "planar",
        "seed": seed, "permanent": plan != "copper-relay",
        "season_ticks": 60, "initial_hunger": 20, "hunger_interval": 4,
        "regions": result, "workshop": catalog(result, recipes),
    }


HUES = {"surface": .31, "ember": .035, "sky": .58, "shore": .24, "cavern": .65,
        "ruins": .73, "ocean": .56}


def generate_palettes(data):
    for region_index, area in enumerate(data["regions"]):
        hue = HUES[area["habitat"]] + ((data["seed"] + region_index) % 11 - 5) * .002
        area["palette"] = [
            sum(round(channel * 255) << shift
                for channel, shift in zip(colorsys.hsv_to_rgb(hue, sat, value), (16, 8, 0)))
            for sat, value in ((.36, .43), (.39, .53), (.45, .59), (.26, .31))
        ]


GLYPHS = {
    "source": (0, 4, 14, 31, 14, 4, 0),
    "portal": (14, 17, 21, 17, 21, 17, 14),
    "enemy": (0, 17, 21, 31, 14, 10, 0),
    "boss": (21, 31, 31, 21, 31, 14, 4),
    "raid": (4, 14, 4, 31, 4, 10, 17),
    "trade": (4, 14, 21, 4, 21, 14, 4),
    "anchor": (4, 14, 4, 4, 21, 31, 14),
    "dock": (4, 6, 7, 4, 31, 14, 0),
    "repair": (0, 17, 10, 4, 10, 17, 0),
    "tame": (10, 0, 21, 0, 14, 31, 14),
    "breed": (0, 10, 31, 31, 14, 4, 0),
    "rest": (4, 14, 31, 17, 21, 21, 31),
    "chapter": (4, 6, 7, 6, 4, 4, 14),
}


def png_chunk(key, data):
    return struct.pack(">I", len(data)) + key + data + struct.pack(">I", zlib.crc32(key + data))


def icon_png(glyph):
    # Transparent square with 3 px cells and a 3 px border on either side.
    width = 21
    pixels = bytearray()
    for bits in glyph:
        row = bytearray((0, 0, 0, 0) * 3)
        for bit in range(4, -1, -1):
            alpha = 255 if bits & (1 << bit) else 0
            row.extend(bytes((255, 255, 255, alpha)) * 3)
        row.extend(bytes(12))
        pixels.extend((b"\x00" + row) * 3)
    header = struct.pack(">IIBBBBB", width, width, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
            + png_chunk(b"IDAT", zlib.compress(pixels)) + png_chunk(b"IEND", b""))


def create_icons(data):
    kinds = {spot["kind"] for area in data["regions"] for spot in area["sites"]}
    return {kind: icon_png(GLYPHS[kind]) for kind in sorted(kinds)}


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def resource(key, kind, filename, refs=()):
    return {"id": key, "kind": kind, "path": f"assets/{filename}", "refs": list(refs)}


def write_project(path, plan, data, icons):
    assets = path / "assets"
    assets.mkdir(parents=True)
    data["icons"] = {kind: f"icon-{kind}" for kind in icons}
    resources = [resource("entry", "scene", "entry.json", ["expedition"]),
                 resource("expedition", "data", "expedition.json", data["icons"].values())]
    for kind, png in icons.items():
        asset = data["icons"][kind]
        (assets / f"{asset}.png").write_bytes(png)
        resources.append(resource(asset, "image", f"{asset}.png"))
    scene = {"format_version": 1, "entities": []}
    if data["view"] == "voxel":
        scene = {"format_version": 1, "kind": "voxel", "materials": "palette",
                 "terrain": {"seed": data["seed"], "profile": "frontier-v1"}}
        resources[0]["refs"].append("palette")
        resources.append(resource("palette", "text", "palette.txt"))
        (assets / "palette.txt").write_text(
            "wds-sandbox-content-v1\nsky aed2ea\nplace stone\n"
            "grass 739952 breakable\ndirt 9b754f breakable\nstone 828b91 breakable\n",
            encoding="utf-8",
        )
    write_json(assets / "entry.json", scene)
    write_json(assets / "expedition.json", data)
    write_json(path / "wds.project.json", {"format_version": 1, "project_id": plan,
               "revision": 0, "entry_scene": "entry", "resources": resources})


def generate(root, seed=73021, scale=0):
    root.mkdir(parents=True)
    start = time.perf_counter()
    plans = [(plan, rules(plan, seed, scale)) for plan in ("copper-relay", "drift-naturalists")]
    expanded = time.perf_counter()
    for _, data in plans:
        generate_palettes(data)
    colored = time.perf_counter()
    icons = [create_icons(data) for _, data in plans]
    illustrated = time.perf_counter()
    for (plan, data), batch in zip(plans, icons):
        write_project(root / plan, plan, data, batch)
    written = time.perf_counter()
    return {
        "expand_ms": (expanded - start) * 1000,
        "palette_ms": (colored - expanded) * 1000,
        "icon_ms": (illustrated - colored) * 1000,
        "write_ms": (written - illustrated) * 1000,
        "icons": sum(len(batch) for batch in icons),
        "scenes": len(plans),
        "regions": sum(len(data["regions"]) for _, data in plans),
        "sites": sum(len(area["sites"]) for _, data in plans for area in data["regions"]),
        "palette_colors": sum(len(area["palette"]) for _, data in plans for area in data["regions"]),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--seed", type=int, default=73021)
    parser.add_argument("--scale", type=int, default=0, choices=range(5))
    args = parser.parse_args()
    if not 0 <= args.seed < 2**32:
        parser.error("--seed must be an unsigned 32-bit integer")
    print(json.dumps(generate(args.output, args.seed, args.scale), indent=2))


if __name__ == "__main__":
    main()
