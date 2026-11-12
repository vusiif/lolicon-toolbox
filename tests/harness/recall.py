"""Candidate recall for lossy GBK repair (harness experiment P1).

For every corpus document: replace characters GBK cannot encode with '?' (as
the corpus generator's encode_errors=replace does), damage the text per byte
with LossyGbkTool, check that the original reproduces the damage, then ask
LossyGbkTool for the likeliest fitting fills of every damaged run and report
how often the original run is among the first k (Oracle Recall@k: the ceiling
of any harness that only picks from these lists). Runs come from the damaged
text alone, as at run time; the original is used only for scoring.

    python tests/harness/recall.py --tool cmake-build-release/tests/LossyGbkTool.exe --out <dir>
"""
import argparse
import collections
import json
import pathlib
import subprocess
import sys

KS = (1, 5, 20, 100)


def encodable(text: str) -> str:
    out = []
    for c in text:
        try:
            c.encode("gbk")
            out.append(c)
        except UnicodeEncodeError:
            out.append("?")
    return "".join(out)


def run(tool, *args) -> str:
    done = subprocess.run([str(tool), *map(str, args)], capture_output=True)
    text = done.stdout.decode("utf-8")
    if done.returncode not in (0, 1):
        sys.exit(f"{pathlib.Path(tool).name} {args[0]} failed: {text.strip()}")
    return text


def bucket(lost: int) -> str:
    return {0: "0", 1: "1", 2: "2"}.get(lost, "3-4" if lost <= 4 else "5+")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--tool", required=True)
    p.add_argument("--corpus", default="tests/data/corpus")
    p.add_argument("--out", required=True)
    p.add_argument("--limit", type=int, default=100)
    p.add_argument("--beam", type=int, default=128)
    p.add_argument("--only")
    a = p.parse_args()
    a.tool = pathlib.Path(a.tool).resolve()
    out = pathlib.Path(a.out)
    total = collections.Counter()
    by_bucket = collections.defaultdict(collections.Counter)
    for doc in sorted(pathlib.Path(a.corpus).iterdir()):
        if not doc.is_dir() or (a.only and doc.name != a.only):
            continue
        work = out / doc.name
        work.mkdir(parents=True, exist_ok=True)
        with open(doc / "ground_truth.txt", encoding="utf-8-sig", newline="") as f:
            raw = f.read()
        truth = encodable(raw)
        (work / "truth.txt").write_text(truth, encoding="utf-8", newline="")
        run(a.tool, "damage", work / "truth.txt", work / "damaged.txt")
        verdict = run(a.tool, "validate", work / "truth.txt", work / "damaged.txt").strip()
        units = [json.loads(line) for line in run(a.tool, "candidates", work / "damaged.txt", a.limit, a.beam, 0).splitlines() if line.strip()]
        fills = {str(u["id"]): truth[u["begin"]:u["end"]] for u in units}
        (work / "truth_fills.json").write_text(json.dumps(fills, ensure_ascii=False), encoding="utf-8")
        def check(*extra):
            lines = run(a.tool, "check", work / "damaged.txt", work / "truth_fills.json", 0, *extra).splitlines()
            return {j["id"]: j["fits"] for j in map(json.loads, lines) if "fits" in j}
        # Alone, a run is checked next to the local guesses of nearby runs, which may be wrong;
        # jointly, next to the original's other runs. Only the joint check must accept every original run.
        fits, joint = check(), check("--joint")
        c = collections.Counter()
        with open(work / "units.jsonl", "w", encoding="utf-8") as log:
            for u in units:
                want = fills[str(u["id"])]
                texts = [x["text"] for x in u["candidates"]]
                rank = texts.index(want) + 1 if want in texts else None
                b = by_bucket[bucket(u["lost"])]
                for counter in (c, total, b):
                    counter["units"] += 1
                    counter["fits"] += bool(fits.get(u["id"]))
                    counter["joint"] += bool(joint.get(u["id"]))
                    counter["empty"] += not texts
                    for k in KS:
                        counter[f"@{k}"] += rank is not None and rank <= k
                log.write(json.dumps({**u, "truth": want, "rank": rank, "truth_fits": fits.get(u["id"])}, ensure_ascii=False) + "\n")
        replaced = sum(x != y for x, y in zip(raw, truth))
        print(f"{doc.name}: {len(truth)} chars ({replaced} not GBK -> ?), original {verdict}")
        print("   " + line(c))
    print("all:  " + line(total))
    for name in ("0", "1", "2", "3-4", "5+"):
        if by_bucket[name]["units"]:
            print(f"  lost {name:>3}: " + line(by_bucket[name]))


def line(c) -> str:
    n = c["units"] or 1
    recall = "  ".join(f"R@{k} {100 * c[f'@{k}'] / n:5.1f}%" for k in KS)
    return f"{c['units']:4d} runs  {recall}  original fits alone {100 * c['fits'] / n:.1f}% jointly {100 * c['joint'] / n:.1f}%  no candidates {c['empty']}"


if __name__ == "__main__":
    main()
