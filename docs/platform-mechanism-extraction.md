# Platform Mechanism Extraction

This document records the current extraction boundary for the ordinary-form
platform mechanism. The raw Ghidra output, platform binaries, and decompiled
bodies stay in ignored `work/` or `scan-output/` directories. Tracked files
contain only the reproducible extraction tool and sanitized mechanism map.

## Extraction Command

```bash
tools/ghidra_decompile_form_functions.sh \
  work/platform85-libs/dsgnfrm.so \
  work/ghidra-platform-mechanism-current
```

Historical note: the old post-processor for mechanism JSON/Markdown was removed
with the previous implementation. Use git history if that research helper is
needed again; current product code should stay native C++.

## Platform Entry Points

Addresses below use Ghidra image base `0x100000`, not runtime module offsets.
For the current `dsgnfrm.so`, SHA-256 is
`416f3d4317a9c18949a4527f1ccc817dc8bf067f87a2414f99a469ca32574523`.
The extractor records `imageBase` and `imageOffset` to make this distinction
explicit.

## Проверка идентичности объектов, 2026-10-03

Старые пробы ошибочно называли `FormDocument` класс с CLSID
`0ec7b148-cdf9-451c-9821-022b95c0fa23` и интерфейсом
`364f0971-70a0-47dd-af6d-b094a7f63afb`. Создание этих объектов возвращает
успех, но текущая ELF relocation связывает их таблицу методов
`dsgnfrm+0x1f6720` с RTTI `core::SCOM_Object<DeferredHelpProvider>`.
Свежая декомпиляция показывает операции со списком модулей и справкой.
Успех этой пробы не доказывает создание или сохранение документа формы.

Настоящий `core::SCOM_Object<FormDocument>` имеет другую RTTI-запись
`dsgnfrm+0x1fa5d0` и основную таблицу методов `dsgnfrm+0x1fa2d8`.
Назначение интерфейса нужно подтверждать вместе с типом объекта и его
таблицей методов. Одного имени константы GUID в исследовательском коде
недостаточно.

Для этого же бинарника установлена цепочка:

| Сущность | Подтвержденная связь |
| --- | --- |
| CLSID `FormDocument` | `a3f2959b-9763-43d6-909d-1a37c17d3d48`; регистрация указывает callback `dsgnfrm+0x1bdb50` |
| Создание без агрегации | Callback вызывает `dsgnfrm+0x1bdba0`, выделяет объект размером `0x260` и устанавливает таблицы методов `SCOM_Object<FormDocument>` |
| IID `IFormDocument` | `da8583a2-a3dd-42fe-8995-1ff64eaa5905`; `QueryInterface` и RTTI согласованно указывают на подобъект `this+0x138` |
| `IDocument` / `IPersistableDocument` | Общий подобъект `this+0x128`; GUID `425ee300-9dd3-11d4-84ae-008048da06df` и `39d1961c-1881-4e3f-8453-16ad0a648b05` возвращают этот указатель, их индивидуальные имена пока не разведены |

Все 15 наблюдавшихся регистраций используют общую фабрику
`ATL::CComObjectNoLock<ATL::CComClassFactory>`. Ее `CreateInstance` по адресу
`dsgnfrm+0x167e40` вызывает индивидуальный callback из поля `factory+0x38`.
Поэтому тип общей фабрики сам по себе тоже не определяет создаваемый класс.
Исправленные пробы выводят callback, RTTI и проверяют точный тип результата.

| Эксперимент | Результат и граница доказательства |
| --- | --- |
| Создание внутри Designer | Оба `CreateInstance` возвращают `hr=0`; RTTI основного и запрошенного интерфейсов соответствует `SCOM_Object<FormDocument>`. Синтетическая обработка выгружена, код Designer `0` |
| Создание в отдельном процессе | Создание и RTTI подтверждены. Последующая инициализация с подставными сервисами падает: `SIGSEGV`, `dsgnfrm+0x167322`, адрес обращения `0x209`; дочерний процесс `139`, оболочка `1` |
| Загрузка, изменение, сохранение через созданный объект | `UNKNOWN`: эти методы не вызывались. Выгрузка обработки Designer не доказывает их работоспособность у отдельно созданного объекта |

Повторная декомпиляция показывает, что `IFormDocument` содержит операции
над ссылкой на другой объект, а не готовую пару функций чтения/записи
`Form.bin`. Для текущего бинарника установлены следующие границы:

| Ghidra-адрес | Наблюдаемая операция |
| --- | --- |
| `0x2b5ff0 -> 0x2b5c70` | Метод `IFormDocument` передает указатель в поле полного объекта `+0x160`, управляет ссылками и связанными объектами |
| `0x2b6030` | Возвращает ссылку из того же поля через скрытый параметр результата C++ и увеличивает счетчик ссылок; нельзя вызывать как обычную C-функцию с одним аргументом |
| `0x295230 -> 0x295dd0` | Перечисляет связанные объекты; прямого чтения потока нет |
| `0x295260 -> 0x296220` | Работает с сервисами интерфейса, сообщением и результатом диалога; прямого чтения потока нет |
| `0x295270 -> 0x298950` | Обходит связанные представления и вызывает сервис интерфейса |
| `0x2955b0 -> 0x2b6900` | Меняет флаг и распространяет его на связанные объекты и представления |

Это локализует следующий вопрос: кто создает и сериализует объект,
передаваемый в `IFormDocument`, и какие именно дескрипторы свойств он
использует. Расширять подставное окружение до копии Designer или вызывать
неустановленные сигнатуры ради успешного кода завершения не требуется.
Полный цикл загрузки, изменения свойства и сохранения через библиотеки
остается неподтвержденным.

Дополнительная трассировка источника данных документа установила следующую
цепочку в том же `dsgnfrm.so`:

| Узел | Проверенное наблюдение |
| --- | --- |
| `0x273470` | Создает объект через `0x288360`, сохраняет его в поле владельца `+0x170`, запрашивает интерфейс и передает его созданному FormDocument через основной vtable slot `+0x38` |
| `0x288360 -> 0x2948b0` | Обращается к `core::current_process()` и общей фабрике; CLSID `d5ca80b3-5363-41a9-b71b-99bd9b17f35a`, первоначальный IID `d7f21e7f-c920-417b-948b-d884ecca5f40` |
| Интерфейс переданного объекта | IID `63e6dc41-7d8e-11d4-9423-008048da11f9`; конкретное имя класса и его сериализатор пока не установлены |

Полная 16-байтовая запись CLSID найдена также в `mngui.so`, первое
совпадение по файловому смещению `0x44cce8`. Повторение GUID не доказывает
назначение таблицы или наличие схемы. Текущий список импортов этой библиотеки
показывает `IInPersistenceStorage`, `IOutPersistenceStorage`,
`GenericValue::serialize/deserialize` и функции хранения `V8Picture`.
Это свидетельство общего слоя сохранения типизированных значений;
его связь с данным объектом формы еще не подтверждена.

Полный анализ `mngui.so` в Ghidra остановлен после 12 минут без результата
xref JSON. Этот запуск не доказывает ни существование, ни отсутствие
универсального сериализатора. Следующая проверка должна получить адресные
ссылки на полный GUID и конкретный callback создания, затем RTTI и методы
чтения/записи. Повторять полный импорт ради каждого метода не требуется.
Гипотезы о DFM, VCL, Windows-схеме или едином XSD остаются неподтвержденными.
Текущее свидетельство и ограничение: `ev_8eef3dedfea245a4ac942c9f9381c3d8`.

Доказательство исправления классификации и проверки двух ОС:
`ev_c5010733fc4c4678877d5ce9613cd9eb`; итоговая повторная проверка создания
настоящего документа: `ev_4c6d7d21265a42748af50941a68c4f71`;
карта интерфейсов и проверка адресов: `ev_83670f1e9a2a4cec907cbb63b2e2cf8a`.
Сырые тела функций и журналы остаются в игнорируемых результатах исследования.

## Корневой объект CustomForm, текущая проверка 2026-10-03

Адресная проба регистрации внутри Designer уточнила владельца CLSID
`d5ca80b3-5363-41a9-b71b-99bd9b17f35a`: его регистрирует `frame.so`,
а не `mngui.so`. Совпадения GUID в `mngui.so` остаются ссылками,
не доказательством владения фабрикой.

Для исследованного `frame.so` SHA-256:
`d696135e088a25bf1c88ba9179b4d5673cb4bb072e55f5fc062f4cd1b652aae7`.
Следующие адреса являются смещениями от базы модуля во время исполнения,
не файловыми смещениями и не адресами Ghidra:

| Сущность | Проверенный результат |
| --- | --- |
| Общий `CreateInstance` фабрики | `frame+0x25b86c0` |
| Индивидуальный callback создания | `frame+0x28c1700` |
| Тип созданного объекта | `core::SCOM_Object<CustomForm>`, RTTI `N4core11SCOM_ObjectI10CustomFormEE` |
| RTTI объекта | `frame+0x3774e60`, вычислено по общей базе модуля из текущего журнала |
| Первоначальный IID | `d7f21e7f-c920-417b-948b-d884ecca5f40`; создание с этим IID и с `IUnknown` вернуло `hr=0` и одинаковое имя RTTI |

Независимый адресный разбор ELF показывает две ветви callback:
без внешнего объекта агрегации он переходит в `frame+0x28c1750`,
выделяет `0xff8` байт, вызывает конструктор `frame+0x2850410`
и устанавливает основную таблицу методов `frame+0x3772cd0`.
При ненулевом внешнем объекте используется `frame+0x28c1a30`,
выделение `0x1010` байт и оболочка `ATL::CComAggObject<CustomForm>`.
Эти ветви нельзя смешивать при восстановлении сигнатур и интерфейсов.
Доказательство адресного разбора: `ev_1d56cb8f96ae4420ad79b23c309e0023`.

Проба только регистрации завершилась с кодом `0` и строгой выгрузкой
синтетической обработки. Проба создания завершилась с кодом `139` после
вывода идентичности, во время освобождения объектов или последующей
инициализации; точное место сбоя пока не установлено. Методы чтения и
записи не вызывались. Полный цикл сериализации остается `PARTIAL`.
Проверка `type_match` в временной копии старой пробы сравнивала тип с
`FormDocument`, поэтому ее отрицательный результат неприменим к
`CustomForm` и не используется как доказательство.

Предыдущий незавершенный проект Ghidra имеет пустой `OOF.gpr` и не содержит
сохраненного анализа. Библиотека `mngui.so` доступна; отсутствие ее в
обычном выводе `rg --files` объясняется игнорированием каталога `work/`.
Следующий адресный поиск должен идти от callback и RTTI `CustomForm`
в `frame.so`, затем к интерфейсам сохранения. Повторный широкий анализ
`mngui.so` не требуется.

Регистрация: `ev_91aebad8aea14b75849c03472a7e1335`.
Идентичность и сбой пробы создания:
`ev_622595b3ce2b47d8b5e4295d0e8bd2c7`.

## Граница переносимости

Текущие `liboof` и `oof` собираются без библиотек 1С. На 2026-10-03 свежие
Release-сборки и CTest прошли на macOS arm64 и Debian 12 amd64, по 11/11.
Синтетический XML и модуль с кириллицей дали одинаковый `Form.bin` и
побайтово одинаковые результаты обратной выгрузки на обеих ОС. Проверка
Windows остается `UNKNOWN`, среды компиляции и запуска нет.

Тот же созданный `Form.bin` помещен целиком в исходники синтетической
обработки и собран штатной командой Designer
`/LoadExternalDataProcessorOrReportFromFiles`, код `0`. Последующая строгая
выгрузка через `tools/platform_validate_epf.sh` также завершилась с кодом
`0`. После нее `oof dump` восстановил побайтово тот же публичный XML;
в модуле изменились только окончания строк `LF -> CRLF`. Проверен один
синтетический `Button` с кириллицей, координатами, флагами и обработчиком
`Click`. Доказательство: `ev_4471669be664425a8c8348f044b49326`.

Это доказательство переносимости реализованного подмножества, не полноты
формата. Неподдержанные объекты по-прежнему отклоняются. Продуктовый путь
остается `Form.bin -> OrdinaryForm -> Form.xml + Module.bsl` и обратно;
платформенные библиотеки, Ghidra и Designer служат для исследования и
независимой проверки. Визуальный редактор можно строить над этой же моделью
после расширения чтения и записи, он не устраняет пробелы сериализации.

## Уточнение прежних кандидатов

The former persistence candidates `00255f70` and `00256510` serialize and
restore the designer setting `FormDesigner/UseWizardForInsertControl` using
list streams. Fresh decompilation on 2026-10-03 confirmed the settings access
and a global GUID/flag collection. They are not evidence of a `Form.bin`
document reader or writer and have been removed from the default target set.

The following functions concern control transfer formats. Their existence
does not establish a complete document persistence API:
- `002709e0`, `00270da0`, `00270fe0`: ordinary-control triplet entries. Each
  calls `wbase::cf_form_controls8`,
  `wbase::cf_form_controls_position8`, and
  `wbase::cf_form_controls_info8`.
- `002c9430`: ordinary-control info entry. It calls
  `wbase::cf_form_controls_info8` without the full triplet.

The xref surface also shows data-table references for the same three
`cf_form_controls*` factories. That means the native implementation should
model them as a descriptor registry, not as ad hoc per-form fallbacks.

## Form Object Surface

The `dsgnfrm.so` string surface around the ordinary-form mechanism includes
these form/designer/runtime objects:

- transfer/enumeration: `FormDataObject`, `FormFormatEnumerator`;
- designer document/view: `FormDesDoc`, `FormDesView`,
  `FormDesDocFactory`, `CustomFormLoader`, `ControlSite`;
- runtime document/view: `FormDocument`, `FormDocumentView`,
  `FormDocumentFactory`, `FormDocumentMoxelFactory`;
- services/properties/undo: `FormDesignerService`, `FormProperties`,
  `FormDocPropertiesWrapper`, `FormUndoManager`;
- support dialogs/sites: `FormDesignerSite`, `TestForm`, `ControlSelDlg`,
  `FieldsDialog`, `PropertiesEditDialog`, `GridParametersDialog`.

These names describe the platform's internal responsibilities. They are not
a requirement to reproduce the Designer object hierarchy in the product.
The product only needs the named form model and its verified codec.

## Transfer Formats

- `cf_form_controls8`: `0x2500`.
- `cf_form_controls_position8`: `0x5500`.
- `cf_form_controls_info8`: `0x9d00`.
- Common transfer count prefix: 4 bytes.
- `cf_form_controls_info8` transfer record: 16 bytes.
- `cf_form_controls_position8` transfer record: 32 bytes.
- Format entry record: 40 bytes.

The proven GetData boundary is not symmetric for all three formats:

- `cf_form_controls_position8` is count plus 32-byte records.
- `cf_form_controls_info8` is count plus 16-byte records copied from the info
  linked list.
- `cf_form_controls8` is returned as an existing raw payload file/HGLOBAL in
  `FUN_00270da0`; do not model it as a fixed 40-byte record list at that
  boundary. Native tests may use a diagnostic fixture chunk to exercise the
  transfer envelope, but that fixture is not a platform control record shape.

## Core Value Surface

The relevant platform libraries import these serializers/value types around
the ordinary-form mechanism:

- stream layer: `ListInStream`, `ListOutStream`;
- typed metadata/value layer: `TypeDomainPattern`, `CompositeID`,
  `GenericValue`, `LocalWString`, `FormattedString`;
- UI value layer: `Color`, `Font`, `V8Border`, `V8Picture`;
- scalar support: `ShortCut`, `Date`, `Numeric`;
- persistence support: `IInPersistenceStorage`, `IOutPersistenceStorage`.

These are candidates for targeted extraction when a supported public property
needs them. Porting every platform service or serializer before extending the
form model is not a prerequisite.

`LocalWString`/`FormattedString` localization support must be extracted from
the platform serializers themselves. The local Linux library set referenced
these symbols from `dsgnfrm.so`, `mngbase.so`, and `mngui.so`, but only proved
the platform object names and call surface. The platform container
`ghcr.io/mussolene/1c-developer:8.5.1.1343` provides the serializer bodies in
`core85.so`; selected libraries were copied only to ignored
`work/platform85-container-libs/` for inspection.

Container `core85.so` definitions:

- `core::FormattedString::serialize(core::ListOutStream&) const`: `0x5347b0`.
- `core::FormattedString::deserialize(core::ListInStream&)`: `0x534800`.
- `core::LocalWString::serialize(core::ListOutStream&) const`: `0x57c520`.
- `core::LocalWString::deserialize(core::ListInStream&)`: `0x57c610`.
- `core::LocalWString::addItem(...)`: `0x579d40`.

The sanitized platform-derived shape is:

- `FormattedString` writes a list/version marker `1`, then delegates to
  `LocalWString`, then writes the formatting flag stored at object offset
  `0x40`.
- `LocalWString` writes a list/version marker `1`, then an item count, then
  per localized item writes two `BasicString<char16_t>` values through the
  list stream. The first item is stored inline on the object and additional
  items use vector entries with stride `0x30`; each entry contains the two
  strings at offsets `0x0` and `0x18`.
- `LocalWString::deserialize` reads the same version/count pair, resets the
  object, allocates additional vector entries when count is greater than one,
  and reads the two strings for each item through the platform list-stream
  string reader.

Do not port an old localized-text helper shape as if it were platform evidence.
Native `platform_value` work for localized values must be derived from
`core85.so` serializer evidence or a live platform oracle.

The local `.so` resource extraction found no embedded XML/XSD fragments in
`dsgnfrm.so`, `frmcore.so`, `mngui.so`, or `mngbase.so`. The model catalog from
strings/imports found 12 metadata object candidates and 25 type tree
candidates in the same local library set. If a fuller platform root with
`.res`/`.hbk` resources is available, rerun the extractor against that root.

## Native Engine Mapping

- `list_stream`: parse and write platform bracket/list stream syntax.
- `platform_value`: implement `CompositeID`, `TypeDomainPattern`,
  `core::LocalWString`, `core::FormattedString`, generic scalar values,
  color/font/border/picture records from platform serializers.
- `form_bin`: split and join the ordinary `Form.bin` section container.
- `ordinary_controls`: implement the descriptor registry for
  `cf_form_controls8`, `cf_form_controls_position8`, and
  `cf_form_controls_info8`.
- `object_model_bridge`: map descriptor records to public `Form.xml` concepts
  only after the internal platform graph is complete enough.

## Direct Port Order

1. Copy the current platform constants and object surfaces into native code.
2. Implement `ListInStream`/`ListOutStream` behavior first.
3. Implement core typed values exactly as observed by platform serializers.
4. Implement `FormDataObject` and `FormFormatEnumerator` semantics for the
   three `cf_form_controls*` transfer formats.
5. Implement `FormDesDoc/FormDesView` and `FormDocument/FormDocumentView`
   record graph structures only where persistence evidence requires them.
