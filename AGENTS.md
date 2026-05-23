## OACS Repo Workflow

For any non-trivial work in this repository, including form parser changes,
schema/XSD changes, corpus experiments, platform export/rebuild checks,
platform-library inspection, documentation changes, and release work, use OACS
as the durable project memory, context, and evidence surface.

OACS is mandatory for parser and platform work. Do not treat chat history,
local recollection, or unrecorded terminal output as durable project state. If
work changes behavior, tests a platform hypothesis, rejects an approach, or
finds a blocker, it must leave an OACS evidence trail and checkpoint.

Required sequence:

1. State the task scope and explicit acceptance criteria (`AC1`, `AC2`, ...)
   before implementation.
2. Export repo-local ACS state before using ACS:
   `export OACS_DB="$PWD/.agent/oacs/oacs.db"`.
   Local development stores may use OACS `local_unlocked` key material.
3. Check the OACS consumer pack before substantial OACS-dependent work:
   `acs --version`.
4. Ask the reference context gate before building context:
   `acs context gate --intent repo_development --scope project --task "<task>" --json`.
   Treat `decision=build` as the signal to run `acs context build`. Treat
   `decision=skip` as valid only for tiny visible-file edits that do not depend
   on prior parser/platform decisions. If unsure, build context anyway.
5. Query durable memory first, then build or inspect fresh context when the gate
   says `build`, when prior project memory/evidence may matter, or when in
   doubt:
   `acs memory query --query "<task intent>" --scope project --json` and
   `acs context build --intent "<task intent>" --scope project --json`.
6. Treat command outputs, Docker checks, OACS/MCP results, runtime checks,
   failed experiments, and rejected hypotheses as evidence with
   `acs tool ingest-result ...`.
7. Promote verified reusable conclusions with `acs memory propose`,
   `acs memory commit`, and `acs memory sharpen`.
8. Record a checkpoint for each completed, partial, or blocked iteration with
   outcome, PASS/FAIL/PARTIAL acceptance criteria, evidence refs, remaining
   risk, and next step:
   `acs checkpoint add ... --evidence <ev_...> --json`.
9. Before every commit, inspect staged changes and generated files for
   non-project information and
   sensitive data: no private EPF/ERF files, customer dumps, local host paths,
   `.env`, OACS DB files, `nethasp.ini` contents, credentials, tokens, license
   data, platform archives, local volumes, or unrelated artifacts.
10. Close each completed work iteration with a focused commit after checks pass.
    If there are unrelated dirty files, leave them unstaged and mention that
    they were not part of the iteration.

Hard rules:

- Do not claim completion unless every acceptance criterion is `PASS`.
- Do not claim completion unless current verification, OACS evidence, and an
  OACS checkpoint exist for the iteration.
- Do not hide partial work behind successful unrelated checks. If build,
  platform validation, or object-model serialization is incomplete, say
  `PARTIAL` or `BLOCKED`, record it in OACS, and state the next concrete step.
- Current code and current command results are the source of truth, not prior
  chat claims.
- Parser, serializer, XSD, and platform-library claims must cite current
  evidence from OACS or a command run in the current iteration. If evidence is
  stale, rerun the check or downgrade the claim.
- Keep secrets out of OACS: no customer file paths, EPF payloads, ITS
  credentials, license data, `nethasp.ini` contents, platform archives, full
  help dumps, or local host paths.
- Do not read, print, or commit `.agent/oacs/key.json`,
  `.agent/oacs/unlocked.key`, `.agent/oacs`, `.oacs`, local databases,
  passphrases, or private agent state.
- Keep private processors and generated exports out of git. Use ignored
  `scan-output/`, `work/`, or `/tmp` for corpus experiments.
- Prefer platform `ibcmd` for export/import/rebuild validation. Do not route
  this repository's parser work through `vrunner` unless a task explicitly
  requires that separate tool.
- For ordinary Form.bin validity, prefer strict Designer
  `/DumpExternalDataProcessorOrReportToFiles` validation via
  `tools/platform_validate_epf.sh`; metadata-only load/check results are not
  enough to prove the form stream opens correctly.
- Do not commit generated private reports from `scan-output/`, copied platform
  libraries, DMF jars, platform archives, local database directories, or
  extracted customer configuration trees.
- If a command fails because the local environment lacks license, container, or
  platform prerequisites, ingest the failure as evidence and checkpoint the
  blocked state instead of silently skipping validation.

## Ordinary Form Target Architecture / Целевая архитектура обычных форм

### English

This repository builds a Git-friendly source representation for 1C ordinary
forms. The goal is to make ordinary forms look and behave like managed forms in
source control:

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.gif
```

`Form.xml` is the public editable object model. It must contain named form
objects and properties: `Form`, `Title`, `Attributes`, `Events`, `Pages`,
`Page`, `Panel`, `Button`, `LabelDecoration`, `PictureDecoration`,
`InputField`, `Position`, `Bindings`, and other platform-derived
control/property names. Top-level `Commands` and generic `ChildItems` are not
part of the public ordinary-form contract unless platform evidence proves a
real ordinary-form object with that name. Events/actions should live on the
form or the concrete control where the platform exposes them. It should be
understandable to a 1C developer in the same way managed-form XML is
understandable.

The ordinary form binary internals are implementation details. The parser and
writer may internally decode and encode 1C list-stream/bracket data. Platform
symbols known from 8.2.19 as `cf_form_controls8`,
`cf_form_controls_position8`, and `cf_form_controls_info8` identify
ordinary-control payload formats in the platform mechanism. Treat them as
internal codec evidence, not as public XML.

Hard public XML rule: never expose raw or indexed platform data under any name.
Do not add `ObjectModel`, `ListStream`, `BracketStream`, `FormBin`,
`LogicalStream`, `RawBracket`, `PlatformRecords`, `Field kind="list"`,
`Field kind="atom"`, base64 source streams, binary placeholders, or any
equivalent renamed dump/fallback/lossless structure. Passing platform validation
is not enough if the public XML is just a renamed raw stream dump.

If a low-level value is required for correct rebuild, promote it into a named,
schema-backed object-model concept: a control, type-specific property,
type-specific event, binding, picture reference, type descriptor, or other
platform-derived public property. The serializer must map that named XML back
to the internal platform list-stream representation and then write
ListOutStream/Form.bin.

### Русский

Этот репозиторий делает человекочитаемое и удобное для Git представление
обычных форм 1С. Цель - чтобы обычные формы в исходниках выглядели и
использовались максимально похоже на управляемые формы:

```text
Forms/<ИмяФормы>/Ext/Form.xml
Forms/<ИмяФормы>/Ext/Form/Module.bsl
Forms/<ИмяФормы>/Ext/Form/Items/<ИмяЭлемента>/Picture.gif
```

`Form.xml` - это публичная редактируемая объектная модель. В нем должны быть
именованные объекты и свойства формы: `Form`, `Title`, `Attributes`, `Events`,
`Pages`, `Page`, `Panel`, `Button`, `LabelDecoration`, `PictureDecoration`,
`InputField`, `Position`, `Bindings` и другие имена контролов/свойств, взятые
из платформенной модели. Верхнеуровневый `Commands` и общий контейнер
`ChildItems` не входят в публичный контракт обычной формы, пока платформенные
данные не доказывают реальный объект обычной формы с таким именем.
События/действия должны лежать на форме или конкретном контроле, где их
показывает платформа. Такой XML должен быть понятен разработчику 1С примерно
так же, как понятен XML управляемой формы.

Внутренности обычной `Form.bin` - это деталь реализации. Парсер и writer могут
внутри разбирать и собирать list-stream/скобкоформат 1С. Платформенные символы
8.2.19 `cf_form_controls8`, `cf_form_controls_position8`,
`cf_form_controls_info8` являются идентификаторами форматов payload обычных
контролов в типовом механизме платформы. Их нужно считать внутренним
свидетельством codec-слоя, а не публичным XML.

Жесткое правило публичного XML: нельзя выводить наружу сырые или индексные
платформенные данные ни под каким названием. Запрещены `ObjectModel`,
`ListStream`, `BracketStream`, `FormBin`, `LogicalStream`, `RawBracket`,
`PlatformRecords`, `Field kind="list"`, `Field kind="atom"`, base64-потоки,
binary placeholders и любые аналогичные переименованные dump/fallback/lossless
структуры. Если XML является просто переименованным сырым потоком, это
неправильная архитектура даже тогда, когда платформа его принимает.

Если низкоуровневое значение нужно для корректной обратной сборки, его нужно
поднять в именованное понятие объектной модели со схемой: контрол,
типизированное свойство конкретного контрола, типизированное событие
конкретного контрола, привязка, ссылка на картинку, описание типа или другое
публичное платформенное свойство. Serializer должен преобразовывать такой XML
во внутреннее платформенное list-stream-представление и уже затем писать
ListOutStream/Form.bin.
