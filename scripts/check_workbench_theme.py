"""Protect the shared color and stylesheet sources of the Qt workbench."""

import re
import sys
from pathlib import Path

STUDIO = Path(__file__).resolve().parents[1] / "src" / "studio"
THEME = STUDIO / "theme"
TOKEN = re.compile(r"@([a-z][a-z0-9_]*)@")
DEFINITION = re.compile(r'WDS_THEME_COLOR\(([a-z][a-z0-9_]*), "#[0-9a-fA-F]{6}"\)')
STYLE_COLOR = re.compile(r"#[0-9a-fA-F]{3,8}\b|\b(?:rgb|rgba|hsl|hsla)\(")
CODE_COLOR = re.compile(
    r'"#[0-9a-fA-F]{3,8}"|\bQColor\s*\(\s*\d|\bQColor::from(?:Rgb|Hsv|Hsl|Cmyk|String)\('
)


def check():
    errors = []
    known = set()
    for line_number, line in enumerate(
        (THEME / "colors.inc").read_text(encoding="utf-8").splitlines(), 1
    ):
        if not line.strip():
            continue
        match = DEFINITION.fullmatch(line)
        if not match or match[1] in known:
            errors.append(f"theme/colors.inc:{line_number}: 颜色定义不合法或名称重复")
            continue
        known.add(match[1])

    stylesheet = THEME / "workbench.qss"
    used = set(TOKEN.findall(stylesheet.read_text(encoding="utf-8")))
    for name in sorted(used - known):
        errors.append(f"theme/workbench.qss: 未定义的颜色 @{name}@")
    for path in sorted(STUDIO.rglob("*.qss")):
        if path != stylesheet:
            errors.append(
                f"{path.relative_to(STUDIO)}: 样式统一写入 theme/workbench.qss"
            )

    for path in sorted(STUDIO.rglob("*")):
        if path.suffix not in {".cpp", ".hpp", ".qss"} or not path.is_file():
            continue
        source = path.read_text(encoding="utf-8")
        if path.suffix != ".qss":
            used.update(re.findall(r"\bcolor::([a-z][a-z0-9_]*)\b", source))
        for number, line in enumerate(source.splitlines(), 1):
            label = f"{path.relative_to(STUDIO)}:{number}"
            if path.suffix == ".qss" and STYLE_COLOR.search(line):
                errors.append(f"{label}: 使用 colors.inc 中的颜色角色")
            if path.suffix != ".qss" and CODE_COLOR.search(line):
                errors.append(f"{label}: 使用 theme::color 中的颜色角色")
            if path != THEME / "theme.cpp" and re.search(r"\bsetStyleSheet\s*\(", line):
                errors.append(f"{label}: 通过全局工作台主题设置样式")
    for name in sorted(known - used):
        errors.append(f"theme/colors.inc: 颜色角色 {name} 已无引用")
    return errors


def main():
    errors = check()
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("工作台主题检查通过。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
