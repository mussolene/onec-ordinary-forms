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

Свойства и события контролов зависят от типа контрола и берутся из
платформенной палитры обычных элементов. В XML должны попадать только явно
заданные значения; дефолты платформы остаются неявными.

Схема еще расширяется, но направление фиксированное: публичный XML должен быть
похож на XML управляемой формы, быть именованным и понятным разработчику 1С.

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

## Status

Current release: `0.2.0`.

Current implementation status:

- read ordinary `Form.bin` containers;
- dump readable object-model `Form.xml`;
- extract `Module.bsl` and picture sidecars;
- validate `Form.xml` against bundled XSD schemas;
- scan local EPF/ERF corpora without committing private processors or exports.

The final writer is still under active development. The accepted architecture
is to build `Form.bin` from the named object XML plus `Module.bsl` and picture
sidecars. Raw public stream dumps and renamed low-level record trees are not
part of the target format.

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

Build `Form.bin` back from the object XML package:

```bash
onec-ordinary-forms build-bin \
  --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml \
  --out-bin scan-output/rebuilt/Form.bin
```

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
