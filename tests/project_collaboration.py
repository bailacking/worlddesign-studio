"""Merge two local worktrees while preserving distinct WDS resource identities."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"
SCENE = Path("assets/field.json")
ART = Path("assets/art.png")
MANIFEST = "wds.project.json"


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def put(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def test(cli, git_binary, root):
    left = root / "left"
    right = root / "right"
    baseline = root / "baseline"
    hooks = root / "empty-hooks"
    hooks.mkdir()
    shutil.copytree(EXAMPLES / "scene-editing", left)
    png = EXAMPLES / "reef-delivery" / "assets"
    shutil.copyfile(png / "courier.png", left / ART)
    manifest = read(left / MANIFEST)
    manifest["resources"][0]["refs"].append("art")
    manifest["resources"].append({
        "id": "art", "kind": "image", "path": ART.as_posix(), "refs": [],
    })
    put(left / MANIFEST, manifest)
    shutil.copytree(left, baseline)

    def git(repo, *args, expected=0):
        result = subprocess.run([
            git_binary, "-c", "user.name=WDS Fixture", "-c", "user.email=fixture@example.invalid",
            "-c", f"core.hooksPath={hooks}", "-c", "commit.gpgsign=false",
            "-C", repo, *args,
        ], capture_output=True, timeout=20, check=False)
        assert result.returncode == expected, result
        return result.stdout

    def request(operation, values, revision, mode="read", expected="ok", *, project=baseline):
        request_data = {"version": 1, "operation": operation, "input": values,
                        "expected_revision": revision, "mode": mode}
        access = ["--allow-project-write"] if mode == "apply" else []
        result = subprocess.run([cli, "project", "request", project, *access],
                                input=json.dumps(request_data), encoding="utf-8",
                                capture_output=True, timeout=20, check=False)
        reply = json.loads(result.stdout)
        assert reply["code"] == expected and result.returncode == (expected != "ok"), reply
        return reply

    git(left, "init", "-q")
    git(left, "add", ".")
    git(left, "commit", "-qm", "Shared scene, rules and binary asset")
    git(left, "worktree", "add", "-qb", "collaborator", right)

    edited_scene = read(left / SCENE)
    edited_scene["entities"][1]["vx"] = 11
    put(left / SCENE, edited_scene)
    shutil.copyfile(png / "hazard.png", left / ART)
    git(left, "add", ".")
    git(left, "commit", "-qm", "Change scene speed and image")

    manifest["revision"] = 1
    new_rules = "assets/rules.json"
    manifest["resources"][1]["path"] = new_rules
    git(right, "mv", "assets/tuning.json", new_rules)
    rules = read(right / new_rules)
    rules["steps_to_converge"] = 24
    put(right / new_rules, rules)
    put(right / MANIFEST, manifest)
    shutil.copyfile(png / "landing.png", right / ART)
    git(right, "add", ".")
    git(right, "commit", "-qm", "Move and edit rules and change image")

    git(left, "merge", "collaborator", "--no-edit", expected=1)
    assert git(left, "diff", "--name-only", "--diff-filter=U").decode().strip() == ART.as_posix()
    ours = git(left, "show", f":2:{ART.as_posix()}")
    theirs = git(left, "show", f":3:{ART.as_posix()}")
    assert ours != theirs
    other_art = "assets/collaborator-art.png"
    (left / ART).write_bytes(ours)
    (left / other_art).write_bytes(theirs)
    merged = read(left / MANIFEST)
    merged["revision"] = 2
    merged["resources"][0]["refs"].append("collaborator-art")
    merged["resources"].append({
        "id": "collaborator-art", "kind": "image", "path": other_art, "refs": [],
    })
    put(left / MANIFEST, merged)
    git(left, "add", ".")
    git(left, "commit", "-qm", "Preserve both image versions and validate merged project")
    assert not git(left, "status", "--porcelain")
    inspected = subprocess.run([cli, "project", "open", left], capture_output=True,
                               check=True, timeout=20)
    assert json.loads(inspected.stdout)["project_id"] == "scene-editing"
    assert (left / ART).read_bytes() == ours and (left / other_art).read_bytes() == theirs

    comparison = {"other_directory": str(left), "other_revision": 2}
    changes = request("project.diff", comparison, 0)
    by_id = {row["id"]: row for row in changes["resources"]}
    assert by_id["field"]["fields"] == [{"path": "/entities/drifter/vx", "kind": "changed"}]
    assert by_id["tuning"]["fields"] == [{"path": "/steps_to_converge", "kind": "changed"}]
    assert by_id["tuning"]["path"] == {"before": "assets/tuning.json", "after": new_rules}
    assert by_id["art"]["content"] == "bytes" and by_id["collaborator-art"]["kind"] == "added"
    request("project.diff", {**comparison, "other_revision": 1}, 0, expected="revision_conflict")
    assert not git(left, "status", "--porcelain")

    relocated = root / "relocated"
    git(left, "worktree", "move", right, relocated)
    moved = request("project.diff", {"other_directory": str(relocated), "other_revision": 1}, 0)
    moved_rules = next(row for row in moved["resources"] if row["id"] == "tuning")
    assert moved_rules["path"]["after"] == new_rules and (relocated / new_rules).exists()

    old_scene = (left / SCENE).read_bytes()
    unintentionally_changed = request(
        "scene.field.set", {"entity_id": "drifter", "field": "vx",
                            "value": 17, "capture_change": True},
        2, "apply", project=left,
    )
    assert unintentionally_changed["revision"] == 3
    undo = unintentionally_changed["scene_change"]
    undo["before"], undo["after"] = undo["after"], undo["before"]
    restored = request("scene.restore", {"change": undo}, 3, "apply", project=left)
    assert restored["revision"] == 4 and (left / SCENE).read_bytes() == old_scene
    assert (left / ART).read_bytes() == ours and (left / other_art).read_bytes() == theirs
    print("Two Git worktrees, semantic merge, both binary versions, relocation and scene recovery")


if __name__ == "__main__":
    cli_path, git_path, work_path = map(Path, sys.argv[1:])
    work_path.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="w31-", dir=work_path) as folder:
        test(cli_path.resolve(), git_path.resolve(), Path(folder))
