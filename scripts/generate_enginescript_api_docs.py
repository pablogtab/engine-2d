#!/usr/bin/env python3
"""Generate the current EngineScript API reference from engine_api.json."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "src/enginescript/api/engine_api.json"
OUTPUT = ROOT / "docs/ENGINESCRIPT_API.md"


def parameter_text(parameter: dict) -> str:
    text = f"{parameter['name']}: {parameter['type']}"
    if parameter.get("default", "") != "":
        text += f" = {parameter['default']}"
    elif parameter.get("optional", False):
        text += "?"
    return text


def callable_text(entry: dict, owner: str = "") -> str:
    prefix = f"{owner}." if owner else ""
    parameters = ", ".join(parameter_text(value) for value in entry.get("parameters", []))
    nullable = "?" if entry.get("nullableReturn", False) else ""
    return f"`{prefix}{entry['name']}({parameters}): {entry['returnType']}{nullable}`"


def render(catalog: dict) -> str:
    lines = [
        "# EngineScript API Reference",
        "",
        "This file is generated from `src/enginescript/api/engine_api.json`. It",
        "describes only the current compiler and runtime contract.",
        "",
        "## Contract",
        "",
        f"- Language: `{catalog['languageVersion']}`",
        f"- ABI: `{catalog['abiVersion']}`",
        f"- SDK: `{catalog['sdkHash']}`",
        f"- Compiler cache: `{catalog['compilerCacheId']}`",
        "",
        "## Language features",
        "",
    ]
    for name, value in catalog.get("languageFeatures", {}).items():
        lines.append(f"- `{name}`: `{str(value).lower()}`")

    lines += ["", "## Types", ""]
    for entry in catalog.get("types", []):
        if entry.get("availability", "available") != "available":
            continue
        suffix = f" — {entry['documentation']}" if entry.get("documentation") else ""
        lines.append(f"- `{entry['name']}`{suffix}")

    lines += ["", "## Global functions", ""]
    for entry in catalog.get("functions", []):
        if entry.get("availability", "available") != "available":
            continue
        suffix = f" — {entry['documentation']}" if entry.get("documentation") else ""
        lines.append(f"- {callable_text(entry)}{suffix}")

    lines += ["", "## Properties", ""]
    for entry in catalog.get("properties", []):
        if entry.get("availability", "available") != "available":
            continue
        access = "read/write" if entry.get("writable", False) else "read-only"
        nullable = "?" if entry.get("nullable", False) else ""
        suffix = f" — {entry['documentation']}" if entry.get("documentation") else ""
        lines.append(f"- `{entry['ownerType']}.{entry['name']}: {entry['type']}{nullable}` ({access}){suffix}")

    lines += ["", "## Methods", ""]
    for entry in catalog.get("methods", []):
        if entry.get("availability", "available") != "available":
            continue
        suffix = f" — {entry['documentation']}" if entry.get("documentation") else ""
        lines.append(f"- {callable_text(entry, entry['ownerType'])}{suffix}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail when the generated reference is stale")
    args = parser.parse_args()
    generated = render(json.loads(CATALOG.read_text(encoding="utf-8")))
    if args.check:
        return 0 if OUTPUT.exists() and OUTPUT.read_text(encoding="utf-8") == generated else 1
    OUTPUT.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
