"""Deterministically compile attributed reference pairs into the Toolbox rule pack.

No network, JavaScript execution or upstream code is used at build time.
Source mappings retain their recorded license status; this is not relicensing.
"""
import argparse
import copy
import hashlib
import json
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "resources/styles"


def encode(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def compile_pack(base, references):
    pack = copy.deepcopy(base)
    pack["metadata"]["version"] = "2.0.0"
    pack["metadata"]["license"] = "Mixed provenance: MIT and NOASSERTION; see data notices"
    pack["metadata"]["sources"] = [
        {"label": "Toolbox original curated character/phrase mappings", "url": "", "license": "MIT",
         "notes": "Original 1.0.0 curation is retained; external additions are not relicensed."},
        *[{"label": source["id"], "url": source["url"], "license": source["license"],
           "notes": f"Pinned revision {source['revision']}; {source['file']}; SHA-256 {source['sha256']}. "
                    + ("Preserve the upstream MIT notice." if source["license"] == "MIT" else "No explicit license found; attribution does not grant permission.")}
          for source in references["sources"]],
    ]
    pairs = defaultdict(lambda: defaultdict(set))
    identities = 0
    for row in references["pairs"]:
        source, target = row["source"], row["target"]
        if len(source) != 1 or len(target) != 1:
            raise ValueError("Reference mappings must contain one Unicode scalar on each side")
        if source == target:
            identities += 1
            continue
        pairs[source][target].update(row["origins"])
    existing = {rule["source"]: rule for rule in pack["rules"]}
    for source in sorted(pairs):
        rule = existing.get(source)
        if rule is None:
            rule = {"id": f"dictionary_u{ord(source):04x}", "kind": "character", "group": "dictionary",
                    "source": source, "candidates": []}
            pack["rules"].append(rule)
        known = {choice["text"] for choice in rule["candidates"]}
        # Existing order/weights remain; shared references never amplify a vote.
        for target in sorted(pairs[source], key=lambda t: ("marspeak" not in pairs[source][t], ord(t))):
            if target not in known:
                rule["candidates"].append({"text": target, "weight": 40 if "marspeak" in pairs[source][target] else 20})
                known.add(target)
        if len(rule["candidates"]) > 16:
            raise ValueError(f"Too many candidates for {source!r}; refusing to silently truncate")
    pack["rule_groups"].append("dictionary")
    for preset in pack["presets"].values():
        preset["rule_groups"].append("dictionary")
    reverse = defaultdict(set)
    for rule in pack["rules"]:
        for choice in rule["candidates"]:
            reverse[choice["text"]].add(rule["source"])
    reverse_data = {"schema_version": 1, "style_id": "mars_classic", "style_version": "2.0.0",
                    "note": "Candidate index only; no unique or lossless reverse conversion is promised.",
                    "candidates": [{"text": text, "sources": sorted(sources)} for text, sources in sorted(reverse.items())]}
    report = {"style_id": "mars_classic", "style_version": "2.0.0",
              "reference_pairs": len(references["pairs"]), "identity_reference_pairs_excluded": identities,
              "character_rules": sum(rule["kind"] == "character" for rule in pack["rules"]),
              "phrase_rules": sum(rule["kind"] == "phrase" for rule in pack["rules"]),
              "rules": len(pack["rules"]), "candidates": sum(len(rule["candidates"]) for rule in pack["rules"]),
              "multi_candidate_rules": sum(len(rule["candidates"]) > 1 for rule in pack["rules"]),
              "max_candidates_per_rule": max(len(rule["candidates"]) for rule in pack["rules"]),
              "reverse_targets": len(reverse), "ambiguous_reverse_targets": sum(len(v) > 1 for v in reverse.values()),
              "origin_counts": references["origin_counts"]}
    report["style_sha256"] = hashlib.sha256(encode(pack)).hexdigest()
    return pack, reverse_data, report


def validate(base, references, pack, reverse, report):
    rules = {row["source"]: row for row in pack["rules"]}
    assert len(rules) == len(pack["rules"])
    assert len({row["id"] for row in pack["rules"]}) == len(pack["rules"])
    for row in references["pairs"]:
        if row["source"] != row["target"]:
            assert row["target"] in {choice["text"] for choice in rules[row["source"]]["candidates"]}
    for rule in base["rules"]:
        merged = rules[rule["source"]]
        assert merged["id"] == rule["id"] and merged["group"] == rule["group"]
        assert merged["candidates"][:len(rule["candidates"])] == rule["candidates"]
        if rule["kind"] == "phrase":
            assert merged == rule
    for row in pack["rules"]:
        choices = row["candidates"]
        assert all(choice["text"] != row["source"] and choice["weight"] > 0 for choice in choices)
        assert len({choice["text"] for choice in choices}) == len(choices)
    assert report["ambiguous_reverse_targets"] > 0
    assert any(len(row["sources"]) > 1 for row in reverse["candidates"])
    reordered = copy.deepcopy(references)
    reordered["pairs"].reverse()
    for row in reordered["pairs"]:
        row["origins"].reverse()
    assert compile_pack(base, reordered) == (pack, reverse, report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify committed generated files without modifying them")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    base = json.loads((DATA / "curation/mars_classic_base.json").read_text(encoding="utf-8"))
    references = json.loads((DATA / "curation/dictionary_references.json").read_text(encoding="utf-8"))
    pack, reverse, report = compile_pack(base, references)
    validate(base, references, pack, reverse, report)
    artifacts = {DATA / "mars_classic.json": pack, DATA / "curation/reverse_candidates.json": reverse,
                 DATA / "curation/merge_report.json": report}
    for path, value in artifacts.items():
        if args.check or args.self_test:
            if path.read_bytes() != encode(value):
                raise ValueError(f"Generated artifact is stale: {path.name}; run {Path(__file__).name}")
        else:
            path.write_bytes(encode(value))
    print(f"Rules {report['rules']}; characters {report['character_rules']}; phrases {report['phrase_rules']}; "
          f"alternatives {report['multi_candidate_rules']}; SHA-256 {report['style_sha256']}")


if __name__ == "__main__":
    main()
