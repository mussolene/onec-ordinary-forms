#!/usr/bin/env python3
"""Object-model coverage gate for the native ordinary-form engine.

This gate is the anti-loop guardrail described in
docs/ordinary-form-pattern-audit.md. It refuses two failure modes that have
historically caused circular rewrites of colors/fonts/pictures:

1. writable-but-unproven slot: a property is exposed as ``writable=true`` while
   its slot codec is not in the proven settable set, or its ``codecStatus`` is
   still ``pending``/``unproven``. Writability must come from proven slot
   evidence (runtime differential oracle), never from schema existence alone.
2. raw-shape vocabulary: a public object-model name leaks renamed list-stream
   structure (``SerializationProfile``, ``slotN``, ``RawBracket``,
   ``ListStream``, ``FormBin``, ``ObjectModel``, ``LogicalStream``,
   ``PlatformRecords``, ``BracketStream``).

Unproven value properties are allowed to exist as readable coverage gaps with a
``slotBinding``/``codecStatus`` of record. They are not allowed to be writable
until a proven slot binding promotes them.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


# Slot codecs whose base-preserving setter is proven and implemented. This must
# stay aligned with the native runtime "implementedWritableSlotCodecs" surface.
PROVEN_WRITABLE_CODECS = {
    "name-record",
    "scalar-flag",
    "position-record",
    "binding-record",
    "attribute-record",
    "command-record",
    "event-action-record",
}

# Value-layer codecs whose info8 object-property semantics are not yet proven by
# the runtime differential oracle. They may be readable, never writable, until a
# proven slot binding is recorded.
UNPROVEN_VALUE_CODECS = {
    "color-record",
    "font-record",
    "picture-record",
    "border-record",
}

PENDING_STATUS_TOKENS = ("pending", "unproven", "unknown-residue")

FORBIDDEN_RAW_SHAPE_TOKENS = (
    "SerializationProfile",
    "RawBracket",
    "BracketStream",
    "ListStream",
    "FormBin",
    "ObjectModel",
    "LogicalStream",
    "PlatformRecords",
)
FORBIDDEN_SLOTN = re.compile(r"\bslot\d+\b", re.IGNORECASE)


def run_native(native_bin: Path, command: str) -> dict:
    proc = subprocess.run(
        [str(native_bin), command],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(proc.stdout)


def is_pending_status(status: str) -> bool:
    status = (status or "").lower()
    return any(token in status for token in PENDING_STATUS_TOKENS)


def check_writability(label: str, name: str, slot_codec: str, codec_status: str, writable: bool) -> list[str]:
    if not writable:
        return []
    violations: list[str] = []
    if slot_codec in UNPROVEN_VALUE_CODECS:
        violations.append(
            f"{label} {name!r}: writable=true but slotCodec={slot_codec!r} is an "
            f"unproven value codec (info8 object semantics not proven by oracle)"
        )
    elif slot_codec not in PROVEN_WRITABLE_CODECS:
        violations.append(
            f"{label} {name!r}: writable=true but slotCodec={slot_codec!r} is not "
            f"in the proven settable set {sorted(PROVEN_WRITABLE_CODECS)}"
        )
    if is_pending_status(codec_status):
        violations.append(
            f"{label} {name!r}: writable=true but codecStatus={codec_status!r} is "
            f"not proven"
        )
    return violations


def check_raw_shape(label: str, name: str) -> list[str]:
    violations: list[str] = []
    for token in FORBIDDEN_RAW_SHAPE_TOKENS:
        if token in name:
            violations.append(f"{label} {name!r}: raw-shape vocabulary token {token!r} in public name")
    if FORBIDDEN_SLOTN.search(name):
        violations.append(f"{label} {name!r}: raw slotN vocabulary in public name")
    return violations


def gate(native_bin: Path) -> tuple[list[str], dict]:
    violations: list[str] = []
    stats = {
        "schemaMembersChecked": 0,
        "registryDescriptorsChecked": 0,
        "writableValueCodecs": 0,
        "readableValueCoverageGaps": 0,
    }

    schema = run_native(native_bin, "platform-object-schema")
    for schema_object in schema.get("schemas", []):
        type_name = schema_object.get("typeName", "")
        for member in schema_object.get("members", schema_object.get("xsdMembers", [])):
            stats["schemaMembersChecked"] += 1
            name = member.get("name", "")
            slot_codec = member.get("slotCodec", "")
            codec_status = member.get("codecStatus", "")
            writable = bool(member.get("writable", False))
            label = f"schema[{type_name}]"
            violations += check_writability(label, name, slot_codec, codec_status, writable)
            violations += check_raw_shape(label, name)
            if slot_codec in UNPROVEN_VALUE_CODECS:
                if writable:
                    stats["writableValueCodecs"] += 1
                else:
                    stats["readableValueCoverageGaps"] += 1

    registry = run_native(native_bin, "platform-property-registry")
    for descriptor in registry.get("descriptors", []):
        stats["registryDescriptorsChecked"] += 1
        name = descriptor.get("name", "")
        slot_codec = descriptor.get("slotCodec", "")
        codec_status = descriptor.get("codecStatus", "")
        writable = bool(descriptor.get("writable", False))
        label = "registry"
        violations += check_writability(label, name, slot_codec, codec_status, writable)
        violations += check_raw_shape(label, name)

    return violations, stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--native-bin",
        default=str(Path(__file__).resolve().parents[1] / "sidecars/onec-form-native/build/oof-native"),
        help="Path to the built oof-native binary",
    )
    args = parser.parse_args()
    native_bin = Path(args.native_bin)
    if not native_bin.exists():
        print(f"object-model-gate: native binary not found: {native_bin}", file=sys.stderr)
        return 2

    violations, stats = gate(native_bin)
    print("object-model-gate stats: " + json.dumps(stats))
    if violations:
        print(f"object-model-gate: FAIL ({len(violations)} violation(s))", file=sys.stderr)
        for violation in violations:
            print(f"  - {violation}", file=sys.stderr)
        return 1
    print("object-model-gate: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
