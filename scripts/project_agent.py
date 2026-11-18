"""Operator-controlled model adapter with project-scoped previews and writes."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from project_workflow import invoke

WRITE_OPERATIONS = frozenset(
    {
        "resource.field.set",
        "scene.field.set",
        "scene.entity.apply",
        "scene.tile.set",
        "scene.palette.set",
    }
)


@dataclass(frozen=True)
class Limits:
    model_calls: int
    tokens: int
    writes: int


class Agent:
    def __init__(self, cli, project, adapters, cwd, limits):
        self.command = [str(cli), "project", "request", str(project)]
        self.adapters = adapters
        self.cwd = cwd
        self.limits = limits
        self.backend = next(iter(adapters))
        self.grants = set()
        self.paused = False
        self.human = False
        self.proposal = None
        self.model_calls = 0
        self.tokens = 0
        self.writes = 0

    def status(self):
        return {
            "adapter": self.backend,
            "permissions": sorted(self.grants),
            "paused": self.paused,
            "owner": "human" if self.human else "agent",
            "pending": self.proposal is not None,
            "usage": {
                "model_calls": self.model_calls,
                "tokens": self.tokens,
                "writes": self.writes,
            },
            "limits": {
                "model_calls": self.limits.model_calls,
                "tokens": self.limits.tokens,
                "writes": self.limits.writes,
            },
        }

    def project(self, operation, mode, arguments=None, revision=None):
        command = self.command + (["--allow-project-write"] if mode == "apply" else [])
        request = {
            "operation": operation,
            "version": 1,
            "mode": mode,
            "input": arguments or {},
        }
        if revision is not None:
            request["expected_revision"] = revision
        return invoke(command, request)

    def propose(self, task):
        if not isinstance(task, str) or not task.strip() or len(task) > 2048:
            return {"code": "invalid_task"}
        self.proposal = None
        if (
            self.model_calls >= self.limits.model_calls
            or self.tokens >= self.limits.tokens
        ):
            return {"code": "budget_exhausted"}
        index = self.project("project.index", "read")
        if index["code"] != "ok":
            return {"code": "project_unavailable", "result": index}
        request = {
            "task": task,
            "operations": sorted(WRITE_OPERATIONS),
            "project": {
                "id": index["project_id"],
                "revision": index["revision"],
                "scene": index.get("scene"),
            },
            "max_total_tokens": self.limits.tokens - self.tokens,
        }
        self.model_calls += 1
        try:
            process = subprocess.run(
                self.adapters[self.backend],
                cwd=self.cwd,
                capture_output=True,
                check=False,
                input=json.dumps(request, ensure_ascii=False).encode(),
                timeout=15,
            )
            if process.returncode or len(process.stdout) > 64 * 1024:
                return {"code": "adapter_failed"}
            response = json.loads(process.stdout)
            usage = response["usage"]
            counts = [usage["input_tokens"], usage["output_tokens"]]
            if any(type(value) is not int or value < 0 for value in counts):
                return {"code": "invalid_usage"}
            self.tokens += sum(counts)
            if self.tokens > self.limits.tokens:
                return {"code": "budget_exhausted"}
            suggestion = response["proposal"]
            if (
                not isinstance(suggestion, dict)
                or set(suggestion) != {"operation", "input"}
                or not isinstance(suggestion["operation"], str)
                or suggestion["operation"] not in WRITE_OPERATIONS
                or not isinstance(suggestion["input"], dict)
            ):
                return {"code": "invalid_proposal"}
        except (OSError, subprocess.TimeoutExpired, ValueError, KeyError, TypeError):
            return {"code": "adapter_failed"}
        preview = self.project(
            suggestion["operation"], "preview", suggestion["input"], index["revision"]
        )
        if preview["code"] != "ok":
            return {"code": "preview_failed", "result": preview}
        self.proposal = {**suggestion, "revision": index["revision"]}
        return {"code": "ok", "proposal": self.proposal, "preview": preview}

    def commit(self):
        if not self.grants:
            return {"code": "permission_denied"}
        if self.proposal is None:
            return {"code": "no_proposal"}
        proposal = self.proposal
        if proposal["operation"] not in self.grants:
            return {"code": "permission_denied"}
        if self.writes >= self.limits.writes or self.tokens > self.limits.tokens:
            return {"code": "budget_exhausted"}
        self.proposal = None
        self.writes += 1
        result = self.project(
            proposal["operation"], "apply", proposal["input"], proposal["revision"]
        )
        return {"code": result["code"], "result": result}

    def control(self, message):
        if not isinstance(message, dict):
            return {"code": "invalid_message"}
        event = message.get("type")
        if event == "status":
            return {"code": "ok"}
        if event == "handoff":
            self.human, self.paused = True, True
            self.grants.clear()
            self.proposal = None
            return {"code": "ok", "project": self.project("project.index", "read")}
        if event == "return":
            self.human, self.paused = False, False
            return {"code": "ok"}
        if self.human:
            return {"code": "human_control"}
        if event == "pause":
            self.paused = True
        elif event == "resume":
            self.paused = False
        elif event == "revoke":
            self.grants.clear()
            self.proposal = None
        elif event == "grant":
            operations = message.get("operations")
            if not isinstance(operations, list) or any(
                not isinstance(item, str) or item not in WRITE_OPERATIONS
                for item in operations
            ):
                return {"code": "invalid_permissions"}
            self.grants = set(operations)
        elif event == "switch":
            name = message.get("adapter")
            if not isinstance(name, str) or name not in self.adapters:
                return {"code": "unknown_adapter"}
            self.backend = name
            self.proposal = None
        elif event == "propose" or event == "commit":
            if self.paused:
                return {"code": "paused"}
            return (
                self.propose(message.get("task"))
                if event == "propose"
                else self.commit()
            )
        else:
            return {"code": "invalid_message"}
        return {"code": "ok"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "project", "adapters"):
        parser.add_argument(
            f"--{name}", type=lambda raw: Path(raw).resolve(), required=True
        )
    parser.add_argument("--max-model-calls", type=int, default=4)
    parser.add_argument("--max-tokens", type=int, default=1000)
    parser.add_argument("--max-writes", type=int, default=1)
    options = parser.parse_args()
    limits = Limits(options.max_model_calls, options.max_tokens, options.max_writes)
    if min(limits.model_calls, limits.tokens, limits.writes) < 0:
        parser.error("Budgets must be nonnegative.")
    try:
        adapters = json.loads(options.adapters.read_bytes())
        if (
            not isinstance(adapters, dict)
            or not adapters
            or len(adapters) > 16
            or any(
                not isinstance(command, list)
                or not command
                or len(command) > 32
                or any(
                    not isinstance(part, str) or not part or "\0" in part
                    for part in command
                )
                for command in adapters.values()
            )
        ):
            parser.error("Adapters must be named executable argument lists.")
    except (OSError, ValueError) as error:
        parser.error(str(error))
    session = Agent(
        options.cli, options.project, adapters, options.adapters.parent, limits
    )
    while line := sys.stdin.buffer.readline(16 * 1024 + 1):
        if len(line) > 16 * 1024:
            return 2
        try:
            message = json.loads(line)
        except ValueError:
            result = {"code": "invalid_message"}
        else:
            result = session.control(message)
        result["state"] = session.status()
        sys.stdout.buffer.write(
            (json.dumps(result, ensure_ascii=False) + "\n").encode()
        )
        sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
