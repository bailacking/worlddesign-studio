from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Any

SOURCE = "wds-content-source-v1"
BUNDLE = "wds-content-bundle-v1"
ID = re.compile(r"[a-z][a-z0-9-]{0,63}\Z")
RULE_FIELDS = {
    "block": {"property", "range"},
    "item": {"name"},
    "recipe": {"output", "ingredients", "layout"},
    "state": {"enabled"},
}


class ContentError(Exception):
    def __init__(self, code: str, location: str, message: str):
        super().__init__(message)
        self.issue = {"code": code, "location": location, "message": message}


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(token: str) -> None:
    raise ValueError(f"Invalid JSON value: {token}")


def read_entries(path: Path, format_name: str) -> dict[str, Any]:
    try:
        doc = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=unique_object,
            parse_constant=reject_constant,
        )
    except (ValueError, RecursionError) as error:
        raise ContentError("invalid_json", "/", f"{path}: {error}") from error
    if (
        not isinstance(doc, dict)
        or set(doc) != {"format", "entries"}
        or doc["format"] != format_name
        or not isinstance(doc["entries"], dict)
    ):
        raise ContentError(
            "invalid_document",
            "/",
            f"{path}: expected {format_name} and an entries object",
        )
    return doc["entries"]


def valid_id(value: Any) -> bool:
    return isinstance(value, str) and ID.fullmatch(value) is not None


def require_ids(values: Any, location: str) -> None:
    if not isinstance(values, list) or not values:
        raise ContentError("invalid_rule", location, "Expected a nonempty list of IDs")
    for index, value in enumerate(values):
        if not valid_id(value):
            raise ContentError(
                "invalid_rule", f"{location}/{index}", "Expected a stable ID"
            )


def variants_for(
    kind: str, rule: dict[str, Any], location: str
) -> list[dict[str, Any]]:
    match kind:
        case "block":
            prop = rule["property"]
            if not valid_id(prop):
                raise ContentError(
                    "invalid_rule", f"{location}/property", "Expected a stable ID"
                )
            bounds = rule["range"]
            if (
                not isinstance(bounds, list)
                or len(bounds) != 2
                or any(type(value) is not int for value in bounds)
                or not -(2**31) <= bounds[0] <= bounds[1] < 2**31
                or bounds[1] - bounds[0] >= 256
            ):
                raise ContentError(
                    "invalid_rule",
                    f"{location}/range",
                    "Expected ascending 32-bit integer bounds with at most 256 values",
                )
            return [{prop: value} for value in range(bounds[0], bounds[1] + 1)]
        case "item":
            name = rule["name"]
            if not isinstance(name, str) or not name.strip():
                raise ContentError(
                    "invalid_rule", f"{location}/name", "Expected a name"
                )
        case "recipe":
            if not valid_id(rule["output"]):
                raise ContentError(
                    "invalid_rule", f"{location}/output", "Expected an item ID"
                )
            require_ids(rule["ingredients"], f"{location}/ingredients")
            if rule["layout"] not in ("row", "shapeless", "partial"):
                raise ContentError(
                    "invalid_rule",
                    f"{location}/layout",
                    "Use row, shapeless, or partial",
                )
        case "state":
            enabled = rule["enabled"]
            require_ids(enabled, f"{location}/enabled")
            if len(set(enabled)) != len(enabled):
                raise ContentError(
                    "invalid_rule", f"{location}/enabled", "State IDs must be unique"
                )
            return [{"id": state, "enabled": True} for state in enabled]
    return []


def expand_entry(entry_id: str, record: Any) -> dict[str, Any]:
    if not valid_id(entry_id):
        raise ContentError("invalid_id", "/entries", f"Invalid entry ID: {entry_id}")
    location = f"/entries/{entry_id}"
    if not isinstance(record, dict) or set(record) != {"kind", "reference", "rule"}:
        raise ContentError(
            "invalid_entry", location, "Expected kind, reference, and rule"
        )
    kind = record["kind"]
    if not isinstance(kind, str) or kind not in RULE_FIELDS:
        raise ContentError(
            "invalid_kind", f"{location}/kind", "Use block, item, recipe, or state"
        )
    reference = record["reference"]
    if not isinstance(reference, str) or not reference.strip():
        raise ContentError(
            "invalid_entry", f"{location}/reference", "Expected a reference ID"
        )
    rule = record["rule"]
    if not isinstance(rule, dict) or set(rule) != RULE_FIELDS[kind]:
        fields = ", ".join(sorted(RULE_FIELDS[kind]))
        raise ContentError(
            "invalid_rule", f"{location}/rule", f"Expected fields: {fields}"
        )
    return {**record, "variants": variants_for(kind, rule, f"{location}/rule")}


def validate_bundle(entries: dict[str, Any]) -> list[dict[str, str]]:
    issues = []
    for entry_id, record in entries.items():
        try:
            if not isinstance(record, dict):
                raise ContentError(
                    "invalid_entry", f"/entries/{entry_id}", "Expected an entry object"
                )
            expected = expand_entry(
                entry_id,
                {key: value for key, value in record.items() if key != "variants"},
            )
            if json.dumps(record.get("variants"), sort_keys=True) != json.dumps(
                expected["variants"], sort_keys=True
            ):
                raise ContentError(
                    "invalid_expansion",
                    f"/entries/{entry_id}/variants",
                    "Variants do not match rule",
                )
        except ContentError as error:
            issues.append(error.issue)
            continue
        if record["kind"] != "recipe":
            continue
        rule = record["rule"]
        references = [("output", rule["output"])]
        references.extend(
            (f"ingredients/{index}", item)
            for index, item in enumerate(rule["ingredients"])
        )
        for field, item in references:
            target = entries.get(item)
            if isinstance(target, dict) and target.get("kind") == "item":
                continue
            issues.append(
                {
                    "code": "missing_reference"
                    if item not in entries
                    else "invalid_reference",
                    "location": f"/entries/{entry_id}/rule/{field}",
                    "message": f"Expected an item entry for {item}",
                }
            )
    return issues


def write_bundle(path: Path, entries: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with NamedTemporaryFile(
            "w", encoding="utf-8", newline="\n", dir=path.parent, delete=False
        ) as stream:
            temporary = Path(stream.name)
            json.dump(
                {"format": BUNDLE, "entries": entries},
                stream,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
            stream.write("\n")
        os.replace(temporary, path)
    except UnicodeEncodeError as error:
        raise ContentError(
            "invalid_json", "/entries", "Text has an unpaired Unicode surrogate"
        ) from error
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def execute(args: argparse.Namespace) -> dict[str, Any]:
    if args.action == "validate":
        entries = read_entries(args.bundle, BUNDLE)
        issues = validate_bundle(entries)
        return (
            {"code": "invalid_content", "errors": issues}
            if issues
            else {"code": "ok", "records": len(entries)}
        )

    inputs = [args.source]
    if args.action == "repair":
        inputs.append(args.base)
    if args.output.resolve() in {path.resolve() for path in inputs}:
        raise ContentError(
            "invalid_output", "/output", "Use a different path from the input files"
        )
    source = read_entries(args.source, SOURCE)
    entries = read_entries(args.base, BUNDLE) if args.action == "repair" else {}
    selected = (
        list(dict.fromkeys(args.ids)) if args.action == "repair" else list(source)
    )
    issues = []
    for entry_id in selected:
        try:
            if entry_id not in source:
                raise ContentError(
                    "missing_entry", "/entries", f"Source does not contain {entry_id}"
                )
            entries[entry_id] = expand_entry(entry_id, source[entry_id])
        except ContentError as error:
            issues.append(error.issue)
    issues.extend(validate_bundle(entries))
    if issues:
        return {"code": "invalid_content", "errors": issues}
    write_bundle(args.output, entries)
    return {"code": "ok", "records": len(entries), "updated": selected}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Expand, validate and repair WDS content bundles"
    )
    commands = parser.add_subparsers(dest="action", required=True)
    expand = commands.add_parser("expand")
    expand.add_argument("source", type=Path)
    expand.add_argument("output", type=Path)
    validate = commands.add_parser("validate")
    validate.add_argument("bundle", type=Path)
    repair = commands.add_parser("repair")
    repair.add_argument("source", type=Path)
    repair.add_argument("base", type=Path)
    repair.add_argument("output", type=Path)
    repair.add_argument("ids", nargs="+")
    try:
        result = execute(parser.parse_args())
    except ContentError as error:
        result = {"code": "invalid_content", "errors": [error.issue]}
    except OSError as error:
        result = {
            "code": "io_error",
            "errors": [{"code": "io_error", "location": "/", "message": str(error)}],
        }
    print(json.dumps(result))
    return 0 if result["code"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
