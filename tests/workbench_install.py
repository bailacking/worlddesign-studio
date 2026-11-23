"""Install and relocate a standalone workbench, then edit a project copy."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def isolated_environment(sdk):
    environment = os.environ.copy()
    for key in (
        "QT_PLUGIN_PATH",
        "QT_QPA_PLATFORM_PLUGIN_PATH",
        "LD_LIBRARY_PATH",
        "DYLD_LIBRARY_PATH",
        "DYLD_FRAMEWORK_PATH",
    ):
        environment.pop(key, None)
    environment["PATH"] = os.pathsep.join(
        entry
        for entry in environment.get("PATH", "").split(os.pathsep)
        if entry and not Path(entry).resolve().is_relative_to(sdk)
    )
    environment["QT_QPA_PLATFORM"] = "offscreen"
    return environment


def verify(args):
    build = args.build.resolve(strict=True)
    sdk = args.qt_config.resolve(strict=True).parents[2]
    example = args.source / "scene-editing"
    source = example / "assets" / "field.json"
    original = source.read_bytes()
    with tempfile.TemporaryDirectory(prefix="workbench-install-", dir=build) as scratch:
        root = Path(scratch).resolve()
        stage = root / "staging"
        installed = root / "relocated workbench"
        subprocess.run(
            [args.cmake, "--install", build, "--component", "Workbench", "--prefix", stage.name,
             "--config", args.config],
            cwd=root,
            check=True,
            timeout=90,
        )
        if not stage.resolve(strict=True).is_relative_to(root):
            raise RuntimeError("Installation escaped its test directory")
        stage.rename(installed)
        folder = (installed / "wds_studio_modern.app" / "Contents" / "MacOS"
                  if sys.platform == "darwin" else installed / args.bindir)
        suffix = ".exe" if os.name == "nt" else ""
        studio, cli = (folder / (name + suffix) for name in ("wds_studio_modern", "wds_cli"))
        project = root / "editable copy"
        shutil.copytree(example, project)
        environment = isolated_environment(sdk)
        subprocess.run(
            [studio, "--smoke", args.source / "voxel-workyard", root / "workyard.png"],
            cwd=root, env=environment, check=True, timeout=30,
        )
        subprocess.run([studio, "--smoke-edit", project], cwd=root, env=environment,
                       check=True, timeout=30)
        result = subprocess.run(
            [cli, "project", "request", project], cwd=root, env=environment,
            input=json.dumps({
                "version": 1, "operation": "scene.inspect", "mode": "read",
                "expected_revision": 1, "input": {},
            }), encoding="utf-8", check=True, capture_output=True, timeout=10,
        )
        state = json.loads(result.stdout)
        assert state["revision"] == 1
        assert next(entity for entity in state["entities"] if entity["id"] == "drifter")["vx"] == 31
        assert source.read_bytes() == original
    print("Relocated workbench opened a voxel scene, saved an inspector edit and reopened revision 1")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--qt-config", required=True, type=Path)
    parser.add_argument("--bindir", required=True)
    parser.add_argument("--config", required=True)
    verify(parser.parse_args())
