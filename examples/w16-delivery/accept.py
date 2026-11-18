"""Exercise both Windows delivery flows and a restricted agent session."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from contextlib import contextmanager

LAB = Path(__file__).resolve().parent
AGENT = LAB.parents[1] / "scripts" / "project_agent.py"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, request=None, expected="ok"):
    result = subprocess.run(
        args,
        input=json.dumps(request).encode() if request is not None else None,
        capture_output=True,
        timeout=30,
        check=False,
    )
    receipt = (
        json.loads(result.stdout)
        if result.stdout
        else {"code": "ok" if result.returncode == 0 else "failed"}
    )
    assert receipt["code"] == expected and result.returncode == (
        0 if expected == "ok" else 1
    ), (
        args,
        receipt,
        result.stderr,
    )
    return receipt


class Project:
    def __init__(self, cli, folder, identity):
        self.cli = cli
        self.folder = folder
        self.identity = identity

    def request(self, operation, mode, arguments=None, revision=None, expected="ok"):
        command = [self.cli, "project", "request", self.folder]
        if mode == "apply":
            command.append(
                "--allow-destination-write"
                if operation == "project.export"
                else "--allow-project-write"
            )
        body = {
            "operation": operation,
            "version": 1,
            "mode": mode,
            "input": arguments or {},
        }
        if revision is not None:
            body["expected_revision"] = revision
        return run(command, body, expected=expected)

    @property
    def source(self):
        return self.folder / "assets" / "intro.json"

    def replace(self, scene, revision):
        change = {
            "project_id": self.identity,
            "scene_id": "intro",
            "resource_path": "assets/intro.json",
            "before": self.source.read_bytes().decode(),
            "after": json.dumps(scene, indent=2) + "\n",
        }
        return self.request("scene.restore", "apply", {"change": change}, revision)

    def export(self, revision, destination):
        receipt = self.request(
            "project.export",
            "apply",
            {"target": "windows-x64", "destination": str(destination)},
            revision,
        )
        exe = destination / "Play.exe"
        env = os.environ.copy()
        env["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
        verified = subprocess.run(
            [exe, "--check"], cwd=destination.parent, env=env, timeout=10, check=False
        )
        assert verified.returncode == 0
        receipt["player_sha256"] = digest(exe)
        return receipt

    def fork(self, destination):
        shutil.copytree(self.folder, destination)
        return Project(self.cli, destination, self.identity)


@contextmanager
def agent(project, output):
    config = output / "adapters.json"
    config.write_text(
        json.dumps(
            {
                name: [sys.executable, str(LAB / "offline_model.py"), name]
                for name in ("east", "west")
            }
        ),
        encoding="utf-8",
    )
    process = subprocess.Popen(
        [
            sys.executable,
            AGENT,
            "--cli",
            project.cli,
            "--project",
            project.folder,
            "--adapters",
            config,
            "--max-model-calls",
            "3",
            "--max-tokens",
            "70",
            "--max-writes",
            "1",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:

        def control(event, expected="ok", **fields):
            process.stdin.write((json.dumps({"type": event, **fields}) + "\n").encode())
            process.stdin.flush()
            line = process.stdout.readline()
            assert line, process.stderr.read().decode()
            result = json.loads(line)
            assert result["code"] == expected, result
            return result

        yield control
    finally:
        process.stdin.close()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
        process.stdout.close()
        process.stderr.close()


def agent_walkthrough(project, root):
    before = digest(project.source)
    receipts = []
    op = ["scene.field.set"]
    with agent(project, root) as send:

        def control(event, expected="ok", **fields):
            receipt = send(event, expected, **fields)
            receipts.append({"event": event, "response": receipt})
            return receipt

        control("grant", "invalid_permissions", operations=["project.export"])
        control("propose", task="Move the runner start to x=-112")
        control("commit", "permission_denied")
        control("grant", operations=op)
        control("pause")
        control("commit", "paused")
        assert digest(project.source) == before
        control("resume")
        assert control("commit")["result"]["revision"] == 2
        moved = digest(project.source)
        control("switch", adapter="west")
        control("propose", task="Move the runner start to x=-104")
        control("commit", "budget_exhausted")
        control("revoke")
        control("commit", "permission_denied")
        control("propose", "budget_exhausted", task="Try another restricted write")
        assert digest(project.source) == moved
        assert control("handoff")["project"]["revision"] == 2
        control("commit", "human_control")
    (root / "agent-receipts.json").write_text(
        json.dumps(receipts, indent=2) + "\n", encoding="utf-8"
    )
    return receipts[-1]["response"]["state"]


def planar(cli, root):
    project = Project(cli, root / "平面初稿", "signal-relay")
    assert (
        project.request("project.create", "apply", {"project_id": project.identity})[
            "revision"
        ]
        == 0
    )
    scene = json.loads((LAB / "planar.json").read_bytes())
    assert project.replace(scene, 0)["revision"] == 1
    advanced = project.request("project.play", "read", {"ticks": 6, "keys": ["D"]}, 1)
    assert advanced["entities"][0]["x"] == -84
    state = agent_walkthrough(project, root)
    initial = project.export(2, root / "风标接力 初版")
    continued = project.fork(root / "风标接力 工程")
    edit = {"entity_id": "runner", "field": "x", "value": -100}
    assert continued.request("scene.field.set", "apply", edit, 2)["revision"] == 3
    final = continued.export(3, root / "风标接力 玩家版")
    return {
        "agent": state,
        "play_x": -84,
        "before_handoff": initial,
        "after_handoff": final,
    }


def voxel(cli, root):
    project = Project(cli, root / "方块初稿", "slope-camp")
    assert (
        project.request(
            "project.create", "apply", {"project_id": project.identity, "kind": "voxel"}
        )["revision"]
        == 0
    )
    scene = json.loads(project.source.read_bytes())
    scene["world"] = "wds-voxel-seeded-v1\n12 3 14 3\n13 3 14 3\n"
    assert project.replace(scene, 0)["revision"] == 1
    start = project.request("scene.inspect", "read", {}, 1)
    advanced = project.request("project.play", "read", {"ticks": 30, "keys": ["D"]}, 1)
    assert advanced["position"][0] > 12.5
    before = digest(project.source)
    preview = project.request(
        "scene.palette.set", "preview", {"provider": "amber-dusk"}, 1
    )
    assert preview["changed"] and digest(project.source) == before
    swap = project.request("scene.palette.set", "apply", {"provider": "amber-dusk"}, 1)
    dusk = project.request("scene.inspect", "read", {}, 2)
    assert dusk["palette"]["sky"] != start["palette"]["sky"]
    project.request(
        "scene.palette.set",
        "apply",
        {"provider": "missing"},
        2,
        expected="invalid_palette_plugin",
    )
    assert project.request("scene.inspect", "read", {}, 2)["palette"] == dusk["palette"]
    change = swap["scene_change"]
    change["before"], change["after"] = change["after"], change["before"]
    assert (
        project.request("scene.restore", "apply", {"change": change}, 2)["revision"]
        == 3
    )
    restored = project.request("scene.inspect", "read", {}, 3)
    assert digest(project.source) == before and restored["palette"] == start["palette"]
    initial = project.export(3, root / "坡地营地 初版")
    continued = project.fork(root / "坡地营地 工程")
    scene["world"] += "14 3 14 3\n"
    assert continued.replace(scene, 3)["revision"] == 4
    final = continued.export(4, root / "坡地营地 玩家版")
    return {
        "play_position": advanced["position"],
        "palettes": [start["palette"], dusk["palette"], restored["palette"]],
        "before_handoff": initial,
        "after_handoff": final,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "output"):
        parser.add_argument(
            f"--{name}", type=lambda value: Path(value).resolve(), required=True
        )
    args = parser.parse_args()
    args.output.mkdir(parents=True)
    report = {
        "planar": planar(args.cli, args.output),
        "voxel": voxel(args.cli, args.output),
    }
    (args.output / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(args.output / "report.json")


if __name__ == "__main__":
    main()
