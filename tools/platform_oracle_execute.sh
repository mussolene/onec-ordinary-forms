#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 4 || $# -gt 5 ]]; then
  echo "Usage: $0 <source-root.xml> <input-stream.txt|-> <script.bsl> <output-stream.txt> [out-dir]" >&2
  exit 2
fi

if [[ -z "${OOF_PLATFORM_CONTAINER:-}" && ( -z "${NETHASP_INI_PATH:-}" || ! -r "$NETHASP_INI_PATH" ) ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to an already licensed 1C container, or set NETHASP_INI_PATH for throwaway docker run fallback" >&2
  exit 2
fi

repo_root=$(pwd)
source_root=$(python3 - "$1" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).expanduser().resolve())
PY
)
input_stream=$2
script_path=$(python3 - "$3" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).expanduser().resolve())
PY
)
output_stream=$(python3 - "$4" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).expanduser().resolve())
PY
)

container_path() {
  local path="$1"
  case "$path" in
    "$repo_root"/*) printf '/workspace/%s' "${path#"$repo_root"/}" ;;
    *)
      echo "Path must be inside repository so it is visible in the container: $path" >&2
      exit 2
      ;;
  esac
}

if [[ ! -f "$source_root" ]]; then
  echo "Source root.xml does not exist: $source_root" >&2
  exit 2
fi
if [[ ! -f "$script_path" ]]; then
  echo "Oracle script does not exist: $script_path" >&2
  exit 2
fi
if [[ "$input_stream" != "-" && ! -f "$input_stream" ]]; then
  echo "Input stream does not exist: $input_stream" >&2
  exit 2
fi

if [[ $# -eq 5 ]]; then
  out_dir=$5
else
  out_dir="scan-output/platform-oracle-execute"
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

rm -rf "$out_abs"
mkdir -p "$out_abs"/{source,parts,logs,conf}
cp -R "$(dirname "$source_root")"/. "$out_abs/source/"
printf 'DisableUnsafeActionProtection=.*\n' > "$out_abs/conf/conf.cfg"

form_bin=$(find "$out_abs/source" -path '*/Forms/*/Ext/Form.bin' -type f | head -n 1)
if [[ -z "$form_bin" ]]; then
  echo "Cannot find Forms/*/Ext/Form.bin under copied source" >&2
  exit 2
fi

PYTHONPATH=src python3 -m onec_ordinary_forms.cli unpack-bin --bin "$form_bin" --out-dir "$out_abs/parts" \
  >"$out_abs/logs/unpack.log" 2>&1

cat > "$out_abs/parts/Module.bsl" <<'BSL'
Процедура ПриОткрытии()
	Части = СтрРазделить(ПараметрЗапуска, "|");
	ПутьВход = "";
	ПутьВыход = "";
	ПутьСкрипт = "";
	Если Части.Количество() > 0 Тогда ПутьВход = Части[0]; КонецЕсли;
	Если Части.Количество() > 1 Тогда ПутьВыход = Части[1]; КонецЕсли;
	Если Части.Количество() > 2 Тогда ПутьСкрипт = Части[2]; КонецЕсли;

	Вход = "";
	Если ПутьВход <> "" Тогда
		ДокВход = Новый ТекстовыйДокумент;
		ДокВход.Прочитать(ПутьВход);
		Вход = ДокВход.ПолучитьТекст();
	КонецЕсли;

	Объект = Неопределено;
	Если Вход <> "" Тогда
		Объект = ЗначениеИзСтрокиВнутр(Вход);
	КонецЕсли;

	Результат = ЭтаФорма;
	Если ПутьСкрипт <> "" Тогда
		ДокСкрипт = Новый ТекстовыйДокумент;
		ДокСкрипт.Прочитать(ПутьСкрипт);
		Выполнить(ДокСкрипт.ПолучитьТекст());
	КонецЕсли;

	Если ПутьВыход <> "" Тогда
		ДокВыход = Новый ТекстовыйДокумент;
		ДокВыход.УстановитьТекст(ЗначениеВСтрокуВнутр(Результат));
		ДокВыход.Записать(ПутьВыход, КодировкаТекста.UTF8);
	КонецЕсли;

	Сообщить("OOF_ORACLE_OK");
	ЗавершитьРаботуСистемы(Ложь);
КонецПроцедуры
BSL

PYTHONPATH=src python3 -m onec_ordinary_forms.cli pack-bin --parts-dir "$out_abs/parts" --out-bin "$form_bin" \
  >"$out_abs/logs/pack.log" 2>&1

input_abs=""
if [[ "$input_stream" != "-" ]]; then
  input_abs=$(python3 - "$input_stream" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).expanduser().resolve())
PY
)
fi
input_container=""
if [[ -n "$input_abs" ]]; then
  input_container=$(container_path "$input_abs")
fi
output_container=$(container_path "$output_stream")
script_container=$(container_path "$script_path")

docker_env=()
if [[ "${PLATFORM_ORACLE_LD_DEBUG:-}" == "1" ]]; then
  docker_env+=("-e" "LD_DEBUG=libs")
fi
if [[ -n "${PLATFORM_ORACLE_LD_AUDIT:-}" ]]; then
  docker_env+=("-e" "LD_AUDIT=${PLATFORM_ORACLE_LD_AUDIT}")
fi

if [[ -n "${OOF_PLATFORM_CONTAINER:-}" ]]; then
  if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != "true" ]]; then
    echo "OOF_PLATFORM_CONTAINER is not a running container: $OOF_PLATFORM_CONTAINER" >&2
    exit 2
  fi
  container_base="/tmp/oof-platform-oracle"
  docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "rm -rf '$container_base' && mkdir -p '$container_base/source' '$container_base/logs' '$container_base/dbroot'"
  docker cp "$out_abs/source/." "$OOF_PLATFORM_CONTAINER:$container_base/source/"
  docker cp "$script_path" "$OOF_PLATFORM_CONTAINER:$container_base/script.bsl"
  if [[ -n "$input_abs" ]]; then
    docker cp "$input_abs" "$OOF_PLATFORM_CONTAINER:$container_base/input.txt"
  else
    docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "printf '' > '$container_base/input.txt'"
  fi
  set +e
  docker exec ${docker_env[@]+"${docker_env[@]}"} "$OOF_PLATFORM_CONTAINER" sh -lc "set -eu
    base='$container_base/dbroot'
    db=db
    /opt/1cv8/x86_64/8.5.1.1343/ibcmd infobase --data=\"\$base\" --database-path=\"\$db\" create --locale=ru_RU \
      >'$container_base/logs/create.log' 2>&1
    xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 DESIGNER \
      /F \"\$base/\$db\" \
      /LoadExternalDataProcessorOrReportFromFiles '$container_base/source/root.xml' '$container_base/oracle.epf' \
      /Out '$container_base/logs/load.log' -NoTruncate /DisableStartupDialogs \
      >'$container_base/logs/load-stdout.log' 2>'$container_base/logs/load-stderr.log'
    set +e
    xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 ENTERPRISE \
      /F \"\$base/\$db\" /RunModeOrdinaryApplication \
      /Execute '$container_base/oracle.epf' \
      /C '$container_base/input.txt|$container_base/output.txt|$container_base/script.bsl' \
      /Out '$container_base/logs/enterprise.log' -NoTruncate /DisableStartupDialogs \
      >'$container_base/logs/enterprise-stdout.log' 2>'$container_base/logs/enterprise-stderr.log'
    code=\$?
    echo \"\$code\" >'$container_base/logs/code.txt'
    exit \"\$code\"
  "
  exec_code=$?
  set -e
  docker cp "$OOF_PLATFORM_CONTAINER:$container_base/logs/." "$out_abs/logs/"
  if docker exec "$OOF_PLATFORM_CONTAINER" test -f "$container_base/output.txt"; then
    docker cp "$OOF_PLATFORM_CONTAINER:$container_base/output.txt" "$output_stream"
  fi
  if [[ -f "$out_abs/logs/code.txt" ]]; then
    exit "$(cat "$out_abs/logs/code.txt")"
  fi
  exit "$exec_code"
fi

docker run --rm --platform linux/amd64 --entrypoint sh \
  ${docker_env[@]+"${docker_env[@]}"} \
  -v "$repo_root:/workspace" \
  -v "$NETHASP_INI_PATH:/opt/1cv8/conf/nethasp.ini:ro" \
  -v "$out_abs/conf/conf.cfg:/opt/1cv8/conf/conf.cfg:ro" \
  -v "$out_abs/conf/conf.cfg:/opt/1cv8/x86_64/8.5.1.1343/conf/conf.cfg:ro" \
  ghcr.io/mussolene/1c-developer:8.5.1.1343 \
  -lc "set -eu
    cd /workspace
    base=/tmp/oof-platform-oracle
    db=db
    rm -rf \"\$base\"
    mkdir -p \"\$base\" \"/workspace/$out_rel/logs\"
    /opt/1cv8/x86_64/8.5.1.1343/ibcmd infobase --data=\"\$base\" --database-path=\"\$db\" create --locale=ru_RU \
      >\"/workspace/$out_rel/logs/create.log\" 2>&1
    xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 DESIGNER \
      /F \"\$base/\$db\" \
      /LoadExternalDataProcessorOrReportFromFiles \"/workspace/$out_rel/source/root.xml\" \"/workspace/$out_rel/oracle.epf\" \
      /Out \"/workspace/$out_rel/logs/load.log\" -NoTruncate /DisableStartupDialogs \
      >\"/workspace/$out_rel/logs/load-stdout.log\" 2>\"/workspace/$out_rel/logs/load-stderr.log\"
    set +e
    xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 ENTERPRISE \
      /F \"\$base/\$db\" /RunModeOrdinaryApplication \
      /Execute \"/workspace/$out_rel/oracle.epf\" \
      /C \"$input_container|$output_container|$script_container\" \
      /Out \"/workspace/$out_rel/logs/enterprise.log\" -NoTruncate /DisableStartupDialogs \
      >\"/workspace/$out_rel/logs/enterprise-stdout.log\" 2>\"/workspace/$out_rel/logs/enterprise-stderr.log\"
    code=\$?
    echo \"\$code\" >\"/workspace/$out_rel/logs/code.txt\"
    exit \"\$code\"
  "
