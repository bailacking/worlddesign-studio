from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from copy import deepcopy
from pathlib import Path
from time import perf_counter_ns
from typing import Any

from content_bundle import (
    BUNDLE,
    SOURCE,
    ContentError,
    expand_entry,
    read_entries,
    validate_bundle,
    write_bundle,
)

ROOT = Path(__file__).resolve().parents[1]


def encode(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode()


def variants(
    arm: str, authored: bytes, baseline: dict[str, Any]
) -> tuple[dict[str, Any], list[dict[str, str]]]:
    result = dict(baseline)
    issues = []
    for entry_id, item in json.loads(authored).items():
        prop = item["property"]
        source = {
            "kind": "block",
            "reference": "W4-original-content",
            "rule": {"property": prop, "range": item["range"]},
        }
        if arm == "batch":
            try:
                result[entry_id] = expand_entry(entry_id, source)
            except ContentError as error:
                issues.append(error.issue)
        else:
            result[entry_id] = {
                **source,
                "variants": [{prop: level} for level in item["rows"]],
            }
    issues.extend(validate_bundle(result))
    return result, issues


def measured(arm: str, authored: bytes, baseline: dict[str, Any]) -> tuple:
    start = perf_counter_ns()
    result, issues = variants(arm, authored, baseline)
    output = encode({"format": BUNDLE, "entries": result}) if not issues else b""
    return result, issues, output, (perf_counter_ns() - start) / 1_000_000


def study(destination: Path) -> dict[str, Any]:
    inputs = json.loads(
        (ROOT / "examples/w4-lab/content-input.json").read_text(encoding="utf-8")
    )
    baseline = {
        entry_id: expand_entry(entry_id, entry)
        for entry_id, entry in read_entries(
            ROOT / "examples/content-generation/entries.json", SOURCE
        ).items()
    }
    authored = {
        "batch": encode(
            {
                entry_id: {"property": item["property"], "range": item["range"]}
                for entry_id, item in inputs.items()
            }
        ),
        "explicit": encode(inputs),
    }

    first = {arm: measured(arm, data, baseline) for arm, data in authored.items()}
    if (
        any(run[1] for run in first.values())
        or first["batch"][2] != first["explicit"][2]
    ):
        raise ValueError(
            f"Inputs produce different content or fail validation: {first['batch'][1]} / {first['explicit'][1]}"
        )
    durations = {arm: [] for arm in authored}
    for pair in range(15):
        order = list(authored)
        if pair % 2:
            order.reverse()
        for arm in order:
            _, issues, result, elapsed = measured(arm, authored[arm], baseline)
            if issues or result != first[arm][2]:
                raise ValueError(f"Non-repeatable output for {arm}")
            durations[arm].append(elapsed)

    repairs = {}
    for arm in authored:
        draft = json.loads(authored[arm])
        if arm == "batch":
            draft["amber-gate"]["range"] = [12, -3]
        else:
            draft["amber-gate"]["rows"].remove(10)
        _, issues, _, rejected_ms = measured(arm, encode(draft), baseline)
        expected = "invalid_rule" if arm == "batch" else "invalid_expansion"
        if [issue["code"] for issue in issues] != [expected]:
            raise ValueError(f"Fault was not isolated for {arm}: {issues}")

        repair_start = perf_counter_ns()
        # Rebuild the affected entry only, using the remaining entries from the validated bundle.
        known_good = deepcopy(first[arm][0])
        item = draft["amber-gate"]
        if arm == "batch":
            item["range"] = [-3, 12]
        else:
            item["rows"].insert(item["rows"].index(11), 10)
        repaired, repair_issues = variants(
            arm, encode({"amber-gate": item}), known_good
        )
        elapsed = (perf_counter_ns() - repair_start) / 1_000_000
        if repair_issues or repaired != first[arm][0]:
            raise ValueError(f"Repair changed unrelated content for {arm}")
        repairs[arm] = {
            "issue": issues[0],
            "reject_ms": rejected_ms,
            "repair_and_validate_ms": elapsed,
            "records_replaced": 1,
        }

    destination.mkdir(parents=True, exist_ok=True)
    for arm, data in authored.items():
        (destination / f"{arm}-input.json").write_bytes(data + b"\n")
        write_bundle(destination / f"{arm}-bundle.json", first[arm][0])
    report = {
        "fixture": "four original integer state families, shared W3 reference entries",
        "reference_records": len(baseline),
        "variant_rows": sum(len(item["rows"]) for item in inputs.values()),
        "records": len(first["batch"][0]),
        "output_sha256": hashlib.sha256(first["batch"][2]).hexdigest(),
        "pairs": 15,
        "routes": {
            arm: {
                "first_pass": not first[arm][1],
                "input_bytes": len(authored[arm]),
                "pipeline_p50_ms": statistics.median(durations[arm]),
                "pipeline_min_ms": min(durations[arm]),
                "pipeline_max_ms": max(durations[arm]),
                "fault_injection": repairs[arm],
            }
            for arm in authored
        },
    }
    (destination / "content-report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Measure compact and explicitly listed WDS content"
    )
    parser.add_argument("destination", type=Path)
    print(json.dumps(study(parser.parse_args().destination), ensure_ascii=False))
