#!/usr/bin/env python3
"""Deterministic, task-configured benchmark for generated QEMU artifacts."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


def _matches(text: str, patterns: list[str], mode: str) -> bool:
    found = [re.search(pattern, text, re.MULTILINE | re.DOTALL) is not None
             for pattern in patterns]
    return all(found) if mode == "all" else any(found)


def _json_value(document: Any, dotted_path: str) -> Any:
    value = document
    for part in dotted_path.split("."):
        value = value[int(part)] if isinstance(value, list) else value[part]
    return value


def _run_check(candidate: Path, check: dict[str, Any]) -> tuple[bool, str]:
    names = check.get("paths", [check.get("path")])
    paths = [candidate / name for name in names]
    missing = [name for name, path in zip(names, paths) if not path.is_file()]
    if missing:
        return False, f"missing {', '.join(missing)}"

    text = "\n".join(path.read_text(encoding="utf-8", errors="replace")
                       for path in paths)
    if "min_lines" in check and len(text.splitlines()) < check["min_lines"]:
        return False, f"{', '.join(names)} has fewer than {check['min_lines']} lines"
    if check.get("all") and not _matches(text, check["all"], "all"):
        return False, check.get("failure", "required patterns not all present")
    if check.get("any") and not _matches(text, check["any"], "any"):
        return False, check.get("failure", "none of the accepted patterns present")
    if check.get("none") and _matches(text, check["none"], "any"):
        return False, check.get("failure", "forbidden pattern present")

    if check.get("json"):
        try:
            document = json.loads(text)
            for assertion in check["json"]:
                actual = _json_value(document, assertion["path"])
                if "equals" in assertion and actual != assertion["equals"]:
                    return False, f"{assertion['path']} is {actual!r}"
                if "contains" in assertion and assertion["contains"] not in actual:
                    return False, f"{assertion['path']} lacks {assertion['contains']!r}"
        except (json.JSONDecodeError, KeyError, IndexError, TypeError, ValueError) as error:
            return False, f"invalid manifest: {error}"
    return True, "ok"


def score_candidate(candidate: Path, rubric_path: Path) -> dict[str, Any]:
    rubric = json.loads(rubric_path.read_text(encoding="utf-8"))
    checks = []
    earned = 0
    category_scores: dict[str, dict[str, int]] = {}

    for check in rubric["checks"]:
        passed, detail = _run_check(candidate, check)
        points = check["points"] if passed else 0
        earned += points
        category = category_scores.setdefault(check["category"],
                                              {"earned": 0, "possible": 0})
        category["earned"] += points
        category["possible"] += check["points"]
        checks.append({
            "id": check["id"],
            "category": check["category"],
            "description": check["description"],
            "passed": passed,
            "points": points,
            "possible": check["points"],
            "detail": detail,
        })

    possible = sum(check["points"] for check in rubric["checks"])
    if possible != 100:
        raise ValueError(f"rubric points must total 100, got {possible}")
    return {
        "benchmark": rubric["name"],
        "version": rubric["version"],
        "score": earned,
        "possible": possible,
        "threshold": rubric["threshold"],
        "passed": earned >= rubric["threshold"],
        "categories": category_scores,
        "checks": checks,
    }


def markdown_report(result: dict[str, Any]) -> str:
    status = "PASS" if result["passed"] else "FAIL"
    lines = [
        "# Benchmark result",
        "",
        f"- Benchmark: `{result['benchmark']}` v{result['version']}",
        f"- Score: **{result['score']}/{result['possible']} ({status})**",
        f"- Pass threshold: {result['threshold']}",
        "",
        "## Categories",
        "",
        "| Category | Score |",
        "|---|---:|",
    ]
    for name, score in result["categories"].items():
        lines.append(f"| {name} | {score['earned']}/{score['possible']} |")
    lines.extend(["", "## Checks", ""])
    for check in result["checks"]:
        marker = "PASS" if check["passed"] else "FAIL"
        lines.append(
            f"- [{marker}] {check['id']} ({check['points']}/{check['possible']}): "
            f"{check['description']} — {check['detail']}"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--rubric", type=Path,
                        default=Path("benchmarks/stm32f103-tim2/rubric.json"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    result = score_candidate(args.candidate.resolve(), args.rubric.resolve())
    report = markdown_report(result)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
        args.output.with_suffix(".json").write_text(
            json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(report, end="")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
