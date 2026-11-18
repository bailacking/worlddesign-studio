"""Run and record a WDS operation plan through the shared JSON endpoint."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


def load_plan(path: Path) -> list[dict[str, Any]]:
    plan = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(plan, list) or not plan:
        raise ValueError("The plan must be a non-empty JSON array.")
    seen = set()
    for step in plan:
        if not isinstance(step, dict):
            raise ValueError("Each step must be a JSON object.")
        identity = step.get("request_id")
        if not isinstance(identity, str) or not identity or identity in seen:
            raise ValueError("Every step needs a unique request_id.")
        if not isinstance(step.get("expect", "ok"), str):
            raise ValueError(f"Step {identity} has an invalid expect value.")
        if "restore_from" in step and (
            not isinstance(step["restore_from"], str)
            or step["restore_from"] not in seen
            or step.get("operation") != "scene.restore"
            or step.get("mode") != "apply"
            or "input" in step
        ):
            raise ValueError(f"Step {identity} must restore an earlier receipt.")
        seen.add(identity)
    return plan


def invoke(command: list[str], request: dict[str, Any]) -> dict[str, Any]:
    try:
        completed = subprocess.run(
            command,
            input=json.dumps(request, ensure_ascii=False).encode("utf-8"),
            capture_output=True,
            timeout=None if request.get("mode") == "apply" else 15,
            check=False,
        )
        result = json.loads(completed.stdout)
        if (
            isinstance(result, dict)
            and isinstance(result.get("code"), str)
            and completed.returncode == (0 if result["code"] == "ok" else 1)
        ):
            return result
        return {"code": "cli_invalid_result"}
    except subprocess.TimeoutExpired:
        return {"code": "cli_timeout"}
    except OSError:
        return {"code": "cli_unavailable"}
    except (UnicodeError, ValueError):
        return {"code": "cli_invalid_result"}


def replay(
    command: list[str], plan: list[dict[str, Any]], model_usage: dict[str, int] | None
) -> dict[str, Any]:
    started = time.monotonic()
    receipts: dict[str, dict[str, Any]] = {}
    trace = []
    service_ms = 0.0
    calls = 0
    code = "ok"
    for step in plan:
        request = step.copy()
        expected = request.pop("expect", "ok")
        restore_from = request.pop("restore_from", None)
        if restore_from is None:
            result = invoke(command, request)
            calls += 1
        else:
            change = receipts[restore_from].get("scene_change")
            if isinstance(change, dict) and {"before", "after"} <= change.keys():
                request["input"] = {
                    "change": {
                        **change,
                        "before": change["after"],
                        "after": change["before"],
                    }
                }
                result = invoke(command, request)
                calls += 1
            else:
                result = {"code": "missing_scene_change", "location": restore_from}
        receipts[request["request_id"]] = result
        trace.append({"request": request, "expected_code": expected, "result": result})
        service_ms += result.get("usage", {}).get("elapsed_ms", 0.0)
        if result["code"] != expected:
            code = "unexpected_result"
            break
    return {
        "code": code,
        "steps": trace,
        "usage": {
            "requests": calls,
            "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
            "service_elapsed_ms": round(service_ms, 3),
            "model_tokens": model_usage,
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--allow-project-write", action="store_true")
    parser.add_argument("--allow-destination-write", action="store_true")
    parser.add_argument("--input-tokens", type=int)
    parser.add_argument("--output-tokens", type=int)
    options = parser.parse_args(argv)
    tokens = (options.input_tokens, options.output_tokens)
    if any(value is not None for value in tokens) and not all(
        value is not None and value >= 0 for value in tokens
    ):
        parser.error("Supply both token counts as non-negative integers.")
    model_usage = None
    if options.input_tokens is not None and options.output_tokens is not None:
        model_usage = {"input": options.input_tokens, "output": options.output_tokens}
    try:
        plan = load_plan(options.plan)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    command = [
        str(options.cli.resolve()),
        "project",
        "request",
        str(options.project.resolve()),
    ]
    if options.allow_project_write:
        command.append("--allow-project-write")
    if options.allow_destination_write:
        command.append("--allow-destination-write")
    result = replay(command, plan, model_usage)
    sys.stdout.buffer.write(
        (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode()
    )
    return 0 if result["code"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
