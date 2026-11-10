#!/usr/bin/env python3
"""Fail closed on missing, stale or incompatible release evidence.

Native platform checks run in the licensed environment. Their reviewed public
summary binds to sourceDigest; this gate reruns portable tests and CLI coverage.
Print the digest with --source-digest after freezing product and gate sources.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
TAG = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?(\+[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?")
ISSUE = re.compile(r"https://github\.com/[^/\s]+/[^/\s]+/issues/[1-9][0-9]*")
NATIVE_CHECKS = ("strictDesigner", "xmlBinRoundTrip", "nativeGetters", "strictReload")
ZERO_REMAINING = ("properties", "events", "unknownDefaults", "missingStorageCodecs", "unknownValueKinds")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def source_digest():
    paths = set(subprocess.check_output(
        ["git", "ls-files", "-z", "--", "CMakeLists.txt", "cmake", "sidecars", "schemas"],
        cwd=ROOT).decode().split("\0")) - {""}
    paths.update(("Makefile", ".github/workflows/release.yml", "tools/release_gate.py"))
    digest = hashlib.sha256()
    for name in sorted(paths):
        if Path(name).name == "AGENTS.md" or "docs" in Path(name).parts:
            continue
        path = ROOT / name
        require(not path.is_symlink(), f"Fingerprint refuses symlink: {name}")
        digest.update(name.encode() + b"\0" + hashlib.sha256(path.read_bytes()).digest())
    return "sha256:" + digest.hexdigest()


def validate_manifest(manifest, tag, digest):
    match = TAG.fullmatch(tag)
    require(match is not None, "RELEASE_TAG must be vMAJOR.MINOR.PATCH with optional prerelease/build suffix")
    prerelease = bool(match.group(4))
    require(type(manifest.get("schemaVersion")) is int and manifest["schemaVersion"] == 1,
            "Manifest schemaVersion must be 1")
    require(manifest.get("version") == tag[1:], "Manifest version differs from RELEASE_TAG")
    require(manifest.get("sourceDigest") == digest, "Native proof is stale: sourceDigest differs")
    verification = manifest["verification"]
    require(type(verification.get("realCorpusComplete")) is bool, "realCorpusComplete must be explicit")
    require(type(verification.get("descriptorCoverageComplete")) is bool,
            "descriptorCoverageComplete must be explicit")
    native = verification["native"]
    for check in NATIVE_CHECKS:
        require(native.get(check) == "PASS", f"Native proof missing PASS: {check}")
    refs = native.get("evidenceRefs")
    require(isinstance(refs, list) and refs, "Native evidenceRefs must not be empty")
    require(all(isinstance(ref, str) and re.fullmatch(r"(?:ev_[A-Za-z0-9_-]+|https://[^\s]+)", ref)
                for ref in refs), "Native evidenceRefs must be OACS evidence IDs or HTTPS URLs")
    limitations = manifest["limitations"]
    require(isinstance(limitations, list), "limitations must be a list")
    by_id = {}
    for item in limitations:
        require(isinstance(item.get("id"), str) and item["id"] and item["id"] not in by_id,
                "Limitations need unique nonempty IDs")
        require(isinstance(item.get("issue"), str) and ISSUE.fullmatch(item["issue"]),
                "Every limitation needs a GitHub issue URL")
        require(type(item.get("open")) is bool, "Every limitation needs explicit open status")
        by_id[item["id"]] = item
    if not verification["realCorpusComplete"]:
        require(prerelease, "Stable release requires complete real corpus validation")
        require(any(by_id.get(key, {}).get("open") is True
                    for key in ("real-corpus", "legacy-table-columns")),
                "Partial real corpus validation requires an open limitation issue")
    if not prerelease:
        require(verification["descriptorCoverageComplete"], "Stable release requires complete descriptor coverage")
        require(not any(item["open"] for item in limitations), "Stable release has open limitations")
    return prerelease, by_id


def validate_coverage(coverage, manifest, prerelease, limitations):
    require(coverage.get("ok") is True and coverage.get("targetPlatformVersion") == "8.5.1.1343" and
            coverage.get("catalogControlCount") == 26 and
            coverage.get("targetControlCount") == 25 and
            coverage.get("excludedControls") == ["ActiveXControl"],
            "Control coverage must match the reviewed target: 25 of 26, ActiveXControl excluded")
    require("activex" in limitations, "Excluded ActiveXControl requires an explicit limitation issue")
    remaining = coverage["remaining"]
    require(set(remaining) == set(ZERO_REMAINING) | {"unclassifiedValueCodecs"},
            "Unexpected coverage counters require release gate review")
    for key in ZERO_REMAINING:
        require(type(remaining[key]) is int and remaining[key] == 0, f"Coverage remaining.{key} must be zero")
    live = remaining["unclassifiedValueCodecs"]
    require(type(live) is int and live >= 0, "Invalid unclassifiedValueCodecs")
    declared = manifest["verification"]["descriptorCoverageComplete"]
    require(type(coverage.get("descriptorCoverageComplete")) is bool and
            coverage["descriptorCoverageComplete"] == declared,
            "Manifest descriptorCoverageComplete differs from actual CLI coverage")
    if live:
        limitation = limitations.get("live-property-values", {})
        require(prerelease and live <= 14 and limitation.get("open") is True and
                type(limitation.get("remaining")) is int and limitation["remaining"] == live,
                "Unclassified live values require a matching prerelease limitation (maximum 14)")
    if not declared:
        require(prerelease and live > 0, "Incomplete descriptor coverage has no supported prerelease exception")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-digest", action="store_true")
    parser.add_argument("--tag", default="")
    parser.add_argument("--manifest", type=Path, default=ROOT / "docs/release-evidence.json")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/sidecars/onec-form-native/oof")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--ctest", default="ctest")
    args = parser.parse_args()
    digest = source_digest()
    if args.source_digest:
        print(digest)
        return
    manifest = json.loads(args.manifest.read_text())
    prerelease, limitations = validate_manifest(manifest, args.tag, digest)
    binary = str(args.binary.resolve())
    version = subprocess.check_output([binary, "--version"], text=True).strip()
    require(version == "oof " + args.tag[1:], "CLI version differs from RELEASE_TAG")
    coverage = json.loads(subprocess.check_output([binary, "--coverage", "--json"], text=True))
    validate_coverage(coverage, manifest, prerelease, limitations)
    command = [args.ctest, "--test-dir", str(args.build_dir), "--build-config", args.config,
               "--output-on-failure", "--no-tests=error"]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run(command + ["-L", "smoke"], cwd=ROOT, check=True)
    require(source_digest() == digest, "Sources changed while release gate ran")
    print(f"Release gate PASS: {args.tag}; {digest}; prerelease={str(prerelease).lower()}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        print(f"Release gate FAIL: {error}", file=sys.stderr)
        sys.exit(1)
