#!/usr/bin/env bash
# Prove a value-property slot by native Form.bin/XML round-trip differential.
#
# When licensed ENTERPRISE auto-execute hangs, this still produces slot evidence:
# baseline payload vs payload after applying one named XML property edit through
# the native PlatformObject package build path (formbin-build-package).
#
# Usage:
#   tools/native_property_slot_proof.sh \
#     --form-bin work/oracle-runtime/seed-button-source/root/Forms/Форма/Ext/Form.bin \
#     --property TextColor \
#     --xml-value '#FF0000'

set -euo pipefail

form_bin=""
property_label=""
xml_value=""
out_dir="scan-output/native-property-proof"
native_bin="sidecars/onec-form-native/build/oof-native"

usage() {
  sed -n '3,20p' "$0" >&2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --form-bin) form_bin=${2:?}; shift 2 ;;
    --property) property_label=${2:?}; shift 2 ;;
    --xml-value) xml_value=${2:?}; shift 2 ;;
    --out-dir) out_dir=${2:?}; shift 2 ;;
    --native-bin) native_bin=${2:?}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$form_bin" || -z "$property_label" || -z "$xml_value" ]]; then
  echo "Required: --form-bin --property --xml-value" >&2
  usage
  exit 2
fi

repo_root=$(pwd)
case "$out_dir" in
  /*) out_abs=$out_dir ;;
  *) out_abs="$repo_root/$out_dir" ;;
esac
case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *)
    echo "Output must be under scan-output/: $out_abs" >&2
    exit 2
    ;;
esac

if [[ ! -x "$native_bin" ]]; then
  make -C sidecars/onec-form-native >/dev/null
fi

prop_dir="$out_abs/$property_label"
rm -rf "$prop_dir"
mkdir -p "$prop_dir"

baseline_xml="$prop_dir/baseline.xml"
edited_xml="$prop_dir/edited.xml"
mutated_bin="$prop_dir/mutated.bin"

"$native_bin" formbin-dump-package "$form_bin" "$baseline_xml" >/dev/null

python3 - "$baseline_xml" "$edited_xml" "$property_label" "$xml_value" <<'PY'
import sys
import xml.etree.ElementTree as ET

baseline_path, edited_path, prop_name, prop_value = sys.argv[1:5]
root = ET.parse(baseline_path).getroot()

def local(tag: str) -> str:
    return tag.split("}", 1)[-1] if "}" in tag else tag

button = None
for el in root.iter():
    if local(el.tag) == "Button":
        button = el
        break
if button is None:
    raise SystemExit("no <Button> in native dump")

# Remove default-omitted value nodes, then set explicit value object.
for child in list(button):
    if local(child.tag) in {"TextColor", "BackColor", "BorderColor", "Font", "Picture"}:
        button.remove(child)

if prop_name == "TextColor":
    node = ET.SubElement(button, "TextColor")
    vo = ET.SubElement(node, "ColorValue", {
        "constructor": "New Color",
        "storage": "absolute-rgb",
    })
    vo.text = prop_value
elif prop_name == "Font":
    node = ET.SubElement(button, "Font")
    vo = ET.SubElement(node, "FontValue", {
        "constructor": "New Font",
        "storage": "inline",
    })
    vo.text = prop_value
elif prop_name == "Picture":
    node = ET.SubElement(button, "Picture")
    vo = ET.SubElement(node, "PictureValue", {
        "constructor": "New Picture",
        "storage": "inline-base64",
    })
    vo.text = prop_value
else:
    raise SystemExit(f"unsupported proof property: {prop_name}")

ET.ElementTree(root).write(edited_path, encoding="utf-8", xml_declaration=True)
print(f"edited {prop_name} on Button")
PY

"$native_bin" formbin-build-package "$form_bin" "$edited_xml" "$mutated_bin" >/dev/null

python3 - <<'PY' "$form_bin" "$mutated_bin" "$prop_dir"
import json
import subprocess
import sys
from pathlib import Path

form_bin, mutated_bin, prop_dir = sys.argv[1:4]
native = Path("sidecars/onec-form-native/build/oof-native")

def payload_hex(path: str) -> str:
    out = subprocess.check_output([str(native), "formbin-info", path], text=True)
    data = json.loads(out)
    for f in data.get("files", []):
        if f.get("name") == "form":
            return f.get("payloadHexPrefix", "")
    return ""

def payload_bytes(path: str) -> bytes:
    info = json.loads(subprocess.check_output([str(native), "formbin-info", path], text=True))
    # Re-read via python pack path: form file is inside container; use unpack-bin
    parts = Path(prop_dir) / ("parts-" + Path(path).stem)
    parts.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        ["python3", "-m", "onec_ordinary_forms.cli", "unpack-bin", "--bin", path, "--out-dir", str(parts)],
        env={"PYTHONPATH": "src"},
    )
    return (parts / "Form.xml").read_bytes()

base = payload_bytes(form_bin)
mut = payload_bytes(mutated_bin)
Path(prop_dir, "baseline-payload.bin").write_bytes(base)
Path(prop_dir, "mutated-payload.bin").write_bytes(mut)
print("baseline bytes", len(base), "mutated bytes", len(mut), "equal", base == mut)
PY

# Write bracket text files for slot diff (strip BOM if present).
python3 - <<'PY' "$prop_dir"
from pathlib import Path
import sys

prop_dir = Path(sys.argv[1])
for name in ("baseline", "mutated"):
    raw = (prop_dir / f"{name}-payload.bin").read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    (prop_dir / f"{name}-payload.txt").write_bytes(raw)
PY

python3 tools/property_slot_diff.py \
  --property "$property_label" \
  --baseline "$prop_dir/baseline-payload.txt" "$prop_dir/baseline-payload.txt" \
  --mutated "$prop_dir/mutated-payload.txt" "$prop_dir/mutated-payload.txt" \
  --out "$prop_dir/slot-evidence.json"
