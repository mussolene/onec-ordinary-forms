#!/usr/bin/env bash
# Generate BSL oracle scripts that add one ordinary-form control per script.
set -euo pipefail

out_dir=${1:-scan-output/platform-control-oracle/scripts}
repo_root=$(pwd)
case "$out_dir" in
  /*) out_abs=$out_dir ;;
  *) out_abs="$repo_root/$out_dir" ;;
esac
case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *) echo "Output directory must be under scan-output/: $out_abs" >&2; exit 2 ;;
esac

rm -rf "$out_abs"
mkdir -p "$out_abs"

write_script() {
  local file="$1"
  local type_expr="$2"
  local name="$3"
  local title="${4:-}"
  cat > "$out_abs/$file.bsl" <<BSL
Попытка
	Элемент = ЭлементыФормы.Добавить($type_expr, "$name", Истина);
	Элемент.Лево = 8;
	Элемент.Верх = 33;
	Элемент.Ширина = 120;
	Элемент.Высота = 24;
BSL
  if [[ -n "$title" ]]; then
    cat >> "$out_abs/$file.bsl" <<BSL
	Попытка
		Элемент.Заголовок = "$title";
	Исключение
	КонецПопытки;
BSL
  fi
  cat >> "$out_abs/$file.bsl" <<'BSL'
	Результат = ЭтаФорма;
Исключение
	Сводка = Новый Структура;
	Сводка.Вставить("Status", "ERROR");
	Сводка.Вставить("Error", ОписаниеОшибки());
	Сводка.Вставить("TypeName", Строка(ТипЗнч(Неопределено)));
	Результат = Сводка;
КонецПопытки;
BSL
}

write_script "01-Panel" 'Тип("Панель")' "OraclePanel" "Oracle Panel"
write_script "02-CommandBar" 'Тип("КоманднаяПанель")' "OracleCommandBar" "Oracle CommandBar"
write_script "03-Button" 'Тип("Кнопка")' "OracleButton" "Oracle Button"
write_script "04-Image" 'Тип("ПолеКартинки")' "OracleImage" ""
write_script "05-CheckBox" 'Тип("Флажок")' "OracleCheckBox" "Oracle CheckBox"
write_script "06-ChoiceField" 'Тип("ПолеВыбора")' "OracleChoiceField" "Oracle Choice"
write_script "07-RadioButton" 'Тип("Переключатель")' "OracleRadioButton" "Oracle Radio"
write_script "08-InputField" 'Тип("ПолеВвода")' "OracleInputField" "Oracle Input"
write_script "09-GroupBox" 'Тип("РамкаГруппы")' "OracleGroupBox" "Oracle Group"
write_script "10-Splitter" 'Тип("Разделитель")' "OracleSplitter" ""
write_script "11-Chart" 'Тип("Диаграмма")' "OracleChart" ""
write_script "12-PivotChart" 'Тип("СводнаяДиаграмма")' "OraclePivotChart" ""
write_script "13-GanttChart" 'Тип("ДиаграммаГанта")' "OracleGanttChart" ""
write_script "14-Dendrogram" 'Тип("Дендрограмма")' "OracleDendrogram" ""
write_script "15-HTMLDocumentField" 'Тип("ПолеHTMLДокумента")' "OracleHTMLDocument" ""
write_script "16-ListBox" 'Тип("ПолеСписка")' "OracleListBox" "Oracle List"
write_script "17-ProgressBar" 'Тип("Индикатор")' "OracleProgressBar" ""
write_script "18-TrackBar" 'Тип("ПолосаРегулирования")' "OracleTrackBar" ""
write_script "19-CalendarField" 'Тип("ПолеКалендаря")' "OracleCalendar" ""
write_script "20-TextDocumentField" 'Тип("ПолеТекстовогоДокумента")' "OracleTextDocument" ""
write_script "21-GeographicalSchemaField" 'Тип("ПолеГеографическойСхемы")' "OracleGeoSchema" ""
write_script "22-GraphicalSchemaField" 'Тип("ПолеГрафическойСхемы")' "OracleGraphSchema" ""
write_script "23-Table" 'Тип("ТабличноеПоле")' "OracleTable" ""
write_script "24-SpreadsheetDocumentField" 'Тип("ПолеТабличногоДокумента")' "OracleSpreadsheet" ""
write_script "25-Label" 'Тип("Надпись")' "OracleLabel" "Oracle Label"
write_script "26-ActiveXControl" 'Тип("ЭлементУправления")' "OracleActiveX" ""

printf 'generated %s scripts in %s\n' "$(find "$out_abs" -name '*.bsl' | wc -l | tr -d ' ')" "$out_abs"
