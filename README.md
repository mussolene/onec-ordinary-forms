# onec-ordinary-forms

Tools for converting 1C ordinary form `Form.bin` into a Git-friendly source
package and building it back. The ordinary-form codec is being moved into the
native C++ sidecar; Python currently provides the CLI/API wrapper, schema
validation, corpus diagnostics, and release gates.

## English

### What This Project Does

1C ordinary forms are stored inside `Form.bin`. That binary contains the form
module, pictures, and the ordinary-form layout/control data in the platform's
internal list-stream format. This is hard to review, diff, edit, and merge.

The product target is to expose ordinary forms as source files that are close to
managed-form source exports: readable XML for the form object model,
`Module.bsl` as a separate file, and pictures as sidecar files. This managed-form
style package is the public architecture even though ordinary forms are stored
by the platform in `Form.bin`.

The target public source layout mirrors managed forms:

```text
Forms/Form/Ext/Form.xml
Forms/Form/Ext/Form/Module.bsl
Forms/Form/Ext/Form/Items/<ElementName>/Picture.gif
```

### Target Public XML

`Form.xml` is the public editable object model. It should describe the form with
named controls and properties, not with raw platform records:

```xml
<Form ordinaryFormVersion="2.0"
      xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
      xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd">
  <Title>
    <Item lang="ru">Form title</Item>
  </Title>
  <Attributes>
    <Attribute name="InputValue">
      <Type>...</Type>
    </Attribute>
  </Attributes>
  <ChildItems>
    <Page name="Main">
      <ChildItems>
        <Panel name="MainPanel">
          <Position left="8" top="8" right="640" bottom="480"/>
          <LabelDecoration name="Caption">
            <Title>
              <Item lang="ru">Caption text</Item>
            </Title>
          </LabelDecoration>
          <Button name="RunButton">
            <Title>
              <Item lang="ru">Run</Item>
            </Title>
            <Events>
              <Event name="Нажатие">Run</Event>
            </Events>
          </Button>
          <PictureDecoration name="Logo">
            <Picture file="Items/Logo/Picture.gif"/>
          </PictureDecoration>
        </Panel>
      </ChildItems>
    </Page>
  </ChildItems>
</Form>
```

Control properties and events are type-specific and come from the platform help
palette for ordinary controls. The XML should contain only values explicitly
set on the form; platform defaults should stay implicit.

Public XML uses one English vocabulary for element and attribute names. Russian
platform names for ordinary-form controls, properties, events, and value types
belong in `OrdinaryFormPalette.xsd` as `xs:annotation/xs:appinfo` metadata, not
in a separate mapping file and not as public XML tag names.

Public `Form.xml` follows the managed-form-style tree in `OrdinaryForm.xsd`:
`ChildItems`, nested controls, `Attributes`, `Commands`, and `Events`.

### Reading Form.xml

The ordinary form source package is meant to be read from the top down:

- `Form.xml` is the form object model.
- `Form/Module.bsl` is the ordinary form module.
- `Form/Items/...` contains extracted sidecar files such as pictures.

Inside `Form.xml`, the main sections are:

- `Title` - localized form title.
- `Events` - form-level event handlers, for example `ПриОткрытии`.
- `Attributes` - form attributes and their 1C type descriptions.
- `ChildItems` - top-level pages, panels, and nested controls (managed-form style).
- `Commands` - form commands when the platform materializes them.
- control nodes such as `Panel`, `InputField`, `Button`, `Table`,
  `CommandBar`, `LabelDecoration`, and `PictureDecoration`.
- `Position` - control geometry and bindings.
- `Action` or `Events` under a control - handlers connected to that control.
- `Picture file="..."` - a reference to a sidecar image next to the XML.

For example, a button is edited as a named object:

```xml
<Button name="RunButton" id="12">
  <Title>
    <Item lang="ru">Run</Item>
  </Title>
  <Position left="16" top="40" right="120" bottom="64"/>
  <Action name="RunButtonНажатие" title="Run button click"/>
</Button>
```

In the target architecture, `build-bin` serializes these named XML objects into
the platform list-stream representation and then packs `form`, `module`, and
picture payloads into `Form.bin`. That container layer is internal; users edit
`Form.xml`, `Module.bsl`, and sidecar files.

### Internal Platform Pipeline

The target internal pipeline is symmetric:

```text
Form.bin -> form raw stream -> ListInStream -> platform object model -> XSD-backed Form.xml
Form.xml -> platform object model -> ListOutStream -> form raw stream -> Form.bin
```

The schema layer is intentionally small and focused on ordinary forms:

- `OrdinaryForm.xsd` describes the public managed-style `Form.xml` root and control tree;
- `OrdinaryFormPalette.xsd` holds the platform palette, per-control property/event vocabulary, and reusable value types for tooling;
- `PlatformConfigStructure.xsd` records platform-derived configuration,
  metadata tree, type-domain, `CompositeID`, `ValueToStringInternal`/
  `ValueFromStringInternal`, and serializer evidence used by the codec layer.

These codec concepts are not a public raw-stream dump. Full platform XSD
extractions are kept as reproducible research artifacts under ignored
`scan-output/`, not vendored into the package.

Regenerate platform schema evidence from a local platform mirror with:

```bash
python3 tools/extract_platform_xml_resources.py \
  --root <platform-bin-dir> \
  --out-dir scan-output/platform85/xml-clean \
  --schemas-only
python3 tools/vendor_platform_schemas.py \
  --resources-json scan-output/platform85/xml-clean/resources.json \
  --source-dir scan-output/platform85/xml-clean \
  --out-dir scan-output/platform85/vendor-check \
  --platform-version 8.5
```

### What Must Not Be In Public XML

Public `Form.xml` must not contain raw or renamed platform dumps. The following
are not acceptable public structures:

- `ObjectModel`
- `ListStream`
- `BracketStream`
- `FormBin`
- `LogicalStream`
- `RawBracket`
- `PlatformRecords`
- `SerializationProfile`
- `DataSourceProfile`
- `ViewProfile`
- `StateBlob`
- `ValueDescriptor`
- profile/slot-preservation attributes such as `profileUuid`,
  `actionProfileState`, or `linkModeShape`
- indexed trees such as `Field kind="list"` or `Field kind="atom"`
- embedded base64 source streams
- binary placeholders or other lossless/fallback stream copies

The parser/writer may use the platform list-stream format internally. Platform
symbols such as `cf_form_controls8`, `cf_form_controls_position8`, and
`cf_form_controls_info8` identify ordinary-control payload formats in the
platform mechanism. They are implementation details, not public XML nodes. If
a value is needed for rebuild, it must be promoted to a named XML concept: a
control, property, event, command, binding, picture reference, or type
descriptor.

Passing 1C Designer validation is required, but not sufficient by itself: the
public XML must also remain a clean object model, not a renamed raw stream.

Current native rebuild is deliberately conservative: `build-bin` applies the
public package to the original ordinary `Form.bin` object graph passed as
`--base-bin`. The baseline is a private codec input, not a public XML fallback or
renamed raw stream. The native C++ package command owns both `Form.xml` and
`Module.bsl` and existing picture sidecars in the same command path.

## Русский

### Что делает проект

Обычные формы 1С лежат внутри `Form.bin`. В этом бинарном файле находятся
модуль формы, картинки и данные обычной формы во внутреннем list-stream /
скобкоформате платформы. Такой файл сложно смотреть в Git, сравнивать,
редактировать и мержить.

Цель продукта - разложить обычную форму в исходники примерно так же, как
платформа раскладывает управляемую форму: человекочитаемый XML объектной
модели, отдельный `Module.bsl` и картинки рядом. Такая managed-form style
структура остается целевой публичной архитектурой, хотя платформа хранит
обычные формы внутри `Form.bin`.

Целевая структура файлов:

```text
Forms/Form/Ext/Form.xml
Forms/Form/Ext/Form/Module.bsl
Forms/Form/Ext/Form/Items/<ИмяЭлемента>/Picture.gif
```

### Целевой публичный XML

`Form.xml` - это публичная редактируемая объектная модель. Он должен описывать
форму именованными элементами и свойствами, а не сырыми платформенными
записями:

```xml
<Form ordinaryFormVersion="2.0"
      xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
      xsi:noNamespaceSchemaLocation="OrdinaryForm.xsd">
  <Title>
    <Item lang="ru">Заголовок формы</Item>
  </Title>
  <Attributes>
    <Attribute name="ВходноеЗначение">
      <Type>...</Type>
    </Attribute>
  </Attributes>
  <ChildItems>
    <Page name="Основная">
      <ChildItems>
        <Panel name="ОсновнаяПанель">
          <Position left="8" top="8" right="640" bottom="480"/>
          <LabelDecoration name="Надпись">
            <Title>
              <Item lang="ru">Текст надписи</Item>
            </Title>
          </LabelDecoration>
          <Button name="КнопкаВыполнить">
            <Title>
              <Item lang="ru">Выполнить</Item>
            </Title>
            <Events>
              <Event name="Нажатие">Выполнить</Event>
            </Events>
          </Button>
          <PictureDecoration name="Логотип">
            <Picture file="Items/Логотип/Picture.gif"/>
          </PictureDecoration>
        </Panel>
      </ChildItems>
    </Page>
  </ChildItems>
</Form>
```

Публичный XML использует единый английский словарь элементов и свойств.
Платформенные русские имена свойств, событий и типов обычной формы хранятся в
`OrdinaryFormPalette.xsd` как `xs:annotation/xs:appinfo`, а не отдельным mapping
файлом и не публичными XML-тегами. В XML должны попадать только явно заданные
значения; дефолты платформы остаются неявными.

Публичный `Form.xml` следует дереву управляемой формы из `OrdinaryForm.xsd`:
`ChildItems`, вложенные контролы, `Attributes`, `Commands` и `Events`.

### Как читать Form.xml

Пакет исходников обычной формы читается сверху вниз:

- `Form.xml` - объектная модель формы.
- `Form/Module.bsl` - модуль обычной формы.
- `Form/Items/...` - вынесенные рядом файлы, например картинки.

Внутри `Form.xml` основные разделы такие:

- `Title` - локализованный заголовок формы.
- `Events` - события самой формы, например `ПриОткрытии`.
- `Attributes` - реквизиты формы и описания их типов 1С.
- `ChildItems` - страницы, панели и вложенные элементы (как в управляемой форме).
- `Commands` - команды формы, если платформа их материализует.
- узлы контролов: `Panel`, `InputField`, `Button`, `Table`, `CommandBar`,
  `LabelDecoration`, `PictureDecoration` и другие элементы палитры.
- `Position` - геометрия элемента и привязки.
- `Action` или `Events` внутри элемента - обработчики, подключенные к нему.
- `Picture file="..."` - ссылка на картинку рядом с XML.

Например, кнопка редактируется как именованный объект:

```xml
<Button name="КнопкаВыполнить" id="12">
  <Title>
    <Item lang="ru">Выполнить</Item>
  </Title>
  <Position left="16" top="40" right="120" bottom="64"/>
  <Action name="КнопкаВыполнитьНажатие" title="Нажатие кнопки выполнить"/>
</Button>
```

В целевой архитектуре `build-bin` сериализует эти именованные XML-объекты во
внутренний list-stream/скобкоформат платформы и затем упаковывает документы
`form`, `module` и картинки в `Form.bin`. Этот контейнерный слой внутренний;
пользователь редактирует `Form.xml`, `Module.bsl` и файлы рядом.

### Внутренний платформенный конвейер

Целевой внутренний конвейер симметричный:

```text
Form.bin -> raw-поток form -> ListInStream -> объектная модель платформы -> Form.xml по XSD
Form.xml -> объектная модель платформы -> ListOutStream -> raw-поток form -> Form.bin
```

Слой схем специально небольшой и сфокусирован на обычных формах:

- `OrdinaryForm.xsd` описывает публичный managed-style корень `Form.xml` и дерево контролов;
- `OrdinaryFormPalette.xsd` хранит палитру платформы, словарь свойств/событий и типы значений для tooling;
- `PlatformConfigStructure.xsd` фиксирует платформенные сведения о структуре
  конфигурации, дереве метаданных, type-domain, `CompositeID`,
  `ValueToStringInternal`/`ValueFromStringInternal` и сведения о
  сериализаторах, используемые слоем кодека.

Эти понятия кодека не являются публичным дампом сырого потока. Полные извлечения
платформенных XSD хранятся как воспроизводимые исследовательские артефакты в
игнорируемом `scan-output/`, а не поставляются внутри пакета.

Обновить платформенные сведения для схем из локального зеркала платформы можно
так:

```bash
python3 tools/extract_platform_xml_resources.py \
  --root <platform-bin-dir> \
  --out-dir scan-output/platform85/xml-clean \
  --schemas-only
python3 tools/vendor_platform_schemas.py \
  --resources-json scan-output/platform85/xml-clean/resources.json \
  --source-dir scan-output/platform85/xml-clean \
  --out-dir scan-output/platform85/vendor-check \
  --platform-version 8.5
```

### Чего не должно быть в публичном XML

Публичный `Form.xml` не должен содержать сырые или переименованные дампы
платформенного формата. Нельзя выводить наружу:

- `ObjectModel`
- `ListStream`
- `BracketStream`
- `FormBin`
- `LogicalStream`
- `RawBracket`
- `PlatformRecords`
- `SerializationProfile`
- `DataSourceProfile`
- `ViewProfile`
- `StateBlob`
- `ValueDescriptor`
- атрибуты сохранения profile/slot-формы вроде `profileUuid`,
  `actionProfileState` или `linkModeShape`
- индексные деревья вроде `Field kind="list"` или `Field kind="atom"`
- встроенные base64-потоки исходного файла
- бинарные заглушки и другие побайтно-сохраняющие или резервные копии
  потоковых структур

Парсер и writer могут использовать list-stream/скобкоформат платформы внутри.
Платформенные символы `cf_form_controls8`, `cf_form_controls_position8`,
`cf_form_controls_info8` являются идентификаторами форматов данных обычных
контролов в типовом механизме платформы. Это внутренняя реализация, а не
публичные XML-узлы. Если значение нужно для обратной сборки, его нужно поднять
в именованное понятие XML: контрол, свойство, событие, команда, привязка,
ссылка на картинку или описание типа.

Проверка через 1C Designer обязательна, но сама по себе недостаточна:
публичный XML все равно должен оставаться чистой объектной моделью, а не
переименованным сырым потоком.

Текущая native-сборка намеренно консервативна: `build-bin` применяет публичный
пакет к исходному графу объектов обычной формы, переданному через `--base-bin`.
Этот baseline является приватным входом codec-слоя, а не публичным
fallback/XML-дампом. Native C++ package-команда уже владеет `Form.xml` и
`Module.bsl`, а также существующими sidecar-картинками в том же codec-пути.

## Status / Статус

Current release: `0.4.6`.

Current implementation status:

- read ordinary `Form.bin` containers;
- dump readable object-model `Form.xml` and `Form/Module.bsl` through one native
  C++ package backend;
- validate `Form.xml` against bundled schemas;
- build ordinary `Form.bin` by applying supported public XML edits to the
  original native object graph passed with `--base-bin`;
- read changed `Form/Module.bsl` back into `Form.bin` while rebuilding from the
  native baseline;
- dump existing picture payloads to `Form/Items/.../Picture.*` sidecars and
  apply changed picture sidecars back to existing baseline picture slots;
- delete leaf form controls by removing the named control node from public
  `ChildItems`;
- scan local EPF/ERF corpora without committing private artifacts.

Target implementation status:

- add new controls from public `ChildItems` through the native package backend;
- create new picture payload slots from the named package when the baseline
  object does not already contain one;
- build ordinary `Form.bin` from that named package without requiring a source
  `Form.bin` baseline;
- keep this package codec in C++, with Python limited to orchestration and
  validation helpers.

Текущий статус реализации:

- чтение контейнеров обычных `Form.bin`;
- выгрузка читаемого объектного `Form.xml` и `Form/Module.bsl` через единый
  native C++ package backend;
- проверка `Form.xml` по встроенным схемам обычных форм;
- сборка обычного `Form.bin` путем применения поддержанных правок публичного XML
  к исходному native-графу объектов, переданному через `--base-bin`;
- чтение измененного `Form/Module.bsl` обратно в `Form.bin` при сборке из native
  baseline;
- выгрузка существующих картинок в `Form/Items/.../Picture.*` sidecars и
  применение измененных sidecar-картинок обратно к существующим picture-слотам
  baseline;
- удаление leaf-элементов формы через удаление именованного узла из публичного
  `ChildItems`;
- сканирование локальных EPF/ERF-корпусов без коммита приватных артефактов.

Целевой статус реализации:

- добавление новых контролов из публичного `ChildItems` через native package
  backend;
- создание новых picture payload slots из именованного пакета, если в baseline
  объекте еще нет такого слота;
- сборка обычного `Form.bin` из этого именованного пакета без исходного
  `Form.bin` как baseline;
- удержание package codec в C++, при Python только как слой orchestration и
  validation.

Validation status:

- `v0.4.5` stabilizes typed ordinary-form `Color` and `Font` round-trips:
  rebuilt extended base-info records are dumped again without losing
  `TextColor`, `BackColor`, `BorderColor`, or `Font`, and the first 100 UT
  forms with Color/Font are stable across repeated build/dump cycles.
- `v0.4.4` writes regular `Panel` page metadata with the platform 25-page
  capacity profile, fixing strict Designer load for XML-writer rebuilt forms
  that contain nested panel pages and typed `ActiveXControl` payloads.
- `v0.4.3` adds typed ordinary-form `ActiveXControl` XML/XSD support,
  including CLSID and state blobs, and preserves the PDF ActiveX control found
  in a direct `Form.bin` dump/build check.
- `v0.4.2` extends the `v0.4.1` edit stabilization with strict
  platform-validated add `Button` and delete leaf `LabelDecoration` scenarios.
- `v0.4.1` preserves the `v0.4.0` no-op rebuild baseline and adds a strict
  platform-validated XML add-control path for a paged `LabelDecoration` with
  `id=max(existing)+1`.
- `v0.4.0` has one UT ordinary list-form no-op dump/build byte-identical for
  the full `Form.bin` container, form payload, and module payload.
- The first-50 UT corpus smoke currently has no exceptions and keeps module
  payloads stable, but only 1/50 forms are full byte-identical.
- The next known mismatch class is a root format/profile length variant.

Статус проверок:

- `v0.4.5` стабилизирует обратную сборку типизированных `Color` и `Font` для
  обычных форм: расширенные base-info записи после rebuild снова выгружаются
  без потери `TextColor`, `BackColor`, `BorderColor` и `Font`, а первые 100 UT
  форм с Color/Font стабильны на повторных циклах build/dump.
- `v0.4.4` пишет metadata страниц обычного `Panel` через платформенный
  capacity-профиль на 25 страниц, что чинит строгую загрузку Designer для форм,
  пересобранных XML-writer-ом, с вложенными страницами панели и типизированным
  `ActiveXControl`.
- `v0.4.3` добавляет типизированную XML/XSD-поддержку обычного
  `ActiveXControl`, включая CLSID и state blobs, и сохраняет PDF ActiveX
  control в прямой проверке `Form.bin` dump/build.
- `v0.4.2` расширяет стабилизацию редактирования `v0.4.1` строгими
  платформенными проверками добавления `Button` и удаления leaf
  `LabelDecoration`.
- `v0.4.1` сохраняет baseline обратной сборки `v0.4.0` и добавляет строгую
  платформенную проверку добавления paged `LabelDecoration` из XML с
  `id=max(existing)+1`.
- в `v0.4.0` один UT-сценарий обратной сборки обычной list-form без изменений
  совпадает побайтно по всему контейнеру `Form.bin`, данным формы и данным
  модуля;
- first-50 UT smoke сейчас проходит без исключений и сохраняет данные модулей,
  но полное побайтное совпадение есть только у 1/50 форм;
- следующий известный класс расхождений - вариант длины корневого
  format/profile.

Automation:

- CI runs on GitHub Actions for pushes, pull requests, and manual dispatch on
  Python 3.10, 3.11, and 3.12.
- CI executes tests, CLI smoke, package build, and package metadata checks.
- Release workflow runs for `v*` tags or manual dispatch with a tag input,
  rebuilds the package, rechecks it, and publishes GitHub Release assets.

Автоматизация:

- CI запускается в GitHub Actions для push, pull request и manual dispatch на
  Python 3.10, 3.11 и 3.12;
- CI выполняет тесты, CLI smoke, сборку пакета и проверку package metadata;
- релизный workflow запускается для тегов `v*` или вручную с указанием тега,
  пересобирает пакет, проверяет его и публикует артефакты в GitHub Release.

## Install

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e '.[dev]'
make test
```

For the same checks used in CI:

```bash
PYTHONPATH=src pytest -q
make smoke
python -m build
python -m twine check dist/*
```

## CLI

Dump an ordinary form binary:

```bash
onec-ordinary-forms dump-bin \
  --bin scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  --out scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

The command is backed by the native C++ `oof-native formbin-dump-package` engine
and writes one managed-form-like package:

```text
scan-output/exported/Object/Forms/Form/Ext/Form.xml
scan-output/exported/Object/Forms/Form/Ext/Form/Module.bsl
scan-output/exported/Object/Forms/Form/Ext/Form/Items/<ElementName>/Picture.gif
```

Validate and format the XML:

```bash
onec-ordinary-forms validate --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml
onec-ordinary-forms format-xml --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

Show bundled schemas:

```bash
onec-ordinary-forms schemas
```

Build `Form.bin` back by applying the package to the original native baseline:

```bash
onec-ordinary-forms build-bin \
  --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml \
  --base-bin scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  --out-bin scan-output/rebuilt/Form.bin
```

Before platform import, use a copy of the source tree where the public ordinary
`Ext/Form.xml` and `Ext/Form/` sidecar directory are removed. The platform
source layout for ordinary forms should contain `Ext/Form.bin`; managed forms
keep their native `Ext/Form.xml`.

Writer behavior is intentionally conservative while the named ordinary-form
object model is being completed. The public source contract is the package
`Form.xml`, `Form/Module.bsl`, and `Form/Items/.../Picture.*` sidecars. The
rebuild algorithm uses the original native `Form.bin` as a private object-graph
baseline and does not expose raw stream/profile data. Picture edits are applied
to controls that already have a writable picture payload in that baseline. Leaf
controls can be deleted by removing their public XML node.

Diagnostic commands:

```bash
onec-ordinary-forms unpack-bin --bin Form.bin --out-dir scan-output/form-parts
onec-ordinary-forms pack-bin --parts-dir scan-output/form-parts --out-bin Form.bin
onec-ordinary-forms digest-xml --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml --out-json scan-output/form-digest.json
onec-ordinary-forms scan-corpus --root "<private-processors-dir>" --out-json scan-output/corpus.json
onec-ordinary-forms scan-corpus \
  --root "<private-processors-dir>" \
  --exported-root scan-output/platform-export \
  --compare-exported-root scan-output/platform-redump \
  --out-json scan-output/corpus-semantic.json
```

`unpack-bin` and `pack-bin` are diagnostics for `Form.bin` container research.
They are not the target public source layout and should not be used as the
editable representation of a form.

`digest-xml` reports a normalized semantic graph hash for object-model
comparisons: controls, parent order, positions, bindings, events, attributes,
table columns/editor controls, pictures, fonts, colors, and command sources.
Use it before byte-level corpus reports to distinguish real semantic loss from
platform serialization noise.

`scan-corpus --semantic-digest` adds the same hash and summary to each exported
ordinary `Form.xml`. `--compare-exported-root` compares matching forms in two
exported trees and reports `equal`, `different`, `sourceUnavailable`, or
`targetUnavailable` without exposing absolute local paths.

## Python API

```python
from onec_ordinary_forms import build_form_bin, dump_form_bin, validate_form_xml

dump_form_bin(
    "scan-output/exported/Object/Forms/Form/Ext/Form.bin",
    "scan-output/exported/Object/Forms/Form/Ext/Form.xml",
)

validate_form_xml("scan-output/exported/Object/Forms/Form/Ext/Form.xml")

build_form_bin(
    "scan-output/exported/Object/Forms/Form/Ext/Form.xml",
    "scan-output/rebuilt/Form.bin",
    "scan-output/exported/Object/Forms/Form/Ext/Form.bin",
)
```

`dump_form_bin` and `build_form_bin` use the native C++ backend. Rebuild requires
the original `Form.bin` as the object-graph baseline until native create-from-XML
is proven separately.

## Platform Validation

For writer changes, validate rebuilt processors through the 1C platform, not
only through metadata-level checks. The helper in
`tools/platform_validate_epf.sh` runs Designer batch export and catches
malformed ordinary form streams.

Private processors, platform exports, license configuration, and generated
reports must stay in ignored local directories such as `scan-output/`, `work/`,
or `/tmp`.

## Documentation

- [Architecture](docs/architecture.md)
- [Development](docs/development.md)
- [Container validation](docs/containers.md)
- [Research notes](docs/research-map.md)

## Fixture Policy

Do not commit private EPF/ERF files, CF/DT dumps, platform archives, license
files, OACS databases, generated platform exports, or customer metadata. The
`examples/fixtures/` directory is ignored except for `.gitkeep`.
