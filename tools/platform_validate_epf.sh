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

repo_root=$(cd "$(dirname "$0")/.." && pwd -P)
input_path=$(python3 - "$1" <<'PY_INPUT'
import pathlib, sys
if any(ord(ch) < 32 for ch in sys.argv[1]):
    raise SystemExit("input path must not contain control characters")
p = pathlib.Path(sys.argv[1]).expanduser().resolve(strict=True)
if not p.is_file():
    raise SystemExit("input must be a regular file")
print(p)
PY_INPUT
)

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

out_rel=${out_abs#"$repo_root"/}

if [[ -n "${OOF_PLATFORM_CONTAINER:-}" ]]; then
  if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != "true" ]]; then
    echo "OOF_PLATFORM_CONTAINER is not a running container: $OOF_PLATFORM_CONTAINER" >&2
    exit 2
  fi
  container_base=$(docker exec "$OOF_PLATFORM_CONTAINER" mktemp -d /tmp/oof-platform-validate.XXXXXX)
  trap 'docker exec "$OOF_PLATFORM_CONTAINER" rm -rf -- "$container_base" >/dev/null 2>&1 || true' EXIT
  docker exec "$OOF_PLATFORM_CONTAINER" mkdir -p -- "$container_base/input" "$container_base/out/dump" "$container_base/dbroot"
  docker cp "$input_path" "$OOF_PLATFORM_CONTAINER:$container_base/input/$input_name"
  set +e
  docker exec -i "$OOF_PLATFORM_CONTAINER" sh -s -- "$container_base" "$input_name" <<'REMOTE'
set -eu
container_base=$1
input_name=$2
base="$container_base/dbroot"
db=db
/opt/1cv8/x86_64/8.5.1.1343/ibcmd   infobase --data="$base" --database-path="$db" create --locale=ru_RU   >"$container_base/out/create.log" 2>&1
set +e
xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 DESIGNER   /F "$base/$db"   /DumpExternalDataProcessorOrReportToFiles "$container_base/out/dump/root.xml" "$container_base/input/$input_name"   -Format Hierarchical /Out "$container_base/out/platform-dump.log" -NoTruncate /DisableStartupDialogs   >"$container_base/out/stdout.log" 2>"$container_base/out/stderr.log"
code=$?
printf '%s\n' "$code" >"$container_base/out/code.txt"
exit "$code"
REMOTE
  exec_code=$?
  set -e
  docker cp "$OOF_PLATFORM_CONTAINER:$container_base/out/." "$out_abs/"
  if [[ -f "$out_abs/code.txt" ]]; then
    exit "$(cat "$out_abs/code.txt")"
  fi
  exit "$exec_code"
fi

docker run --rm -i --platform linux/amd64 --entrypoint sh   -v "$repo_root:/workspace"   -v "$input_dir:/input:ro"   -v "$NETHASP_INI_PATH:/opt/1cv8/conf/nethasp.ini:ro"   ghcr.io/mussolene/1c-developer:8.5.1.1343   -s -- "$out_rel" "$input_name" <<'REMOTE'
set -eu
out_rel=$1
input_name=$2
cd /workspace
base=/tmp/oof-platform-validate
db=db
mkdir -p "$base" "/workspace/$out_rel/dump"
/opt/1cv8/x86_64/8.5.1.1343/ibcmd   infobase --data="$base" --database-path="$db" create --locale=ru_RU   >"/workspace/$out_rel/create.log" 2>&1
set +e
xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 DESIGNER   /F "$base/$db"   /DumpExternalDataProcessorOrReportToFiles "/workspace/$out_rel/dump/root.xml" "/input/$input_name"   -Format Hierarchical /Out "/workspace/$out_rel/platform-dump.log" -NoTruncate /DisableStartupDialogs   >"/workspace/$out_rel/stdout.log" 2>"/workspace/$out_rel/stderr.log"
code=$?
printf '%s\n' "$code" >"/workspace/$out_rel/code.txt"
exit "$code"
REMOTE
