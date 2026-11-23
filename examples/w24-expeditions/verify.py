"""Exercise both expeditions, compatible export, interrupted writes and scale."""

import argparse
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from generate import generate, write_json


def action(key, target=None):
    return {"command": key} if target is None else {"command": key, "target": target}


def use(target):
    return action("use", target)


def gather(target, count=1):
    return [action("gather", target)] * count


def attack(target, count=1):
    return [action("attack", target)] * count


EAST = action("east")
WEST = action("west")
SOUTH = action("south")
WAIT = action("wait")

COPPER_TO_SKY = [
    *gather("copper-vein", 3), *gather("field-store", 2), action("craft", "spear"),
    EAST, use("mason"), EAST, use("keeper"), EAST, use("pens"), EAST, use("burrow"),
    EAST, *attack("settlement-raid", 2), EAST, EAST, use("kiln-gate"),
    use("ember-market"), use("kiln-anchor"), EAST, use("fortress-ruin"),
    EAST, use("cooling-vault"), EAST, use("sky-gate"),
]

COPPER = [
    *COPPER_TO_SKY, *attack("ring-warden", 3), EAST, EAST,
    use("spire-found"), WEST, WEST, use("land-gate"), EAST, use("return"),
]

DRIFT_TO_SHORE = [
    *gather("driftwood", 6), *gather("beach-food", 4), action("craft", "spear"),
    action("craft", "lantern"), action("craft", "skiff"), EAST, use("rising"), EAST,
    use("cave-mouth"), *attack("cave-creature"), EAST, use("to-vault"),
    use("inscription"), *gather("spark"), use("to-cave"), WEST, use("reed-mouth"),
]

DRIFT = [
    *DRIFT_TO_SHORE, EAST, EAST, EAST, use("shipyard"), EAST, use("to-current"),
    *attack("current-raider", 2), use("repair-deck"), EAST, EAST, use("landfall"),
    *attack("shoal-giant", 2), EAST, EAST, use("shoal-crossing"), *attack("storm-giant", 3), EAST, EAST, use("return"),
]


def call(cli, directory, operation, values, revision=0, mode="read", access=()):
    data = {"version": 1, "operation": operation, "mode": mode,
            "expected_revision": revision, "input": values}
    result = subprocess.run([cli, "project", "request", directory, *access],
                            input=json.dumps(data), capture_output=True, text=True,
                            encoding="utf-8", timeout=30, check=False)
    reply = json.loads(result.stdout)
    assert result.returncode == (0 if reply["code"] == "ok" else 1), (reply, result.stderr)
    return reply


def replay(cli, project, actions, revision=0):
    result = call(cli, project, "expedition.replay", {"actions": actions}, revision)
    assert result["code"] == "ok", result
    result.pop("usage")
    return result


def causes(steps):
    return {item["cause"] for step in steps for item in step["receipts"]}


def carry(step, item):
    return any(row["item"] == item for row in step["workshop"]["bag"])


def supply_chain(cli, project, revision=0):
    craft = action("craft", "refined-1")
    trade, shelter = use("outpost-0-1"), use("shelter-0-1")
    route = [SOUTH, SOUTH, SOUTH, *([EAST] * 4), *gather("deposit-0-1"), craft,
             *gather("deposit-0-1"), EAST, trade, craft, trade, trade, EAST, shelter]
    steps = replay(cli, project, route, revision)["steps"]
    expected = {8: "missing_ingredients", 11: "trade_unavailable", 14: "trade_unavailable"}
    for index, step in enumerate(steps):
        assert step["code"] == expected.get(index, "ok"), (project.name, route[index], step)
        if index in expected:
            assert step["tick"] == steps[index - 1]["tick"]
            assert step["workshop"] == steps[index - 1]["workshop"]
    assert carry(steps[12], "refined-1") and not carry(steps[-1], "refined-1")
    assert not carry(steps[12], "sample-1") and carry(steps[-1], "voucher-1")
    assert "shelter" in causes(steps[-1:])
    return len(steps)


def journey(cli, projects):
    copper = projects / "copper-relay"
    drift = projects / "drift-naturalists"
    reports = {}
    for name, project, path in (("copper", copper, COPPER), ("drift", drift, DRIFT)):
        result = replay(cli, project, path)
        failures = [(index, path[index], step["code"], step["region"], step["positions"])
                    for index, step in enumerate(result["steps"]) if step["code"] != "ok"]
        assert not failures, failures
        assert result["state"]["ending"], result["state"]
        assert replay(cli, project, path) == result
        reports[name] = result
    a = reports["copper"]
    assert a["state"]["positions"] == {"cloud-ring": [1, 1], "meadow": [9, 1],
                                      "slag-field": [4, 1]}, a["state"]
    assert a["state"]["population"] == 2 and a["state"]["companions"] == 1
    assert a["state"]["encounters"]["ring-warden"] == 0 and carry(a["state"], "spire-emblem")
    assert {"embers", "raid", "anchor_charged", "boss_enraged", "arrival", "victory"} <= causes(a["steps"])
    assert len({tuple(step["actors"]["settlement-raid"]) for step in a["steps"]}) > 1
    b = reports["drift"]
    assert b["state"]["region"] == "heron-isle" and carry(b["state"], "naturalist")
    assert b["state"]["positions"]["blue-cave"] == [1, 1]
    assert {"launch", "retaliation", "repair", "ending", "chapter"} <= causes(b["steps"])
    assert min(step["boat"] for step in b["steps"] if step["region"] == "open-current") < 8
    for site in ("current-raider", "shoal-giant", "storm-giant"):
        assert b["state"]["encounters"][site] == 0

    recovery = replay(cli, copper, [*COPPER_TO_SKY, *([WAIT] * 140), action("revive")])
    assert any(step["dead"] for step in recovery["steps"])
    assert recovery["state"]["region"] == "slag-field" and recovery["state"]["anchor_charge"] == 0
    assert not recovery["state"]["dead"] and "revival" in causes(recovery["steps"][-1:])
    assert carry(recovery["state"], "spear") and recovery["state"]["positions"]["cloud-ring"] == [1, 1]

    at_market = COPPER_TO_SKY.index(use("ember-market"))
    capacities = replay(cli, copper, [
        *gather("copper-vein", 3), *COPPER_TO_SKY[:at_market],
        *([use("ember-market"), use("kiln-anchor")] * 3),
    ])
    assert all(step["code"] == "ok" for step in capacities["steps"][:-1])
    rejected = capacities["steps"][-1]
    assert rejected["code"] == "anchor_full" and rejected["anchor_charge"] == 2
    assert rejected["tick"] == capacities["steps"][-2]["tick"] and carry(rejected, "charge")

    permanent = replay(cli, drift, [*([WAIT] * 132), action("revive")])
    assert permanent["steps"][-1]["code"] == "permanent_death" and permanent["state"]["dead"]
    token = replay(cli, drift, [*DRIFT_TO_SHORE, *([WAIT] * 150), action("revive")])
    assert not token["state"]["dead"] and not carry(token["state"], "lifeline")
    shade = replay(cli, drift, [EAST, use("rising"), EAST, use("cave-mouth"), *([WAIT] * 24)])
    assert "darkness" in causes(shade["steps"]) and shade["state"]["morale"] < 20

    weather = replay(cli, drift, [EAST, *([use("camp")] * 241)])
    assert {step["season"] for step in weather["steps"]} == {0, 1, 2, 3}
    assert weather["state"]["season"] == 0 and weather["state"]["tick"] == 242
    assert {"drenched", "overheat", "starvation"} <= causes(weather["steps"])
    assert any({"starvation", "overheat"} <= causes([step]) for step in weather["steps"])

    blocked = replay(cli, drift, [WAIT, WAIT, use("shipyard")])
    assert blocked["steps"][-1]["code"] == "out_of_reach"
    assert blocked["steps"][-1]["tick"] == blocked["steps"][-2]["tick"]
    return reports


def persistence(probe, project, save):
    for stage, expected in (("start", 0), ("interrupt", 73), ("verify", 0)):
        result = subprocess.run([probe, project, save, stage], check=False, timeout=30)
        assert result.returncode == expected, (stage, result.returncode)


def reauthor(cli, project, route):
    path = project / "assets" / "expedition.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    start = time.perf_counter()
    initial = data["regions"][0]["sites"][0]["reward"]
    data["regions"][0]["sites"][0]["reward"] = "unknown"
    write_json(path, data)
    trial = call(cli, project, "expedition.replay", {"actions": []})
    assert trial["code"] == "invalid_expedition" and trial["location"].endswith("/reward"), trial
    data["regions"][0]["sites"][0]["reward"] = initial
    write_json(path, data)
    icon = project / "assets" / "icon-source.png"
    original = icon.read_bytes()
    icon.write_bytes(original[:12])
    trial = call(cli, project, "expedition.replay", {"actions": []})
    assert trial["code"] == "invalid_expedition" and trial["location"].endswith("/icons/source"), trial
    icon.write_bytes(original)
    area = data["regions"][0]
    override = next((spot for spot in area["sites"] if "icon" in spot), None)
    if override:
        initial = override["icon"]
        override["icon"] = "unlinked-icon"
        write_json(path, data)
        trial = call(cli, project, "expedition.replay", {"actions": []})
        location = f"/regions/{area['id']}/sites/{override['id']}/icon"
        assert trial["code"] == "invalid_expedition" and trial["location"].endswith(location), trial
        override["icon"] = initial
        write_json(path, data)
    repair_ms = (time.perf_counter() - start) * 1000
    rejected = call(cli, project, "resource.field.set",
                    {"resource_id": "expedition", "field": "hunger_interval", "value": 0},
                    mode="apply", access=("--allow-project-write",))
    assert rejected["code"] == "invalid_expedition" and rejected["revision"] == 0, rejected
    start = time.perf_counter()
    for expected, (field, value) in enumerate((
        ("hunger_interval", 3),
        ("seed", (data["seed"] + 1) % 2**32),
    )):
        receipt = call(
            cli, project, "resource.field.set",
            {"resource_id": "expedition", "field": field, "value": value},
            revision=expected, mode="apply", access=("--allow-project-write",),
        )
        assert receipt["code"] == "ok" and receipt["revision"] == expected + 1, receipt
    edit_ms = (time.perf_counter() - start) * 1000
    saved = json.loads(path.read_text(encoding="utf-8"))
    assert saved["seed"] != data["seed"] and saved["hunger_interval"] == 3
    after = replay(cli, project, [WAIT] * 3, revision=2)
    assert after["state"]["hunger"] == 19
    revised = replay(cli, project, route, revision=2)
    failures = [(index, step["code"]) for index, step in enumerate(revised["steps"])
                if step["code"] != "ok"]
    assert not failures and revised["state"]["ending"], (project.name, failures)
    return {"repair_ms": repair_ms, "edit_ms": edit_ms}


def export(cli, source, destination, revision):
    start = time.perf_counter()
    result = subprocess.run([cli, "project", "export", source, str(revision), "windows-x64", destination],
                            capture_output=True, text=True, encoding="utf-8", timeout=30, check=False)
    assert not result.returncode, (result.stdout, result.stderr)
    packed = time.perf_counter()
    images = list((source / "assets").glob("*.png"))
    assert any(image.name == "icon-portal.png" for image in images)
    for image in images:
        assert (destination / "game" / "assets" / image.name).read_bytes() == image.read_bytes()
    env = {**os.environ, "PATH": str(Path(os.environ["SystemRoot"]) / "System32")}
    checks = {"pack_ms": (packed - start) * 1000}
    for mode, metric in (("--check", "check_ms"), ("--expedition-smoke", "graphics_ms")):
        start = time.perf_counter()
        subprocess.run([destination / "Play.exe", mode], cwd=destination.parent,
                       env=env, timeout=20, check=True)
        checks[metric] = (time.perf_counter() - start) * 1000
    return checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--seed", type=int, default=73021)
    parser.add_argument("--scale", type=int, default=4, choices=range(5))
    args = parser.parse_args()
    if not 0 <= args.seed < 2**32:
        parser.error("--seed must be an unsigned 32-bit integer")
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="w24-", dir=args.work) as folder:
        root = Path(folder)
        start = time.perf_counter()
        metrics = generate(root / "projects", seed=args.seed, scale=args.scale)
        generation = time.perf_counter()
        reports = journey(args.cli.resolve(), root / "projects")
        copper = root / "projects" / "copper-relay"
        drift = root / "projects" / "drift-naturalists"
        supply_steps = []
        if args.scale:
            supply_steps.extend(supply_chain(args.cli.resolve(), p) for p in (copper, drift))
        replayed = time.perf_counter()
        persistence(args.probe.resolve(), drift, root / "save.json")
        checkpoint = time.perf_counter()
        packages = {}
        if args.runtime:
            packages["copper-r0"] = export(args.runtime.resolve(), copper, root / "copper-original", 0)
            packages["drift-r0"] = export(args.runtime.resolve(), drift, root / "drift-original", 0)
        first_packed = time.perf_counter()
        changes = {"copper": reauthor(args.cli.resolve(), copper, COPPER),
                   "drift": reauthor(args.cli.resolve(), drift, DRIFT)}
        if args.scale:
            supply_steps.extend(supply_chain(args.cli.resolve(), p, 2) for p in (copper, drift))
        before_pack = time.perf_counter()
        if args.runtime:
            packages["copper-r2"] = export(args.runtime.resolve(), copper, root / "copper-revised", 2)
            packages["drift-r2"] = export(args.runtime.resolve(), drift, root / "drift-revised", 2)
        result = {**metrics, "seed": args.seed, "scale": args.scale,
                  "total_generation_ms": (generation - start) * 1000,
                  "replay_and_coverage_ms": (replayed - generation) * 1000,
                  "checkpoint_ms": (checkpoint - replayed) * 1000,
                  "reauthor_ms": (before_pack - first_packed) * 1000,
                  "reauthor": changes, "packages": packages,
                  "route_steps": {key: len(value["steps"]) for key, value in reports.items()},
                  "supply_steps_checked": supply_steps,
                  "independent_packages_checked": len(packages)}
        if args.report:
            write_json(args.report, result)
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
