#!/usr/bin/env bash
# Execute a small BSL oracle script inside an ordinary-form external processor.
#
# The runner copies a source tree, injects a temporary module into the first
# ordinary Form.bin through the native C++ package path, imports the processor
# into the 1C platform, runs it in ordinary application mode, and writes
# ЗначениеВСтрокуВнутр(Результат) to the requested output file.
set -euo pipefail

module_target=form
output_dir=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --module-target)
      module_target=${2:?}
      shift 2
      ;;
    --module-target=*)
      module_target=${1#*=}
      shift
      ;;
    --output-dir)
      output_dir=${2:?}
      shift 2
      ;;
    --output-dir=*)
      output_dir=${1#*=}
      shift
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
    *)
      break
      ;;
  esac
done

case "$module_target" in
  form|object) ;;
  *) echo "Unsupported --module-target: $module_target" >&2; exit 2 ;;
esac

if [[ $# -lt 4 || $# -gt 5 ]]; then
  echo "Usage: $0 [--module-target form|object] [--output-dir scan-output/dir] <source-root.xml> <input-stream.txt|-> <script.bsl> <output-stream.txt> [out-dir]" >&2
  exit 2
fi

if [[ -z "${OOF_PLATFORM_CONTAINER:-}" && ( -z "${NETHASP_INI_PATH:-}" || ! -r "${NETHASP_INI_PATH:-}" ) ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to a licensed 1C container, or set NETHASP_INI_PATH for docker fallback" >&2
  exit 2
fi

repo_root=$(pwd)
native_bin=${OOF_NATIVE_BIN:-sidecars/onec-form-native/build/oof-native}
if [[ ! -x "$native_bin" ]]; then
  make -C sidecars/onec-form-native >/dev/null
fi

abs_path() {
  local path="$1"
  case "$path" in
    /*)
      printf '%s\n' "$path"
      ;;
    *)
      local dir
      local base
      dir=$(dirname "$path")
      base=$(basename "$path")
      printf '%s/%s\n' "$(cd "$dir" && pwd)" "$base"
      ;;
  esac
}

source_root=$(abs_path "$1")
input_stream=$2
script_path=$(abs_path "$3")
case "$4" in
  /*) output_stream="$4" ;;
  *) output_stream="$repo_root/$4" ;;
esac
out_dir=${5:-scan-output/platform-oracle-execute}
case "$out_dir" in
  /*) out_abs="$out_dir" ;;
  *) out_abs="$repo_root/$out_dir" ;;
esac
if [[ -n "$output_dir" ]]; then
  case "$output_dir" in
    /*) output_dir_abs="$output_dir" ;;
    *) output_dir_abs="$repo_root/$output_dir" ;;
  esac
else
  output_dir_abs=""
fi

case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *) echo "Output directory must be under scan-output/: $out_abs" >&2; exit 2 ;;
esac
if [[ -n "$output_dir_abs" ]]; then
  case "$output_dir_abs" in
    "$repo_root"/scan-output/*) ;;
    *) echo "Output directory must be under scan-output/: $output_dir_abs" >&2; exit 2 ;;
  esac
fi

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

container_path() {
  local path="$1"
  case "$path" in
    "$repo_root"/*) printf '/workspace/%s' "${path#"$repo_root"/}" ;;
    *) echo "Path must be inside repository so it is visible in the container: $path" >&2; exit 2 ;;
  esac
}

copy_host_dir_to_container() {
  local src_dir="$1"
  local dst_dir="$2"
  COPYFILE_DISABLE=1 tar --disable-copyfile -C "$src_dir" --exclude='._*' -cf - . \
    | docker exec -i "$OOF_PLATFORM_CONTAINER" sh -lc "mkdir -p '$dst_dir' && tar -C '$dst_dir' -xf -"
}

copy_host_file_to_container() {
  local src_file="$1"
  local dst_file="$2"
  local dst_dir
  dst_dir=$(dirname "$dst_file")
  COPYFILE_DISABLE=1 tar --disable-copyfile -C "$(dirname "$src_file")" -cf - "$(basename "$src_file")" \
    | docker exec -i "$OOF_PLATFORM_CONTAINER" sh -lc "mkdir -p '$dst_dir' && tar -C '$dst_dir' -xf - && mv '$dst_dir/$(basename "$src_file")' '$dst_file'"
}

copy_container_dir_to_host() {
  local src_dir="$1"
  local dst_dir="$2"
  mkdir -p "$dst_dir"
  docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "cd '$src_dir' && tar -cf - ." | tar -C "$dst_dir" -xf -
}

copy_container_file_to_host() {
  local src_file="$1"
  local dst_file="$2"
  mkdir -p "$(dirname "$dst_file")"
  docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "cat '$src_file'" > "$dst_file"
}

rm -rf "$out_abs"
mkdir -p "$out_abs"/{source,package,logs,conf}
cp -R "$(dirname "$source_root")"/. "$out_abs/source/"
printf 'DisableUnsafeActionProtection=.*\n' > "$out_abs/conf/conf.cfg"

form_bin=$(find "$out_abs/source" -path '*/Forms/*/Ext/Form.bin' -type f | head -n 1)
if [[ -z "$form_bin" ]]; then
  echo "Cannot find Forms/*/Ext/Form.bin under copied source" >&2
  exit 2
fi

if [[ "$module_target" == "form" ]]; then
  package_xml="$out_abs/package/Form.xml"
  "$native_bin" formbin-dump-package "$form_bin" "$package_xml" >"$out_abs/logs/dump-package.json"

  cat > "$out_abs/package/Form/Module.bsl" <<'BSL'
Функция OOF_ПараметрыЗапуска()
	Результат = Новый Соответствие;
	Для Каждого Часть Из СтрРазделить(ПараметрЗапуска, ";") Цикл
		ПозицияРавно = СтрНайти(Часть, "=");
		Если ПозицияРавно = 0 Тогда Продолжить; КонецЕсли;
		Имя = СокрЛП(Лев(Часть, ПозицияРавно - 1));
		Значение = Сред(Часть, ПозицияРавно + 1);
		Результат.Вставить(Имя, Значение);
	КонецЦикла;
	Возврат Результат;
КонецФункции

Процедура ПриОткрытии()
	Параметры = OOF_ПараметрыЗапуска();
	ПутьВход = Параметры.Получить("InputFile");
	ПутьВыход = Параметры.Получить("OutputFile");
	ПутьСкрипт = Параметры.Получить("ScriptFile");
	Если ПутьВход = Неопределено Тогда ПутьВход = ""; КонецЕсли;
	Если ПутьВыход = Неопределено Тогда ПутьВыход = ""; КонецЕсли;
	Если ПутьСкрипт = Неопределено Тогда ПутьСкрипт = ""; КонецЕсли;

	Вход = "";
	Если ПутьВход <> "" Тогда
		ДокВход = Новый ТекстовыйДокумент;
		ДокВход.Прочитать(ПутьВход);
		Вход = ДокВход.ПолучитьТекст();
	КонецЕсли;

	Объект = Неопределено;
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

  rebuilt_form_bin="$out_abs/package/rebuilt-Form.bin"
  "$native_bin" formbin-build-source-package "$package_xml" "$rebuilt_form_bin" >"$out_abs/logs/build-package.json"
  mv "$rebuilt_form_bin" "$form_bin"
else
  mkdir -p "$out_abs/source/root/Ext"
  cat > "$out_abs/source/root/Ext/ObjectModule.bsl" <<'BSL'
Функция OOF_ПараметрыЗапуска()
	Результат = Новый Соответствие;
	Для Каждого Часть Из СтрРазделить(ПараметрЗапуска, ";") Цикл
		ПозицияРавно = СтрНайти(Часть, "=");
		Если ПозицияРавно = 0 Тогда Продолжить; КонецЕсли;
		Имя = СокрЛП(Лев(Часть, ПозицияРавно - 1));
		Значение = Сред(Часть, ПозицияРавно + 1);
		Результат.Вставить(Имя, Значение);
	КонецЦикла;
	Возврат Результат;
КонецФункции

Параметры = OOF_ПараметрыЗапуска();
ПутьВход = Параметры.Получить("InputFile");
ПутьВыход = Параметры.Получить("OutputFile");
ПутьКаталогВыхода = Параметры.Получить("OutputDir");
Если ПутьВход = Неопределено Тогда ПутьВход = ""; КонецЕсли;
Если ПутьВыход = Неопределено Тогда ПутьВыход = ""; КонецЕсли;
Если ПутьКаталогВыхода = Неопределено Тогда ПутьКаталогВыхода = ""; КонецЕсли;

Вход = "";
Если ПутьВход <> "" Тогда
	ДокВход = Новый ТекстовыйДокумент;
	ДокВход.Прочитать(ПутьВход);
	Вход = ДокВход.ПолучитьТекст();
КонецЕсли;

Форма = ЭтотОбъект.ПолучитьФорму("Форма");
ЭтаФорма = Форма;
ЭлементыФормы = Форма.ЭлементыФормы;
Объект = Неопределено;
Результат = Форма;
BSL
  cat "$script_path" >> "$out_abs/source/root/Ext/ObjectModule.bsl"
  cat >> "$out_abs/source/root/Ext/ObjectModule.bsl" <<'BSL'

Если ПутьВыход <> "" Тогда
	ДокВыход = Новый ТекстовыйДокумент;
	ДокВыход.УстановитьТекст(ЗначениеВСтрокуВнутр(Результат));
	ДокВыход.Записать(ПутьВыход, КодировкаТекста.UTF8);
КонецЕсли;

Сообщить("OOF_ORACLE_OK");
ЗавершитьРаботуСистемы(Ложь);
BSL
fi

trace_env_cmd=""
if [[ -n "${PLATFORM_ORACLE_LD_DEBUG:-}" ]]; then
  if [[ "${PLATFORM_ORACLE_LD_DEBUG:-}" == "1" ]]; then
    trace_env_cmd="$trace_env_cmd LD_DEBUG=libs,bindings"
  else
    trace_env_cmd="$trace_env_cmd LD_DEBUG=${PLATFORM_ORACLE_LD_DEBUG}"
  fi
fi
if [[ -n "${PLATFORM_ORACLE_LD_AUDIT:-}" ]]; then
  trace_env_cmd="$trace_env_cmd LD_AUDIT=${PLATFORM_ORACLE_LD_AUDIT}"
fi
if [[ -n "$trace_env_cmd" ]]; then
  trace_env_cmd="env$trace_env_cmd"
fi

if [[ -n "${OOF_PLATFORM_CONTAINER:-}" ]]; then
  if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != "true" ]]; then
    echo "OOF_PLATFORM_CONTAINER is not a running container: $OOF_PLATFORM_CONTAINER" >&2
    exit 2
  fi
  container_base="/tmp/oof-platform-oracle"
  container_output_dir=""
  if [[ -n "$output_dir_abs" ]]; then
    container_output_dir="$container_base/output-dir"
  fi
  docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "rm -rf '$container_base' && mkdir -p '$container_base/source' '$container_base/logs' '$container_base/dbroot' '$container_base/output-dir'"
  copy_host_dir_to_container "$out_abs/source" "$container_base/source"
  copy_host_file_to_container "$script_path" "$container_base/script.bsl"
  if [[ "$input_stream" != "-" ]]; then
    copy_host_file_to_container "$(abs_path "$input_stream")" "$container_base/input.txt"
  else
    docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "printf '' > '$container_base/input.txt'"
  fi
  set +e
  docker exec "$OOF_PLATFORM_CONTAINER" sh -lc "set -eu
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
    xvfb-run -a timeout 300 $trace_env_cmd /opt/1cv8/x86_64/8.5.1.1343/1cv8 ENTERPRISE \
      /F \"\$base/\$db\" /RunModeOrdinaryApplication \
      /Execute '$container_base/oracle.epf' \
      /C 'InputFile=$container_base/input.txt;OutputFile=$container_base/output.txt;OutputDir=$container_output_dir;ScriptFile=$container_base/script.bsl' \
      /Out '$container_base/logs/enterprise.log' -NoTruncate /DisableStartupDialogs \
      >'$container_base/logs/enterprise-stdout.log' 2>'$container_base/logs/enterprise-stderr.log'
    code=\$?
    echo \"\$code\" >'$container_base/logs/code.txt'
    exit \"\$code\"
  "
  exec_code=$?
  set -e
  copy_container_dir_to_host "$container_base/logs" "$out_abs/logs"
  if docker exec "$OOF_PLATFORM_CONTAINER" test -f "$container_base/output.txt"; then
    copy_container_file_to_host "$container_base/output.txt" "$output_stream"
  fi
  if [[ -n "$output_dir_abs" ]] && docker exec "$OOF_PLATFORM_CONTAINER" test -d "$container_output_dir"; then
    rm -rf "$output_dir_abs"
    copy_container_dir_to_host "$container_output_dir" "$output_dir_abs"
  fi
  if [[ -f "$out_abs/logs/code.txt" ]]; then
    exit "$(cat "$out_abs/logs/code.txt")"
  fi
  exit "$exec_code"
fi

out_rel=${out_abs#"$repo_root"/}
input_container=""
if [[ "$input_stream" != "-" ]]; then
  input_container=$(container_path "$(abs_path "$input_stream")")
fi
output_container=$(container_path "$output_stream")
script_container=$(container_path "$script_path")
output_dir_container=""
if [[ -n "$output_dir_abs" ]]; then
  mkdir -p "$output_dir_abs"
  output_dir_container=$(container_path "$output_dir_abs")
fi

docker run --rm --platform linux/amd64 --entrypoint sh \
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
    xvfb-run -a timeout 300 $trace_env_cmd /opt/1cv8/x86_64/8.5.1.1343/1cv8 ENTERPRISE \
      /F \"\$base/\$db\" /RunModeOrdinaryApplication \
      /Execute \"/workspace/$out_rel/oracle.epf\" \
      /C \"InputFile=$input_container;OutputFile=$output_container;OutputDir=$output_dir_container;ScriptFile=$script_container\" \
      /Out \"/workspace/$out_rel/logs/enterprise.log\" -NoTruncate /DisableStartupDialogs \
      >\"/workspace/$out_rel/logs/enterprise-stdout.log\" 2>\"/workspace/$out_rel/logs/enterprise-stderr.log\"
    code=\$?
    echo \"\$code\" >\"/workspace/$out_rel/logs/code.txt\"
    exit \"\$code\"
  "
