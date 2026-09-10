#!/usr/bin/env python3
"""Minimal eval harness for QA / factual-accuracy testing against Claude.

Usage:
    export ANTHROPIC_API_KEY=sk-...
    python run_eval.py --dataset datasets/sample_qa.jsonl \\
        --model claude-sonnet-5 \\
        --scorer llm_judge \\
        --output results/run1.json

Dataset format (JSONL), one item per line:
    {"id": "1", "input": "What is the capital of France?", "expected": "Paris", "category": "geography"}

"expected" and "category" are optional. Without "expected", scoring is skipped
and only raw outputs are recorded (useful for eyeballing / manual grading).
"""
import argparse
import json
import os
import sys
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from scoring import SCORERS


def load_dataset(path: str) -> list[dict]:
    items = []
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                items.append(json.loads(line))
            except json.JSONDecodeError as e:
                raise ValueError(f"{path}:{lineno}: invalid JSON — {e}")
    if not items:
        raise ValueError(f"{path}: no items found")
    for it in items:
        if "input" not in it:
            raise ValueError(f"item missing required 'input' field: {it}")
        it.setdefault("id", str(items.index(it)))
    return items


def run_item(client, item: dict, model: str, system: str | None, max_tokens: int,
             scorer_name: str, judge_model: str, retries: int = 3) -> dict:
    last_err = None
    for attempt in range(retries):
        try:
            kwargs = dict(
                model=model,
                max_tokens=max_tokens,
                messages=[{"role": "user", "content": item["input"]}],
            )
            if system:
                kwargs["system"] = system
            msg = client.messages.create(**kwargs)
            response_text = "".join(
                block.text for block in msg.content if hasattr(block, "text")
            )
            break
        except Exception as e:  # noqa: BLE001 — surface all API errors, retry regardless
            last_err = e
            time.sleep(min(2 ** attempt, 10))
    else:
        return {
            **item,
            "response": None,
            "error": str(last_err),
            "score": None,
            "detail": "request failed after retries",
        }

    result = {**item, "response": response_text, "error": None}
    expected = item.get("expected")
    if expected is not None and scorer_name != "none":
        scorer = SCORERS[scorer_name]
        try:
            scored = scorer(response_text, expected, item, client=client, judge_model=judge_model) \
                if scorer_name == "llm_judge" else scorer(response_text, expected, item, client=client)
        except Exception as e:  # noqa: BLE001
            scored = {"score": None, "detail": f"scoring failed: {e}"}
        result.update(scored)
    else:
        result["score"] = None
        result["detail"] = "no 'expected' field — not scored"
    return result


def summarize(results: list[dict]) -> dict:
    scored = [r for r in results if r["score"] is not None]
    by_category = defaultdict(list)
    for r in scored:
        by_category[r.get("category", "uncategorized")].append(r["score"])

    summary = {
        "total_items": len(results),
        "scored_items": len(scored),
        "errors": sum(1 for r in results if r.get("error")),
        "overall_accuracy": (sum(r["score"] for r in scored) / len(scored)) if scored else None,
        "by_category": {
            cat: {"n": len(scores), "accuracy": sum(scores) / len(scores)}
            for cat, scores in by_category.items()
        },
    }
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset", required=True, help="Path to JSONL dataset")
    ap.add_argument("--model", default="claude-sonnet-5", help="Model to evaluate")
    ap.add_argument("--system", default=None, help="System prompt text")
    ap.add_argument("--system-file", default=None, help="Path to a file containing the system prompt")
    ap.add_argument("--scorer", default="llm_judge", choices=[*SCORERS.keys(), "none"],
                     help="Scoring method (default: llm_judge)")
    ap.add_argument("--judge-model", default="claude-haiku-4-5-20251001",
                     help="Model used as judge when --scorer llm_judge")
    ap.add_argument("--max-tokens", type=int, default=1024)
    ap.add_argument("--concurrency", type=int, default=5)
    ap.add_argument("--limit", type=int, default=None, help="Only run the first N items")
    ap.add_argument("--output", default=None, help="Path to write JSON report (default: results/<timestamp>.json)")
    ap.add_argument("--min-accuracy", type=float, default=None,
                     help="If set, exit 1 when overall_accuracy is below this threshold (for CI gating).")
    args = ap.parse_args()

    try:
        import anthropic
    except ImportError:
        print("Missing dependency: pip install anthropic --break-system-packages", file=sys.stderr)
        sys.exit(1)

    if not os.environ.get("ANTHROPIC_API_KEY"):
        print("ANTHROPIC_API_KEY is not set.", file=sys.stderr)
        sys.exit(1)

    system = args.system
    if args.system_file:
        system = Path(args.system_file).read_text(encoding="utf-8")

    items = load_dataset(args.dataset)
    if args.limit:
        items = items[: args.limit]

    client = anthropic.Anthropic()

    print(f"Running {len(items)} items against {args.model} (scorer={args.scorer}, concurrency={args.concurrency})")
    results = [None] * len(items)
    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        futures = {
            pool.submit(run_item, client, item, args.model, system, args.max_tokens,
                        args.scorer, args.judge_model): i
            for i, item in enumerate(items)
        }
        done = 0
        for fut in as_completed(futures):
            i = futures[fut]
            results[i] = fut.result()
            done += 1
            print(f"  [{done}/{len(items)}] id={results[i]['id']} score={results[i]['score']}", file=sys.stderr)

    summary = summarize(results)

    output_path = args.output or f"results/run_{int(time.time())}.json"
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    report = {
        "model": args.model,
        "scorer": args.scorer,
        "dataset": args.dataset,
        "system": system,
        "summary": summary,
        "results": results,
    }
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print("\n=== Summary ===")
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    print(f"\nFull report written to {output_path}")

    if args.min_accuracy is not None:
        acc = summary["overall_accuracy"]
        if acc is None:
            print(f"\nCI gate: no scored items, cannot compare to --min-accuracy {args.min_accuracy}", file=sys.stderr)
            sys.exit(1)
        if acc < args.min_accuracy:
            print(f"\nCI gate FAILED: overall_accuracy {acc:.3f} < --min-accuracy {args.min_accuracy:.3f}", file=sys.stderr)
            sys.exit(1)
        print(f"\nCI gate OK: overall_accuracy {acc:.3f} >= {args.min_accuracy:.3f}", file=sys.stderr)


if __name__ == "__main__":
    main()
