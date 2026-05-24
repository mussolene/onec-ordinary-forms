#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  cat >&2 <<'USAGE'
Usage:
  tools/ghidra_extract_form_xrefs.sh /path/to/platform-library.so [out-dir]

Runs Ghidra headless in Docker and extracts symbol/xref metadata around
ordinary-form ListStream serializer candidates. Keep platform binaries in
/tmp, work/, or another ignored/private directory.
USAGE
  exit 2
fi

input_path="$1"
out_dir="${2:-/tmp/oof-ghidra-out}"
image="${GHIDRA_IMAGE:-blacktop/ghidra@sha256:1f53ed72405d1f51122c498df0f3c0660b62a6ebe9dd941199c5ca1739959b4b}"

if [[ ! -f "$input_path" ]]; then
  echo "Input library does not exist: $input_path" >&2
  exit 1
fi

mkdir -p "$out_dir"

input_abs="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$input_path")"
out_abs="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$out_dir")"
script_abs="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$(dirname "$0")/ghidra/ExtractFormXrefs.java")"
input_dir="$(dirname "$input_abs")"
input_file="$(basename "$input_abs")"
project_dir="$out_abs/project"
json_out="/work/out/${input_file}.ghidra-xrefs.json"

rm -rf "$project_dir"
mkdir -p "$project_dir"

docker run --rm \
  -v "$input_dir:/work/input:ro" \
  -v "$out_abs:/work/out" \
  -v "$script_abs:/work/scripts/ExtractFormXrefs.java:ro" \
  --entrypoint /ghidra/support/analyzeHeadless \
  "$image" \
  /work/out/project OOF \
  -import "/work/input/$input_file" \
  -scriptPath /work/scripts \
  -postScript ExtractFormXrefs.java "$json_out" \
  -deleteProject

echo "$out_abs/${input_file}.ghidra-xrefs.json"
