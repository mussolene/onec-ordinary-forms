#!/usr/bin/env python3
"""Differential slot prover for ordinary-form value properties.

ЗначениеВСтрокуВнутр of a runtime ordinary form is non-idempotent (proven:
two consecutive serializations of the same form differ in volatile UUID/counter
positions). A naive baseline-vs-mutated diff is therefore noisy. This analyzer
subtracts the volatile mask:

1. parse N baseline serializations and N mutated serializations into nested
   list-streams using the repository list-stream parser;
2. flatten each to a path -> atom map (path is the index chain into the tree);
3. volatile mask = any path whose value disagrees within the baseline set or
   within the mutated set, or whose presence is unstable;
4. stable delta = paths that are identical within each set but differ between
   baseline[0] and mutated[0] and are not in the volatile mask.

The stable delta is the proven slot evidence for the mutated property: it tells
exactly which list-stream position(s) carry the value, with before/after atoms.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC = REPO_ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from onec_ordinary_forms.liststream import parse_list_stream  # noqa: E402


def flatten(node: object, path: tuple, out: dict[tuple, str]) -> None:
    if isinstance(node, list):
        for index, child in enumerate(node):
            flatten(child, path + (index,), out)
    else:
        out[path] = "" if node is None else str(node)


def parse_and_flatten(path: Path) -> dict[tuple, str]:
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    tree = parse_list_stream(text, allow_trailing=True)
    out: dict[tuple, str] = {}
    flatten(tree, (), out)
    return out


def volatile_paths(maps: list[dict[tuple, str]]) -> set[tuple]:
    if not maps:
        return set()
    volatile: set[tuple] = set()
    all_paths: set[tuple] = set()
    for m in maps:
        all_paths |= set(m.keys())
    for path in all_paths:
        values = [m.get(path) for m in maps]
        if any(v != values[0] for v in values):
            volatile.add(path)
    return volatile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", nargs="+", required=True, help="Baseline serialization files (>=2)")
    parser.add_argument("--mutated", nargs="+", required=True, help="Mutated serialization files (>=2)")
    parser.add_argument("--property", default="", help="Property label for the report")
    parser.add_argument("--out", help="Write JSON report")
    args = parser.parse_args()

    baseline_maps = [parse_and_flatten(Path(p)) for p in args.baseline]
    mutated_maps = [parse_and_flatten(Path(p)) for p in args.mutated]

    volatile = volatile_paths(baseline_maps) | volatile_paths(mutated_maps)

    base = baseline_maps[0]
    mut = mutated_maps[0]
    all_paths = sorted(set(base.keys()) | set(mut.keys()))

    stable_deltas = []
    for path in all_paths:
        if path in volatile:
            continue
        before = base.get(path)
        after = mut.get(path)
        if before != after:
            stable_deltas.append(
                {
                    "path": ".".join(str(i) for i in path),
                    "before": before,
                    "after": after,
                }
            )

    report = {
        "property": args.property,
        "baselineRuns": len(baseline_maps),
        "mutatedRuns": len(mutated_maps),
        "baselineAtoms": len(base),
        "mutatedAtoms": len(mut),
        "volatilePaths": len(volatile),
        "stableDeltaCount": len(stable_deltas),
        "stableDeltas": stable_deltas,
        "proven": len(stable_deltas) > 0,
    }
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
