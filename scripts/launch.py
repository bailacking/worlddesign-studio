"""Open the WDS workbench, a voxel scene, or the Weather Garden preview."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build_program(args: argparse.Namespace, name: str) -> Path:
    return args.build / (name + (".exe" if os.name == "nt" else ""))


def workbench_environment(args: argparse.Namespace, program: Path) -> dict[str, str]:
    environment = os.environ.copy()
    if os.name != "nt" or (program.parent / "Qt6Core.dll").is_file():
        return environment
    qt = args.qt_root
    cache = args.build / "CMakeCache.txt"
    if qt is None and cache.is_file():
        prefix = "Qt6_DIR:PATH="
        for line in cache.read_text(encoding="utf-8").splitlines():
            if line.startswith(prefix):
                cmake_dir = Path(line.removeprefix(prefix))
                if len(cmake_dir.parents) >= 3:
                    qt = cmake_dir.parents[2]
                break
    if qt is not None and (qt / "bin" / "Qt6Core.dll").is_file():
        existing_path = environment.get("PATH", "")
        environment["PATH"] = os.pathsep.join([str(qt / "bin"), existing_path])
        environment["QT_PLUGIN_PATH"] = str(qt / "plugins")
    return environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("studio", "voxel", "planar"))
    parser.add_argument("--build", type=Path, default=ROOT / "build" / "native")
    parser.add_argument("--qt-root", type=Path)
    parser.add_argument("--project", type=Path, help="project folder to open")
    parser.add_argument("--package", type=Path, help="existing Windows package")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true", help="print URL only")
    parser.add_argument("--check", action="store_true", help="check paths")
    args = parser.parse_args()
    args.build = args.build.resolve()
    if args.mode == "studio" and args.package:
        parser.error("--package requires voxel or planar")
    if args.project and (args.package or args.mode == "planar"):
        parser.error("--project requires studio or voxel without --package")
    if args.package and os.name != "nt":
        parser.error("--package requires Windows")
    if not 0 <= args.port <= 65535:
        parser.error("--port must be from 0 to 65535")

    environment = None
    if args.package:
        package = args.package.resolve()
        program = package / "Play.exe"
        if not (package / "wds.release.json").is_file():
            parser.error(f"Windows package manifest not found in {package}")
        command = [str(program)]
        directory = package
    elif args.mode == "planar":
        program = build_program(args, "wds_cli")
        bridge = ROOT / "examples" / "web-garden" / "serve.py"
        command = [
            sys.executable,
            str(bridge),
            "--cli",
            str(program),
            "--port",
            str(args.port),
        ]
        if not args.no_browser:
            command.append("--open")
        directory = ROOT
    else:
        program = build_program(args, "wds_studio_modern")
        command = [str(program)]
        project = args.project
        if project is None and args.mode == "voxel":
            project = ROOT / "examples" / "voxel-workyard"
        if project:
            project = project.resolve()
            if not (project / "wds.project.json").is_file():
                parser.error(f"Project manifest not found in {project}")
            command.append(str(project))
        environment = workbench_environment(args, program)
        directory = ROOT

    if not program.is_file():
        parser.error(f"Executable not found: {program}")
    if args.check:
        print(subprocess.list2cmdline(command))
        return 0
    try:
        result = subprocess.run(command, cwd=directory, env=environment, check=False)
        return result.returncode
    except KeyboardInterrupt:
        return 130
    except OSError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
