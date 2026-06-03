#!/usr/bin/env bash
set -euo pipefail

# Batch property-proof harness.
#
# Drives the licensed 1C platform oracle to PROVE which list-stream slot a single
# ordinary-form value property occupies. For each property it runs the baseline
# serialization (no mutation) and a mutated serialization (one property changed)
# several times, then subtracts the volatile (non-idempotent) noise with
# tools/property_slot_diff.py to leave only the stable slot delta.
#
# This is the evidence generator that promotes value properties (TextColor,
# BackColor, Font, Picture, ...) from "readable coverage gap" to "proven
# writable slot". It does NOT itself change the schema; it produces evidence.
#
# Usage:
#   OOF_PLATFORM_CONTAINER=oof-1c85-licensed \
#   tools/platform_property_proof.sh \
#     --source-root work/oracle-runtime/source/root.xml \
#     --property TextColor \
#     --mutation 'ЭтаФорма.ЭлементыФормы.SeedButton.ЦветТекста = Новый Цвет(255,0,0);' \
#     --runs 2

usage() {
  sed -n '3,30p' "$0" >&2
}

source_root=""
property_label=""
mutation=""
runs=2
out_dir="scan-output/platform-property-proof"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-root) source_root=${2:?}; shift 2 ;;
    --property) property_label=${2:?}; shift 2 ;;
    --mutation) mutation=${2:?}; shift 2 ;;
    --runs) runs=${2:?}; shift 2 ;;
    --out-dir) out_dir=${2:?}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$source_root" || -z "$property_label" || -z "$mutation" ]]; then
  echo "Missing required option (--source-root, --property, --mutation)" >&2
  usage
  exit 2
fi
if [[ -z "${OOF_PLATFORM_CONTAINER:-}" && ( -z "${NETHASP_INI_PATH:-}" || ! -r "${NETHASP_INI_PATH:-}" ) ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to a licensed 1C container (e.g. oof-1c85-licensed)" >&2
  exit 2
fi

repo_root=$(pwd)
case "$out_dir" in
  /*) out_abs=$out_dir ;;
  *) out_abs="$repo_root/$out_dir" ;;
esac
case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *) echo "Output directory must be under scan-output/: $out_abs" >&2; exit 2 ;;
esac

prop_dir="$out_abs/$property_label"
rm -rf "$prop_dir"
mkdir -p "$prop_dir/scripts" "$prop_dir/streams" "$prop_dir/oracle"

baseline_script="$prop_dir/scripts/baseline.bsl"
mutated_script="$prop_dir/scripts/mutated.bsl"
printf 'Результат = ЭтаФорма;\n' > "$baseline_script"
printf '%s\nРезультат = ЭтаФорма;\n' "$mutation" > "$mutated_script"

run_oracle() {
  local script="$1"
  local output="$2"
  tools/platform_oracle_execute.sh \
    "$source_root" \
    - \
    "$script" \
    "$output" \
    "$prop_dir/oracle/$(basename "$output" .txt)"
}

baseline_files=()
mutated_files=()
for i in $(seq 1 "$runs"); do
  b="$prop_dir/streams/baseline-$i.txt"
  m="$prop_dir/streams/mutated-$i.txt"
  echo "proof[$property_label] baseline run $i" >&2
  run_oracle "$baseline_script" "$b"
  echo "proof[$property_label] mutated run $i" >&2
  run_oracle "$mutated_script" "$m"
  baseline_files+=("$b")
  mutated_files+=("$m")
done

python3 tools/property_slot_diff.py \
  --property "$property_label" \
  --baseline "${baseline_files[@]}" \
  --mutated "${mutated_files[@]}" \
  --out "$prop_dir/slot-evidence.json"
