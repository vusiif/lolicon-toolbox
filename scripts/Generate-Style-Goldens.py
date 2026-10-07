"""Independent integer-stream reference for the fixed simple CJK/ASCII sample.

This is deliberately not a Unicode EGC implementation or production converter.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = (ROOT / "resources/styles/mars_classic.json").read_bytes()
PACK = json.loads(RAW)
INPUT = "你好，我喜欢你！ABC 123 😀\n谢谢朋友，晚安。"
MASK = (1 << 64) - 1


class Stream:
    def __init__(self, seed):
        self.state, self.calls = seed, 0

    def bounded(self, bound):
        threshold = ((1 << 64) - bound) % bound
        while True:
            self.calls += 1
            self.state = (self.state + 0x9E3779B97F4A7C15) & MASK
            z = self.state
            z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
            z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
            z ^= z >> 31
            if z >= threshold:
                return z % bound

    def gate(self, rate):
        return rate == 10000 or (rate > 0 and self.bounded(10000) < rate)

    def choose(self, choices):
        if len(choices) == 1:
            return choices[0]
        draw = self.bounded(sum(c["weight"] for c in choices))
        for choice in choices:
            if draw < choice["weight"]:
                return choice
            draw -= choice["weight"]
        raise AssertionError("invalid choice")


def sample(preset_id):
    rng = Stream(42)
    preset = PACK["presets"][preset_id]
    rules = sorted([r for r in PACK["rules"] if r["group"] in preset["rule_groups"]], key=lambda r: -len(r["source"]))
    decorators = {d["id"]: d for d in PACK["decorators"]}
    lines, matched, replaced, decorated = [], 0, 0, 0
    for line in INPUT.split("\n"):
        offset, pieces = 0, []
        while offset < len(line):
            rule = next((r for r in rules if line.startswith(r["source"], offset)), None)
            if rule is None:
                pieces.append(line[offset]); offset += 1
                continue
            matched += 1
            if rng.gate(preset["replacement_rate_bp"]):
                pieces.append(rng.choose(rule["candidates"])["text"]); replaced += 1
            else:
                pieces.append(rule["source"])
            offset += len(rule["source"])
        body = "".join(pieces)
        if line.strip() and rng.gate(preset["decorator_rate_bp"]):
            wrapper = rng.choose([decorators[d] for d in preset["decorator_ids"]])
            body = wrapper["prefix"] + body + wrapper["suffix"]; decorated += 1
        lines.append(body)
    return {"preset": preset_id, "seed": "42", "output": "\n".join(lines), "matched": matched,
            "replaced": replaced, "decorated": decorated, "random_calls": rng.calls}


data = {"input": INPUT, "style_sha256": hashlib.sha256(RAW).hexdigest(),
        "reference": "Independent Python integer-stream trace for this simple CJK/ASCII sample, not a general Unicode segmenter.",
        "cases": [sample(preset) for preset in ("light", "medium", "heavy")]}
(ROOT / "tests/data/style-release-goldens.json").write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(data, ensure_ascii=True, indent=2))
