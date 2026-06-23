#!/usr/bin/env bash
# Run the 26 ordinary-control platform oracle scripts and collect runtime streams.
set -euo pipefail

source_root="work/oracle-runtime/blank-source/root.xml"
out_dir="scan-output/platform-control-oracle"
native_bin=${OOF_NATIVE_BIN:-sidecars/onec-form-native/build/oof-native}
module_target=object
single_pass=0

usage() {
  cat >&2 <<'TXT'
Usage: tools/run_control_oracle_batch.sh [--source-root root.xml] [--out-dir scan-output/platform-control-oracle] [--module-target form|object] [--single-pass]

Requires either OOF_PLATFORM_CONTAINER=<running licensed 1C container> or
NETHASP_INI_PATH=<readable nethasp.ini>. Outputs stay under scan-output/.
TXT
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-root) source_root=${2:?}; shift 2 ;;
    --out-dir) out_dir=${2:?}; shift 2 ;;
    --module-target) module_target=${2:?}; shift 2 ;;
    --single-pass) single_pass=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

case "$module_target" in
  form|object) ;;
  *) echo "Unsupported --module-target: $module_target" >&2; exit 2 ;;
esac

repo_root=$(pwd)
case "$out_dir" in
  /*) out_abs=$out_dir ;;
  *) out_abs="$repo_root/$out_dir" ;;
esac
case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *) echo "Output directory must be under scan-output/: $out_abs" >&2; exit 2 ;;
esac

if [[ ! -f "$source_root" ]]; then
  echo "Source root.xml does not exist: $source_root" >&2
  exit 2
fi
if [[ -z "${OOF_PLATFORM_CONTAINER:-}" && ( -z "${NETHASP_INI_PATH:-}" || ! -r "${NETHASP_INI_PATH:-}" ) ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to a licensed 1C container, or set NETHASP_INI_PATH for docker fallback" >&2
  exit 2
fi
if [[ ! -x "$native_bin" ]]; then
  make -C sidecars/onec-form-native >/dev/null
fi

rm -rf "$out_abs"
mkdir -p "$out_abs"/{scripts,streams,graphs,nodes,logs,oracle}

write_single_pass_case() {
  local code="$1"
  local type_name="$2"
  local name="$3"
  local title="${4:-}"
  cat >> "$out_abs/scripts/all-controls-single-pass.bsl" <<BSL
Элемент = Неопределено;
Попытка
	Элемент = ЭлементыФормы.Добавить(Тип("$type_name"), "$name", Истина);
	Элемент.Лево = 8;
	Элемент.Верх = 33;
	Элемент.Ширина = 120;
	Элемент.Высота = 24;
BSL
  if [[ -n "$title" ]]; then
    cat >> "$out_abs/scripts/all-controls-single-pass.bsl" <<BSL
	Попытка
		Элемент.Заголовок = "$title";
	Исключение
	КонецПопытки;
BSL
  fi
  cat >> "$out_abs/scripts/all-controls-single-pass.bsl" <<BSL
	ДокЗначение = Новый ТекстовыйДокумент;
	ДокЗначение.УстановитьТекст(ЗначениеВСтрокуВнутр(Форма));
	ДокЗначение.Записать(ПутьКаталогВыхода + "/$code.form.txt", КодировкаТекста.UTF8);
	Сводка = Сводка + "$code" + Символы.Таб + "OK" + Символы.Таб + "$name" + Символы.Таб + "" + Символы.ПС;
Исключение
	Сводка = Сводка + "$code" + Символы.Таб + "ERROR" + Символы.Таб + "$name" + Символы.Таб + СтрЗаменить(СтрЗаменить(ОписаниеОшибки(), Символы.Таб, " "), Символы.ПС, " ") + Символы.ПС;
КонецПопытки;
Если Элемент <> Неопределено Тогда
	Попытка
		ЭлементыФормы.Удалить(Элемент);
	Исключение
		Попытка
			ЭлементыФормы.Удалить("$name");
		Исключение
		КонецПопытки;
	КонецПопытки;
КонецЕсли;

BSL
}

if [[ "$single_pass" == "1" ]]; then
  cat > "$out_abs/scripts/all-controls-single-pass.bsl" <<'BSL'
Сводка = "script" + Символы.Таб + "status" + Символы.Таб + "object_name" + Символы.Таб + "error" + Символы.ПС;
ДокЗначение = Новый ТекстовыйДокумент;
ДокЗначение.УстановитьТекст(ЗначениеВСтрокуВнутр(Форма));
ДокЗначение.Записать(ПутьКаталогВыхода + "/00-Baseline.form.txt", КодировкаТекста.UTF8);
Сводка = Сводка + "00-Baseline" + Символы.Таб + "OK" + Символы.Таб + "" + Символы.Таб + "" + Символы.ПС;

BSL
  write_single_pass_case "01-Panel" "Панель" "OraclePanel" "Oracle Panel"
  write_single_pass_case "02-CommandBar" "КоманднаяПанель" "OracleCommandBar" "Oracle CommandBar"
  write_single_pass_case "03-Button" "Кнопка" "OracleButton" "Oracle Button"
  write_single_pass_case "04-Image" "ПолеКартинки" "OracleImage" ""
  write_single_pass_case "05-CheckBox" "Флажок" "OracleCheckBox" "Oracle CheckBox"
  write_single_pass_case "06-ChoiceField" "ПолеВыбора" "OracleChoiceField" "Oracle Choice"
  write_single_pass_case "07-RadioButton" "Переключатель" "OracleRadioButton" "Oracle Radio"
  write_single_pass_case "08-InputField" "ПолеВвода" "OracleInputField" "Oracle Input"
  write_single_pass_case "09-GroupBox" "РамкаГруппы" "OracleGroupBox" "Oracle Group"
  write_single_pass_case "10-Splitter" "Разделитель" "OracleSplitter" ""
  write_single_pass_case "11-Chart" "Диаграмма" "OracleChart" ""
  write_single_pass_case "12-PivotChart" "СводнаяДиаграмма" "OraclePivotChart" ""
  write_single_pass_case "13-GanttChart" "ДиаграммаГанта" "OracleGanttChart" ""
  write_single_pass_case "14-Dendrogram" "Дендрограмма" "OracleDendrogram" ""
  write_single_pass_case "15-HTMLDocumentField" "ПолеHTMLДокумента" "OracleHTMLDocument" ""
  write_single_pass_case "16-ListBox" "ПолеСписка" "OracleListBox" "Oracle List"
  write_single_pass_case "17-ProgressBar" "Индикатор" "OracleProgressBar" ""
  write_single_pass_case "18-TrackBar" "ПолосаРегулирования" "OracleTrackBar" ""
  write_single_pass_case "19-CalendarField" "ПолеКалендаря" "OracleCalendar" ""
  write_single_pass_case "20-TextDocumentField" "ПолеТекстовогоДокумента" "OracleTextDocument" ""
  write_single_pass_case "21-GeographicalSchemaField" "ПолеГеографическойСхемы" "OracleGeoSchema" ""
  write_single_pass_case "22-GraphicalSchemaField" "ПолеГрафическойСхемы" "OracleGraphSchema" ""
  write_single_pass_case "23-Table" "ТабличноеПоле" "OracleTable" ""
  write_single_pass_case "24-SpreadsheetDocumentField" "ПолеТабличногоДокумента" "OracleSpreadsheet" ""
  write_single_pass_case "25-Label" "Надпись" "OracleLabel" "Oracle Label"
  write_single_pass_case "26-ActiveXControl" "ЭлементУправления" "OracleActiveX" ""
  cat >> "$out_abs/scripts/all-controls-single-pass.bsl" <<'BSL'
ДокСводка = Новый ТекстовыйДокумент;
ДокСводка.УстановитьТекст(Сводка);
ДокСводка.Записать(ПутьКаталогВыхода + "/summary.tsv", КодировкаТекста.UTF8);
Результат = Форма;
BSL
else
  tools/generate_control_oracle_scripts.sh "$out_abs/scripts" > "$out_abs/logs/generate.log"
fi

summary="$out_abs/summary.tsv"
printf 'script\tmodule_target\tstatus\tstream_bytes\tmaterialized_items\tschema_backed_items\n' > "$summary"

postprocess_stream() {
  local stream="$1"
  local name="$2"
  local graph="$out_abs/graphs/$name.json"
  if "$native_bin" runtime-form-object-graph "$stream" > "$graph" 2>"$out_abs/logs/$name.graph.stderr"; then
    local materialized
    local schema_backed
    materialized=$(sed -n 's/.*"materializedItems":\([0-9][0-9]*\).*/\1/p' "$graph" | head -n 1)
    schema_backed=$(sed -n 's/.*"schemaBackedItems":\([0-9][0-9]*\).*/\1/p' "$graph" | head -n 1)
    if command -v python3 >/dev/null 2>&1; then
      python3 - "$graph" "$stream" "$out_abs/nodes/$name.json" "$native_bin" <<'PY' || true
import json, subprocess, sys
graph_path, stream_path, node_path, native_bin = sys.argv[1:5]
graph = json.load(open(graph_path, encoding="utf-8"))
items = graph.get("items", [])
candidate = None
for item in items:
    name = item.get("name", "")
    if name.startswith("Oracle"):
        candidate = item
        break
if candidate is not None and candidate.get("path"):
    with open(node_path, "w", encoding="utf-8") as out:
        subprocess.run([native_bin, "runtime-form-node", stream_path, candidate["path"]], check=True, stdout=out)
PY
    fi
    printf '%s\t%s\tOK\t%s\t%s\t%s\n' "$name" "$module_target" "$(wc -c < "$stream" | tr -d ' ')" "$materialized" "$schema_backed" >> "$summary"
  else
    printf '%s\t%s\tstream-not-form\t%s\t\t\n' "$name" "$module_target" "$(wc -c < "$stream" | tr -d ' ')" >> "$summary"
  fi
}

write_descriptor_slice() {
  local descriptor_json="$out_abs/logs/platform-control-info-descriptors.json"
  "$native_bin" platform-control-info-descriptors > "$descriptor_json"
  python3 - "$out_abs" "$descriptor_json" "$native_bin" <<'PY'
import json
import os
import subprocess
import sys

out_abs, descriptor_json, native_bin = sys.argv[1:4]
focus = {
    "01-Panel": "Panel",
    "02-CommandBar": "CommandBar",
    "03-Button": "Button",
    "08-InputField": "InputField",
    "25-Label": "Label",
}

with open(descriptor_json, encoding="utf-8-sig") as f:
    descriptor_payload = json.load(f)
descriptors = {
    item["controlType"]: item
    for item in descriptor_payload.get("descriptors", [])
}

rows = []
for case, expected_control in focus.items():
    graph_path = os.path.join(out_abs, "graphs", f"{case}.json")
    stream_path = os.path.join(out_abs, "streams", f"{case}.form.txt")
    node_path = os.path.join(out_abs, "nodes", f"{case}.json")
    row = {
        "case": case,
        "expectedControl": expected_control,
        "status": "missing-graph",
    }
    if not os.path.exists(graph_path):
        rows.append(row)
        continue
    with open(graph_path, encoding="utf-8-sig") as f:
        graph = json.load(f)
    candidate = None
    expected_name = "Oracle" + expected_control
    for item in graph.get("items", []):
        if item.get("name") == expected_name or item.get("name", "").startswith("Oracle"):
            candidate = item
            break
    if candidate is None:
        row.update({
            "status": "missing-oracle-item",
            "materializedItems": graph.get("materializedItems", 0),
            "schemaBackedItems": graph.get("schemaBackedItems", 0),
        })
        rows.append(row)
        continue

    descriptor = descriptors.get(expected_control, {})
    slots = descriptor.get("slots", [])
    roundtrip_status = "not-run"
    roundtrip_issue = ""
    if os.path.exists(stream_path):
        try:
            result = subprocess.run(
                [native_bin, "runtime-form-object-roundtrip-diff", stream_path],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            diff = json.loads(result.stdout)
            roundtrip_status = (
                "stable"
                if diff.get("objectSignatureEqualAfterFirstWrite")
                and diff.get("objectSignatureStableAfterSecondWrite")
                and diff.get("payloadStableAfterSecondWrite")
                else "diff"
            )
            details = diff.get("firstWriteDiff", {}).get("details", [])
            roundtrip_issue = ",".join(
                f"{item.get('platformType', '')}.{item.get('property', '')}:{item.get('kind', '')}"
                for item in details[:5]
            )
        except Exception as exc:
            roundtrip_status = "error:" + str(exc).splitlines()[0]
            roundtrip_issue = roundtrip_status

    row.update({
        "status": "OK",
        "runtimeName": candidate.get("name", ""),
        "objectId": candidate.get("objectId", ""),
        "path": candidate.get("path", ""),
        "guid": candidate.get("guid", ""),
        "platformType": candidate.get("platformType", ""),
        "streamElement": candidate.get("streamElement", ""),
        "schemaBacked": bool(candidate.get("schemaBacked", False)),
        "descriptorControl": descriptor.get("controlType", ""),
        "infoKind": descriptor.get("infoKind", ""),
        "writerDescriptor": bool(descriptor.get("writerDescriptor", False)),
        "slotCount": descriptor.get("slotCount", 0),
        "slots": ",".join(f"{slot.get('name')}:{slot.get('index')}" for slot in slots),
        "nodeExtracted": os.path.exists(node_path),
        "roundtrip": roundtrip_status,
        "roundtripIssue": roundtrip_issue,
        "decision": "exact" if candidate.get("platformType") == expected_control else "alias-by-guid",
    })
    rows.append(row)

json_path = os.path.join(out_abs, "descriptor-slice.json")
tsv_path = os.path.join(out_abs, "descriptor-slice.tsv")
with open(json_path, "w", encoding="utf-8") as f:
    json.dump({
        "source": "platform-control-oracle descriptor slice",
        "focusControls": list(focus.values()),
        "descriptorRegistry": "platform-control-info-descriptors",
        "rows": rows,
    }, f, ensure_ascii=False, indent=2)

fields = [
    "case",
    "expectedControl",
    "status",
    "runtimeName",
    "objectId",
    "path",
    "guid",
    "platformType",
    "streamElement",
    "schemaBacked",
    "descriptorControl",
    "infoKind",
    "writerDescriptor",
    "slotCount",
    "slots",
    "nodeExtracted",
    "roundtrip",
    "roundtripIssue",
    "decision",
]
with open(tsv_path, "w", encoding="utf-8") as f:
    f.write("\t".join(fields) + "\n")
    for row in rows:
        f.write("\t".join(str(row.get(field, "")) for field in fields) + "\n")
PY
}

if [[ "$single_pass" == "1" ]]; then
  script="$out_abs/scripts/all-controls-single-pass.bsl"
  set +e
  tools/platform_oracle_execute.sh --module-target "$module_target" --output-dir "$out_abs/streams" "$source_root" - "$script" "$out_abs/final.txt" "$out_abs/oracle/single-pass" \
    >"$out_abs/logs/single-pass.stdout" 2>"$out_abs/logs/single-pass.stderr"
  code=$?
  set -e
  if [[ -f "$out_abs/streams/summary.tsv" ]]; then
    cp "$out_abs/streams/summary.tsv" "$out_abs/oracle-summary.tsv"
  fi
  for stream in "$out_abs"/streams/*.form.txt; do
    [[ -f "$stream" ]] || continue
    name=$(basename "$stream" .form.txt)
    postprocess_stream "$stream" "$name"
  done
  if [[ "$code" -ne 0 ]]; then
    printf 'single-pass\t%s\tplatform-code-%s\t0\t\t\n' "$module_target" "$code" >> "$summary"
  fi
else
  for script in "$out_abs"/scripts/*.bsl; do
  name=$(basename "$script" .bsl)
  stream="$out_abs/streams/$name.txt"
  run_dir="$out_abs/oracle/$name"
  set +e
  tools/platform_oracle_execute.sh --module-target "$module_target" "$source_root" - "$script" "$stream" "$run_dir" \
    >"$out_abs/logs/$name.stdout" 2>"$out_abs/logs/$name.stderr"
  code=$?
  set -e

  status="platform-code-$code"
  stream_bytes=0
  materialized=""
  schema_backed=""
  if [[ -f "$stream" ]]; then
    if "$native_bin" runtime-form-object-graph "$stream" > "$out_abs/graphs/$name.json" 2>"$out_abs/logs/$name.graph.stderr"; then
      postprocess_stream "$stream" "$name"
      continue
    else
      status="stream-not-form"
    fi
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$module_target" "$status" "$stream_bytes" "$materialized" "$schema_backed" >> "$summary"
  done
fi

write_descriptor_slice

printf 'control oracle batch summary: %s\n' "$summary"
