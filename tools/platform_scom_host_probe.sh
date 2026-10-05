#!/usr/bin/env bash
# Build and run a standalone SCOM_Main loader against installed 1C platform .so files.
set -euo pipefail
if [[ $# -gt 2 || -z "${OOF_PLATFORM_CONTAINER:-}" ]]; then
  echo "Usage: OOF_PLATFORM_CONTAINER=<running-container> $0 [mode] [out-dir]" >&2
  exit 2
fi
if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != "true" ]]; then
  echo "OOF_PLATFORM_CONTAINER is not running" >&2
  exit 2
fi
mode=${1:-0}
out_dir=${2:-scan-output/platform-scom-host-probe}
repo_root=$(cd "$(dirname "$0")/.." && pwd -P)
if ! out_abs=$(python3 - "$repo_root" "$out_dir" <<'PY_PATH'
import pathlib, sys
repo = pathlib.Path(sys.argv[1]).resolve()
scan = repo / "scan-output"
if scan.is_symlink():
    raise SystemExit("scan-output must not be a symbolic link")
if any(ord(ch) < 32 for ch in sys.argv[2]):
    raise SystemExit("output path must not contain control characters")
candidate = pathlib.Path(sys.argv[2])
if not candidate.is_absolute():
    candidate = repo / candidate
if candidate.is_symlink():
    raise SystemExit("output must not be a symbolic link")
resolved = candidate.resolve(strict=False)
try:
    relative = resolved.relative_to(scan)
except ValueError:
    raise SystemExit("output must resolve below scan-output")
if not relative.parts or resolved.exists():
    raise SystemExit("output must be a new directory below scan-output")
print(resolved)
PY_PATH
); then
  exit 2
fi
mkdir -p "$out_abs"
base=$(docker exec "$OOF_PLATFORM_CONTAINER" mktemp -d /tmp/oof-scom-host-probe.XXXXXX)
trap 'docker exec "$OOF_PLATFORM_CONTAINER" rm -rf -- "$base" >/dev/null 2>&1 || true' EXIT
docker exec "$OOF_PLATFORM_CONTAINER" mkdir -p -- "$base/out"
docker cp "$repo_root/tools/oof_scom_host.c" "$OOF_PLATFORM_CONTAINER:$base/oof_scom_host.c"
set +e
docker exec -i "$OOF_PLATFORM_CONTAINER" sh -s -- "$base" "$mode"   "${OOF_SCOM_HOST_FAKE_REGISTRAR:-0}" "${OOF_SCOM_HOST_LOAD_EXTRA:-0}"   "${OOF_SCOM_HOST_ONLY:-}" "${OOF_SCOM_HOST_FAKE_PROCESS_NAME:-}"   "${OOF_SCOM_HOST_CREATE_FORMDOCUMENT:-0}" <<'REMOTE'
set -eu
base=$1
mode=$2
platform=/opt/1cv8/x86_64/8.5.1.1343
if ! command -v gcc >/dev/null 2>&1; then
  echo 'gcc is required in the 1C container' >&2
  exit 127
fi
gcc -O2 -rdynamic -ldl "$base/oof_scom_host.c" -o "$base/oof_scom_host"
set +e
OOF_SCOM_HOST_FAKE_REGISTRAR="$3" OOF_SCOM_HOST_LOAD_EXTRA="$4" OOF_SCOM_HOST_ONLY="$5" OOF_SCOM_HOST_FAKE_PROCESS_NAME="$6" OOF_SCOM_HOST_CREATE_FORMDOCUMENT="$7" LD_LIBRARY_PATH="$platform"   timeout 60 "$base/oof_scom_host" "$platform" "$mode"   >"$base/out/stdout.log" 2>"$base/out/stderr.log"
code=$?
printf '%s\n' "$code" >"$base/out/code.txt"
exit 0
REMOTE
runner_code=$?
set -e
docker cp "$OOF_PLATFORM_CONTAINER:$base/out/." "$out_abs/" >/dev/null
exit "$runner_code"
