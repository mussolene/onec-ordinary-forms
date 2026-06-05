# Ordinary Control Auto Map

This note records the current platform-oracle mapping for ordinary form controls.
It is based on the 26-control `ЗначениеВСтрокуВнутр(ЭтаФорма)` oracle batch,
the native runtime graph summaries, `control_info_descriptor_registry.hpp`, and
`platform_form_descriptor_join.hpp`.

Acceptance criteria for this investigation:

- AC1: derive the table from current oracle artifacts and the C++ registry.
- AC2: mark every control as exact, alias, partial, gap, or blocked.
- AC3: state the native auto-mapping algorithm needed for the object layer.

## Mapping Result

| Case | BSL type | Public XML object | Runtime platformType | GUID | XSD element | Schema-backed | Decision |
|---|---|---|---|---|---|---|---|
| 01-Panel | Панель | Panel | Panel | 09ccdc77-ea1a-4a6d-ab1c-3435eada2433 | pnl | yes | exact |
| 02-CommandBar | КоманднаяПанель | CommandBar | CommandBar | e69bf21d-97b2-4f37-86db-675aea9ec2cb | cmdb | yes | exact |
| 03-Button | Кнопка | Button | Button | 6ff79819-710e-4145-97cd-1618da79e3e2 | btn | yes | exact |
| 04-Image | ПолеКартинки | PictureDecoration | Image | 151ef23e-6bb2-4681-83d0-35bc2217230c | img | yes | alias |
| 05-CheckBox | Флажок | CheckBox | CheckBox | 35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26 | chk | yes | exact |
| 06-ChoiceField | ПолеВыбора | ChoiceField | TextBox | 64483e7f-3833-48e2-8c75-2c31aac49f6e | txt | yes | alias by GUID |
| 07-RadioButton | Переключатель | RadioButton | RadioButton | 782e569a-79a7-4a4f-a936-b48d013936ec | rbtn | yes | exact |
| 08-InputField | ПолеВвода | InputField | TextBox | 381ed624-9217-4e63-85db-c4c3cb87daae | txt | yes | alias by GUID |
| 09-GroupBox | РамкаГруппы | UsualGroup | GroupBox | 90db814a-c75f-4b54-bc96-df62e554d67d | grpb | yes | alias |
| 10-Splitter | Разделитель | Splitter | Separator | 36e52348-5d60-4770-8e89-a16ed50a2006 | sep | yes | alias |
| 11-Chart | Диаграмма | Chart | Chart | a8b97779-1a4b-4059-b09c-807f86d2a461 | chrt | yes | exact |
| 12-PivotChart | СводнаяДиаграмма | PivotChart | PivotChart | a26da99e-184a-4823-b0d6-62816d38dc4e | chart_root:PivotChart | yes | schema-backed special |
| 13-GanttChart | ДиаграммаГанта | GanttChart | GanttChart | e5fdc112-5c84-4a16-9728-72b85692b6e2 | gchrt | yes | exact |
| 14-Dendrogram | Дендрограмма | Dendrogram | Dendrogram | 984981b1-622d-4ebc-94f7-885f0cdfb59a | dndrgm | yes | exact |
| 15-HTMLDocumentField | ПолеHTMLДокумента | HTMLDocumentField | HTML | d92a805c-98ae-4750-9158-d9ce7cec2f20 | html/htmlData | yes | alias; Windows oracle pending |
| 16-ListBox | ПолеСписка | ListBox | TextBox | 19f8b798-314e-4b4e-8121-905b2a7a03f5 | txt | yes | alias by GUID |
| 17-ProgressBar | Индикатор | ProgressBar | ProgressBar | b1db1f86-abbb-4cf0-8852-fe6ae21650c2 | prgb | yes | exact |
| 18-TrackBar | ПолосаРегулирования | TrackBar | TrackBar | 6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef | trckb | yes | exact |
| 19-CalendarField | ПолеКалендаря | CalendarField | Calendar | e3c063d8-ef92-41be-9c89-b70290b5368b | clndr | yes | alias |
| 20-TextDocumentField | ПолеТекстовогоДокумента | TextDocumentField | TextDocument | 14c4a229-bfc3-42fe-9ce1-2da049fd0109 | txtd | yes | alias |
| 21-GeographicalSchemaField | ПолеГеографическойСхемы | GeographicalSchemaField | GeographicalMap | ad37194e-555e-4305-b718-5dca84baf145 | gm | yes | alias |
| 22-GraphicalSchemaField | ПолеГрафическойСхемы | GraphicalSchemaField | Flowchart | 42248403-7748-49da-b782-e4438fd7bff3 | flwchrt | yes | alias |
| 23-Table | ТабличноеПоле | Table | TableBox | ea83fe3a-ac3c-4cce-8045-3dddf35b28b1 | tbl | yes | alias |
| 24-SpreadsheetDocumentField | ПолеТабличногоДокумента | SpreadsheetDocumentField | Spreadsheet | 236a17b3-7f44-46d9-a907-75f9cdc61ab5 | sprdsht | yes | alias |
| 25-Label | Надпись | LabelDecoration | Label | 0fc7e20d-f241-460c-bdf4-5ad88e5474a5 | lbl | yes | alias |
| 26-ActiveXControl | ЭлементУправления | ActiveXControl | - | - | - | no | blocked |

Coverage summary:

- 25 controls are schema-backed now.
- `PivotChart` is schema-backed through `chart_root` `PivotChart`, but still has
  no logform layouter stream element.
- `HTMLDocumentField` is a public alias for platform `HTML/html` and
  `Field/htmlData` `HTMLFieldData`; Linux oracle did not materialize the runtime
  item, so Windows oracle validation is still pending.
- 1 control is blocked: `ActiveXControl` returns a non-form error stream because
  the platform oracle cannot resolve `ЭлементУправления` as a form control type.

## Native Mapping Rule

The object materializer must not map by `platformType` alone. `TextBox` covers
at least `InputField`, `ChoiceField`, and `ListBox`, and several public ordinary
form names differ from the platform XSD/runtime type.

Use this key order:

1. Resolve by control GUID.
2. Verify the resolved binding against `platformType` and `streamElement`.
3. Emit the public XML object name from the binding.
4. Fall back to exact `platformType` only when the GUID is absent and the type is
   unambiguous.

The same registry must drive both materialization and serialization:

```text
GUID -> platformType -> streamElement -> publicXmlObject -> infoDescriptor
```

This keeps the mapping automatic and schema-backed while still preserving the
public managed-form-style XML vocabulary.
