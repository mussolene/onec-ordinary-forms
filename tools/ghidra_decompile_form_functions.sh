#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  cat >&2 <<'USAGE'
Usage:
  tools/ghidra_decompile_form_functions.sh /path/to/platform-library.so [out-dir] [address-or-function ...]

Runs Ghidra headless in Docker and decompiles ordinary-form serializer
candidates. If no functions are provided, the default target set covers the
known cf_form_controls_info8 xrefs plus ListOutStream/ListInStream paths:
  FUN_002709e0 FUN_00270da0 FUN_00270fe0 FUN_002c9430 FUN_00255f70 FUN_00256510

Keep platform binaries and generated decompile output in /tmp, work/, or
another ignored/private directory. Do not commit those artifacts.
USAGE
  exit 2
fi

input_path="$1"
shift

out_dir="/tmp/oof-ghidra-out"
is_target() {
  case "$1" in
    FUN_*|0x*) return 0 ;;
  esac
  [[ "$1" =~ ^[0-9A-Fa-f]+$ ]]
}

if [[ $# -gt 0 ]] && ! is_target "$1"; then
  out_dir="$1"
  shift
fi

image="${GHIDRA_IMAGE:-blacktop/ghidra@sha256:1f53ed72405d1f51122c498df0f3c0660b62a6ebe9dd941199c5ca1739959b4b}"

if [[ ! -f "$input_path" ]]; then
  echo "Input library does not exist: $input_path" >&2
  exit 1
fi

mkdir -p "$out_dir"

input_abs="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$input_path")"
out_abs="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$out_dir")"
script_abs="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$(dirname "$0")/ghidra/ExtractFormDecompile.java")"
input_dir="$(dirname "$input_abs")"
input_file="$(basename "$input_abs")"
project_dir="$out_abs/project-decompile"
json_out="/work/out/${input_file}.ghidra-decompile.json"

rm -rf "$project_dir"
mkdir -p "$project_dir"

docker run --rm \
  -v "$input_dir:/work/input:ro" \
  -v "$out_abs:/work/out" \
  -v "$script_abs:/work/scripts/ExtractFormDecompile.java:ro" \
  --entrypoint /ghidra/support/analyzeHeadless \
  "$image" \
  /work/out/project-decompile OOF \
  -import "/work/input/$input_file" \
  -scriptPath /work/scripts \
  -postScript ExtractFormDecompile.java "$json_out" "$@" \
  -deleteProject

echo "$out_abs/${input_file}.ghidra-decompile.json"
