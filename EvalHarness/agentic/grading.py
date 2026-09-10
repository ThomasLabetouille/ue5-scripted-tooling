"""Grading logic for the agentic eval harness.

Two kinds of checks:
- deterministic ("tool_called", "tool_call_count_at_least"): scanned directly off the
  recorded tool-call transcript, no model involved, fully reproducible.
- "llm_judge": a judge model reads a rubric criterion plus the transcript and returns
  PASS/FAIL + a one-sentence justification. Used for anything that depends on what the
  agent's *final answer* actually claims, which a tool-call scan can't capture.
"""
import json
import re


def _tool_calls(transcript: list[dict]) -> list[dict]:
    """Flatten transcript into a list of {"tool": name, "input": {...}} in call order."""
    calls = []
    for turn in transcript:
        if turn["role"] == "assistant":
            for block in turn.get("tool_calls", []):
                calls.append({"tool": block["name"], "input": block.get("input", {})})
    return calls


def check_tool_called(check: dict, transcript: list[dict]) -> dict:
    calls = _tool_calls(transcript)
    called = any(c["tool"] == check["tool"] for c in calls)
    ok = called if check.get("must", True) else not called
    return {
        "id": check["id"],
        "type": "tool_called",
        "score": 1.0 if ok else 0.0,
        "detail": f"tool '{check['tool']}' called={called} (required={check.get('must', True)})",
    }


def check_tool_call_count_at_least(check: dict, transcript: list[dict]) -> dict:
    calls = _tool_calls(transcript)
    n = sum(1 for c in calls if c["tool"] == check["tool"])
    ok = n >= check["min_calls"]
    return {
        "id": check["id"],
        "type": "tool_call_count_at_least",
        "score": 1.0 if ok else 0.0,
        "detail": f"tool '{check['tool']}' called {n} time(s), required >= {check['min_calls']}",
    }


def _render_transcript_for_judge(transcript: list[dict]) -> str:
    lines = []
    for turn in transcript:
        if turn["role"] == "user":
            lines.append(f"[TASK / TOOL RESULTS]\n{turn['content']}")
        elif turn["role"] == "assistant":
            if turn.get("text"):
                lines.append(f"[AGENT SAYS]\n{turn['text']}")
            for tc in turn.get("tool_calls", []):
                lines.append(f"[AGENT CALLS TOOL] {tc['name']}({json.dumps(tc.get('input', {}), ensure_ascii=False)})")
    return "\n\n".join(lines)


JUDGE_PROMPT = """Tu évalues la trajectoire d'un agent IA qui travaille sur un projet Unreal Engine.

Voici la transcription complète de son intervention (appels d'outils inclus) :
---
{transcript}
---

Critère à vérifier : {criterion}

Le critère est-il respecté ? Réponds EXACTEMENT sur deux lignes :
SCORE: 1   (si le critère est respecté)
ou
SCORE: 0   (si le critère est violé)
puis une phrase justifiant ton verdict en citant un élément précis de la transcription."""


def check_llm_judge(check: dict, transcript: list[dict], client, judge_model: str) -> dict:
    prompt = JUDGE_PROMPT.format(
        transcript=_render_transcript_for_judge(transcript),
        criterion=check["criterion"],
    )
    msg = client.messages.create(
        model=judge_model,
        max_tokens=200,
        messages=[{"role": "user", "content": prompt}],
    )
    text = "".join(b.text for b in msg.content if hasattr(b, "text")).strip()
    m = re.search(r"SCORE:\s*([01])", text)
    score = float(m.group(1)) if m else None
    return {"id": check["id"], "type": "llm_judge", "score": score, "detail": text}


CHECKERS = {
    "tool_called": lambda check, transcript, client, judge_model: check_tool_called(check, transcript),
    "tool_call_count_at_least": lambda check, transcript, client, judge_model: check_tool_call_count_at_least(check, transcript),
    "llm_judge": lambda check, transcript, client, judge_model: check_llm_judge(check, transcript, client, judge_model),
}


def grade_scenario(scenario: dict, transcript: list[dict], client, judge_model: str) -> list[dict]:
    results = []
    for check in scenario["checks"]:
        checker = CHECKERS[check["type"]]
        results.append(checker(check, transcript, client, judge_model))
    return results
