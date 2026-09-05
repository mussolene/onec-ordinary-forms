# onec-ordinary-forms

## Проверенный статус на 2026-09-05

Проект пока не готов как универсальная утилита обычных форм.
Ветка `matvienko/product-model-rebuild` содержит новую библиотеку `liboof`
и CLI `oof`. Старый `oof-native` остается исследовательской реализацией.
Описания старой линии ниже не являются гарантией возможностей нового CLI.

Новый двоичный кодек поддерживает ограниченный вариант формата 27/18:
форма, реквизиты без связей, максимум одна кнопка, Caption, Enabled,
Position/Visible, Button.Click и модуль. Конкретные неподдержанные варианты
отклоняются с диагностикой. Однокнопочный сценарий проверен строгой выгрузкой
Designer 8.5.1.1343; 59 локальных входных форм новый CLI пока отклоняет.
Команды `validate`, `diff`, `edit` пока не реализованы.

Сборка требует CMake 3.20+, компилятор C++20, zlib и libxml2 с заголовками:

```bash
make test
build/sidecars/onec-form-native/oof --help
build/sidecars/onec-form-native/oof dump input/Form.bin output/Form.xml --json
build/sidecars/onec-form-native/oof build output/Form.xml rebuilt/Form.bin --json
```

`make test` выполняет все зарегистрированные CTest проверки, включая новую
библиотеку. Их успех не заменяет проверку формы платформой. `make release-gate`
намеренно завершается ошибкой до выполнения условий выпуска.

Причины неудач, текущие доказательства и минимальный порядок доведения:
[аудит проекта](docs/ordinary-form-pattern-audit.md).

## Историческое описание и целевой контракт

Tools for converting 1C ordinary forms into a Git-friendly source package and
building them back. The current release line uses a platform-like
`OrdinaryForm` object graph as the product object and a public source package
as the source of truth: `Form.xml`, `Form/Module.bsl`, and
`Form/Items/.../Picture.*`. The implementation and CLI surface are native C++ through
`sidecars/onec-form-native/build/oof-native`; the old implementation has been
removed to keep one product surface.

Hard architecture rule: `Form.bin` is only a container for the serialized form
stream and module stream. It is not the model. Existing `Form.bin` state,
baseline diff, patch workers, raw/list-stream preservation fields, hidden raw
object models, and compatibility profiles are not the product path. See
[`docs/ordinary-form-target-contract.md`](docs/ordinary-form-target-contract.md).

## English

### What This Project Does

1C ordinary forms are stored inside `Form.bin`. That binary contains the form
module, pictures, and the ordinary-form layout/control data in the platform's
internal list-stream format. This is hard to review, diff, edit, and merge.

The product target is to expose ordinary forms as source files that are close to
managed-form source exports: readable XML for the form object model,
`Module.bsl` as a separate file, and picture files under `Form/Items`.
This managed-form style package is the public architecture even though ordinary
forms are stored by the platform in `Form.bin`.

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
- `Form/Items/...` contains picture files that belong to object properties.

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
- `Picture file="..."` - a reference to a picture file in the source package.

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

In the target architecture, `build-bin` parses these named XML objects into the
`OrdinaryForm` graph, serializes that graph into the platform list-stream
representation, and then packs `form` and `module` streams into `Form.bin`.
That container layer is internal; users edit `Form.xml`, `Module.bsl`, and
`Items/.../Picture.*`.

### Internal Platform Pipeline

The target internal pipeline is symmetric:

```text
Form.bin -> form stream -> ListInStream -> OrdinaryForm object graph -> XSD-backed Form.xml
Form.xml -> OrdinaryForm object graph -> ListOutStream -> form stream -> Form.bin
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

Platform schema evidence is now maintained through native code/resources and
OACS evidence. Historical extraction scripts were removed with the old
implementation; use git history if a previous extraction helper is needed for
research.

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

`build-bin` must rebuild `Form.bin` from public `Form.xml`, `Form/Module.bsl`,
and `Form/Items/.../Picture.*` through `OrdinaryForm`. The release target is
`Form.xml -> OrdinaryForm -> ListOutStream -> Form.bin`; missing behavior must
be implemented as named object descriptors and serializers, not by keeping an
old `Form.bin` shape.

## Русский

### Что делает проект

Обычные формы 1С лежат внутри `Form.bin`. В этом бинарном файле находятся
модуль формы, картинки и данные обычной формы во внутреннем list-stream /
скобкоформате платформы. Такой файл сложно смотреть в Git, сравнивать,
редактировать и мержить.

Цель продукта - разложить обычную форму в исходники примерно так же, как
платформа раскладывает управляемую форму: человекочитаемый XML объектной
модели, отдельный `Module.bsl` и файлы картинок в `Form/Items`. Такая
managed-form style структура остается целевой публичной архитектурой, хотя
платформа хранит обычные формы внутри `Form.bin`.

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

Платформенные сведения для схем теперь поддерживаются через native-код,
ресурсы и OACS evidence. Исторические скрипты извлечения удалены вместе со
старой реализацией; при необходимости их можно поднять из git history для
отдельного исследования.

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

`build-bin` должен собирать `Form.bin` из публичного `Form.xml`,
`Form/Module.bsl` и `Form/Items/.../Picture.*` через `OrdinaryForm`.
Native-сборка с исходным `Form.bin` - это только diagnostic-only путь для уже
поддержанных edit-codec'ов. Это не целевая source-build архитектура, и ее
нельзя расширять raw fallback'ами, patch-worker'ами, baseline diff logic или
профилями сохранения baseline. Цель релиза остается `Form.xml ->
OrdinaryForm -> ListOutStream -> Form.bin`.

## Status / Статус

Current release: `0.4.6`.

Current implementation status:

- read ordinary `Form.bin` containers;
- dump readable object-model `Form.xml` and `Form/Module.bsl` through one native
  C++ package backend;
- validate `Form.xml` against bundled schemas;
- build ordinary `Form.bin` directly from public `Form.xml`, `Form/Module.bsl`,
  and `Form/Items/.../Picture.*` without requiring a source `Form.bin`;
- read changed `Form/Module.bsl` back into `Form.bin` during direct source
  rebuild;
- dump existing picture payloads to `Form/Items/.../Picture.*` and apply
  changed picture properties through the object/ListOut writer where supported;
- delete leaf form controls by removing the named control node from public
  `ChildItems`;
- scan local EPF/ERF corpora without committing private artifacts.

Target implementation status:

- keep the public package as the only editable source form;
- harden and extend the direct object/ListOut writer for more ordinary-form
  controls and properties;
- keep the implementation in native C++ without reintroducing seed templates,
  raw fallbacks, mandatory base bins, patch workers, or a parallel writer.

Текущий статус реализации:

- чтение контейнеров обычных `Form.bin`;
- выгрузка читаемого объектного `Form.xml` и `Form/Module.bsl` через единый
  native C++ package backend;
- проверка `Form.xml` по встроенным схемам обычных форм;
- сборка обычного `Form.bin` напрямую из публичного `Form.xml`,
  `Form/Module.bsl` и `Form/Items/.../Picture.*` без исходного `Form.bin`;
- чтение измененного `Form/Module.bsl` обратно в `Form.bin` при прямой сборке
  из source package;
- выгрузка существующих картинок в `Form/Items/.../Picture.*` и применение
  измененных картинок как свойств объекта через object/ListOut writer там, где это
  поддержано;
- удаление leaf-элементов формы через удаление именованного узла из публичного
  `ChildItems`;
- сканирование локальных EPF/ERF-корпусов без коммита приватных артефактов.

Целевой статус реализации:

- удержание публичного package как единственного редактируемого источника;
- расширение и укрепление прямого object/ListOut writer для большего числа
  контролов и свойств обычных форм;
- удержание реализации в native C++ без возврата seed templates, raw fallback,
  обязательного исходного `Form.bin`, patch-worker'ов или параллельного writer.

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

- CI runs on GitHub Actions for pushes, pull requests, and manual dispatch.
- CI builds the native C++ tool, runs native tests, and runs native smoke.
- Release workflow runs for `v*` tags or manual dispatch with a tag input,
  rebuilds the native binary, rechecks it, and publishes GitHub Release assets.

Автоматизация:

- CI запускается в GitHub Actions для push, pull request и manual dispatch;
- CI собирает native C++ tool, выполняет native tests и native smoke;
- релизный workflow запускается для тегов `v*` или вручную с указанием тега,
  пересобирает native binary, проверяет его и публикует артефакты в GitHub Release.

## Install

```bash
make -C sidecars/onec-form-native
make test
```

For the same checks used in CI:

```bash
make test
make smoke
```

## CLI

Dump an ordinary form binary:

```bash
sidecars/onec-form-native/build/oof-native formbin-dump-package \
  scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

The command writes one managed-form-like package:

```text
scan-output/exported/Object/Forms/Form/Ext/Form.xml
scan-output/exported/Object/Forms/Form/Ext/Form/Module.bsl
scan-output/exported/Object/Forms/Form/Ext/Form/Items/<ElementName>/Picture.gif
```

Schemas are kept in the repository-level `schemas/` directory:

```bash
ls schemas
```

Build `Form.bin` directly from the source package:

```bash
sidecars/onec-form-native/build/oof-native formbin-build-source-package \
  scan-output/exported/Object/Forms/Form/Ext/Form.xml \
  scan-output/rebuilt/Form.bin
```

The release-facing path is source package to `OrdinaryForm` to `Form.bin`
through native C++. There is no release build path that requires an existing
source `Form.bin`.

Before platform import, use a copy of the source tree where the public ordinary
`Ext/Form.xml` and `Ext/Form/` sidecar directory are removed. The platform
source layout for ordinary forms should contain `Ext/Form.bin`; managed forms
keep their native `Ext/Form.xml`.

Writer behavior is intentionally conservative while the named ordinary-form
object model is being completed. The public source contract is the package
`Form.xml`, `Form/Module.bsl`, and `Form/Items/.../Picture.*`. The direct
rebuild path materializes that package into `OrdinaryForm`, serializes the graph
into the internal platform list-stream, and assembles a new `Form.bin`
container without exposing raw stream/profile data. Base-backed rebuild commands
are kept only for diagnostics and must not be used as release evidence for the
source package writer. Leaf controls can be deleted by removing their public XML
node where the writer supports that shape.

Diagnostic commands:

```bash
sidecars/onec-form-native/build/oof-native container-extract Form.bin scan-output/form-parts
sidecars/onec-form-native/build/oof-native formbin-roundtrip Form.bin
sidecars/onec-form-native/build/oof-native formbin-info Form.bin
sidecars/onec-form-native/build/oof-native object-model-gate
```

`container-extract` and `formbin-info` are diagnostics for `Form.bin` container
research. They are not the target public source layout and should not be used as
the editable representation of a form.

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
