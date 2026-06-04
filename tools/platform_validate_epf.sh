#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <input.epf|input.erf> [out-dir]" >&2
  exit 2
fi

if [[ -z "${OOF_PLATFORM_CONTAINER:-}" && ( -z "${NETHASP_INI_PATH:-}" || ! -r "$NETHASP_INI_PATH" ) ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to an already licensed 1C container, or set NETHASP_INI_PATH for throwaway docker run fallback" >&2
  exit 2
fi

repo_root=$(pwd)
abs_path() {
  local path="$1"
  case "$path" in
    ~/*) path="${HOME}${path#~}" ;;
  esac
  local dir
  local base
  dir="$(cd "$(dirname "$path")" && pwd -P)"
  base="$(basename "$path")"
  printf '%s/%s\n' "$dir" "$base"
}

input_path="$(abs_path "$1")"

if [[ ! -f "$input_path" ]]; then
  echo "Input file does not exist: $input_path" >&2
  exit 2
fi

input_dir=$(dirname "$input_path")
input_name=$(basename "$input_path")

if [[ $# -eq 2 ]]; then
  out_dir="$2"
else
  stem=${input_name%.*}
  out_dir="scan-output/platform-validate/$stem"
fi

mkdir -p "$out_dir"
out_abs="$(cd "$out_dir" && pwd -P)"

case "$out_abs" in
  "$repo_root"/*) ;;
  *)
    echo "Output directory must be inside repository: $out_abs" >&2
    exit 2
    ;;
esac

out_rel=${out_abs#"$repo_root"/}

if [[ -n "${OOF_PLATFORM_CONTAINER:-}" ]]; then
  if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != "true" ]]; then
    echo "OOF_PLATFORM_CONTAINER is not a running container: $OOF_PLATFORM_CONTAINER" >&2
    exit 2
  fi
  container_base="/tmp/oof-platform-validate"
  docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "rm -rf '$container_base' && mkdir -p '$container_base/input' '$container_base/out/dump' '$container_base/dbroot'"
  docker cp "$input_path" "$OOF_PLATFORM_CONTAINER:$container_base/input/$input_name"
  set +e
  docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "set -eu
    base='$container_base/dbroot'
    db=db
    /opt/1cv8/x86_64/8.5.1.1343/ibcmd \
      infobase --data=\"\$base\" --database-path=\"\$db\" create --locale=ru_RU \
      >'$container_base/out/create.log' 2>&1
    set +e
    xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 DESIGNER \
      /F \"\$base/\$db\" \
      /DumpExternalDataProcessorOrReportToFiles '$container_base/out/dump/root.xml' '$container_base/input/$input_name' \
      -Format Hierarchical \
      /Out '$container_base/out/platform-dump.log' -NoTruncate \
      /DisableStartupDialogs \
      >'$container_base/out/stdout.log' 2>'$container_base/out/stderr.log'
    code=\$?
    set -e
    echo \"\$code\" >'$container_base/out/code.txt'
    exit \"\$code\"
  "
  exec_code=$?
  set -e
  docker cp "$OOF_PLATFORM_CONTAINER:$container_base/out/." "$out_abs/"
  if [[ -f "$out_abs/code.txt" ]]; then
    exit "$(cat "$out_abs/code.txt")"
  fi
  exit "$exec_code"
fi

docker run --rm --platform linux/amd64 --entrypoint sh \
  -v "$repo_root:/workspace" \
  -v "$input_dir:/input:ro" \
  -v "$NETHASP_INI_PATH:/opt/1cv8/conf/nethasp.ini:ro" \
  ghcr.io/mussolene/1c-developer:8.5.1.1343 \
  -lc "set -eu
    cd /workspace
    base=/tmp/oof-platform-validate
    db=db
    rm -rf \"\$base\"
    mkdir -p \"\$base\" \"/workspace/$out_rel/dump\"
    /opt/1cv8/x86_64/8.5.1.1343/ibcmd \
      infobase --data=\"\$base\" --database-path=\"\$db\" create --locale=ru_RU \
      >\"/workspace/$out_rel/create.log\" 2>&1
    set +e
    xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 DESIGNER \
      /F \"\$base/\$db\" \
      /DumpExternalDataProcessorOrReportToFiles \"/workspace/$out_rel/dump/root.xml\" \"/input/$input_name\" \
      -Format Hierarchical \
      /Out \"/workspace/$out_rel/platform-dump.log\" -NoTruncate \
      /DisableStartupDialogs \
      >\"/workspace/$out_rel/stdout.log\" 2>\"/workspace/$out_rel/stderr.log\"
    code=\$?
    set -e
    echo \"\$code\" >\"/workspace/$out_rel/code.txt\"
    exit \"\$code\"
  "
