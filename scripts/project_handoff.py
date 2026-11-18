"""Build a Markdown handoff summary for a WDS project through the CLI."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

EXIT_ERROR = 1
EXIT_CONFLICT = 2
EMPTY_CELL = "—"
ENTITY_FIELDS = ("x", "y", "vx", "vy")


class HandoffError(Exception):
    """Raised when the project cannot be described through the CLI."""

    def __init__(self, message: str, code: str = "") -> None:
        super().__init__(message)
        self.code = code


def run_cli(cli: Path, *arguments: str) -> dict[str, Any]:
    """Run one CLI query and return its JSON object when the call succeeds."""
    label = " ".join(arguments)
    try:
        completed = subprocess.run(
            [str(cli), *arguments],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise HandoffError(f"{label} 超时") from error
    except OSError as error:
        reason = error.strerror or str(error)
        raise HandoffError(f"无法执行 {cli}：{reason}") from error
    output = completed.stdout.strip()
    if not output:
        detail = completed.stderr.strip() or f"退出码 {completed.returncode}"
        raise HandoffError(f"{label} 无输出：{detail}")
    try:
        result = json.loads(output)
    except json.JSONDecodeError as error:
        raise HandoffError(f"{label} 输出不是 JSON：{error}") from error
    if not isinstance(result, dict):
        raise HandoffError(f"{label} 返回的不是 JSON 对象")
    code = result.get("code")
    if code != "ok" or completed.returncode != 0:
        location = result.get("location") or "/"
        raise HandoffError(f"{label} 失败：{code} at {location}", str(code))
    return result


def load_project(cli: Path, project: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    """Read the resource list, then the entry scene at the revision it reported."""
    resources = run_cli(cli, "project", "resources", str(project))
    revision = resources.get("revision")
    if not isinstance(revision, int) or isinstance(revision, bool):
        raise HandoffError("project resources 未返回修订号")
    scene = run_cli(cli, "project", "scene", str(project), str(revision))
    if scene.get("revision") != revision:
        raise HandoffError(
            f"查询期间修订已变化：资源 {revision}，场景 {scene.get('revision')}",
            "revision_conflict",
        )
    return resources, scene


def cell(value: Any) -> str:
    """Render one Markdown table cell, escaping the column separator."""
    text = EMPTY_CELL if value is None or value == "" else str(value)
    return text.replace("|", "\\|")


def resource_rows(resources: dict[str, Any]) -> list[dict[str, Any]]:
    entries = resources.get("resources")
    if not isinstance(entries, list):
        raise HandoffError("project resources 未返回资源列表")
    return [entry for entry in entries if isinstance(entry, dict)]


def entity_rows(scene: dict[str, Any]) -> list[dict[str, Any]]:
    if scene.get("kind") == "voxel":
        return []
    entries = scene.get("entities")
    if not isinstance(entries, list):
        raise HandoffError("project scene 未返回实体列表")
    return [entry for entry in entries if isinstance(entry, dict)]


def render(resources: dict[str, Any], scene: dict[str, Any]) -> str:
    """Render the summary without touching the project or the CLI."""
    entries = resource_rows(resources)
    entities = entity_rows(scene)
    project_id = cell(resources.get("project_id"))
    revision = cell(resources.get("revision"))
    scene_id = cell(scene.get("scene_id") or resources.get("entry_scene"))
    lines = [
        "# 项目交接摘要",
        "",
        f"- 项目 ID：`{project_id}`",
        f"- 修订：{revision}",
        f"- 入口场景：`{scene_id}`",
        f"- 资源：{len(entries)} 项",
        f"- 实体：{len(entities)} 个",
        "",
        "## 资源",
        "",
        "| ID | 类型 | 路径 | 引用 |",
        "| --- | --- | --- | --- |",
    ]
    for entry in entries:
        refs = entry.get("refs")
        declared = [str(ref) for ref in refs] if isinstance(refs, list) else []
        lines.append(
            f"| {cell(entry.get('id'))} | {cell(entry.get('kind'))} "
            f"| {cell(entry.get('path'))} | {cell('、'.join(declared) or None)} |"
        )
    lines.extend(["", "## 实体", ""])
    if scene.get("kind") == "voxel":
        lines.append("入口为 3D 方块场景。")
    elif not entities:
        lines.append("入口场景没有实体。")
    else:
        header = " | ".join(("ID", *[field.upper() for field in ENTITY_FIELDS]))
        divider = " | ".join(["---"] * (len(ENTITY_FIELDS) + 1))
        lines.extend([f"| {header} |", f"| {divider} |"])
        for entry in entities:
            values = " | ".join(cell(entry.get(field, 0)) for field in ENTITY_FIELDS)
            lines.append(f"| {cell(entry.get('id'))} | {values} |")
    lines.extend(
        [
            "",
            "由 `wds_cli project resources` 与 `project scene` 生成，只读取项目，不写盘。",
        ]
    )
    return "\n".join(lines)


def build(cli: Path, project: Path) -> str:
    """Query the project and render its handoff summary."""
    resources, scene = load_project(cli, project)
    return render(resources, scene)


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="用 wds_cli 生成 Markdown 项目交接摘要。",
    )
    parser.add_argument(
        "--cli", required=True, type=Path, help="wds_cli 可执行文件路径"
    )
    parser.add_argument("--project", required=True, type=Path, help="项目目录")
    parser.add_argument("--output", type=Path, help="写入文件，省略则打印到标准输出")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    try:
        summary = build(arguments.cli, arguments.project)
    except HandoffError as error:
        print(f"交接摘要失败：{error}", file=sys.stderr)
        return EXIT_CONFLICT if error.code == "revision_conflict" else EXIT_ERROR
    text = summary + "\n"
    if arguments.output:
        try:
            arguments.output.write_text(text, encoding="utf-8")
        except OSError as error:
            print(
                f"交接摘要失败：无法写入 {arguments.output}（{error}）", file=sys.stderr
            )
            return EXIT_ERROR
    else:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
