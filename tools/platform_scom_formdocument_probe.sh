#!/usr/bin/env bash
# Run an in-process SCOM/FormDocument probe inside the 1C platform process.
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 || -z "${OOF_PLATFORM_CONTAINER:-}" ]]; then
  echo "Usage: OOF_PLATFORM_CONTAINER=<running-container> $0 <input.epf|input.erf> [out-dir]" >&2
  exit 2
fi
if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != "true" ]]; then
  echo "OOF_PLATFORM_CONTAINER is not running" >&2
  exit 2
fi
input_abs=$(python3 - "$1" <<'PY_INPUT'
import pathlib, sys
if any(ord(ch) < 32 for ch in sys.argv[1]):
    raise SystemExit("input path must not contain control characters")
p = pathlib.Path(sys.argv[1]).resolve(strict=True)
if not p.is_file():
    raise SystemExit("input must be a regular file")
print(p)
PY_INPUT
)
case "$input_abs" in
  *.epf|*.EPF) extension=epf ;;
  *.erf|*.ERF) extension=erf ;;
  *) echo "Input must be an EPF or ERF file" >&2; exit 2 ;;
esac
out_dir=${2:-scan-output/platform-scom-formdocument-probe}
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
base=$(docker exec "$OOF_PLATFORM_CONTAINER" mktemp -d /tmp/oof-scom-formdocument-probe.XXXXXX)
trap 'docker exec "$OOF_PLATFORM_CONTAINER" rm -rf -- "$base" >/dev/null 2>&1 || true' EXIT
docker exec "$OOF_PLATFORM_CONTAINER" mkdir -p -- "$base/input" "$base/out/dump" "$base/dbroot"
docker cp "$input_abs" "$OOF_PLATFORM_CONTAINER:$base/input/input.$extension"
docker cp "$repo_root/tools/oof_scom_formdocument_probe.c" "$OOF_PLATFORM_CONTAINER:$base/oof_scom_formdocument_probe.c"
set +e
docker exec -i "$OOF_PLATFORM_CONTAINER" sh -s -- "$base" "$extension" <<'REMOTE'
set -eu
base=$1
extension=$2
platform=/opt/1cv8/x86_64/8.5.1.1343
if ! command -v gcc >/dev/null 2>&1; then
  echo 'gcc is required in the 1C container' >&2
  exit 127
fi
gcc -shared -fPIC -O2 -ldl "$base/oof_scom_formdocument_probe.c" -o "$base/oof_scom_formdocument_probe.so"
"$platform/ibcmd" infobase --data="$base/dbroot" --database-path=db create --locale=ru_RU   >"$base/out/create.log" 2>&1
set +e
OOF_PROXY_FORMDOCUMENT_FACTORY=1 OOF_CREATE_FORMDOCUMENT=1 LD_PRELOAD="$base/oof_scom_formdocument_probe.so"   xvfb-run -a timeout 120 "$platform/1cv8" DESIGNER   /F "$base/dbroot/db"   /DumpExternalDataProcessorOrReportToFiles "$base/out/dump/root.xml" "$base/input/input.$extension"   -Format Hierarchical /Out "$base/out/platform-dump.log" -NoTruncate /DisableStartupDialogs   >"$base/out/stdout.log" 2>"$base/out/stderr.log"
code=$?
printf '%s\n' "$code" >"$base/out/code.txt"
exit "$code"
REMOTE
code=$?
set -e
docker cp "$OOF_PLATFORM_CONTAINER:$base/out/." "$out_abs/" >/dev/null
exit "$code"
