# onec-ordinary-forms

Python tools for converting 1C ordinary form `Form.bin` into a Git-friendly
source package and building it back.

## English

### What This Project Does

1C ordinary forms are stored inside `Form.bin`. That binary contains the form
module, pictures, and the ordinary-form layout/control data in the platform's
internal list-stream format. This is hard to review, diff, edit, and merge.

The goal of this project is to expose ordinary forms as source files that are
close to managed-form source exports: readable XML for the form object model,
`Module.bsl` as a separate file, and pictures as sidecar files.

The public source layout mirrors managed forms:

```text
Forms/Form/Ext/Form.xml
Forms/Form/Ext/Form/Module.bsl
Forms/Form/Ext/Form/Items/<ElementName>/Picture.gif
```

### Target Public XML

`Form.xml` is the public editable object model. It should describe the form with
named controls and properties, not with raw platform records:

```xml
<Form>
  <Title>
    <Item lang="ru">Form title</Item>
  </Title>
  <Attributes>
    <Attribute name="InputValue">
      <Type>...</Type>
    </Attribute>
  </Attributes>
  <Pages>
    <Page name="Main">
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
    </Page>
  </Pages>
</Form>
```

Control properties and events are type-specific and come from the platform help
palette for ordinary controls. The XML should contain only values explicitly
set on the form; platform defaults should stay implicit.

The exact schema is still being expanded, but the target direction is fixed:
public XML must be managed-form-like, named, and understandable to a 1C
developer.

### Reading Form.xml

The ordinary form source package is meant to be read from the top down:

- `Form.xml` is the form object model.
- `Form/Module.bsl` is the ordinary form module.
- `Form/Items/...` contains extracted sidecar files such as pictures.

Inside `Form.xml`, the main sections are:

- `Title` - localized form title.
- `Events` - form-level event handlers, for example `ПриОткрытии`.
- `Attributes` - form attributes and their 1C type descriptions.
- `Pages` - top-level form pages and their nested controls.
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

During `build-bin`, the writer serializes these named XML objects into the
platform list-stream representation and then packs `form` and `module` into
`Form.bin`. That container layer is internal; users edit `Form.xml`,
`Module.bsl`, and sidecar files.

### Internal Platform Pipeline

The target internal pipeline is symmetric:

```text
Form.bin -> form raw stream -> ListInStream -> platform object model -> XSD-backed Form.xml
Form.xml -> platform object model -> ListOutStream -> form raw stream -> Form.bin
```

`ordinary-form.xsd` describes the public ordinary-form XML. The separate
`metadata-configuration.xsd` describes platform-derived metadata/value concepts
used by the codec layer, including `CompositeID`, `TypeDomainPattern`,
`ValueToStringInternal`/`ValueFromStringInternal`, and the platform schema
resources extracted from 8.2 resource files. The bundled
`platform_model_catalog.json` is generated from platform resources and EDT EMF
model jars; it records metadata classes, mcore type/value classes, and
serializer evidence without shipping platform binaries. These codec concepts
are not a public raw-stream dump.

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

## Русский

### Что делает проект

Обычные формы 1С лежат внутри `Form.bin`. В этом бинарном файле находятся
модуль формы, картинки и данные обычной формы во внутреннем list-stream /
скобкоформате платформы. Такой файл сложно смотреть в Git, сравнивать,
редактировать и мержить.

Цель проекта - разложить обычную форму в исходники примерно так же, как
платформа раскладывает управляемую форму: человекочитаемый XML объектной
модели, отдельный `Module.bsl` и картинки рядом.

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
<Form>
  <Title>
    <Item lang="ru">Заголовок формы</Item>
  </Title>
  <Attributes>
    <Attribute name="ВходноеЗначение">
      <Type>...</Type>
    </Attribute>
  </Attributes>
  <Pages>
    <Page name="Основная">
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
    </Page>
  </Pages>
</Form>
```

Публичный XML использует единый английский словарь элементов и свойств.
Платформенные русские имена свойств, событий и типов обычной формы хранятся в
`ordinary-form.xsd` как `xs:annotation/xs:appinfo`, а не отдельным mapping
файлом и не публичными XML-тегами. В XML должны попадать только явно заданные
значения; дефолты платформы остаются неявными.

Схема еще расширяется, но направление фиксированное: публичный XML должен быть
похож на XML управляемой формы, быть именованным и понятным разработчику 1С.

### Как читать Form.xml

Пакет исходников обычной формы читается сверху вниз:

- `Form.xml` - объектная модель формы.
- `Form/Module.bsl` - модуль обычной формы.
- `Form/Items/...` - вынесенные рядом файлы, например картинки.

Внутри `Form.xml` основные разделы такие:

- `Title` - локализованный заголовок формы.
- `Events` - события самой формы, например `ПриОткрытии`.
- `Attributes` - реквизиты формы и описания их типов 1С.
- `Pages` - страницы формы и вложенные в них элементы.
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

При `build-bin` writer сериализует эти именованные XML-объекты во внутренний
list-stream/скобкоформат платформы и затем упаковывает документы `form` и
`module` в `Form.bin`. Этот контейнерный слой внутренний; пользователь
редактирует `Form.xml`, `Module.bsl` и файлы рядом.

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
- индексные деревья вроде `Field kind="list"` или `Field kind="atom"`
- встроенные base64-потоки исходного файла
- binary placeholders и другие lossless/fallback копии stream-структур

Парсер и writer могут использовать list-stream/скобкоформат платформы внутри.
Платформенные символы `cf_form_controls8`, `cf_form_controls_position8`,
`cf_form_controls_info8` являются идентификаторами форматов payload обычных
контролов в типовом механизме платформы. Это внутренняя реализация, а не
публичные XML-узлы. Если значение нужно для обратной сборки, его нужно поднять
в именованное понятие XML: контрол, свойство, событие, команда, привязка,
ссылка на картинку или описание типа.

Проверка через 1C Designer обязательна, но сама по себе недостаточна:
публичный XML все равно должен оставаться чистой объектной моделью, а не
переименованным сырым потоком.

Текущая строгая проверка 1C выполняется без template/fallback: `build-bin`
собирает ordinary `Form.bin` из чистого объектного `Form.xml`, `Module.bsl` и
картинок. Перед импортом платформой используется копия source-раскладки, где
публичный ordinary `Ext/Form.xml` и каталог `Ext/Form/` удалены; для обычной
формы в платформенной раскладке остается только `Ext/Form.bin`.

## Status

Current release: `0.3.0`.

Current implementation status:

- read ordinary `Form.bin` containers;
- dump readable object-model `Form.xml`;
- extract `Module.bsl` and picture sidecars;
- validate `Form.xml` against the bundled `ordinary-form.xsd`;
- build ordinary `Form.bin` back from the named object XML package without
  template `Form.bin`;
- scan local EPF/ERF corpora without committing private processors or exports.

## Install

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e '.[dev]'
make test
```

## CLI

Dump an ordinary form binary:

```bash
onec-ordinary-forms dump-bin \
  --bin scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  --out scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

This writes:

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

Build `Form.bin` back from the object XML package:

```bash
onec-ordinary-forms build-bin \
  --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml \
  --out-bin scan-output/rebuilt/Form.bin
```

Before platform import, use a copy of the source tree where the public ordinary
`Ext/Form.xml` and `Ext/Form/` sidecar directory are removed. The platform
source layout for ordinary forms should contain `Ext/Form.bin`; managed forms
keep their native `Ext/Form.xml`.

Use `--asset-root` only when sidecars are not next to the XML as
`<Form.xml without suffix>/...`.

Writer behavior is intentionally conservative while the named ordinary-form
object model is being completed. The only public source contract is `Form.xml`
with named form objects plus sidecars; old base-stream rebuild paths and raw
diagnostic formats are not supported public workflows.

Diagnostic commands:

```bash
onec-ordinary-forms unpack-bin --bin Form.bin --out-dir scan-output/form-parts
onec-ordinary-forms pack-bin --parts-dir scan-output/form-parts --out-bin Form.bin
onec-ordinary-forms extract-elem-json --form scan-output/form-parts/Form.xml --out scan-output/form-parts/elem.json
onec-ordinary-forms scan-corpus --root "<private-processors-dir>" --out-json scan-output/corpus.json
```

`unpack-bin`, `pack-bin`, and `extract-elem-json` are diagnostics for format
research. They are not the target public source layout.

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
)
```

The default asset root is the XML path without the `.xml` suffix. For
`Forms/Form/Ext/Form.xml`, sidecars are read from `Forms/Form/Ext/Form/`.

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
- [Legacy elem-json input](docs/elem-json.md)
- [Research notes](docs/research-map.md)

## Fixture Policy

Do not commit private EPF/ERF files, CF/DT dumps, platform archives, license
files, OACS databases, generated platform exports, or customer metadata. The
`examples/fixtures/` directory is ignored except for `.gitkeep`.
