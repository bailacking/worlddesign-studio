"""Run the W25 circuit and garden through the normal project request entry point."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

SAMPLE = Path(__file__).resolve().parent


def request(cli, project, operation, inputs, expected="ok", *, revision=0, mode="read"):
    data = {
        "version": 1, "mode": mode, "operation": operation,
        "input": inputs,
    }
    if operation != "circuit.replay":
        data["expected_revision"] = revision
    access = ["--allow-project-write"] if mode == "apply" else []
    result = subprocess.run([cli, "project", "request", project, *access],
                            input=json.dumps(data), encoding="utf-8", capture_output=True,
                            timeout=20, check=False)
    assert result.returncode == (0 if expected == "ok" else 1), result
    reply = json.loads(result.stdout)
    assert reply["code"] == expected, reply
    return reply


def circuit(cli, project, checkpoint_path):
    nodes = {
        "rear": {"kind": "lever", "inputs": []},
        "side": {"kind": "lever", "inputs": []},
        "gate": {"kind": "comparator", "inputs": ["rear", "side"], "mode": "compare"},
        "difference": {"kind": "comparator", "inputs": ["rear", "side"], "mode": "subtract"},
        "delay": {"kind": "repeater", "inputs": ["difference"], "delay": 3},
        "lamp": {"kind": "lamp", "inputs": ["delay"]},
    }
    events = [
        {"tick": 0, "id": "rear", "level": 12},
        {"tick": 0, "id": "side", "level": 7},
        {"tick": 4, "id": "side", "level": 12},
        {"tick": 6, "id": "side", "level": 15},
    ]

    def replay(ticks, changes, **extra):
        return request(cli, project, "circuit.replay", {
            "nodes": nodes, "ticks": ticks, "events": changes, **extra,
        })

    complete = replay(10, events)
    first = replay(2, events[:2], checkpoint=True)
    assert first["checkpoint"]["nodes"]["delay"]["pending"] == {"level": 15, "due": 4}
    checkpoint_path.write_text(json.dumps(first["checkpoint"]), encoding="utf-8")
    saved = json.loads(checkpoint_path.read_text(encoding="utf-8"))
    rest = replay(8, [{**change, "tick": change["tick"] - 2} for change in events[2:]], resume=saved)
    assert first["trace"] + rest["trace"] == complete["trace"]
    assert rest["levels"] == complete["levels"]
    assert [(row["tick"], row["to"]) for row in complete["trace"]
            if row["phase"] == "update" and row["id"] == "lamp"] == [(5, 15), (9, 0)]
    assert [(row["tick"], row["to"]) for row in complete["trace"]
            if row["phase"] == "update" and row["id"] == "gate"] == [(0, 12), (6, 0)]
    assert [(row["tick"], row["to"]) for row in complete["trace"]
            if row["phase"] == "update" and row["id"] == "difference"] == [(0, 5), (4, 0)]
    saved["nodes"]["delay"]["pending"]["due"] = 1
    error = request(cli, project, "circuit.replay",
                    {"nodes": nodes, "ticks": 1, "events": [], "resume": saved}, "invalid_circuit")
    assert error["location"] == "/input/resume/nodes/delay/pending/due"
    print("Analog threshold and subtraction, checkpoint replay, malformed pending rejection")


def workyard(cli, project, destination):
    shutil.copytree(project, destination)
    source = destination / "assets" / "yard.json"
    original = source.read_bytes()
    rail = {"x": 13, "y": 2, "z": 17, "dx": 1, "dz": 0, "steps": 12}
    route = request(cli, destination, "scene.voxel.cart", rail)
    assert route["positions"] == [[x, 2, 17] for x in range(13, 19)] and route["blocked"]
    reverse = request(cli, destination, "scene.voxel.cart", {**rail, "x": 18, "dx": -1, "steps": 3})
    assert reverse["positions"] == [[x, 2, 17] for x in (18, 17, 16, 15)]
    assert not reverse["blocked"]
    request(cli, destination, "scene.voxel.cart", {**rail, "x": 19}, "invalid_track")
    request(cli, destination, "scene.voxel.cart", {**rail, "dz": 1}, "invalid_argument")

    area = {"x": 13, "z": 12, "width": 6, "depth": 3, "steps": 1}

    def dynamics(values, expected="ok", **options):
        return request(cli, destination, "scene.voxel.dynamics", values, expected, **options)

    def world():
        return set(json.loads(source.read_text(encoding="utf-8"))["world"].splitlines()[1:])

    preview = dynamics(area, mode="preview")
    assert preview["changed"] and source.read_bytes() == original
    applied = dynamics(area, mode="apply")
    assert applied["revision"] == 1
    for field, expected in {"water_updates": 3, "ignited": 1, "extinguished": 1}.items():
        assert applied[field] == preview[field] == expected
    cells = world()
    assert {"15 4 13 11", "16 4 14 10", "17 4 12 12", "17 4 14 6"} <= cells
    assert {"16 4 12 12", "14 4 13 10", "15 4 12 10", "18 4 12 12"}.isdisjoint(cells)
    reopened = request(cli, destination, "scene.inspect", {}, revision=1)
    assert reopened["terrain"]["profile"] == "workyard-v1"
    second = dynamics(area, revision=1, mode="apply")
    assert second["revision"] == 2 and second["ignited"] == 1
    cells = world()
    assert {"16 4 13 10", "18 4 12 12", "16 3 14 10", "17 4 14 6"} <= cells
    assert "17 4 12 12" not in cells
    saved = source.read_bytes()
    dynamics(area, "revision_conflict", revision=1, mode="apply")
    dynamics({**area, "x": 2147483647}, "invalid_argument", revision=2, mode="apply")
    stationary = dynamics({**area, "z": 17, "depth": 1}, revision=2, mode="apply")
    assert not stationary["changed"] and stationary["revision"] == 2
    assert source.read_bytes() == saved

    joint = destination.with_name("workyard-joint")
    shutil.copytree(project, joint)
    combined = request(cli, joint, "scene.voxel.dynamics", {**area, "steps": 2}, mode="apply")
    assert combined["revision"] == 1
    assert (joint / "assets" / "yard.json").read_bytes() == saved
    request(cli, SAMPLE.parent / "seeded-voxel", "scene.voxel.dynamics", area,
            "unsupported_terrain", mode="preview")
    print("Workyard cart crosses chunks, water quenches fire, replay and save agree")


def action(command, target=None, ingredients=None):
    data = {"command": command}
    if target:
        data["target"] = target
    if ingredients:
        data["ingredients"] = ingredients
    return data


def actuate(cli, project):
    assets = project / "assets"
    assets.mkdir(parents=True)
    manifest = {
        "format_version": 1, "project_id": "actuator-bench", "revision": 0,
        "entry_scene": "test", "resources": [
            {"id": "test", "kind": "scene", "path": "assets/test.json", "refs": ["materials"]},
            {"id": "materials", "kind": "text", "path": "assets/materials.txt", "refs": []},
        ],
    }
    source = assets / "test.json"
    document = {
        "format_version": 1, "kind": "voxel", "materials": "materials",
        "terrain": {"seed": 53207},
        "world": "wds-voxel-seeded-v1\n15 11 1 3\n",
    }
    (project / "wds.project.json").write_text(json.dumps(manifest), encoding="utf-8")
    shutil.copy2(SAMPLE.parent / "seeded-voxel" / "assets" / "slope-colors.txt",
                 assets / "materials.txt")
    source.write_text(json.dumps(document), encoding="utf-8")
    original = source.read_bytes()
    values = {
        "x": 15, "y": 11, "z": 1, "dx": 1, "dz": 0, "trigger": "relay",
        "circuit": {"ticks": 4, "nodes": {
            "switch": {"kind": "lever", "inputs": []},
            "relay": {"kind": "repeater", "inputs": ["switch"], "delay": 2},
        }, "events": []},
    }

    def operation(expected="ok", **options):
        return request(cli, project, "scene.voxel.actuate", values, expected, **options)

    def sky(x, tick, revision):
        return request(cli, project, "scene.landscape.environment",
                       {"x": x, "y": 10, "z": 1, "tick": tick}, revision=revision)

    left, right = sky(15, 90, 0), sky(16, 90, 0)
    assert left["weather"] == right["weather"]
    assert not left["open_sky"] and left["vertical_light"] == 0
    assert right["open_sky"] and right["vertical_light"] > 1
    assert sky(16, 230, 0)["vertical_light"] == 1
    assert not operation(mode="preview")["changed"]
    values["circuit"]["events"].append({"tick": 0, "id": "switch", "level": 15})
    preview = operation(mode="preview")
    assert preview["tick"] == 2 and preview["to"] == [16, 11, 1] and preview["changed"]
    assert source.read_bytes() == original

    blocked = {**document, "world": document["world"] + "16 11 1 3\n"}
    source.write_text(json.dumps(blocked), encoding="utf-8")
    blocked_bytes = source.read_bytes()
    operation("target_occupied", mode="apply")
    assert source.read_bytes() == blocked_bytes

    source.write_bytes(original)
    moved = operation(mode="apply")
    assert moved["tick"] == preview["tick"] and moved["revision"] == 1
    assert json.loads(source.read_text(encoding="utf-8"))["world"] == (
        "wds-voxel-seeded-v1\n16 11 1 3\n"
    )
    left_next, right_next = sky(15, 90, 1), sky(16, 90, 1)
    assert left_next["weather"] == right_next["weather"] == right["weather"]
    assert left_next["open_sky"] and left_next["vertical_light"] == right["vertical_light"]
    assert not right_next["open_sky"] and right_next["vertical_light"] == 0
    operation("revision_conflict", mode="apply")
    operation("immovable", mode="apply", revision=1)
    checked = request(cli, project, "scene.inspect", {}, revision=1)
    assert checked["revision"] == 1 and checked["kind"] == "voxel"

    # Place a three-block chain across the chunk seam in a separate circuit bench.
    chain = project.with_name("chain-bench")
    shutil.copytree(project, chain)
    (chain / "wds.project.json").write_text(json.dumps(manifest), encoding="utf-8")
    chain_source = chain / "assets" / "test.json"
    chain_source.write_text(json.dumps({
        **document, "world": document["world"] + "16 11 1 1\n17 11 1 2\n",
    }), encoding="utf-8")
    before = chain_source.read_bytes()
    nodes = {name: {"kind": "lever", "inputs": []} for name in ("clock", "enable")}
    events = [
        {"tick": 0, "id": "clock", "level": 15},
        {"tick": 1, "id": "clock", "level": 0},
        {"tick": 1, "id": "enable", "level": 15},
        {"tick": 2, "id": "clock", "level": 15},
        {"tick": 2, "id": "enable", "level": 0},
        {"tick": 3, "id": "clock", "level": 0},
        {"tick": 4, "id": "clock", "level": 15},
        {"tick": 4, "id": "enable", "level": 15},
    ]
    chain_input = {
        **values, "capacity": 3, "trigger": "clock", "requires_high": "enable",
        "circuit": {"ticks": 5, "nodes": nodes, "events": events},
    }

    def push(**options):
        return request(cli, chain, "scene.voxel.actuate", options.pop("inputs", chain_input),
                       **options)

    dry = push(mode="preview")
    assert dry["tick"] == 4 and dry["moved"] == 3 and dry["changed"]
    push(inputs={**chain_input, "capacity": 2}, expected="push_limit", mode="apply")
    assert chain_source.read_bytes() == before
    checkpoint = request(cli, chain, "circuit.replay", {
        "ticks": 1, "nodes": nodes, "events": [{"tick": 0, "id": "enable", "level": 15}],
        "checkpoint": True,
    })["checkpoint"]
    resumed = {**chain_input, "circuit": {
        "ticks": 1, "nodes": nodes, "events": [events[0]], "resume": checkpoint,
    }}
    assert push(inputs=resumed, mode="preview")["tick"] == 1
    result = push(mode="apply")
    assert result["tick"] == dry["tick"] and result["moved"] == 3 and result["revision"] == 1
    assert json.loads(chain_source.read_text(encoding="utf-8"))["world"] == (
        "wds-voxel-seeded-v1\n16 11 1 3\n17 11 1 1\n18 11 1 2\n"
    )
    print("Gated piston pushes a contiguous chain across chunks and rejects over-capacity moves")


def garden(cli, project, probe, save):
    def replay(actions):
        return request(cli, project, "expedition.replay", {"actions": actions})["steps"]

    setup = [
        action("gather", "seed-mat"), action("gather", "herbs"), action("gather", "bark-store"),
        action("craft", "build-stove"), action("east"), action("use", "plot"),
    ]
    steps = replay([
        *setup, action("use", "plot"), action("use", "trail"), action("wait"), action("wait"),
        action("use", "return-path"), action("use", "plot"),
        action("cook", "kitchen", ["root", "herb"]), action("east"),
        action("cook", "kitchen", ["root", "herb", "bark"]),
        action("cook", "kitchen", ["herb"]),
        action("cook", "kitchen", ["herb", "root"]),
    ])
    failures = {6: "crop_growing", 12: "out_of_reach",
                14: "missing_ingredients", 15: "no_matching_recipe"}
    for index, step in enumerate(steps):
        assert step["code"] == failures.get(index, "ok"), step
        if index in failures:
            assert {key: value for key, value in step.items() if key != "code"} == {
                key: value for key, value in steps[index - 1].items() if key != "code"
            }
    assert steps[7]["region"] == "tide-terrace" and steps[7]["crops"] == {"plot": 2}
    assert steps[9]["crops"] == {"plot": 0}
    assert steps[-1]["crops"] == {"plot": -1}
    assert {slot["item"] for slot in steps[-1]["workshop"]["bag"]} == {"stove", "stew"}
    assert any(receipt["cause"] == "cooked" for receipt in steps[-1]["receipts"])

    warm = replay([*([action("wait")] * 16), *setup, action("wait"), action("wait")])
    assert warm[-1]["season"] == 3 and warm[-1]["crops"] == {"plot": 4}, warm[-1]
    assert warm[-1]["heat"] > 0

    aged = replay([
        action("gather", "herbs"), action("wait"), action("gather", "herbs"),
        *([action("wait")] * 13),
    ])
    assert aged[2]["workshop"]["bag"] == [
        {"item": "herb", "count": 1, "best_before": 16},
        {"item": "herb", "count": 1, "best_before": 18},
    ]
    assert aged[-1]["workshop"]["bag"] == [
        {"item": "mulch", "count": 1},
        {"item": "herb", "count": 1, "best_before": 18},
    ]
    assert aged[-1]["receipts"][0]["cause"] == "spoilage"
    for stage in ("farm-start", "farm-cook", "farm-verify"):
        subprocess.run([probe, project, save, stage], timeout=20, check=True)
    print("Plant, traverse regions, resume, harvest, prioritize cooking, save and reopen")


def conditions(cli, project, destination):
    shutil.copytree(project, destination)
    path = destination / "assets" / "expedition.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    area, coast = document["regions"]
    area["sites"].append({"id": "remedy-supply", "kind": "source",
                          "at": [0, 1], "reward": "tonic"})
    coast["sites"].append({"id": "stinger", "kind": "enemy",
                           "at": [2, 1], "power": 3, "venom": 4})
    coast["climate"] = {
        "wetness": [1, 3, 2, -2], "heat": [1, -1, 3, -2],
        "eruption": {"interval": 6, "warning_ticks": 2, "season": 0, "damage": 3},
    }
    document["workshop"]["items"]["tonic"] = {"limit": 8, "tags": ["antidote"]}
    document["workshop"]["sources"]["remedy-supply"] = {
        "yield": {"item": "tonic", "count": 1}, "uses": 1,
    }
    path.write_text(json.dumps(document), encoding="utf-8")

    actions = [
        action("gather", "remedy-supply"), action("east"), action("use", "trail"),
        action("attack", "stinger"), action("treat", "herb"), action("treat", "tonic"),
        action("attack", "stinger"), action("use", "return-path"), action("wait"),
    ]
    request_data = {"actions": actions}
    steps = request(cli, destination, "expedition.replay", request_data)["steps"]
    assert steps == request(cli, destination, "expedition.replay", request_data)["steps"]

    def causes(row):
        return [event["cause"] for event in row["receipts"]]

    assert all(row["code"] == "ok" for index, row in enumerate(steps) if index != 4)
    assert steps[3]["poison"] == 3 and steps[3]["health"] == 17
    assert causes(steps[3]) == ["retaliation", "poisoned", "eruption_warning", "poison"]
    assert steps[4]["code"] == "missing_antidote" and steps[4]["tick"] == 4
    assert steps[4]["workshop"] == steps[3]["workshop"] and steps[4]["poison"] == 3
    assert causes(steps[5]) == ["treated"] and "poison" not in steps[5]
    assert not steps[5]["workshop"]["bag"]
    assert causes(steps[6]) == ["retaliation", "poisoned", "eruption", "poison"]
    assert steps[6]["health"] == 11 and steps[6]["heat"] == steps[6]["wetness"] == 4
    assert causes(steps[7]) == ["arrival", "poison"] and steps[7]["region"] == "wind-garden"
    assert steps[7]["poison"] == 2 and steps[7]["wetness"] == 3

    original = path.read_bytes()
    coast["climate"]["heat"][2] = 90
    path.write_text(json.dumps(document), encoding="utf-8")
    error = request(cli, destination, "expedition.replay", request_data, "invalid_expedition")
    assert error["location"].endswith("/regions/1/climate/heat/2")
    path.write_bytes(original)
    print("Seeded eruption warning and damage, regional climate, poison across travel and treatment")


def processing(cli, project, destination):
    shutil.copytree(project, destination)
    path = destination / "assets" / "expedition.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    coast = document["regions"][1]
    coast["sites"].append({"id": "sentinel", "kind": "enemy",
                           "at": [2, 1], "power": 7, "reward": "bark"})
    inventory = document["workshop"]
    inventory["items"]["vial"] = {"limit": 8, "tags": []}
    inventory["items"]["spear"] = {"limit": 1, "tags": [], "durability": 2}
    inventory["recipes"]["distil-gel"] = {
        "method": "brew", "station": "hearth", "unlocks": "refine",
        "output": {"item": "vial", "count": 1},
        "ingredients": [{"item": "herb", "count": 1}],
    }
    inventory["recipes"]["hone-staff"] = {
        "method": "upgrade", "station": "refine",
        "output": {"item": "spear", "count": 1},
        "ingredients": [{"item": "vial", "count": 1}],
    }
    path.write_text(json.dumps(document), encoding="utf-8")
    actions = [
        action("craft", "hone-staff"), action("gather", "bark-store"), action("gather", "herbs"),
        action("craft", "build-stove"), action("craft", "distil-gel"), action("craft", "hone-staff"),
        action("attack", "sentinel"), action("east"), action("use", "trail"),
        *([action("attack", "sentinel")] * 4),
    ]
    steps = request(cli, destination, "expedition.replay", {"actions": actions})["steps"]
    failures = {0: "station_locked", 6: "out_of_reach", 12: "no_target"}
    for index, row in enumerate(steps):
        assert row["code"] == failures.get(index, "ok"), (index, row)
    assert steps[0]["tick"] == 0 and not steps[0]["workshop"]["bag"]
    assert steps[4]["workshop"]["unlocked"] == ["hearth", "refine"]
    for index, uses in ((5, 2), (6, 2), (8, 2), (9, 1)):
        spear = next(slot for slot in steps[index]["workshop"]["bag"] if slot["item"] == "spear")
        assert spear["uses_remaining"] == uses, steps[index]
    assert not any(slot["item"] == "spear" for slot in steps[10]["workshop"]["bag"])
    assert steps[10]["receipts"][-1]["cause"] == "tool_broken"
    assert steps[10]["encounters"]["sentinel"] == 1 and steps[11]["encounters"]["sentinel"] == 0
    assert steps[12]["tick"] == steps[11]["tick"] == 10
    print("Two processing stages and unlocks, durability across travel, breakage and fallback")


def inscriptions(cli, project, destination):
    shutil.copytree(project, destination)
    path = destination / "assets" / "expedition.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    sentinel = next(site for site in document["regions"][1]["sites"] if site["id"] == "sentinel")
    sentinel["power"] = 9
    workshop = document["workshop"]
    workshop["items"]["reef-lance"] = {
        "limit": 1, "tags": [], "durability": 2,
        "inscription": {"force_bonus": 1, "guard_bonus": 1},
    }
    workshop["recipes"]["inscribe-lance"] = {
        "method": "inscribe", "station": "refine",
        "ingredients": [{"item": "spear", "count": 1}],
        "output": {"item": "reef-lance", "count": 1},
    }
    path.write_text(json.dumps(document), encoding="utf-8")
    commands = [
        action("craft", "inscribe-lance"), action("gather", "bark-store"),
        action("gather", "herbs"), action("craft", "build-stove"),
        action("craft", "distil-gel"), action("craft", "hone-staff"),
        action("craft", "inscribe-lance"), action("east"), action("use", "trail"),
        *([action("attack", "sentinel")] * 3),
    ]
    steps = request(cli, destination, "expedition.replay", {"actions": commands})["steps"]
    assert steps[0]["code"] == "station_locked" and all(s["code"] == "ok" for s in steps[1:])
    held = steps[8]["workshop"]["bag"]
    assert any(slot["item"] == "reef-lance" and slot["uses_remaining"] == 2 for slot in held)
    assert not any(slot["item"] == "spear" for slot in held)
    strength = [step["encounters"]["sentinel"] for step in steps[8:]]
    assert strength == [9, 5, 1, 0], strength
    retaliation = next(row for row in steps[9]["receipts"] if row["cause"] == "retaliation")
    assert retaliation["health"] == steps[8]["health"] - 1
    assert any(row["cause"] == "tool_broken" for row in steps[10]["receipts"])
    assert not any(slot["item"] == "reef-lance" for slot in steps[10]["workshop"]["bag"])
    print("Inscribed tool adds force and guard, keeps durability across travel, and breaks")


def story(cli, project, destination):
    shutil.copytree(project, destination)
    path = destination / "assets" / "expedition.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    area, coast = document["regions"]
    ending = {"id": "return", "kind": "chapter", "at": [3, 0],
              "after_any": ["cartographer", "shorewatch"]}
    area["sites"].extend([
        {"id": "briefing", "kind": "chapter", "at": [0, 1], "target": "departure"},
        {"id": "cartographer", "kind": "chapter", "at": [2, 2], "target": "briefing",
         "reward": "bark", "unlock_character": "wayfinder"},
        ending,
    ])
    coast["sites"].append({
        "id": "shorewatch", "kind": "chapter", "at": [2, 1], "target": "briefing",
        "unlock_character": "shorekeeper",
    })
    path.write_text(json.dumps(document), encoding="utf-8")

    def route(actions):
        return request(cli, destination, "expedition.replay", {"actions": actions})["steps"]

    denied = route([action("east"), action("north"), action("use", "return")])
    assert denied[-1]["code"] == "chapter_locked" and denied[-1]["tick"] == 2
    start = [action("use", "briefing"), action("east")]
    finish = [action("north"), action("use", "return")]
    left = route([*start, action("use", "cartographer"), action("use", "cartographer"), *finish])
    assert left[3]["code"] == "chapter_complete" and left[3]["tick"] == left[2]["tick"]
    assert left[3]["workshop"]["bag"] == left[2]["workshop"]["bag"] == [{"item": "bark", "count": 1}]
    assert left[-1]["ending"] and left[-1]["unlocked_characters"] == ["wayfinder"]
    right = route([*start, action("use", "trail"), action("use", "shorewatch"),
                   action("use", "return-path"), *finish])
    assert all(row["code"] == "ok" for row in right)
    assert right[-1]["ending"] and right[-1]["unlocked_characters"] == ["shorekeeper"]
    assert right[-1]["completed_chapters"] == ["briefing", "departure", "return", "shorewatch"]
    original = path.read_bytes()
    ending["after_any"] = ["briefing"]
    area["sites"][-3]["target"] = "return"
    path.write_text(json.dumps(document), encoding="utf-8")
    invalid = request(cli, destination, "expedition.replay", {"actions": []}, "invalid_expedition")
    assert invalid["location"].endswith("/chapters/briefing"), invalid
    path.write_bytes(original)
    print("Alternative chapters, one-time rewards, character unlocks and circular route rejection")


def combined(cli, project, variants, destination):
    shutil.copytree(project, destination)
    path = destination / "assets" / "expedition.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    for variant in variants:
        changed = json.loads((variant / "assets" / "expedition.json").read_text(encoding="utf-8"))
        for base, extra in zip(document["regions"], changed["regions"], strict=True):
            known = {site["id"] for site in base["sites"]}
            base["sites"].extend(site for site in extra["sites"] if site["id"] not in known)
        for group in ("items", "recipes", "sources"):
            document["workshop"][group].update(changed["workshop"][group])
    garden, island = document["regions"]
    for spot in garden["sites"]:
        if spot["id"] == "briefing":
            spot["at"] = [2, 2]
        if spot["id"] == "cartographer":
            spot["at"] = [3, 2]
    island["sites"] = [site for site in island["sites"] if site["id"] != "stinger"]
    for spot in island["sites"]:
        if spot["id"] == "sentinel":
            spot["venom"] = 4
        if spot["id"] == "shorewatch":
            spot["at"] = [2, 2]
    island["climate"] = {
        "wetness": [-1, 1, -1, -1], "heat": [0, 0, 0, 0],
        "eruption": {"interval": 4, "warning_ticks": 1, "season": 1, "damage": 2},
    }
    actions = [
        action("gather", "seed-mat"), action("gather", "herbs"), action("gather", "herbs"),
        action("gather", "bark-store"), action("gather", "remedy-supply"),
        action("craft", "build-stove"), action("craft", "distil-gel"), action("craft", "hone-staff"),
        action("east"), action("use", "briefing"), action("use", "plot"),
        action("use", "trail"), action("attack", "sentinel"), action("treat", "tonic"),
        action("attack", "sentinel"), action("attack", "sentinel"), action("east"),
        action("use", "shorewatch"), action("north"), action("use", "return-path"),
        action("gather", "herbs"), action("use", "plot"), action("east"),
        action("cook", "kitchen", ["root", "herb"]), action("north"), action("use", "return"),
    ]
    inputs = {"actions": actions}
    eruption_ticks = []
    for seed in (53207, 53208):
        document["seed"] = seed
        path.write_text(json.dumps(document), encoding="utf-8")
        result = request(cli, destination, "expedition.replay", inputs)
        assert all(row["code"] == "ok" for row in result["steps"]), [
            (index, row["code"]) for index, row in enumerate(result["steps"]) if row["code"] != "ok"
        ]
        assert len(result["steps"]) == 26
        assert result["state"]["ending"] and result["state"]["unlocked_characters"] == ["shorekeeper"]
        assert any(slot["item"] == "stew" for slot in result["state"]["workshop"]["bag"])
        causes = {row["cause"] for step in result["steps"] for row in step["receipts"]}
        assert {"plant", "eruption", "poison", "treated", "tool_broken", "spoilage", "harvest",
                "cooked", "ending"} <= causes
        eruption_ticks.append([step["tick"] for step in result["steps"]
                               if any(row["cause"] == "eruption" for row in step["receipts"])])
    assert eruption_ticks == [[12], [15]]
    print("Integrated 26-action route on two seeds, with different eruption times")


def controls(cli, project, destination):
    shutil.copytree(project, destination)
    path = destination / "assets" / "expedition.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    first = {"id": "high", "kind": "chapter", "at": [0, 1], "interaction_priority": 10}
    document["regions"][0]["sites"].extend([
        first, {"id": "near", "kind": "chapter", "at": [1, 1]},
    ])

    def replay():
        path.write_text(json.dumps(document), encoding="utf-8")
        actions = {"actions": [action("use"), action("use", "near")]}
        return request(cli, destination, "expedition.replay", actions)["steps"]

    steps = replay()
    assert [step["chapter"] for step in steps] == ["high", "near"]
    first["interaction_priority"] = 0
    assert replay()[0]["chapter"] == "near"
    first["interaction_priority"] = 10
    assert replay()[0]["chapter"] == "high"
    print("Interaction priority overrides proximity until a specific target is selected")


def play_modes(cli, probe, project, root):
    project = Path(project)
    modes = ("journey", "build", "trial", "survey")
    for mode in modes:
        destination = root / mode
        shutil.copytree(project, destination)
        assets = destination / "assets"
        pack_file = assets / "play-rules.json"
        manifest_file = destination / "wds.project.json"
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
        expedition = next(item for item in manifest["resources"] if item["id"] == "expedition")
        expedition["refs"].append("play-rules")
        manifest["resources"].append({
            "id": "play-rules", "kind": "data", "path": "assets/play-rules.json", "refs": [],
        })
        manifest_file.write_text(json.dumps(manifest), encoding="utf-8")
        path = assets / "expedition.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        document.update(view="voxel", rule_pack="play-rules")
        if mode == "survey":
            document["regions"][1]["habitat"] = "ocean"
        path.write_text(json.dumps(document), encoding="utf-8")
        pack = {"version": 1, "mode": mode, "hunger_interval": 1}
        if mode == "journey":
            pack["deny"] = ["craft"]
        pack_file.write_text(json.dumps(pack), encoding="utf-8")

        def replay(actions, expected="ok"):
            return request(cli, destination, "expedition.replay", {"actions": actions}, expected)

        supply = action("supply", "herb")
        visit = action("survey-region", "tide-terrace")
        cases = {
            "journey": ([supply, visit, action("craft", "build-stove"), action("wait")],
                        ["command_forbidden"] * 3 + ["ok"]),
            "build": ([action("supply", "missing"), visit, supply, action("wait")],
                      ["unknown_item", "command_forbidden", "ok", "ok"]),
            "trial": ([action("revive"), supply, action("wait")],
                      ["command_forbidden", "command_forbidden", "ok"]),
            "survey": ([action("survey-region", "missing"), visit, action("east"),
                        action("wait"), supply, action("use", "return-path")],
                       ["unknown_region", "ok", "ok", "command_forbidden",
                        "command_forbidden", "command_forbidden"]),
        }
        actions, codes = cases[mode]
        result = replay(actions)
        assert [step["code"] for step in result["steps"]] == codes, result
        state = result["state"]
        assert state["play_rules"]["mode"] == mode and state["play_rules"]["hunger_interval"] == 1
        assert ("supply" in state["play_rules"]["commands"]) == (mode == "build")
        assert ("survey-region" in state["play_rules"]["commands"]) == (mode == "survey")
        assert ("revive" in state["play_rules"]["commands"]) == (mode in ("journey", "build"))
        assert state["tick"] == (0 if mode == "survey" else 2 if mode == "build" else 1)
        assert state["hunger"] == (20 if mode in ("build", "survey") else 19)
        if mode == "survey":
            assert state["region"] == "tide-terrace" and state["boat"] == 0
            assert not state["workshop"]["bag"]
            assert state["positions"]["tide-terrace"] != document["regions"][1]["spawn"]
        if mode == "build":
            save = destination / "mode.save.json"
            subprocess.run([probe, destination, save, "rules-save"], check=True, timeout=20)
            subprocess.run([probe, destination, save, "rules-open"], check=True, timeout=20)
            saved = save.read_bytes()
            pack_file.write_text(json.dumps({**pack, "season_ticks": 12}), encoding="utf-8")
            changed = subprocess.run([probe, destination, save, "rules-open"], timeout=20)
            assert changed.returncode == 21 and save.read_bytes() == saved
            pack_file.write_text(json.dumps(pack), encoding="utf-8")
        if mode == "journey":
            assert "craft" not in state["play_rules"]["commands"]
            expedition["refs"].remove("play-rules")
            manifest_file.write_text(json.dumps(manifest), encoding="utf-8")
            assert replay([], "invalid_expedition")["location"].endswith("/rule_pack")
            expedition["refs"].append("play-rules")
            manifest_file.write_text(json.dumps(manifest), encoding="utf-8")
            pack_file.write_text(json.dumps({**pack, "deny": ["craft", "craft"]}), encoding="utf-8")
            assert replay([], "invalid_expedition")["location"].endswith("/rule_pack/deny")
            pack_file.write_text(json.dumps(pack), encoding="utf-8")
    print("Four rule packs enforce commands, movement and metabolism, with save identity checks")


def package(cli, project, root, capability, probe=None, *, graphics=True):
    root.mkdir()
    destination = root / "release"
    export = [cli, "project", "export", project, "0", "windows-x64", destination]
    subprocess.run(export, timeout=30, check=True, capture_output=True)
    runtime = {**os.environ, "PATH": str(Path(os.environ["SystemRoot"]) / "System32")}
    modes = ("--check", "--expedition-smoke") if graphics else ("--check",)
    for mode in modes:
        subprocess.run([destination / "Play.exe", mode], cwd=root, env=runtime,
                       timeout=20, check=True)
    if probe:
        save_slot = destination / "expedition.slot-2.json"
        subprocess.run([probe, project, save_slot, "farm-start"], timeout=20, check=True)
        preserved = save_slot.read_bytes()
        result = subprocess.run([destination / "Play.exe", "--check-save=2"], env=runtime, timeout=20)
        assert result.returncode == 0 and save_slot.read_bytes() == preserved
        switch = [destination / "Play.exe", "--check-slot-switch"]
        result = subprocess.run(switch, env=runtime, timeout=20)
        assert (result.returncode == 0 and save_slot.read_bytes() == preserved
                and not (destination / "expedition.save.json").exists()
                and not (destination / "expedition.slot-3.json").exists())
        for invalid in ("--check-save=1", "--check-save=4"):
            result = subprocess.run([destination / "Play.exe", invalid], env=runtime, timeout=20)
            assert result.returncode == 2 and save_slot.read_bytes() == preserved
        damaged = destination / "expedition.slot-3.json"
        damaged.write_bytes(b"{broken")
        result = subprocess.run([destination / "Play.exe", "--check-save=3"], env=runtime, timeout=20)
        assert result.returncode == 1 and damaged.read_bytes() == b"{broken"
        result = subprocess.run(switch, env=runtime, timeout=20)
        assert (result.returncode == 1 and damaged.read_bytes() == b"{broken"
                and save_slot.read_bytes() == preserved)

    prior = root / "without-living-format"
    prior.mkdir()
    old_cli = prior / cli.name
    shutil.copy2(cli, old_cli)
    kit = prior / "wds-runtime" / "windows-x64"
    shutil.copytree(cli.parent / "wds-runtime" / "windows-x64", kit)
    manifest = kit / "wds.runtime.json"
    metadata = json.loads(manifest.read_text(encoding="utf-8"))
    assert metadata.pop(capability) == 1
    manifest.write_text(json.dumps(metadata), encoding="utf-8")
    rejected = root / "rejected"
    failure = subprocess.run([old_cli, *export[1:-1], rejected], capture_output=True,
                             timeout=30, check=False)
    result = json.loads(failure.stdout)
    assert failure.returncode and result["code"] == "invalid_runtime_dependency", result
    assert result["location"].endswith("/" + capability) and not rejected.exists()
    label = "with graphics smoke" if graphics else "headless"
    print(f"Standalone Windows validation {label}, older runtime rejection")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--no-graphics", action="store_true",
                        help="check packaging and saves without opening a graphics window")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="w25-", dir=args.work) as folder:
        root = Path(folder)
        project = root / "weather-garden"
        shutil.copytree(SAMPLE / "assets", project / "assets")
        shutil.copy2(SAMPLE / "wds.project.json", project)
        circuit(args.cli.resolve(), project, root / "circuit.json")
        actuate(args.cli.resolve(), root / "actuator-bench")
        yard = SAMPLE.parent / "voxel-workyard"
        workyard(args.cli.resolve(), yard, root / "workyard")
        garden(args.cli.resolve(), project, args.probe.resolve(), root / "adventure.json")
        coast = root / "coast"
        conditions(args.cli.resolve(), project, coast)
        kiln = root / "kiln"
        processing(args.cli.resolve(), project, kiln)
        engraved = root / "engraved"
        inscriptions(args.cli.resolve(), kiln, engraved)
        chronicle = root / "chronicle"
        story(args.cli.resolve(), project, chronicle)
        combined(args.cli.resolve(), project, (coast, kiln, chronicle), root / "combined")
        control = root / "controls"
        controls(args.cli.resolve(), project, control)
        modes = root / "modes"
        play_modes(args.cli.resolve(), args.probe.resolve(), project, modes)
        if args.runtime:
            runtime = args.runtime.resolve()
            graphics = not args.no_graphics
            package(runtime, project, root / "garden-package", "survival_format",
                    args.probe.resolve(), graphics=graphics)
            package(runtime, coast, root / "coast-package", "exposure_format", graphics=graphics)
            package(runtime, kiln, root / "kiln-package", "processing_format", graphics=graphics)
            package(runtime, engraved, root / "engraved-package", "inscription_format",
                    graphics=graphics)
            package(runtime, chronicle, root / "story-package", "story_format", graphics=graphics)
            package(runtime, control, root / "actions-package", "interaction_format",
                    graphics=graphics)
            package(runtime, yard, root / "workyard-package", "voxel_dynamics_format",
                    graphics=False)
            package(runtime, modes / "build", root / "rules-package", "expedition_rules_format",
                    graphics=graphics)


if __name__ == "__main__":
    main()
