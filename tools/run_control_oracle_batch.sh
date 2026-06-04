#!/usr/bin/env bash
# Run the 26 ordinary-control platform oracle scripts and collect runtime streams.
set -euo pipefail

source_root="work/oracle-runtime/blank-source/root.xml"
out_dir="scan-output/platform-control-oracle"
native_bin=${OOF_NATIVE_BIN:-sidecars/onec-form-native/build/oof-native}

usage() {
  cat >&2 <<'TXT'
Usage: tools/run_control_oracle_batch.sh [--source-root root.xml] [--out-dir scan-output/platform-control-oracle]

Requires either OOF_PLATFORM_CONTAINER=<running licensed 1C container> or
NETHASP_INI_PATH=<readable nethasp.ini>. Outputs stay under scan-output/.
TXT
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-root) source_root=${2:?}; shift 2 ;;
    --out-dir) out_dir=${2:?}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

repo_root=$(pwd)
case "$out_dir" in
  /*) out_abs=$out_dir ;;
  *) out_abs="$repo_root/$out_dir" ;;
esac
case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *) echo "Output directory must be under scan-output/: $out_abs" >&2; exit 2 ;;
esac

if [[ ! -f "$source_root" ]]; then
  echo "Source root.xml does not exist: $source_root" >&2
  exit 2
fi
if [[ -z "${OOF_PLATFORM_CONTAINER:-}" && ( -z "${NETHASP_INI_PATH:-}" || ! -r "${NETHASP_INI_PATH:-}" ) ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to a licensed 1C container, or set NETHASP_INI_PATH for docker fallback" >&2
  exit 2
fi
if [[ ! -x "$native_bin" ]]; then
  make -C sidecars/onec-form-native >/dev/null
fi

rm -rf "$out_abs"
mkdir -p "$out_abs"/{scripts,streams,graphs,logs,oracle}
tools/generate_control_oracle_scripts.sh "$out_abs/scripts" > "$out_abs/logs/generate.log"

summary="$out_abs/summary.tsv"
printf 'script\tstatus\tstream_bytes\tmaterialized_items\tschema_backed_items\n' > "$summary"

for script in "$out_abs"/scripts/*.bsl; do
  name=$(basename "$script" .bsl)
  stream="$out_abs/streams/$name.txt"
  run_dir="$out_abs/oracle/$name"
  set +e
  tools/platform_oracle_execute.sh "$source_root" - "$script" "$stream" "$run_dir" \
    >"$out_abs/logs/$name.stdout" 2>"$out_abs/logs/$name.stderr"
  code=$?
  set -e

  status="platform-code-$code"
  stream_bytes=0
  materialized=""
  schema_backed=""
  if [[ -f "$stream" ]]; then
    stream_bytes=$(wc -c < "$stream" | tr -d ' ')
    if "$native_bin" runtime-form-object-graph "$stream" > "$out_abs/graphs/$name.json" 2>"$out_abs/logs/$name.graph.stderr"; then
      status="OK"
      materialized=$(sed -n 's/.*"materializedItems":\([0-9][0-9]*\).*/\1/p' "$out_abs/graphs/$name.json" | head -n 1)
      schema_backed=$(sed -n 's/.*"schemaBackedItems":\([0-9][0-9]*\).*/\1/p' "$out_abs/graphs/$name.json" | head -n 1)
    else
      status="stream-not-form"
    fi
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$status" "$stream_bytes" "$materialized" "$schema_backed" >> "$summary"
done

printf 'control oracle batch summary: %s\n' "$summary"
