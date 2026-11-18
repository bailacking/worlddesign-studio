"""Build, exercise and seal both W12 projects in a new output directory."""

import argparse
import hashlib
import json
import platform
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from time import perf_counter

from generate import assets_for, catalogs, scene_for, write_json

LAB = Path(__file__).resolve().parent
CONTENT = LAB.parents[1] / "scripts" / "content_bundle.py"
SAMPLES = ("crystal-yard", "wind-delivery")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, detail):
    if not condition:
        raise ValueError(detail)


def timed(phases, name, operation, *args):
    start = perf_counter()
    try:
        return operation(*args)
    finally:
        phases[name] = round(perf_counter() - start, 4)


def invoke(report, name, args, cwd=None, rejected=False, json_output=True):
    start = perf_counter()
    result = subprocess.run(args, cwd=cwd, capture_output=True, timeout=60, check=False)
    output = result.stdout.decode("utf-8", errors="replace").strip()
    receipt = {
        "step": name,
        "exit": result.returncode,
        "elapsed_s": round(perf_counter() - start, 4),
        "injected_failure_expected": rejected,
    }
    report["commands"].append(receipt)
    if result.stderr:
        receipt["stderr"] = result.stderr.decode("utf-8", errors="replace").strip()
    if json_output and output:
        receipt["result"] = json.loads(output)
    else:
        receipt["stdout"] = output
    expected = 1 if rejected else 0
    require(
        result.returncode == expected,
        f"{name}: exit {result.returncode}, expected {expected}",
    )
    return receipt.get("result")


def construct_content(kind, folder, report):
    ordinary, exceptions, invalid = catalogs(kind)
    ids = list(exceptions)
    for name, records in [
        ("ordinary", ordinary),
        ("injected", invalid),
        ("exceptions", exceptions),
    ]:
        write_json(
            folder / f"{name}.json",
            {"format": "wds-content-source-v1", "entries": records},
        )
    base = folder / "base.json"
    attempt = folder / "injected-output.json"
    product = folder / "catalog.json"

    def content(step, *args, rejected=False):
        return invoke(report, step, [sys.executable, CONTENT, *args], rejected=rejected)

    content("expand-ordinary", "expand", folder / "ordinary.json", base)
    initial_hash = sha256(base)
    invalid_result = content(
        "inject-exception",
        "repair",
        folder / "injected.json",
        base,
        attempt,
        *ids,
        rejected=True,
    )
    location = (
        "/entries/crystal-lock/rule/enabled"
        if kind == "crystal-yard"
        else "/entries/slotted-parcel/rule/layout"
    )
    require(
        invalid_result["code"] == "invalid_content"
        and [error["location"] for error in invalid_result["errors"]] == [location],
        "Expected one localized defect",
    )
    require(
        not attempt.exists() and sha256(base) == initial_hash,
        "Failed repair changed the base",
    )
    content(
        "repair-exception", "repair", folder / "exceptions.json", base, product, *ids
    )
    content("validate-complete-catalog", "validate", product)

    records = json.loads(product.read_bytes())["entries"]
    initial = json.loads(base.read_bytes())["entries"]
    require(
        set(records) - set(initial) == set(ids)
        and all(records[key] == item for key, item in initial.items()),
        "Repair changed ordinary records",
    )
    if kind == "crystal-yard":
        require(
            len(initial) == 8
            and all(
                [row["charge"] for row in block["variants"]] == list(range(16))
                for block in initial.values()
            ),
            "Wrong block state range",
        )
        require(
            records["crystal-lock"]["rule"]["enabled"]
            == ["protected-face", "retains-collision"],
            "Wrong locked-face evidence",
        )
    else:
        require(
            len(initial) == 96
            and all(
                records[f"wrap-{index:02}"]["rule"]
                == {
                    "ingredients": [f"fiber-{index:02}"],
                    "layout": "row",
                    "output": f"parcel-{index:02}",
                }
                for index in range(1, 33)
            ),
            "Wrong ordinary recipe links",
        )
        require(
            records["slotted-parcel"]["rule"]["layout"] == "partial"
            and records["reinforced-parcel"]["rule"]["ingredients"]
            == ["fiber-02", "fiber-02"],
            "Exception layout or ingredient count was lost",
        )
    report["content"] = {
        "validated_ordinary_records": len(initial),
        "validated_exception_records": len(ids),
        "expanded_block_states": sum(
            len(row["variants"]) for row in initial.values() if row["kind"] == "block"
        ),
        "exception_ids": ids,
        "human_entered_records": 0,
    }
    return product


def assemble(project, kind, catalog, resources):
    folder = project / "assets"
    (folder / "catalog.json").write_bytes(catalog.read_bytes())
    write_json(folder / "intro.json", scene_for(kind))
    entries = [
        {"id": key, "kind": type_, "path": f"assets/{name}", "refs": []}
        for key, type_, name in [("catalog", "data", "catalog.json"), *resources]
    ]
    intro = {
        "id": "intro",
        "kind": "scene",
        "path": "assets/intro.json",
        "refs": [entry["id"] for entry in entries],
    }
    write_json(
        project / "wds.project.json",
        {
            "format_version": 1,
            "project_id": kind,
            "revision": 0,
            "entry_scene": "intro",
            "resources": [intro, *entries],
        },
    )


def produce(root, kind, options, report):
    start = perf_counter()
    phases = report["phase_s"]
    folder = root / kind
    source, project = folder / "sources", folder / "project"
    source.mkdir(parents=True)
    (project / "assets").mkdir(parents=True)
    product = timed(
        phases, "expand_repair_validate", construct_content, kind, source, report
    )
    resources = timed(phases, "assets", assets_for, kind, project / "assets")
    timed(
        phases,
        "assemble_rules_and_project",
        assemble,
        project,
        kind,
        product,
        resources,
    )
    report["runtime"] = timed(
        phases, "interaction", invoke, report, "play", [options.probe, kind], project
    )
    report["runtime_assets"] = [entry[2] for entry in resources]

    frame = folder / "workbench.png"
    timed(
        phases,
        "desktop",
        lambda: invoke(
            report,
            "open-workbench",
            [options.studio, "--smoke", project, frame],
            json_output=False,
        ),
    )
    require(
        frame.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"),
        "Workbench did not write a PNG",
    )

    if kind == "wind-delivery":

        def export():
            release = folder / "windows-x64"
            receipt = invoke(
                report,
                "export",
                [
                    options.cli,
                    "project",
                    "export",
                    project,
                    "0",
                    "windows-x64",
                    release,
                ],
            )
            require(
                receipt["code"] == "ok"
                and sha256(release / "game/assets/catalog.json") == sha256(product),
                "Export did not carry the verified catalog",
            )
            invoke(
                report,
                "standalone-check",
                [release / "Play.exe", "--check"],
                cwd=release,
                json_output=False,
            )

        timed(phases, "export_and_check", export)
        report["handoff"] = (
            "editable project and independently checked Windows x64 package"
        )
    else:
        report["handoff"] = "editable revision 1, opens in Qt workbench"
    report["machine_s"] = round(perf_counter() - start, 4)
    report["complete"] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ("cli", "studio", "probe", "output"):
        parser.add_argument(
            f"--{argument}", type=lambda path: Path(path).resolve(), required=True
        )
    parser.add_argument("--authoring-start", type=datetime.fromisoformat)
    options = parser.parse_args()
    now = datetime.now().astimezone()
    if options.authoring_start and (
        not options.authoring_start.tzinfo or options.authoring_start > now
    ):
        parser.error("--authoring-start must be a past timestamp with a UTC offset")
    options.output.mkdir(parents=True, exist_ok=False)
    start = perf_counter()
    report = {
        "format": "wds-w12-study-v1",
        "machine_started_at": now.isoformat(),
        "authoring_started_at": options.authoring_start.isoformat()
        if options.authoring_start
        else None,
        "environment": {
            "platform": platform.platform(),
            "cpu": platform.processor(),
            "python": platform.python_version(),
        },
        "tools_sha256": {},
        "samples": {},
        "external_model_tokens": None,
    }
    try:
        for path in (
            options.cli,
            options.studio,
            options.probe,
            options.cli.parent / "wds-runtime/windows-x64/Play.exe",
            CONTENT,
            LAB / "generate.py",
            LAB / "run.py",
            LAB / "probe.cpp",
            LAB / "brief.md",
        ):
            report["tools_sha256"][path.name] = sha256(path)
        for kind in SAMPLES:
            sample = {"phase_s": {}, "commands": [], "complete": False}
            report["samples"][kind] = sample
            produce(options.output, kind, options, sample)
        report["code"] = "ok"
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        report.update(code="failed", error=str(error))
    finally:
        files = {
            path.relative_to(options.output).as_posix(): sha256(path)
            for path in sorted(options.output.rglob("*"))
            if path.is_file()
        }
        write_json(options.output / "checksums.json", files)
        sealed = datetime.now().astimezone()
        report.update(
            sealed_at=sealed.isoformat(),
            machine_total_s=round(perf_counter() - start, 4),
        )
        if options.authoring_start:
            report["production_wall_s"] = round(
                (sealed - options.authoring_start).total_seconds(), 3
            )
        for sample in report["samples"].values():
            calls = sample["commands"]
            sample["command_count"] = len(calls)
            sample["rejected_count"] = sum(call["exit"] != 0 for call in calls)
            sample["injected_count"] = sum(
                call["injected_failure_expected"] for call in calls
            )
            sample["unexpected_failure_count"] = sum(
                call["exit"] != (1 if call["injected_failure_expected"] else 0)
                for call in calls
            )
        write_json(options.output / "report.json", report)
    print(options.output / "report.json")
    if report["code"] != "ok":
        print(report["error"], file=sys.stderr)
    return 0 if report["code"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
