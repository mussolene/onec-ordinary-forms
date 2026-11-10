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

## Интерфейс сохранения и обход дочерних объектов, 2026-10-03

Текущая таблица `QueryInterface` объекта находится по смещению
`frame+0x3774e80`; записи имеют размер `0x18`. Разбор байтов и ELF relocation
независимо подтвердил следующие связи:

| IID | Подобъект `CustomForm` | Таблица методов |
| --- | --- | --- |
| `d7f21e7f-c920-417b-948b-d884ecca5f40`, `ICustomForm` | `+0x588` | `frame+0x3774470` |
| `63e6dc41-7d8e-11d4-9423-008048da11f9`, передаваемый документу интерфейс | `+0x48` | `frame+0x3773a28` |
| `45b81b41-9540-11d4-9427-008048da11f9`, `IPersistableObject` | `+0x5b0` | `frame+0x3774880` |

У `IPersistableObject` первые три метода соответствуют
`QueryInterface`, `AddRef`, `Release`. Метод `+0x18` возвращает CLSID формы.
Метод `+0x20` через thunk `frame+0x28b7470` поправляет `this` на `-0x5b0`
и вызывает запись `frame+0x2866e50`. Метод `+0x28` через
`frame+0x28b7480` вызывает чтение `frame+0x28676f0`.
Это не деструкторы: классификация этих двух методов как деструкторов
в первоначальном отчете агента отвергнута текущим разбором вызовов.

| Уровень | Запись, смещение `frame` | Чтение, смещение `frame` | Подтвержденная операция |
| --- | --- | --- | --- |
| `CustomForm` | `0x2866e50` | `0x28676f0` | Запись версии `27`, общий слой формы, реквизиты и другие коллекции; прямые вызовы `CompositeID` и `TypeDomainPattern` serialize/deserialize |
| Общий слой `FormBase` | `0x3496530` | `0x3496860` | Вложенная версия `18`, заголовок, запрос сохранения связанного объекта, скалярные поля; чтение учитывает версии |
| Обход дочерних записей | `0x348dfe0` | `0x34a1390` | Запись количества выбранных записей, рекурсивный обход; чтение создает объект и восстанавливает его |
| Запись одного объекта | `0x34a10f0` | Через `0x34a1390` | Запрос того же `IPersistableObject`, вызов `+0x20` для записи или `+0x28` для чтения, общие данные записи с версией `14`, вложенные объекты |

В читателе ссылка на тип из потока передается в `frame+0x2a08a50`.
Он получает `core::current_process()` и вызывает `frame+0x2a0caa0`;
этот helper обращается к методу процесса `+0x20`, передает ссылку на тип,
запрашиваемый IID `22d22380-87e3-11d4-9425-008048da11f9`
и указатель результата. Имя этого IID пока не установлено.
Конкретные фабрики каждого типа контрола еще требуется связать с этой цепочкой.

Таким образом, подтвержден общий интерфейс сохранения и рекурсивный обход,
но не универсальная таблица именованных свойств. Восстановление свойств
конкретных классов остается отдельной работой. Наличие `getPropVal` и
`setPropVal` среди символов пока не доказывает их привязку к нужным
интерфейсам `CustomForm`. Полный запуск чтения, изменения и записи через
созданный библиотечный объект остается `UNKNOWN`.

Декомпиляция выполнена адресно, без полного автоматического анализа,
с сохраненным проектом Ghidra и базой образа `0x100000`. Адреса этой таблицы
являются смещениями модуля; к ним нужно добавлять базу при обращении в Ghidra.
Публичный XML и кодек в этой исследовательской итерации не менялись.
Подтверждение методов: `ev_01f57716ee154fc1a0468946d5c02769`.
Подтверждение обхода и исправление классификации:
`ev_14a3b7bcee2047a581f2d533f9e28e14`.

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


## Диспетчер живых значений, 8.5.1.1343

Свежая проверка регистраций ELF, записей перемещения, RTTI и таблиц виртуальных
методов в `core85.so` установила общую основу живых значений.
`ObjectValueCore::isEqual` сравнивает адреса объектов, `hashCode` использует
адрес объекта, а присваивание `Value` удерживает новый `IValue` и освобождает
прежний. Это подтверждает необходимость идентичности и владения, а не
подстановки локального номера или сравнения содержимого коллекций.

Через скрытую таблицу `FixedValueArrayType` найден конструктор
`0x6e5fc0` и путь преобразования `0x6e60e0`. Для каждого элемента он
сравнивает тип с GUID стандартного изменяемого Array. Только эта ветвь
рекурсивно вызывает тот же преобразователь. Остальные типы передаются
без такого преобразования. Отдельные вхождения одного изменяемого массива
копируются независимо. Это отличается от поверхностного копирования
FixedStructure и FixedMap, подтвержденного нативной партией.

Прямую рекурсию платформы нельзя воспроизводить без ограничений: продукт
должен безопасно отклонять цикл из изменяемых массивов или превышение
лимита при создании фиксированной копии. Обычные живые циклические
коллекции при этом остаются допустимыми.

SHA текущего `frame.so` совпал с SHA прежнего адресного исследования
CustomForm: `d696135e088a25bf1c88ba9179b4d5673cb4bb072e55f5fc062f4cd1b652aae7`.
Таким образом, эта цепочка фабрики подтверждена для текущей установленной
8.5.1.1343. Она по-прежнему не является универсальной таблицей свойств
всех контролов.

Доказательство: `ev_9675d251b3c9498d8c2300b078573770`. Адресные производные
отчеты сохранены в `scan-output/platform-value-dispatch-20261104/`;
временные копии библиотек и пакеты анализа удалены. Платформенные
библиотеки и машинные инструкции не входят в исходники продукта.

## Расширения формы: инвентарь и проверка отчета, 2026-11-08

Один проход по 7472 эталонным Form.bin прочитал только логический поток
формы, без извлечения или исполнения модулей. Найдено 7379 распознаваемых
записей расширения и 93 неизвестных внешних формата. Из распознанных записей
1706 используют три поддержанных GUID, 586 не имеют расширения, а 5087
относятся к 24 неподдержанным GUID. Это инвентарь дискриминаторов, а не
счетчик первых ошибок полноценного parser: последний дал 5065 отказов
на неизвестном расширении. Эти показатели имеют разный смысл.

Крупнейший кластер имеет GUID `343b4e00-29f0-11d6-a3c7-0050bae0a776`:
2790 форм. Из них 2768 содержат шесть полей настроек с ведущим значением
2, а 22 содержат пять полей с ведущим значением 1. Корреляция с каталогом
Reports сама по себе не доказывает тип объекта. Отдельный собственный
ExternalReport на 8.5.1.1343 подтвердил этот GUID нативно на 12 обычных
формах с 24 контролами. Расширение отчета пока не реализовано продуктом.

Нативные проверки установили следующие соответствия:

- `RestoreValuesOnOpen`: `payload[3][2][4]`, булево значение меняется
  с 1 на 0. Исходное значение в собственной автоматически созданной форме
  равно true.
- `ReportResult`: `payload[3][2][5]` содержит ID поля табличного документа.
  Назначение собственного поля с ID 3 записывает 3, сброс через Undefined
  записывает 4294967295. Getter у неоткрытой формы возвращал Undefined
  даже после назначения, поэтому живое тождество этой ссылки не доказано.
- Флажок Конфигуратора "Сохранять значения": `payload[3][2][1]`,
  переключение false в true меняет 0 на 1. Сохранение формы также меняло
  GUID собственных команд и счетчик сериализации; равенство остальных
  потоков не утверждается.

Редактор "Сохраняемые значения" показал собственные реквизиты только после
создания связанных с ними полей. Строгие выгрузки с одним и двумя выбранными
реквизитами подтверждены. Текущая запись содержит имя реквизита, UUID
контекста и разрешенную ссылку с ID 2 или 3, совпавшим с ID объявленного
реквизита. В этом собственном ERF UUID контекста равен
`ExternalReport.InternalInfo.ContainedObject.ObjectId`; он отличается от
UUID метаданных формы, TypeId и ValueId. Нельзя автоматически считать его
идентичностью прикладного типа.

Двенадцать образцов старых форм содержали 33 ссылки другого вида, без
разрешенного ID. Семантика перехода между этими видами и роль первой
метки значения пока требуют отдельной нативной проверки. Их нельзя
подменять CompositeId или выводить в публичный XML как безымянный поток.
Следующий кодек должен описать именованную ссылку на сохраняемое значение
и общий блок настроек отчета и обработки.

Свидетельства: инвентарь `ev_1740470af4c24eab8064a81be63b23e5`,
старые ссылки `ev_5444bb5637b54ed995067bce2675a78c`, нативный отчет
`ev_d4e3c8b538234b73890103faefb48627`, связь результата
`ev_d05e1987013a4bc0bfce2bf7fd9fbce8`, редактор сохраняемых значений
`ev_85caa0f2f6344d10bffaf55c574de3fe`. Собственные GUI, оконный менеджер
и временные сеансы завершены. Строгие образцы ERF/BIN временно сохранены
в игнорируемой папке `scan-output/report-saved-values-wm-native-20261107/`
для проверки будущего кодека; они не входят в исходники продукта.


## Полная грамматика сохраняемых значений, 2026-11-08

Следующий структурный проход прочитал все 7472 логических потока формы без
извлечения модулей и без ошибок чтения. Он проверял запись расширения без
ограничения на распознанные предыдущим инвентарем внешние форматы. Поэтому
найдены 2804 записи расширения отчета и 557 расширения обработки, а не только
2790 и 551 из ранее распознанного подмножества. Это уточнение охвата инвентаря,
а не рост числа полностью поддержанных форм.

Все 4121 сохраняемое значение используют одну метку типа
`91f722a0-4cc1-11d6-a3c9-0050bae0a776`. В 3926 записях нет разрешенного ID,
в 195 есть одно числовое указание реквизита. Все 195 указаний совпали ровно
с одним объявленным реквизитом через существующий decode_attributes. Во всех
4121 записях имя непустое и не содержит точки. Совпадение имени с реквизитом
не было сгруппировано отдельно для первого варианта, поэтому такого вывода
этот проход не доказывает. Неизвестных форм записей и меток типа не найдено.
Свидетельство: `ev_b66f420a0d94471e88ec571a3395bea0`.

Самостоятельная нативная десериализация обоих видов вернула
`НеизвестныйОбъект`; сериализация типа сохранила указанный GUID. Это
подтверждает оболочку значения и сохранение содержимого, но не проверяет
семантику ссылки: неизвестный объект не является валидатором ее полей.
Свидетельство: `ev_10ba6c5e25a94851a7419b0a420c9b30`. Семантическое
соответствие собственных выбранных реквизитов установлено отдельно через
редактор Конфигуратора, как описано выше.

В таблицах событий обоих расширений найдено 281 событие с ID 80000 или 80001.
Их имена для обработки подтверждены нативным назначением и затем чтением
собранной формы без SetAction: BeforeSaveValues и AfterRestoreValues.
Поддержка обработки реализована общим кодеком событий. Первоначальная проба
отчета отказала, поскольку обработчики находились в модуле вспомогательной
обработки, а не целевого отчета; каталог для снимков также не был создан.
Это были ошибки стенда, а не лицензии.

Исправленный пакет собрал собственный Report с обеими процедурами в модуле
его формы и заранее создал каталог снимков. На 12 формах все 24 назначения
и 24 чтения действий прошли успешно. Полное структурное сравнение каждой
пары снимков подтвердило изменения только таблицы событий и счетчика
сериализации. BeforeSaveValues имеет ID 80000, AfterRestoreValues имеет
ID 80001; tag совпадает с обработкой. Два сохраняемых значения и остальные
настройки остались прежними. Это нативное подтверждение общей схемы событий
для двух расширений. Текущая реализация общего кодека описана ниже.
Свидетельство: `ev_12830edb94dd461795a8763dc85f48d0`. Временная база,
скомпилированные ERF/EPF, копии BIN и помощники удалены; собственные именованные
исходники и проверочные снимки сохранены в игнорируемом
`scan-output/report-events-owned-module-native-20261108/` для кодека Report.

## Именованные настройки Report и DataProcessor, 2026-11-10

Реализованы общий SavedValueBinding (UUID источника, имя, необязательный
AttributeRef), SaveValues, RestoreValuesOnOpen и отдельный ReportResult.
События имеют общий список допустимых расширений, а не копии дескрипторов.
Reader проверяет метку и форму записи сохраняемого значения; writer собирает
ее из именованных полей. Старый Report версии 1 отличается отсутствием
последнего ReportResult, а не RestoreValuesOnOpen. Это подтверждено 12
нативными случаями: `ev_24174298ced549e7a303af21419e615c`.

Собственные новые отчет и обработка собраны из XML и прошли строгую выгрузку
Designer. Результаты равенства XML и модулей сохранены в
`scan-output/saved-settings-cold-native-20261108/strict-check.json`.
Немедленный цикл закрытия и открытия не подтвердил восстановление значений.
Две искусственно заданные ссылки без AttributeRef были удалены платформой;
это не доказывает, что все такие ссылки недействительны. Их контекст нельзя
выводить из одного имени или GUID владельца.

Следующий опыт использует только три выбранных реквизита каждого объекта
и отдельные такты ожидания открытия, записи, закрытия и повторного чтения.
10 ноября три потока собрались без диагностик, но первая упаковка Designer
завершилась ошибкой лицензии: `ev_c998b1f5864940ee92996d9e6f42da5e`.
После восстановления лицензирования все упаковки, обе строгие выгрузки
и Enterprise завершились с кодом 0. Во всех шести снимках сохранились оба
флажка, три ссылки на выбранные реквизиты и два обработчика; ReportResult
сохранил ссылку 150. Повторно прочитанные значения пустые, обработчики
не вызывались, runtime-значение РезультатОтчета осталось Неопределено.
Этот опыт не выполнял штатную команду сохранения настроек. Справка платформы
требует существующую сохраненную настройку с признаком "Использовать при
открытии"; следовательно, пустое восстановление в таком стенде не является
доказательством ошибки кодека. Свидетельство исправленного критерия:
`ev_0e5704185d1d46119add11a42a22c73e`. Выполнение штатной команды остается
отдельной незавершенной проверкой.
Свидетельства нативного запуска и чтения снимков:
`ev_5536128a8a57472eb9c1e14a502c8af2`,
`ev_8baff6970cb94a5ba4c73bf7c85afb3e`.
Актуальные собственные исходники и журнал сохранены в
`scan-output/saved-settings-ui-lifecycle-20261110/`; временная база удалена.

Полный проход текущего reader по 7472 потокам форм без модулей и контекста
метаданных дал 7470 ошибок, 2 частичных результата и 0 полных. Поэтому новая
поддержка настроек не означает готовность корпуса. Остальные расширения,
форматы элементов и свойства требуют дальнейшего закрытия.
Свидетельство: `ev_b8c4f92a499c4b2da5959557b9b6938a`.

## Общие версии записей корпуса, 2026-11-10

Повторный проход на коммите badaf5b прочитал все 7472 логических потока формы
без чтения содержимого логических модулей. Ошибок извлечения и разбора нет.
Результаты reader совпали с предыдущим аудитом: 7470 отказов, 2 частичных
результата, 0 полных. Этот проход дополнительно группирует структурные пути
отказов и версии вложенных записей. Временный помощник удаляется после опыта;
безопасный агрегат сохранен в игнорируемом
`scan-output/full-corpus-guards-20261110/aggregate.json`.
Свидетельство: `ev_c04152a5548d470683a59976d1e22bc3`.

Три повторяющиеся причины дают 2228 ранних отказов:

| Запись | Ожидается полей | Найдено полей | Число форм |
| --- | --- | --- | --- |
| Страница панели | 12 | 10 | 1030 |
| Страница панели | 12 | 11 | 243 |
| Свойства CommandBar | 14 | 12 | 955 |

В последней группе наблюдается версия свойств 8; текущий writer создает
версию 9. Вложенный общий блок имеет версию 10 и 13 полей; текущий общий
reader требует версию 19 и 21 поле. Независимый поиск сходной структуры
в известных по GUID контролах обнаружил следующие кандидаты общего блока:

| Версия / число полей | Число записей |
| --- | --- |
| 10 / 13 | 74231 |
| 15 / 17 | 1215 |
| 16 / 18 | 49458 |
| 19 / 21 | 9960 |
| 21 / 21 | 4591 |

Это структурный инвентарь, а не доказательство соответствия каждого поля.
Например, вариант 10 / 13 встречается у 20 видов контролов, вариант 16 / 18
у 18. Внутренний номер записи нельзя автоматически приравнивать к номеру
релиза платформы. Пути диагностики могут относиться к уже нормализованному
внутреннему представлению reader.

Следующее общее исправление должно читать именованные поля по подтвержденной
версии записи и заполнять только доказанные значения отсутствующих полей.
Для корневого владельца в коде уже есть переход общего блока 10 в 19;
он не доказывает автоматически все остальные варианты контролов.
Writer по-прежнему собирает актуальную запись из объектной модели.
Отдельное сохранение старого потока, скрытые поля и возврат к бинарному
шаблону не требуются. Неизвестное сохраняемое поле целевой платформы остается
пробелом модели, который нельзя скрыть успешной диагностикой чтения.

Первая проверка в Designer должна сравнить собственный нативный эталон
с собранной формой, затем пересохранить отдельную копию и сопоставить
свойства и версии записей. Попытка этой проверки 10 ноября остановилась
до открытия редактора из-за отсутствия лицензии. Свойства интерфейса
и преобразование при сохранении в этом опыте остаются UNKNOWN.
Свидетельство: `ev_86d6601137214351a49d30acab295386`.
