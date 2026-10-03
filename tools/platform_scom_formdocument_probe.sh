#!/usr/bin/env bash
# Run an in-process SCOM/FormDocument probe inside the 1C platform process.
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <input.epf|input.erf> [out-dir]" >&2
  exit 2
fi

if [[ -z "${OOF_PLATFORM_CONTAINER:-}" ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to a running 1C container" >&2
  exit 2
fi
if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != "true" ]]; then
  echo "OOF_PLATFORM_CONTAINER is not running: $OOF_PLATFORM_CONTAINER" >&2
  exit 2
fi

repo_root=$(pwd)
input=$1
out_dir=${2:-scan-output/platform-scom-formdocument-probe}

abs_path() {
  local path="$1"
  case "$path" in
    /*) printf '%s\n' "$path" ;;
    *) printf '%s/%s\n' "$(cd "$(dirname "$path")" && pwd)" "$(basename "$path")" ;;
  esac
}

input_abs=$(abs_path "$input")
case "$out_dir" in
  /*) out_abs="$out_dir" ;;
  *) out_abs="$repo_root/$out_dir" ;;
esac
case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *) echo "Output directory must be under scan-output/: $out_abs" >&2; exit 2 ;;
esac

if [[ ! -f "$input_abs" ]]; then
  echo "Input does not exist: $input_abs" >&2
  exit 2
fi
if [[ ! -f tools/oof_scom_formdocument_probe.c ]]; then
  echo "Missing tools/oof_scom_formdocument_probe.c" >&2
  exit 2
fi

rm -rf "$out_abs"
mkdir -p "$out_abs"

base=/tmp/oof-scom-formdocument-probe
docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "rm -rf '$base' && mkdir -p '$base/input' '$base/out/dump' '$base/dbroot'"
docker cp "$input_abs" "$OOF_PLATFORM_CONTAINER:$base/input/input.${input_abs##*.}"
docker cp "$repo_root/tools/oof_scom_formdocument_probe.c" "$OOF_PLATFORM_CONTAINER:$base/oof_scom_formdocument_probe.c"

set +e
docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "set -eu
  platform=/opt/1cv8/x86_64/8.5.1.1343
  if ! command -v gcc >/dev/null 2>&1; then
    echo 'gcc is required in the 1C container to build the Linux LD_PRELOAD probe' >&2
    exit 127
  fi
  gcc -shared -fPIC -O2 -ldl '$base/oof_scom_formdocument_probe.c' -o '$base/oof_scom_formdocument_probe.so'
  \"\$platform/ibcmd\" infobase --data='$base/dbroot' --database-path=db create --locale=ru_RU \
    >'$base/out/create.log' 2>&1
  set +e
  OOF_PROXY_FORMDOCUMENT_FACTORY=1 \
  OOF_CREATE_FORMDOCUMENT=1 \
  LD_PRELOAD='$base/oof_scom_formdocument_probe.so' \
  xvfb-run -a timeout 120 \"\$platform/1cv8\" DESIGNER \
    /F '$base/dbroot/db' \
    /DumpExternalDataProcessorOrReportToFiles '$base/out/dump/root.xml' '$base/input/input.${input_abs##*.}' \
    -Format Hierarchical \
    /Out '$base/out/platform-dump.log' -NoTruncate /DisableStartupDialogs \
    >'$base/out/stdout.log' 2>'$base/out/stderr.log'
  code=\$?
  echo \"\$code\" >'$base/out/code.txt'
  exit \"\$code\"
"
code=$?
set -e

docker cp "$OOF_PLATFORM_CONTAINER:$base/out/." "$out_abs/" >/dev/null
exit "$code"
