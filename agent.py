#!/usr/bin/env python3
"""Small, auditable LLM agent for a QEMU peripheral modelling task."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

from benchmark import markdown_report, score_candidate


DEFAULT_API_BASE = "https://api.deepseek.com"
DEFAULT_MODEL = "deepseek-v4-flash"
ALLOWED_INPUT_SUFFIXES = {".c", ".h", ".json", ".md", ".txt"}
REQUIRED_OUTPUTS = {
    "hw/timer/stm32f1xx_timer.c",
    "include/hw/timer/stm32f1xx_timer.h",
    "tests/qtest/stm32f103-timer-test.c",
    "model-manifest.json",
    "REPORT.md",
}


def portable_path(path: Path, base: Path) -> str:
    try:
        return path.resolve().relative_to(base.resolve()).as_posix()
    except ValueError:
        return path.name


def collect_inputs(task_dir: Path) -> tuple[str, str]:
    chunks = []
    digest = hashlib.sha256()
    for path in sorted(task_dir.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in ALLOWED_INPUT_SUFFIXES:
            continue
        relative = path.relative_to(task_dir).as_posix()
        data = path.read_bytes()
        digest.update(relative.encode())
        digest.update(b"\0")
        digest.update(data)
        chunks.append(f"\n===== {relative} =====\n{data.decode('utf-8', 'replace')}")
    if not chunks:
        raise ValueError(f"no supported task inputs under {task_dir}")
    return "".join(chunks), digest.hexdigest()


def _json_from_text(text: str) -> dict[str, Any]:
    text = text.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1].rsplit("```", 1)[0]
    value = json.loads(text)
    if not isinstance(value, dict):
        raise ValueError("model response must be a JSON object")
    return value


def call_model(api_key: str, api_base: str, model: str,
               messages: list[dict[str, str]], max_tokens: int) -> tuple[dict[str, Any], dict[str, Any]]:
    payload = {
        "model": model,
        "messages": messages,
        "thinking": {"type": "disabled"},
        "temperature": 0.1,
        "max_tokens": max_tokens,
        "response_format": {"type": "json_object"},
        "stream": False,
    }
    request = urllib.request.Request(
        api_base.rstrip("/") + "/chat/completions",
        data=json.dumps(payload).encode(),
        headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=300) as response:
            envelope = json.load(response)
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", "replace")[:1000]
        raise RuntimeError(f"model API returned HTTP {error.code}: {body}") from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"model API request failed: {error.reason}") from error

    try:
        content = envelope["choices"][0]["message"]["content"]
        if not content or not content.strip():
            raise ValueError("empty final content")
        result = _json_from_text(content)
    except (KeyError, IndexError, TypeError, json.JSONDecodeError, ValueError) as error:
        raise RuntimeError(f"model returned an invalid JSON response: {error}") from error
    metadata = {
        "id": envelope.get("id"),
        "model": envelope.get("model", model),
        "finish_reason": envelope.get("choices", [{}])[0].get("finish_reason"),
        "usage": envelope.get("usage", {}),
    }
    return result, metadata


def _safe_output_path(root: Path, name: str) -> Path:
    relative = PurePosixPath(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"unsafe model output path: {name}")
    if name not in REQUIRED_OUTPUTS:
        raise ValueError(f"unexpected model output path: {name}")
    return root.joinpath(*relative.parts)


def write_candidate(root: Path, response: dict[str, Any],
                    required_outputs: set[str] = REQUIRED_OUTPUTS) -> None:
    files = response.get("files")
    if isinstance(files, dict):
        files = [{"path": path, "content": content}
                 for path, content in files.items()]
    elif files is None and required_outputs.issubset(response):
        files = [{"path": path, "content": response[path]}
                 for path in sorted(required_outputs)]
    if not isinstance(files, list):
        raise ValueError("generation response lacks a files array or path map")
    seen = set()
    for entry in files:
        if isinstance(entry, dict) and isinstance(entry.get("path"), str) \
                and entry["path"].endswith(".json") \
                and isinstance(entry.get("content"), (dict, list)):
            entry = {**entry, "content": json.dumps(entry["content"], ensure_ascii=False,
                                                     indent=2)}
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str) \
                or not isinstance(entry.get("content"), str):
            raise ValueError("each generated file needs string path and content fields")
        if entry["path"] not in required_outputs:
            raise ValueError(f"unexpected file for this stage: {entry['path']}")
        path = _safe_output_path(root, entry["path"])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(entry["content"].rstrip() + "\n", encoding="utf-8")
        seen.add(entry["path"])
    missing = required_outputs - seen
    if missing:
        raise ValueError(f"generation response omitted: {', '.join(sorted(missing))}")


def _system_prompt() -> str:
    return (
        "You are a QEMU hardware modeller. Use only facts present in the supplied task inputs, "
        "label assumptions, and produce maintainable QEMU-style C. Do not claim that code was "
        "compiled or tested when it was not. The requested scope is a minimal useful model, not "
        "the entire peripheral. Your response must be one valid JSON object."
    )


def _generation_prompt(inputs: str, plan: dict[str, Any]) -> str:
    paths = "\n".join(f"- {path}" for path in sorted(REQUIRED_OUTPUTS))
    return f"""Implement the plan for the task inputs below.

Return exactly this JSON shape:
{{"files":[{{"path":"...","content":"..."}}]}}

Return every required file exactly once and no other path:
{paths}

The C source and header must form a complete QEMU SysBus device. The qtest file must contain
runnable libqtest tests. model-manifest.json must be valid JSON describing address, IRQ, clock,
registers, supported behavior, and known limitations. REPORT.md must distinguish sourced facts,
implementation choices, generated validation, and limitations.

PLAN:
{json.dumps(plan, ensure_ascii=False, indent=2)}

TASK INPUTS:
{inputs}
"""


def _repair_prompt(inputs: str, output: Path, result: dict[str, Any]) -> str:
    feedback = "\n".join(
        f"- {check['id']}: {check['description']} ({check['detail']})"
        for check in result["checks"] if not check["passed"]
    )
    current = []
    for name in sorted(REQUIRED_OUTPUTS):
        current.append(f"\n===== {name} =====\n{(output / name).read_text(encoding='utf-8')}")
    return f"""Revise the candidate to address the benchmark failures below. Return the same
JSON shape with all five complete files, no commentary outside JSON. Do not merely add keywords
or comments: implement and test the missing behavior. Preserve correct existing behavior.

FAILURES:
{feedback}

CURRENT CANDIDATE:
{''.join(current)}

ORIGINAL TASK INPUTS:
{inputs}
"""


def _staged_prompts(inputs: str, plan: dict[str, Any], output: Path):
    plan_text = json.dumps(plan, ensure_ascii=False, indent=2)
    model_paths = {
        "hw/timer/stm32f1xx_timer.c",
        "include/hw/timer/stm32f1xx_timer.h",
    }
    yield "model", model_paths, f"""Generate only the complete QEMU device source and public
header as a JSON object with a files array. Return both required paths and no others:
{chr(10).join(f'- {path}' for path in sorted(model_paths))}

Use symbolic defines for every register offset and supported bit. Follow qemu-api-notes.md
exactly. Derive counter state from virtual elapsed time; do not truncate a 72 MHz tick into an
integer nanosecond value. Implement rc_w0, all four CCMR-selected output channels, pre/post-load,
and every behavior in the task. Do not write pseudocode or integration files.

PLAN:
{plan_text}

TASK INPUTS:
{inputs}
"""

    source = (output / "hw/timer/stm32f1xx_timer.c").read_text(encoding="utf-8")
    header = (output / "include/hw/timer/stm32f1xx_timer.h").read_text(encoding="utf-8")
    test_paths = {"tests/qtest/stm32f103-timer-test.c"}
    yield "qtest", test_paths, f"""Generate only a complete runnable libqtest source as JSON
with a files array containing {next(iter(test_paths))}. It must use machine stm32f103,
libqtest-single, NVIC interception, virtual clock boundary checks, and cover reset, update IRQ
assert/clear, CC2, PSC preload, ARPE, UDIS, URS, and one-pulse. Do not use machine none.

TASK INPUTS:
{inputs}

GENERATED DEVICE SOURCE:
{source}

GENERATED HEADER:
{header}
"""

    doc_paths = {"model-manifest.json", "REPORT.md"}
    yield "docs", doc_paths, f"""Generate only model-manifest.json and REPORT.md as a JSON
object with a files array. The manifest must use the exact top-level schema required by task.md,
with peripheral exactly TIM2. The report must have explicit Sources, Assumptions, Validation,
and Limitations sections and must say the generated C/qtest have not yet been compiled or run.

TASK INPUTS:
{inputs}
"""


def _staged_repair_prompts(inputs: str, output: Path, result: dict[str, Any]):
    failures = "\n".join(
        f"- {check['id']}: {check['description']}"
        for check in result["checks"] if not check["passed"]
    )
    source_path = "hw/timer/stm32f1xx_timer.c"
    header_path = "include/hw/timer/stm32f1xx_timer.h"
    source = (output / source_path).read_text(encoding="utf-8")
    header = (output / header_path).read_text(encoding="utf-8")
    model_paths = {source_path, header_path}
    yield "repair-model", model_paths, f"""Repair only the complete QEMU source and header,
returning a JSON files array with both paths. The current candidate has real semantic defects;
do not echo it unchanged. In particular: derive elapsed counter ticks from base_ns/base_cnt using
muldiv64 instead of truncating one 72 MHz tick to integer ns; schedule exact future tick counts
with muldiv64_round_up; implement SR rc_w0 as stored_flags &= written_value; honor CCMR1/2 CCxS
for each of four channels; make URS suppress only software-UG UIF; and add pre_save/post_load that
latch count then restore IRQ/deadline. Use the current QEMU APIs in qemu-api-notes.md.

BENCHMARK FAILURES:
{failures}

TASK INPUTS:
{inputs}

CURRENT SOURCE:
{source}

CURRENT HEADER:
{header}
"""

    repaired_source = (output / source_path).read_text(encoding="utf-8")
    repaired_header = (output / header_path).read_text(encoding="utf-8")
    test_path = "tests/qtest/stm32f103-timer-test.c"
    old_test = (output / test_path).read_text(encoding="utf-8")
    yield "repair-qtest", {test_path}, f"""Repair only the complete qtest source and return it
in a JSON files array. Use libqtest-single and machine stm32f103. Correct rc_w0 clearing writes
zero, not one. Express times with a ticks-to-ns helper and test one nanosecond before and at the
deadline. Cover IRQ assertion/deassertion, CC2, PSC/ARR preload, UDIS, URS and one-pulse. Ensure
the code matches the generated device and current QEMU test APIs.

TASK INPUTS:
{inputs}

REPAIRED SOURCE:
{repaired_source}

REPAIRED HEADER:
{repaired_header}

CURRENT QTEST:
{old_test}
"""


def run(args: argparse.Namespace) -> int:
    task_dir = args.task.resolve()
    rubric = args.rubric.resolve()
    output = args.output.resolve()
    api_key = os.environ.get("DEEPSEEK_API_KEY")
    if not api_key:
        raise RuntimeError("DEEPSEEK_API_KEY is not set")
    inputs, input_sha256 = collect_inputs(task_dir)
    output.mkdir(parents=True, exist_ok=True)

    started = datetime.now(timezone.utc).isoformat()
    plan_prompt = f"""Read the task inputs and return a modelling plan as JSON with keys:
requirements (array), register_model (array), behavior_model (array), tests (array),
assumptions (array), and risks (array). Do not generate code yet.

TASK INPUTS:
{inputs}
"""
    plan_path = output / "plan.json"
    if args.plan:
        supplied_plan = args.plan.resolve()
        plan = json.loads(supplied_plan.read_text(encoding="utf-8"))
        plan_meta = {"reused_from": portable_path(supplied_plan, Path.cwd())}
    elif args.resume and plan_path.is_file():
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        plan_meta = {"reused": True}
    else:
        plan, plan_meta = call_model(api_key, args.api_base, args.model,
                                      [{"role": "system", "content": _system_prompt()},
                                       {"role": "user", "content": plan_prompt}], 5000)
    (output / "plan.json").write_text(
        json.dumps(plan, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    attempts = []
    if args.strategy == "staged":
        if args.repair_existing:
            current_result = score_candidate(output, rubric)
            stage_prompts = _staged_repair_prompts(inputs, output, current_result)
        else:
            stage_prompts = _staged_prompts(inputs, plan, output)
        api_calls = []
        for stage, paths, prompt in stage_prompts:
            generation_path = output / f"generation-{stage}.json"
            if args.reuse_generations and generation_path.is_file():
                generated = json.loads(generation_path.read_text(encoding="utf-8"))
                call_meta = {"reused": True}
            else:
                generated, call_meta = call_model(
                    api_key, args.api_base, args.model,
                    [{"role": "system", "content": _system_prompt()},
                     {"role": "user", "content": prompt}], args.max_tokens)
            (output / f"generation-{stage}.json").write_text(
                json.dumps(generated, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            write_candidate(output, generated, paths)
            api_calls.append({"stage": stage, **call_meta})
        staged_result = score_candidate(output, rubric)
        attempts.append({
            "attempt": 1,
            "strategy": "staged",
            "api": api_calls,
            "score": staged_result["score"],
            "failed_checks": [c["id"] for c in staged_result["checks"] if not c["passed"]],
        })
        (output / "benchmark-attempt-1.json").write_text(
            json.dumps(staged_result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    elif args.repair_existing:
        missing = [name for name in REQUIRED_OUTPUTS if not (output / name).is_file()]
        if missing:
            raise ValueError(f"cannot repair; candidate lacks: {', '.join(sorted(missing))}")
        prompt = _repair_prompt(inputs, output, score_candidate(output, rubric))
    else:
        prompt = _generation_prompt(inputs, plan)
    if args.strategy == "monolithic":
        best_score = -1
        best_files: dict[str, str] = {}
        for attempt in range(1, args.iterations + 1):
            generated, call_meta = call_model(
                api_key, args.api_base, args.model,
                [{"role": "system", "content": _system_prompt()},
                 {"role": "user", "content": prompt}],
                args.max_tokens,
            )
            (output / f"generation-attempt-{attempt}.json").write_text(
                json.dumps(generated, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            write_candidate(output, generated)
            result = score_candidate(output, rubric)
            if result["score"] > best_score:
                best_score = result["score"]
                best_files = {name: (output / name).read_text(encoding="utf-8")
                              for name in REQUIRED_OUTPUTS}
            attempts.append({"attempt": attempt, "api": call_meta, "score": result["score"],
                             "failed_checks": [c["id"] for c in result["checks"] if not c["passed"]]})
            (output / f"benchmark-attempt-{attempt}.json").write_text(
                json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            if result["passed"] or attempt == args.iterations:
                break
            prompt = _repair_prompt(inputs, output, result)
        for name, content in best_files.items():
            (output / name).write_text(content, encoding="utf-8")

    final_result = score_candidate(output, rubric)
    (output / "BENCHMARK.md").write_text(markdown_report(final_result), encoding="utf-8")
    (output / "benchmark.json").write_text(
        json.dumps(final_result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    metadata = {
        "started_at": started,
        "finished_at": datetime.now(timezone.utc).isoformat(),
        "task": task_dir.name,
        "input_sha256": input_sha256,
        "model_requested": args.model,
        "strategy": args.strategy,
        "api_base": args.api_base,
        "plan_api": plan_meta,
        "attempts": attempts,
        "final_score": final_result["score"],
        "passed": final_result["passed"],
    }
    (output / "run.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(markdown_report(final_result), end="")
    return 0 if final_result["passed"] else 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("task", type=Path)
    parser.add_argument("--rubric", type=Path,
                        default=Path("benchmarks/stm32f103-tim2/rubric.json"))
    parser.add_argument("--output", type=Path, default=Path("runs/stm32f103-tim2"))
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--api-base", default=DEFAULT_API_BASE)
    parser.add_argument("--iterations", type=int, choices=range(1, 4), default=2)
    parser.add_argument("--max-tokens", type=int, default=30000)
    parser.add_argument("--strategy", choices=("staged", "monolithic"), default="staged")
    parser.add_argument("--reuse-generations", action="store_true",
                        help="reuse saved generation-<stage>.json responses")
    parser.add_argument("--resume", action="store_true",
                        help="reuse an existing plan.json in the output directory")
    parser.add_argument("--plan", type=Path,
                        help="reuse a valid plan JSON from another run")
    parser.add_argument("--repair-existing", action="store_true",
                        help="start by repairing the candidate already in the output directory")
    return parser


def main() -> int:
    try:
        return run(build_parser().parse_args())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
