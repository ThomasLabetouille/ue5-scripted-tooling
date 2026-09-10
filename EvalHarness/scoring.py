"""Scoring functions for the eval harness.

Each scorer takes (response: str, expected: str, item: dict, client) and
returns a dict: {"score": float (0-1), "detail": str}.
"""
import re


def _normalize(s: str) -> str:
    s = s.strip().lower()
    s = re.sub(r"[^\w\s]", "", s)
    s = re.sub(r"\s+", " ", s)
    return s


def exact_match(response: str, expected: str, item: dict, client=None) -> dict:
    ok = _normalize(response) == _normalize(expected)
    return {"score": 1.0 if ok else 0.0, "detail": "exact match" if ok else "mismatch"}


def contains(response: str, expected: str, item: dict, client=None) -> dict:
    ok = _normalize(expected) in _normalize(response)
    return {"score": 1.0 if ok else 0.0, "detail": "found" if ok else "not found"}


JUDGE_PROMPT = """You are grading a model's answer against a reference answer.

Question: {question}
Reference answer: {expected}
Model's answer: {response}

Does the model's answer convey the same factual content as the reference answer?
Minor phrasing differences are fine. Reply with exactly one line:
SCORE: 1  (if correct)
or
SCORE: 0  (if incorrect)
Then on a second line, a one-sentence reason."""


def llm_judge(response: str, expected: str, item: dict, client=None, judge_model: str = "claude-haiku-4-5-20251001") -> dict:
    if client is None:
        raise RuntimeError("llm_judge scorer requires an Anthropic client")
    prompt = JUDGE_PROMPT.format(
        question=item.get("input", ""), expected=expected, response=response
    )
    msg = client.messages.create(
        model=judge_model,
        max_tokens=100,
        messages=[{"role": "user", "content": prompt}],
    )
    text = msg.content[0].text.strip()
    m = re.search(r"SCORE:\s*([01])", text)
    score = float(m.group(1)) if m else 0.0
    return {"score": score, "detail": text}


SCORERS = {
    "exact_match": exact_match,
    "contains": contains,
    "llm_judge": llm_judge,
}
