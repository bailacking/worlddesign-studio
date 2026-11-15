"""Validate repository Markdown links and the offline architecture page."""

import re
import subprocess
import sys
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
from tempfile import TemporaryDirectory
from urllib.parse import unquote, urlsplit

from markdown_it import MarkdownIt

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
PAGE = DOCS / "architecture.html"
CSS_URL = re.compile(r"url\(\s*([^)]*)\)", re.IGNORECASE)
CSS_IMPORT = re.compile(r"@import\b", re.IGNORECASE)


def has_external_css(css):
    return bool(CSS_IMPORT.search(css)) or any(
        not match.strip().strip("\"'").startswith(("data:", "#"))
        for match in CSS_URL.findall(css)
    )


def local_link_path(source, target):
    url = urlsplit(target)
    if url.scheme or url.netloc or not url.path:
        return None
    path = unquote(url.path)
    return (
        ROOT / path.lstrip("/") if path.startswith("/") else source.parent / path
    ).resolve()


def local_link_error(source, target):
    destination = local_link_path(source, target)
    if destination is None:
        return None
    if not destination.is_relative_to(ROOT):
        return f"链接超出仓库：{target}"
    if not destination.exists():
        return f"链接目标不存在：{target}"
    return None


class OfflinePage(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = Counter()
        self.anchors = []
        self.errors = []
        self.scripts = []
        self.inline_tag = None

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if attributes.get("id"):
            self.ids[attributes["id"]] += 1
        href = attributes.get("href") or attributes.get("xlink:href") or ""
        if tag == "a" and href.startswith("#") and len(href) > 1:
            self.anchors.append(unquote(href[1:]))

        resources = [attributes.get(name) for name in ("src", "poster", "background")]
        if tag != "a":
            resources.append(href)
        if tag == "object":
            resources.append(attributes.get("data"))
        if any(value and not value.startswith(("data:", "#")) for value in resources):
            self.errors.append(f"{self.getpos()[0]}: <{tag}> 依赖外部文件")
        if attributes.get("srcset") or attributes.get("srcdoc"):
            self.errors.append(f"{self.getpos()[0]}: <{tag}> 包含嵌入文档或候选资源")
        if has_external_css(attributes.get("style") or ""):
            self.errors.append(f"{self.getpos()[0]}: 内联样式包含外部资源")

        if tag in {"script", "style"}:
            self.inline_tag = tag
        if tag == "script":
            self.scripts.append("")

    def handle_endtag(self, tag):
        if tag == self.inline_tag:
            self.inline_tag = None

    def handle_data(self, data):
        if self.inline_tag == "script":
            self.scripts[-1] += data
        if self.inline_tag == "style" and has_external_css(data):
            self.errors.append(f"{self.getpos()[0]}: 样式表包含外部资源")


def check_markdown():
    parser = MarkdownIt("commonmark").enable("table")
    files = sorted(
        [*ROOT.glob("*.md"), *DOCS.rglob("*.md"), *(ROOT / ".github").rglob("*.md")]
    )
    errors = []
    docs_files = {path for path in files if path.is_relative_to(DOCS)}
    links = {path: set() for path in docs_files}
    count = 0
    for path in files:
        for token in parser.parse(path.read_text(encoding="utf-8")):
            for child in token.children or []:
                target = child.attrGet("href") if child.type == "link_open" else None
                if child.type == "image":
                    target = child.attrGet("src")
                if target is None:
                    continue
                count += 1
                error = local_link_error(path, target)
                if error:
                    line = token.map[0] + 1
                    errors.append(f"{path.relative_to(ROOT)}:{line}: {error}")
                elif child.type == "link_open" and path in docs_files:
                    destination = local_link_path(path, target)
                    if destination in docs_files:
                        links[path].add(destination)

    reachable = set()
    pending = [DOCS / "README.md"]
    while pending:
        path = pending.pop()
        if path not in reachable:
            reachable.add(path)
            pending.extend(links.get(path, ()))
    errors.extend(
        f"{path.relative_to(ROOT)}: 从文档中心无法找到此页，请链接到所属主题或移除"
        for path in sorted(docs_files - reachable)
    )
    print(f"已扫描 {len(files)} 份 Markdown 的 {count} 处链接和图片引用。", flush=True)
    return errors


def check_offline_page():
    page = OfflinePage()
    page.feed(PAGE.read_text(encoding="utf-8"))
    page.close()
    errors = list(page.errors)
    errors.extend(f"重复 ID：{name}" for name, count in page.ids.items() if count > 1)
    errors.extend(
        f"页内锚点不存在：#{name}" for name in page.anchors if name not in page.ids
    )

    with TemporaryDirectory() as tmp:
        for index, source in enumerate(page.scripts, start=1):
            script = Path(tmp) / f"inline-{index}.js"
            script.write_text(source, encoding="utf-8")
            result = subprocess.run(
                ["node", "--check", str(script)], capture_output=True, text=True
            )
            if result.returncode:
                errors.append(f"第 {index} 个脚本存在语法错误\n{result.stderr.strip()}")
    return [f"{PAGE.relative_to(ROOT)}: {error}" for error in errors]


def main():
    errors = check_markdown() + check_offline_page()
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("文档入口、本地链接、离线页面资源、页内锚点和脚本语法检查通过。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
