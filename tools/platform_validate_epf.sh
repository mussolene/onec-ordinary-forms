#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <input.epf|input.erf> [out-dir]" >&2
  exit 2
fi

if [[ -z "${NETHASP_INI_PATH:-}" || ! -r "$NETHASP_INI_PATH" ]]; then
  echo "NETHASP_INI_PATH must point to readable nethasp.ini" >&2
  exit 2
fi

repo_root=$(pwd)
input_path=$(python3 - "$1" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).expanduser().resolve())
PY
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

out_abs=$(python3 - "$out_dir" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).expanduser().resolve())
PY
)

case "$out_abs" in
  "$repo_root"/*) ;;
  *)
    echo "Output directory must be inside repository: $out_abs" >&2
    exit 2
    ;;
esac

out_rel=${out_abs#"$repo_root"/}
mkdir -p "$out_abs"

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

