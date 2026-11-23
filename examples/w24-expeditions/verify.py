"""Exercise both expeditions, compatible export, interrupted writes and scale."""

import argparse
import json
import os
import shutil
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
NORTH = action("north")
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


def observed(authored, seen):
    covered = authored & seen
    return {
        "total": len(authored), "covered": len(covered),
        "missing": sorted(authored - covered),
    }


def route_coverage(project, route, report):
    data = json.loads((project / "assets" / "expedition.json").read_text(encoding="utf-8"))
    regions = {area["id"]: area for area in data["regions"]}
    sites = {spot["id"]: spot for area in regions.values() for spot in area["sites"]}
    successful = [(command, state) for command, state in zip(route, report["steps"], strict=True)
                  if state["code"] == "ok"]
    visited = {state["region"] for _, state in successful}
    reached_sites = {cmd.get("target") for cmd, _ in successful
                     if cmd["command"] in ("use", "gather", "attack")} & sites.keys()
    crafted = {cmd["target"] for cmd, _ in successful if cmd["command"] == "craft"}
    held = {item["item"] for _, state in successful
            for container in ("bag", "chest", "loot") for item in state["workshop"][container]}
    icons = set(data.get("icons", {}).values())
    icons.update(spot["icon"] for spot in sites.values() if "icon" in spot)
    return {
        "regions": observed(regions.keys(), visited),
        "habitats": observed({area["habitat"] for area in regions.values()},
                             {regions[key]["habitat"] for key in visited}),
        "sites": observed(sites.keys(), reached_sites),
        "site_kinds": observed({spot["kind"] for spot in sites.values()},
                               {sites[key]["kind"] for key in reached_sites}),
        "recipes": observed(data["workshop"]["recipes"].keys(), crafted),
        "inventory_items": observed(data["workshop"]["items"].keys(), held),
        "decoded_icons": observed(icons, icons if report["code"] == "ok" else set()),
        "failed_steps": [
            {"step": index, "action": command, "code": state["code"], "region": state["region"]}
            for index, (command, state) in enumerate(zip(route, report["steps"], strict=True), 1)
            if state["code"] != "ok"
        ],
    }


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


def check_supply_route(cli, project, prefix, expected_steps):
    steps = replay(cli, project, [*prefix, *(cmd for cmd, _ in expected_steps)])["steps"]
    for index, (command, code) in enumerate(expected_steps, len(prefix)):
        step = steps[index]
        if step["code"] != code:
            return steps, {
                "step": index + 1, "action": command, "code": step["code"],
                "expected": code, "boat": step["boat"], "tick": step["tick"],
            }
        if code != "ok":
            assert step["tick"] == steps[index - 1]["tick"], (project.name, command, step)
            assert step["workshop"] == steps[index - 1]["workshop"], (project.name, command)
    return steps, None


def supply_coverage(cli, project, main_route, main_report):
    authored = json.loads((project / "assets" / "expedition.json").read_text(encoding="utf-8"))
    expected = set()
    completed = set()
    failures = []
    tide_delays = []
    entries = {authored["regions"][0]["id"]: 0}
    for index, step in enumerate(main_report["steps"], 1):
        entries.setdefault(step["region"], index)

    for area in authored["regions"]:
        region = area["id"]
        prefix = main_route[:entries[region]]
        for site in area["sites"]:
            deposit = site["id"]
            if not deposit.startswith("deposit-"):
                continue
            key = deposit.removeprefix("deposit-")
            expected.add(deposit)
            sample = site["reward"]
            slot = sample.removeprefix("sample-")
            item, voucher = f"refined-{slot}", f"voucher-{slot}"
            trade, shelter = use(f"outpost-{key}"), use(f"shelter-{key}")
            recipe = authored["workshop"]["recipes"][item]
            materials = sum(stack["count"] for stack in recipe["ingredients"])

            # Enter on the main route and approach the lane from its west side.
            x, y = site["at"]
            route = [SOUTH] * (y - 1) + [EAST] * (x - 2)
            checks = [
                (action("craft", item), "missing_ingredients"),
                *((cmd, "ok") for cmd in gather(deposit, materials)),
                (EAST, "ok"), (trade, "trade_unavailable"),
                (action("craft", item), "ok"), (trade, "ok"), (trade, "trade_unavailable"),
                (EAST, "ok"), (shelter, "ok"),
            ]
            expected_steps = [*((command, "ok") for command in route), *checks]
            steps, failure = check_supply_route(cli, project, prefix, expected_steps)
            if area["habitat"] == "ocean" and failure and failure["code"] == "boat_required":
                alternatives = [(waits, route, False) for waits in range(1, 16)]
                # Check alternate lanes from the repair deck when the direct lane stays rough.
                for lane in range(y, 1, -1):
                    approach = [SOUTH, SOUTH, use("repair-deck")]
                    approach += ([NORTH] if lane == 2 else [SOUTH] * (lane - 3))
                    approach += [EAST] * (x - 2) + [SOUTH] * (y - lane)
                    alternatives.extend((waits, approach, True) for waits in range(16))
                for waits, approach, repaired in alternatives:
                    delayed = [(WAIT, "ok")] * waits
                    delayed += [(command, "ok") for command in approach] + checks
                    retry, error = check_supply_route(cli, project, prefix, delayed)
                    if not error:
                        tide_delays.append({"deposit": deposit, "waits": waits, "repair": repaired,
                                            "approach": approach, "initial": failure})
                        steps, failure = retry, None
                        break
            if failure:
                failures.append({"deposit": deposit, "region": region, **failure})
                continue
            assert carry(steps[-1], voucher) and "shelter" in causes(steps[-1:]), (project.name, deposit)
            assert not carry(steps[-1], sample) and not carry(steps[-1], item), (project.name, deposit)
            completed.add(deposit)
    return {**observed(expected, completed), "failures": failures, "tide_delays": tide_delays}


def journey(cli, projects, coverage):
    copper = projects / "copper-relay"
    drift = projects / "drift-naturalists"
    reports = {}
    failures = {}
    for name, project, path in (("copper", copper, COPPER), ("drift", drift, DRIFT)):
        result = replay(cli, project, path)
        counts = route_coverage(project, path, result)
        coverage[project.name] = counts
        if counts["failed_steps"]:
            failures[project.name] = counts["failed_steps"]
        reports[name] = result
    assert not failures, failures
    for name, project, path in (("copper", copper, COPPER), ("drift", drift, DRIFT)):
        result = reports[name]
        assert result["state"]["ending"], result["state"]
        assert replay(cli, project, path) == result
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


def checkpoint_handoff(cli, project, relocated):
    route = [*DRIFT, WEST, WEST, *([use("spring-haven")] * 12), use("to-mainland")]
    uninterrupted = replay(cli, project, route)
    assert all(step["code"] == "ok" for step in uninterrupted["steps"])
    assert uninterrupted["state"]["ending"] and uninterrupted["state"]["region"] == "reed-bank"
    assert uninterrupted["state"]["season"] == 1 and carry(uninterrupted["state"], "naturalist")

    copies = [index + 1 for index, command in enumerate(route)
              if command in (use("cave-mouth"), use("to-vault"), use("repair-deck"), use("landfall"))]
    shutil.copytree(project, relocated)
    begin, checkpoint = 0, None
    for end in [*copies, len(route)]:
        options = {"actions": route[begin:end], "checkpoint": True}
        if checkpoint:
            options["resume"] = checkpoint
        result = call(cli, relocated if checkpoint else project, "expedition.replay", options)
        assert result["code"] == "ok", result
        assert result["steps"] == uninterrupted["steps"][begin:end]
        expected_state = uninterrupted["steps"][end - 1].copy()
        expected_state.pop("code")
        assert result["state"] == expected_state
        checkpoint = result["checkpoint"]
        assert checkpoint["actions"] == route[:end]
        begin = end

    for invalid in (
        {**checkpoint, "version": 2},
        {**checkpoint, "identity": "another-project:0:0000"},
        {**checkpoint, "actions": [use("dock-outside-reach")]},
    ):
        result = call(cli, relocated, "expedition.replay", {"resume": invalid, "actions": []})
        assert result["code"] == "invalid_checkpoint" and result["location"] == "/input/resume"
    return len(copies)


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
        coverage = {}
        chains = {}
        try:
            reports = journey(args.cli.resolve(), root / "projects", coverage)
            for name, project, route in (
                ("copper", root / "projects" / "copper-relay", COPPER),
                ("drift", root / "projects" / "drift-naturalists", DRIFT),
            ):
                chains[project.name] = supply_coverage(args.cli.resolve(), project, route, reports[name])
            supply_failures = [row for report in chains.values() for row in report["failures"]]
            assert not supply_failures, supply_failures
        except (AssertionError, subprocess.SubprocessError) as error:
            if args.report:
                write_json(args.report, {
                    "seed": args.seed, "scale": args.scale, "route_validation": "failed",
                    "detail": str(error), "candidate_coverage": coverage,
                    "supply_coverage": chains,
                })
            raise
        copper = root / "projects" / "copper-relay"
        drift = root / "projects" / "drift-naturalists"
        for counts in coverage.values():
            assert not counts["failed_steps"] and not counts["regions"]["missing"], counts
            assert counts["sites"]["covered"] and counts["decoded_icons"]["covered"], counts
        supply_steps = []
        if args.scale:
            supply_steps.extend(supply_chain(args.cli.resolve(), p) for p in (copper, drift))
        replayed = time.perf_counter()
        persistence(args.probe.resolve(), drift, root / "save.json")
        checkpoints = checkpoint_handoff(args.cli.resolve(), drift, root / "relocated-drift")
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
        result = {**metrics, "seed": args.seed, "scale": args.scale, "route_validation": "ok",
                  "total_generation_ms": (generation - start) * 1000,
                  "replay_and_coverage_ms": (replayed - generation) * 1000,
                  "checkpoint_ms": (checkpoint - replayed) * 1000,
                  "checkpoint_handoffs": checkpoints,
                  "reauthor_ms": (before_pack - first_packed) * 1000,
                  "reauthor": changes, "packages": packages,
                  "route_steps": {key: len(value["steps"]) for key, value in reports.items()},
                  "supply_steps_checked": supply_steps,
                  "supply_coverage": chains,
                  "independent_packages_checked": len(packages)}
        if args.report:
            write_json(args.report, {**result, "candidate_coverage": coverage})
        result["candidate_coverage"] = {
            name: {group: value if group == "failed_steps" else {
                "covered": value["covered"], "total": value["total"],
            } for group, value in counts.items()} for name, counts in coverage.items()
        }
        result["supply_coverage"] = {
            name: {key: value[key] for key in ("total", "covered")} for name, value in chains.items()
        }
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
