# Ordinary Form Pattern Audit

## Повторный разбор истории от 2026-09-24

Вердикт: последняя итерация частично повторила прежние исследования. Коммит
`f1d3812` добавил инструменты проверки и протокол, но не расширил продуктовый
кодек. Обещать готовность утилиты на основании этого пилота нельзя.

Критерии разбора: AC1, восстановить попытки по Git и OACS; AC2, отделить
новизну пилота от повторов; AC3, проверить доступную историю задач и явно
указать предел доступа. Код платформы в этой итерации повторно не запускался.
Исторические PASS ниже относятся к своим коммитам, а не к текущему продукту.

### Последовательность попыток

| Период | Реальная работа и доказательство | Почему это не завершило продукт |
| --- | --- | --- |
| Май | Сохранение serialization profiles, затем их удаление: `6294dd3`, `75132ca`; анализ архитектурного цикла записан уже 31 мая | Обратимость за счет исходных записей расходилась с контрактом именованной редактируемой модели |
| 1 июня | `53abed1`: геометрия добавленной Button, значение `base[17]=2`, проверка этого значения в тесте; OACS `mem_c7af0160cc9147d0a59ce84cae844fb5` | Это конкретные факты старого writer, не полная семантика всех свойств; прежние предположения о Enabled позднее исправлялись |
| 4-5 июня | Native writer без исходного Form.bin и строгая проверка Designer: `7d89fd7`, `99dee79`; общий платформенный oracle: `31630d0` | Принятие отдельных форм не означало полноту сериализации всех объектов, событий и значений |
| 23 июня | `1a5dcb8`: матрица изолированных изменений Button, проверка DefaultButton, descriptor slot 5 | Матрица исследовала runtime-сериализацию. Ее нельзя автоматически считать сохранением документа Designer |
| 16 июля | `14a49a2`: композиция новой модели и Form.bin; OACS `mem_d8d3954c6a944517a7d84903e9c11413` уже описывает Caption, Enabled, Position/Visible, Click и модуль одной Button | Новый кодек поддерживал узкий канонический случай, а не все накопленные ранее варианты |
| 5 сентября | `0978668`: аудит с 16/16 внутренних тестов, 0/59 реальных входов и успешным синтетическим примером одной Button | Уже было известно, что нужны реальные независимые формы и расширение кодека; эти результаты не подтверждали готовность |
| 24 сентября | `d94e622`: удаление старой реализации и зависимых инструментов; `f1d3812`: новый пилот Caption/Enabled | Уборка не добавила кодеки. Последующий пилот снова остановился на ограничении reader |

### Что именно оказалось повтором

1. Исследование Button и метод одиночных изменений уже существовали. В
   `1a5dcb8` функция `write_button_matrix_case` создает кнопку, изменяет одно
   свойство, сохраняет runtime-представление и удаляет кнопку. Пилот сентября
   отличается сохранением EPF и повторным открытием в новой сессии Designer,
   но сам принцип и выбранный класс элементов не новые.
2. Caption и Enabled уже входят в июльский продуктовый сценарий. Его
   доказательства: `ev_6d5424e91bbd4700b92f5f61ef954b1a`,
   `ev_180ae26d4c8f444d9a08bf5586a41370`,
   `ev_80616a88fb2841c19bedd75eb32efe2b`. В сентябре проверен другой,
   независимо созданный вход, но поддержка этих свойств заново не реализована.
3. Отличие Button base record `1 -> 2` в позиции 17 уже встречалось:
   `53abed1` явно устанавливал `base[17]="2"` и проверял это тестом. Старое имя
   `baseStyleState` является техническим обозначением, а не доказанным
   публичным свойством платформы. Значение и позиция известны из истории;
   их полная семантика и допустимые варианты требуют отдельного доказательства.
   Просто разрешить любое значение или вернуть это имя в XML было бы неверно.
4. Позиция `$/1/10` уже названа счетчиком сериализации в `14a49a2` и текущем
   `form_stream.cpp`. Последний эксперимент наблюдал ее изменение, но не
   установил смысл независимо. Формулировка "неизвестная позиция" без учета
   существующей реализации скрывала уже имеющуюся гипотезу кодека.
5. Диагноз "чистая архитектура, но только одна кнопка" уже содержался в
   сентябрьском аудите. Новая уборка и повторение этого диагноза не являются
   продвижением по поддержке форм.

### Что последняя итерация действительно добавила

Проверено независимое создание синтетической обычной формы и чтение Caption
и Enabled после закрытия исходной сессии и открытия сохраненной EPF в новой
сессии Designer. Четыре варианта прошли строгую выгрузку платформой, но все
четыре отклонены `oof dump` с OOF1114 на Button base record. Добавлены
проверяющий сценарий и сравнение логических потоков, а не автоматический
генератор вариантов и не новый сериализатор. Доказательство последнего
пилота: `ev_7642eaec0ade4ee3bdbf147b2201cfc5`.

Причина повторения в работе агента: перед пилотом были прочитаны отдельные
записи, но не выполнено сопоставление каждого предполагаемого открытия с
историческим кодом и доказательствами. При уборке удалены также
`run_control_oracle_batch.sh`, `platform_oracle_execute.sh` и
`generate_control_oracle_scripts.sh`, без переноса всей их исследовательской
функциональности. Исходники доступны в Git; знания не потеряны безвозвратно,
но их перестали использовать в рабочем процессе. Запрет старого продуктового
пути не требовал повторного открытия уже установленных фактов.

### Дальнейшая работа и границы вывода

Следующая продуктовая задача должна начинаться с сопоставления старых
доказательств позиции 17 с текущей моделью и кодеком, затем дать полный цикл
для уже созданной независимой формы: чтение, изменение именованного свойства,
сборка без исходного Form.bin, строгая выгрузка Designer и повторное чтение.
Результат измеряется новым поддержанным входом и проверенным изменением, а не
числом инструментов, очищенных строк или повторных аудитов. Старый writer
и сырые свойства в публичный XML не возвращаются.

Три агента GPT-6 Luna medium разобрали Git, OACS и доступные задачи. Из
переписки повторно доступна задача "Аудит и исправление проекта" с аудитом
5 сентября. Списки последних и архивных задач не открыли старые задачи этого
репозитория; доступ Computer Use к Codex запрещен инструментом. Полный аудит
всех чатов не выполнен. Упоминание задачи "Оценить перенос исходников форм"
в разделе ниже является результатом прежнего аудита, а не новым чтением.

Доказательство сверки: `ev_01d5b958ad3f41bdab5578678fc982a5`; checkpoint
`trace_e6f87fb0090346ba88cd98c93daa7a99`.

Статус исторического разбора: AC1 PASS, AC2 PASS, AC3 PARTIAL по полноте
переписок. Статус утилиты остается PARTIAL. Новое переписывание или сброс
ветки не обоснованы найденными фактами.

## Аудит текущей ветки от 2026-09-05

Вердикт: сохранить текущую ветку и доводить `liboof`/`oof`.
Сброс на `main`, откат к релизу или еще одно переписывание не устраняют
отсутствующие кодеки. Небольшими правками можно исправить проверку и описание
проекта; поддержку произвольных обычных форм такими правками получить нельзя.

Исходная точка аудита: `14a49a2`. Рабочее дерево было чистым. Локальная `main`
опережала `origin/main` на 217 коммитов, текущая ветка добавляла еще 15.
Удаленный сервер в этом аудите не опрашивался: сравнение относится к локальным
ссылкам Git.

### Что проверено сейчас

| Проверка | Результат | Граница вывода |
| --- | --- | --- |
| Чистая сборка CMake Release | PASS | Текущий код собирается |
| Полный CTest | 16/16 PASS | Внутренние проверки, не готовность продукта |
| Старый `make test` | Выбирал 1 тест `native.regression` | 9 тестов новой библиотеки не запускались |
| `oof dump`, локальный корпус | 0/59 | Все остановились на OOF1114, корневой записи идентичности |
| `oof build`, одна кнопка, затем `dump` | PASS | Только ограниченный вариант 27/18 |
| Две корректные кнопки в XML | OOF1122 | В кодеке задан предел в одну кнопку |
| Одна кнопка, Designer load и строгий dump 8.5.1.1343 | Оба кода 0 | Платформа принимает этот синтетический пример |
| XML после платформенного цикла | Совпадает | Каноническая форма из примера |
| Модуль после платформенного цикла | Отличается только LF/CRLF | Побайтовое равенство не заявляется |

Доказательства: `ev_8104e9c3719446aea918a53de5d12862` и
`ev_15d441ba3e414383958fa117d39b7f36`. Исходные приватные формы, пути клиентов,
платформенные файлы и журналы в репозиторий не включены.

### Почему работа не привела к готовой утилите

1. Менялся критерий успеха. В истории есть добавление сырых записей, удаление
   этих записей, шаблонная сборка и последующее удаление шаблонов:
   `f9b0b4e`, `52664a1`, `b1fc579`, `32d46a0`, `7286ac5`, `9f2361b`.
   Решение локальной задачи обратимости неоднократно расходилось с требованием
   редактируемой именованной модели без сохраненного исходного потока.
2. Проверялись разные пути под похожими названиями. Собственная обратная
   сборка, сохранение контейнера и совпадение внутренней модели не доказывают
   прием результата Designer. OACS уже фиксировал этот разрыв:
   `mem_4e60da35cfd54afe9f3f74f97f82fbc4`, доказательство
   `ev_ccff182457414fb6a8019f93eb2a43b0`. Это исторический результат,
   а не повторная проверка старого конвертера в настоящем аудите.
3. Широкий словарь ошибочно мог выглядеть как готовое покрытие. Каталог
   метамодели и XSD перечисляют больше свойств, чем умеет двоичный кодек.
   `src/storage/form_stream.cpp` явно запрещает страницы, картинки, команды,
   события формы и более одной кнопки. Справка дает имена и типы, но не
   восстанавливает условия записи, значения по умолчанию и связи в потоке.
4. Новый правильнее разделенный путь не был доведен до используемых входов.
   Коммиты июля выделили модель, контейнер, значения, XML и композицию
   Form.bin, но остановились на одном подтвержденном сценарии. Сейчас все
   59 проверенных входов отклоняются еще до разбора контролов. Исправление
   первой ошибки не означает, что все остальные их конструкции поддержаны.
5. Проверки и документация отстали от реализации. `make test` исключал
   тесты новой библиотеки; workflow не объявлял зависимость libxml2; README
   показывал старый CLI. `release-gate` корректно запрещает выпуск, но это
   означает, что существующий workflow пока не выпускает готовый продукт.
6. Исследования накопились в одном месте. Старый `src/main.cpp` содержит
   15189 строк и менялся в 122 коммитах. Исследовательские команды,
   платформенные пробы и продуктовые маршруты трудно проверять как единый
   контракт. Дальнейшее расширение этого файла увеличит стоимость проверки.

### Что удалось и стоит сохранить

Сохранить новую модель `OrdinaryFormDocument`, кодеки контейнера и list-stream,
типизированные значения, XML-адаптер, генератор схем, соответствующие тесты
и существующий инструмент строгой проверки Designer. Однокнопочный цикл
теперь подтвержден заново на платформе. Это доказательство пригодности
основы, а не обещание низкой стоимости остальных кодеков.

Старые дескрипторы и результаты платформенных экспериментов использовать
как проверяемые источники. Не переносить старый writer целиком и не
подключать его как запасной путь для неподдержанных входов нового CLI.

### История обсуждений и предел охвата

Через инструменты задач прочитано обсуждение от 2026-09-01
"Оценить перенос исходников форм". В нем пользователь уже констатировал
неудачу обычных форм и просил выделить знания управляемых форм в skill.
Ассистент предложил заморозить обычные формы и перенести платформенные XSD.
Это другой результат работы: редактор управляемого XML не исправляет
сериализацию обычного Form.bin.

Полный архив более старых чатов не получен: список задач ограничен последними
50, в списке архивных задач этого проекта нет, доступ инструмента к UI Codex
отклонен. Поэтому выводы об истории разработки опираются также на Git и
OACS, а полный аудит всех переписок не заявляется.

### Минимальный порядок доведения

1. Исправить стандартную проверку и видимый статус. В этой итерации
   `make test` переключен на весь CTest, зависимости workflow дополнены
   CMake и libxml2, README явно разделяет текущий CLI и историческое описание.
   Старый корпусный тест теперь сообщает SKIP при отсутствии входных форм,
   вместо вводящего в заблуждение PASS (0/0).
2. Следующая продуктовая итерация: одна реальная независимая форма, содержащая
   поля ввода, надписи, кнопки и обработчики. Нужны исходная выгрузка платформы,
   новый XML, сборка без исходного Form.bin, строгая выгрузка Designer и
   проверка сохранения каждого измененного свойства. Сначала корневая
   идентичность и необходимые свойства формы, затем конкретные контролы
   и их связи. Все шаги идут через существующие модель и кодек.
3. После первого полного сценария добавить несколько контролов,
   Panel/Page, привязки реквизитов и геометрию; затем CommandBar/Table
   и картинки по требованиям выбранных реальных форм. Новое понятие
   считать поддержанным только после изменения XML и проверки платформой.
4. Расширять корпус по классам входов и сохранять число успешных,
   отклоненных и непроверенных форм. Для форм с типами метаданных нужна
   подходящая конфигурация в проверочной базе. Не заменять эту проверку
   сохранением неизвестных данных или пропуском непонятных полей.
5. После согласованного покрытия закончить упаковку `oof`, установку
   и явный платформенный критерий выпуска для того же коммита. Команды
   diff/edit можно отложить: первая полезная утилита должна надежно
   разбирать, проверять и собирать поддержанные формы.

Откат не рекомендован: старые успехи относятся к другим контрактам.
Новый репозиторий оправдан только организационным отделением исследования
от продукта; необходимой работы с форматом он не уменьшит. Массовое
удаление старого кода до подтверждения рабочего сценария также не помогает.

Статус продукта: PARTIAL. Готовность универсальной утилиты не подтверждена.
Аудит не дает достоверной оценки срока полного покрытия: сначала нужно
закрыть описанную реальную форму и измерить объем недостающих кодеков.

## Исторический архитектурный анализ

Следующие разделы сохранены как история исследовательского пути. Их слова
"current" и заявления о покрытии относятся к прежним итерациям, а не
к проверенному состоянию нового `oof` выше.

This note records the current architectural conclusion for ordinary form
reader/writer work. It is deliberately about the pattern, not about one more
control-specific fix.

## Objective

Build ordinary form source as a typed object model:

```text
Form.bin container
  -> ordinary form payload
  -> ListInStream / cf_form_controls8 graph
  -> typed Form.xml + Module.bsl + picture files

typed Form.xml + Module.bsl + picture files
  -> typed ordinary form graph
  -> descriptor-driven ListOutStream / cf_form_controls8 graph
  -> ordinary form payload
  -> Form.bin container
```

The public XML boundary is the object model. Platform list-stream details are
codec internals.

## Evidence Summary

- Platform scans and LD audit evidence agree on a generic persistence path:
  `ListInStream`, `ListOutStream`, `TypeDomainPattern`, `CompositeID`, and the
  ordinary control payload families `cf_form_controls8`,
  `cf_form_controls_position8`, `cf_form_controls_info8`.
- 8.2 and 8.5 differ in implementation details, but the observed external
  transfer contracts for ordinary control position/info records stay compatible:
  count plus 16-byte records for the transfer payloads.
- Interactive and corpus checks showed that platform save/load can canonicalize
  old record shapes. Therefore byte identity against an old source shape is a
  diagnostic, not the public contract.
- Strict Designer load alone is not enough: missing metadata types in a
  validation infobase can make platform redump lose table/attribute/control
  semantics even when the stream loads.

## Compare/Merge Boundary

Current platform evidence does not support routing ordinary form persistence
through the Designer compare/merge UI.

The compare/merge stack is present, but it is a separate `LogForm` mechanism:

- `frntend.so` contains `ConfigCompareService`, `ConfigMerge`,
  `mergeform.cpp`, `IDS_CMD_COMPARECONFIGS`, `IDS_CMD_MERGEFORMREPORT`,
  `CompareReportGenerateTools<...FormEditHelper>`, and
  `IMDMergeHelperImpl<...FormEditHelper>`;
- `mngdsgn.so` contains `LogFormComparatorReport`,
  `LogFormMergeSettings`, `SWINComparator`, `LogFormDocumentFactory`, and
  merge-module UI interfaces;
- `mngbase.so` contains `LogFormComparator`, `LogFormECComparator`,
  `FormVectorComparator`, `LogFormMerger3`, and imports `core::create_diff`;
- `mngcore.so` contains the `LogForm` object family itself, including
  `LogForm`, `LogFormButton`, `LogFormCommand`, `LogFormElementsChangesFlow`,
  and `LFLFormComparator`.

The ordinary form persistence stack remains in `dsgnfrm`:

- `dsgnfrm.so` contains `CustomFormLoader`, `FormDesignerService`,
  `FormDocument`, `FormDocumentFactory`, and `FormDocumentView`;
- `dsgnfrm.so` imports `core::ListInStream`, `core::ListOutStream`,
  `core::CompositeID`, `core::TypeDomainPattern`, and the ordinary-control
  format ids `wbase::cf_form_controls8`,
  `wbase::cf_form_controls_position8`, and `wbase::cf_form_controls_info8`;
- the 8.2 PE libraries show the same split: `dsgnfrm.dll` imports
  `cf_form_controls*`, while `frntend.dll`, `mngbase.dll`, `mngcore.dll`, and
  `mngdsgn.dll` carry the `LogForm`/merge categories without directly
  importing `dsgnfrm.dll`.

`LD_DEBUG` on a successful 8.5 ordinary-form
`/DumpExternalDataProcessorOrReportToFiles` run loads both families
(`dsgnfrm` and the merge-capable `frntend`/`mngbase`/`mngdsgn` modules), but
that only proves they are resident in Designer. It does not prove that
`LogFormComparator` or `LogFormMerger3` serialize ordinary `Form.bin`.
Therefore compare/merge can be useful as vocabulary evidence for form-shaped
concepts, but it is not the writer path for ordinary forms unless a future
call-path trace proves a concrete bridge from `LogForm` into
`cf_form_controls*`.

## Current Product Pattern

The repository now has the right foundation:

- all supported ordinary controls are present in the public palette/XSD set;
- writer dispatch is registry-based instead of legacy per-control branch
  dispatch;
- shared control info descriptors exist for the supported controls;
- writer template fallback has been removed;
- public control-level raw profile leaks were already reduced for CommandBar,
  PictureDecoration, and Panel by promoting them into named concepts such as
  `CommandSource`, `PictureStyle`, and `PanelLayout`.
- the public position `dimensionProfile` selector and extra dimension
  `slotN` names were removed; writer profile selection now uses named
  dimensions, sections, and internal descriptor checks.
- the constant position `unit="form"` marker and coordinate `slotN` fallback
  were removed from the public position/binding dump path.

The current loop comes from the remaining mixed model:

- some paths build records from descriptors and named XML;
- other paths still preserve stream shape through public profile-like XML;
- tests sometimes assert slot/profile shapes directly, which locks in the
  wrong boundary;
- codec coverage currently proves that builders exist, not that every public
  property is mapped to a platform slot.

## Fresh Re-Audit: 2026-05-31

This re-audit used the current repository state, recent commit history, OACS
memory/context, and local coverage checks. The conclusion did not change, but
the concrete failure point is now sharper.

Current facts:

- Recent history shows the oscillation clearly: many commits first preserved
  compact/root/control/profile shapes to chase byte diffs, then later commits
  removed those same public shapes (`Form/SerializationProfile`,
  `dimensionProfile`, `slotN`, `unit`, `dimensionSegments`,
  `secondaryDimensionMarker`) and replaced some of them with named concepts or
  canonical writer defaults.
- The writer dispatch itself is no longer the main problem. The current codec
  coverage audit reports 26 public controls, 26 writer descriptors, 26 shared
  info descriptors, zero legacy writer branches, and zero template fallback
  tokens.
- The remaining problem is full property-slot implementation. The same audit no
  longer reports `xsdOnlyProperties` for the core editable controls
  `InputField`, `Table`, `Panel`, and `CommandBar`, so their public descriptor
  vocabulary now matches the XSD. Other controls still have smaller descriptor
  gaps, and a descriptor entry is not the same as proven slot read/write
  support.
- `PositionType` used to expose codec-shaped attributes:
  `layoutPreTail`, `primaryDimensionMarker`, `layoutTail`, `layoutMode`,
  `layoutGroup`, `layoutOrder`, `layoutNextOrder`, `layoutFlag1`, and
  `layoutFlag2`. These are now removed from public schema/dump/build. The
  remaining editable layout-order data lives in named `Position/LayoutFlow`,
  while record markers and tails are internal descriptor defaults.
- `RootPanelLayout` and `PanelLayout` no longer expose the raw layout residues
  `dependencyTail`, `pageLayoutHeader`, `postLayoutTailBeforeColor`,
  `postLayoutTailAfterColor`, or dependency group `prefix`/`header`. Empty
  platform dependency counts are represented, where needed, as empty
  `LayoutDependencyGroup` elements.
- Platform evidence in OACS keeps pointing to the same persistence mechanism:
  ordinary forms are serialized through `ListInStream`/`ListOutStream`,
  `TypeDomainPattern`, `CompositeID`, and the `cf_form_controls8`,
  `cf_form_controls_position8`, `cf_form_controls_info8` payload families.
  LD_AUDIT confirms this path for the all-controls fixture. There is no
  evidence that ordinary form persistence is Delphi DFM or another separate
  public form file format.

The actual functional formula is:

```text
container Form.bin
  -> form list-stream
  -> platform ordinary form graph
     controls + info + position + attributes + actions + type-domain values
  -> public typed Form.xml

public typed Form.xml
  -> ordinary form graph
  -> one canonical internally consistent ListOutStream generation
  -> form list-stream
  -> container Form.bin
```

The important word is `graph`. A control record is not independent from the
root panel, attributes table, event/action table, table-column editor records,
UUID identity, type-domain references, and layout dependency graph. Strict-load
failures after editing "just one column type" can still be caused by a broken
cross-record relation, not by the visible column property alone.

For table columns, the right model is also graph-based: a column may have an
`ElementControl` editor that is effectively a nested typed editor/control
concept, such as an input field or choice field. It should be represented as a
named child object of the column, not inferred only from the parent `Table`
branch and not preserved as a binary descriptor.

## Diff Classification Rule

Every new byte/list diff must be classified before code changes:

1. **Public object property** - visible in the palette, property panel, platform
   vocabulary, or stable object behavior. Add a named XSD property, dump/build
   mapping, semantic digest coverage, and a slot descriptor.
2. **Graph identity or relation** - object id, UUID, command source, event
   handler, attribute/type-domain link, table column editor, page ownership, or
   layout dependency. Add a named relation or identity field and validate the
   whole graph.
3. **Canonical writer generation detail** - root record kind, old top-stream
   length, marker, counter, slot count, default root panel shape. Keep this
   internal and emit the current canonical platform generation.
4. **Platform noise** - timestamps, save counters, regenerated UUIDs where the
   platform owns identity, and accepted platform canonicalization. Ignore in
   semantic digest and do not chase byte identity.
5. **Validation environment loss** - missing configuration metadata, absent
   type objects, or unloaded configuration support state. Fix the validation
   infobase, not the writer.
6. **Unknown residue** - do not expose it as XML. Keep the case failing in a
   coverage report until it is classified by platform evidence.

This rule is the missing guardrail. Without it, each strict-load or byte diff
tempts the implementation to add another public marker, and the next cleanup
removes that same marker again.

## Main Divergence From 1C

1C persists an object graph through a generic serializer. Our historical fixes
often treated a byte diff as proof that the original stream shape must be
preserved publicly. That created public `SerializationProfile`, `slotN`,
`dimensionProfile`, and tail fields.

Those fields are not the ordinary form object model. They are symptoms of
missing named concepts or missing internal canonical writer rules.

## Native No-Base Gap: 2026-06-04

The current native-only implementation can dump and rebuild the all-controls
fixture without Python, and it preserves `Module.bsl`. The remaining no-base
failure is in the native public XML/object graph boundary:

- public XML dump exposes names, titles, positions, bindings, attributes,
  commands, and picture sidecars;
- public XML dump does not yet expose or materialize all type-specific control
  payload concepts, such as button event/action records, command-bar button
  groups, table columns and type descriptors, chart state, calendar/list/value
  lists, and other control-specific `cf_form_controls8` data;
- the no-base writer therefore emits a minimal six-field control record
  `{guid, id, title, position, metadata, children}` instead of the platform
  typed payload in the third record field;
- semantic comparison of the current all-controls no-base rebuild reports
  `normalizedEqual=false`, `structuralDiffs=77`, and `semanticDiffs=17`.

The platform library pattern supports the descriptor-driven fix:

- `dsgnfrm.so` `FUN_002709e0` enumerates `cf_form_controls_position8`,
  `cf_form_controls8`, and `cf_form_controls_info8`;
- `FUN_00270da0` returns `cf_form_controls8` as the main existing payload and
  returns `position8`/`info8` as counted transfer records;
- `FUN_00255f70` and `FUN_00256510` are the write/read persistence entries
  around `core::ListOutStream` and `core::ListInStream`.

Therefore the release path is not to restore a Python implementation and not
to expose raw stream data in XML. The native writer must materialize a typed
ordinary form object graph, then serialize each control through descriptor
codecs for the platform payload families. The first implementation checkpoint
should cover the all-controls fixture by adding dump/build mappings for the
named concepts currently lost by the minimal writer.

The correct rule is:

```text
read many platform generations; write one internally consistent canonical
generation; expose only named object-model properties.
```

If a low-level value is required for rebuild, it must be classified:

- real user-visible form/control property -> named XSD property;
- real identity or graph relation -> named `uuid`, `id`, event, binding, page,
  command source, column editor, etc.;
- writer generation/default/counter/noise -> internal descriptor rule or
  explicitly named non-editing metadata only when the platform exposes it as a
  stable concept;
- unknown stream residue -> not public XML and not a fallback; keep it as a
  failing coverage gap until the platform concept is identified.

## Mistakes That Caused The Loop

- Preserving old root/control profiles to chase byte identity before deciding
  whether the value was a platform object property.
- Accepting `SerializationProfile` and `slotN` as "typed enough"; they are
  renamed raw stream structure.
- Treating per-control strict-load success as a full graph success. It misses
  event/action tables, attribute links, table column editor controls, UUID
  identity, and type-domain references.
- Validating corpus forms in an empty or wrong infobase, which can produce
  false semantic loss because metadata object types are unavailable.
- Letting tests encode raw shape preservation. These tests prevent the writer
  from converging on a canonical current platform generation.
- Using coverage numbers for controls as if they were property-slot coverage.
  The audit still shows many public properties that are schema-only or
  descriptor-only.

## What To Delete

- Public form-level `SerializationProfile`, position `dimensionProfile`, and
  all public `slotN` fields.
- Public position shape attributes that only select internal geometry layouts:
  `layoutTail`, `layoutPreTail`, `primaryDimensionMarker`, `layoutMode`,
  `layoutGroup`, `layoutOrder`, `layoutNextOrder`, `layoutFlag1`, and
  `layoutFlag2`.
- Root/panel layout tail/header/prefix attributes:
  `dependencyTail`, `pageLayoutHeader`, `postLayoutTailBeforeColor`,
  `postLayoutTailAfterColor`, and dependency group `prefix`/`header`.
- Tests that require `RootRecord slot5..slot10`, `TopLevel slotN`, or other raw
  stream shape names in public XML. The same applies to tests that assert
  position tail strings instead of named layout semantics.
- Any new sidecar/profile/fallback mechanism whose only purpose is to keep an
  old `Form.bin` shape.
- Public names that say "profile" only because we do not know the platform
  property yet. Keep those as codec gaps, not as editable XML.

## What To Rewrite

- Replace `Form/SerializationProfile` with named concepts only:
  root layout/page state where it is a real form layout concept, stable object
  identity where the platform identity is meaningful, and canonical internal
  defaults for record generation fields.
- Make root record and top-level form stream writing canonical inside the
  native ordinary-control writer; do not read record kind/title
  marker/top-level slots from public XML.
- Move the root panel handling to the same pattern as `PanelLayout`: named page
  state, page layouts, layout dependencies, base style, and no raw profile
  wrapper.
- Treat table column `ElementControl` as a nested typed control/editor concept.
  It can be an InputField, ChoiceField, etc.; do not infer it from only the
  parent table branch.
- Keep `Position/LayoutFlow` limited to the named graph concept: placement mode,
  layout group, order, next order, and horizontal/vertical boundaries. Pure
  stream selectors stay inside internal descriptor profiles.
- Split `geometry_stream_from_xml` into a small public layout model reader and
  internal geometry descriptors. The descriptor should choose the platform
  record shape from named bindings, page ownership, parent size, data binding,
  and control kind, not from XML tail attributes.
- Move object-model XML construction out of the native CLI body into a
  model/dump module. CLI commands should orchestrate commands; they should not
  be the place where raw geometry fragments become public XML.

## What To Add

- A semantic graph digest used in tests and corpus reports:
  controls by id/type/name/uuid, parent-child order, positions, bindings,
  attributes/type domains, events/actions, table columns/editor controls,
  pictures, fonts, colors, and command sources.
- A schema guard that fails on public raw-shape vocabulary:
  `SerializationProfile`, `TopLevel`, `RootRecord`, `slotN`, `Raw*`,
  `PlatformRecords`, `ListStream`, `ObjectModel`, `FormBin`, and equivalent
  renamed dump structures.
- Property-slot coverage, not only control coverage. The audit should report
  which XSD properties have dump and build mappings and which are only schema
  names.
- A list-diff triage tool that reports each mismatch path with the
  classification above: public property, graph relation, canonical writer
  detail, platform noise, validation environment loss, or unknown residue.
- A canonical writer matrix:
  build with current writer, strict-load in 8.5, strict-load in 8.2 where CLI
  supports it, platform redump, compare semantic graph digest.
- Corpus validation in an infobase with the relevant configuration loaded when
  forms contain metadata object types.
- Sanitized OACS checkpoints for every accepted platform conclusion, with
  evidence references attached to memories.

## Correct Next Steps

Done after this audit:

- form-level `SerializationProfile`, `RootRecord`, `TopLevel`, and public
  `slotN` schema fields were removed from `OrdinaryForm.xsd`, dump, build, and
  focused tests;
- root panel state/layout now uses the named `RootPanelLayout` XML node;
- root/top record generation is no longer read from public XML and is handled
  as canonical writer logic;
- position `dimensionProfile` is no longer public XML, and dimensions above
  the named height/minHeight/stretch/width set are emitted as
  `dimension="extra" extraIndex="N"` instead of `dimension="slotN"`;
- simple inline dimension segment counts and default primary dimension markers
  are now derived from `DimensionBinding` sections instead of being written as
  public marker attributes;
- `Position/@unit` is no longer public XML, and `Binding/@coordinate` no
  longer has a `slotN` fallback for the six named coordinate slots;
- `Position/@dimensionSegments` is no longer public XML; segmented dimension
  geometry is derived from the `DimensionBinding section` order by the writer.
- `Position/@secondaryDimensionMarker` is no longer public XML; the writer uses
  the canonical secondary marker for counted/dual dimension groups.
- `Position/@layoutMode`, `@layoutGroup`, `@layoutOrder`, `@layoutNextOrder`,
  `@layoutFlag1`, and `@layoutFlag2` are no longer public XML; dump/build now
  use the named `Position/LayoutFlow` element for the editable layout graph
  relation.
- `RootPanelLayout` no longer exposes `dependencyTail`, `pageLayoutHeader`,
  `postLayoutTailBeforeColor`, or `postLayoutTailAfterColor`; root layout stream
  separators are canonical writer details.
- `PanelLayout` and root layout dependency groups no longer expose
  `prefix`/`header`; zero-count separators are modeled as empty
  `LayoutDependencyGroup` elements when they have to be visible in the layout
  graph.
- The public descriptor vocabulary for `InputField`, `Table`, `Panel`, and
  `CommandBar` now matches their XSD property sets; the coverage audit has a
  regression guard for these four core controls.
- schema tests now guard against reintroducing the removed public raw-shape
  vocabulary.
- semantic graph comparison support exists in native runtime diff commands; it
  compares the normalized object model across controls, positions, bindings,
  events, attributes, pictures, fonts, colors, and command sources while
  ignoring container timestamps and current codec-shaped noise.
- corpus checks should attach the same semantic summaries to exported forms and
  compare matching forms across two exported trees before byte-level analysis.

Remaining next steps:

1. Extend actual dump/build slot implementation for the remaining XSD-only
   properties on non-core controls and for core properties that are present in
   the descriptor vocabulary but still not proven by slot-level tests.
2. Re-run the small all-controls fixture and Diadoc fixture.
3. Re-run UT/Enterprise-style corpus checks only in a matching configured
   infobase to avoid type-loss noise.
4. Use the semantic graph digest as the default corpus success metric. Use
   byte identity only for current-generation platform-oracle fixtures and for
   localizing a strict-load failure.

The goal is not to make every old source byte-identical. The goal is to make
the public XML a complete editable object model and make the writer emit a
platform-readable canonical ordinary form stream without hidden raw data.

## Platform Oracle Control Traces

Do not continue the no-base payload writer by guessing control payloads. The
next control-payload evidence step is a platform oracle batch:

```bash
tools/run_control_oracle_batch.sh \
  --source-root work/oracle-runtime/blank-source/root.xml \
  --out-dir scan-output/platform-control-oracle
```

The batch generates 26 BSL scripts under `scan-output/`, one per currently
writer-described ordinary control. Each script calls
`ЭлементыФормы.Добавить(...)`, assigns simple geometry/title where the platform
allows it, returns `ЭтаФорма`, and the injected oracle module writes
`ЗначениеВСтрокуВнутр(Результат)`.

The intended evidence product is not a writer patch. It is a corpus of platform
runtime streams plus `runtime-form-object-graph` summaries that can be compared
against `cf_form_controls8`, `cf_form_controls_position8`, and
`cf_form_controls_info8` descriptor evidence. Only after those traces identify
which values are public properties, graph relations, canonical generation
details, or unknown residues should a C++ payload codec be changed.

Current platform-oracle note:

- The blank source carrier may open and wait for the `Выполнить` button instead
  of firing the injected form `ПриОткрытии`. Use a carrier whose ordinary form
  already has `ПриОткрытии` wired by the platform; the local carrier dump under
  `scan-output/platform-control-oracle-carrier/` was produced from a private
  EPF and must stay ignored.
- With that carrier, the 26-control oracle batch completed and produced
  runtime streams plus object-graph summaries under
  `scan-output/platform-control-oracle/`. 25 controls produced a materialized
  form graph; `ActiveXControl` returned a short non-form/error stream. Follow-up
  schema join work maps `PivotChart` through `chart_root` `PivotChart` and maps
  public `HTMLDocumentField` to platform `HTML/html` plus `Field/htmlData`
  `HTMLFieldData`; Windows oracle validation for HTML remains pending.
- `LD_AUDIT` is useful for filtered symbol evidence but is not batch-safe in
  this 8.5 container: even a fixed audit module with no-op hooks conflicts with
  platform `libtcmalloc.so.4` and aborts with `Attempt to realloc invalid
  pointer`. `LD_DEBUG=libs,bindings` does not inject audit callbacks, but a
  `Button` pilot generated about 100k binding lines and timed out before the
  oracle script completed. Treat LD tracing as targeted diagnostic evidence,
  not as the primary 26-control corpus path.

## Консилиум: причинные модели, 2026-09-24

### NEUTRAL FRAME

- TASK: объяснить одновременно отказы текущего кодека и повторение исследований.
- SUCCESS CRITERIA: AC1, шесть различных причинных миров с опровержениями;
  AC2, взаимная рецензия, развитие двух миров и граф связей без ранжирования;
  AC3, проверяемая запись в существующем аудите и OACS. Это критерии консилиума,
  а не критерии готовности утилиты.
- FACTS: `form_stream.cpp:586` сравнивает базовую запись с канонической;
  строки 1392-1431 запрещают ряд коллекций и более одной Button.
  `tests/form_bin_test.cpp:55` проверяет собственный цикл одной кнопки.
  Июльская модель уже поддерживала этот случай; сентябрьские независимые входы
  отклонены. Исторические доказательства и границы описаны в начале аудита.
- CONSTRAINTS: один продуктовый путь, без старого writer и сырых полей в XML;
  внешний ActiveX вне целевого охвата; без выбора решения на этой фазе.
- UNKNOWNS: семантика позиции 17, полнота переноса прежних соответствий,
  достаточность контекста проверок и стоимость покрытия сочетаний.
- ASSUMPTIONS: малые правки достаточны для всех элементов; одиночные изменения
  определяют полную сериализацию; наличие 25 типов в каталоге означает
  сопоставимый объем работ. Ни одно из этих предположений не принято за факт.

### WORLDS

**W1. Локальная неполнота кодека**

- Механизм: распознается узкое множество записей; допустимая вариация вне него
  отклоняется до извлечения знакомого свойства.
- Объяснение фактов: точное сравнение базы Button и ограничение одной кнопкой
  совместимы с успехом канонического примера и отказом независимых входов.
- Скрытое допущение: главный барьер локален и не требует внешнего контекста.
- Ослабит: один и тот же логический поток при одинаковом вызове кодека дает
  разные результаты из-за внешнего состояния, а локальные записи неизменны.
- Граница: объясняет место отказа, но не многократное повторение исследования.
  Источник: `src/storage/form_stream.cpp:581-596,1392-1431` в sidecar.

**W2. Неучтенный контекст сохранения**

- Механизм: значение зависит от состояния документа, метаданных или связей,
  отсутствующих в локальном представлении исследуемого элемента.
- Объяснение фактов: исторический аудит описывает влияние недостающих типов
  проверочной базы и различие runtime-объекта и сохраненного документа.
- Скрытое допущение: изменение записи всегда отражает локальное свойство.
- Ослабит: изменение контекста при одинаковых значениях не влияет на результат,
  а единственное локальное соответствие устраняет отказ во всех таких случаях.
- Граница: для нынешнего OOF1114 этот механизм не установлен; точное сравнение
  кода само по себе уже объясняет отказ. Источник: исторические разделы этого
  аудита о проверочной базе и связях объектов.

**W3. Знание не участвует в следующем решении**

- Механизм: факты сохранены, но не сопоставляются с гипотезой нового опыта.
- Объяснение фактов: позиция 17 и метод Button matrix появились до сентябрьского
  пилота, однако не были использованы для определения его новизны.
- Скрытое допущение: наличие записи в Git/OACS обеспечивает ее использование.
- Ослабит: документированное сопоставление всех этих фактов до опыта с явным
  обоснованием, почему повтор необходим в другом контексте.
- Граница: не объясняет двоичную ошибку. Источники: `53abed1`, `1a5dcb8`,
  `bbe92a2` и начало этого аудита.

**W4. Покрытие прерывается при смене реализации**

- Механизм: архитектура переносится в новую библиотеку через минимальный
  сценарий, а накопленные частные соответствия остаются в прежней реализации.
  Удаление прежних инструментов увеличивает стоимость их последующей проверки.
- Объяснение фактов: июльский `liboof` остановился на канонической Button;
  `d94e622` удалил старые реестры и oracle-скрипты; новый пилот не добавил кодек.
- Скрытое допущение: более чистая архитектура автоматически наследует покрытие.
- Ослабит: сопоставление прежнего и нового путей покажет, что все корректные
  именованные возможности уже перенесены, а остаток обеспечивался только
  запрещенным сохранением исходных данных или ошибочными соответствиями.
- Граница: число удаленных строк не доказывает потерю корректных возможностей.
  Источники: `14a49a2`, `d94e622`, прежние descriptor-реестры в Git.

**W5. Промежуточные проверки подменяют конечный результат**

- Механизм: успех внутреннего цикла, приема Designer или runtime-опыта
  трактуется шире проверенного условия при неизменном продуктовом контракте.
- Объяснение фактов: внутренние PASS совместимы с 0/59 реальных входов;
  прием четырех EPF Designer совместим с отказом их чтения продуктом.
- Скрытое допущение: промежуточная проверка надежно предсказывает редактируемость.
- Ослабит: сквозная независимая проверка чтения, правки, сборки и повторного
  чтения устойчиво проходит на новых формах, а прежние метрики предсказывают ее.
- Граница: не отрицает реальное подтверждение сохранения Caption/Enabled
  свежей сессией Designer. Источники: `tests/form_bin_test.cpp:24-83`,
  `tools/verify_button_property_pilot.py:237-250`, результаты последнего пилота.

**W6. Рост сочетаний при известных отдельных свойствах**

- Механизм: даже при известном контексте и корректных отдельных соответствиях
  взаимодействия значений, порядка, родителей и событий требуют новых правил.
- Объяснение фактов: одиночный сценарий не проверяет такие сочетания; план
  экспериментов отдельно перечисляет второй элемент, родителей и зависимости.
- Скрытое допущение: корректность отдельных свойств переносится на их сочетания.
- Ослабит: правила композиции предсказывают прежде не проверенные сочетания
  без дополнительных исключений или потери смысла.
- Граница: текущий отказ одной Button не доказывает комбинаторную причину.
  Это гипотеза масштаба, а не установленный диагноз. Источник: `research-map.md`,
  раздел последовательности экспериментов, и единственная составная фикстура
  `tests/form_bin_test.cpp`.

### SELECTIVE EVOLUTION

W3: неиспользованный факт -> повтор опыта -> растущий архив повторов -> поиск
становится дороже нового узкого опыта -> в пределе накопление доказательств
сосуществует с почти нулевым накоплением используемого знания. Механизм на
всех этапах один: записи не меняют решения следующей итерации.

W6: несколько значений одного свойства -> сочетания свойств -> сочетания
элементов и связей -> перебор всех конфигураций -> при независимых конечных
областях значений размер пространства равен произведению их размеров.
Это предел перебора, а не доказательство необходимости перебора: устойчивые
правила композиции могут сокращать пространство проверок.

### INVARIANTS

- Из W3: объем архива и объем знания, используемого при решении, независимы.
- Из W6: покрытие словаря свойств и покрытие их взаимодействий различаются.
- Из W6: конечная таблица примеров не устанавливает универсальность правила
  за пределами примеров без дополнительных оснований.

### COLLISION GRAPH

- W3 и W4 пересекаются: забытое доказательство и неперенесенная реализация
  усиливают разрыв, но могут существовать по отдельности.
- W5 может скрывать W1 или W4: локальный PASS не обнаруживает их полноту.
- W2 и W6 пересекаются по графу; W2 предполагает недостающий контекст,
  W6 допускает полностью известный контекст и сложные взаимодействия.
- Сильная версия W1, "достаточно локальных правил", противоречит сильной
  версии W2, "результат определяется неучтенным контекстом", для одного отказа.
- W3 объясняет историю процесса, W1 объясняет отказ входа. Это разные уровни.

### UNRESOLVED QUESTIONS

- Что означает позиция 17 и какие значения разрешены при разных условиях?
- Какие прежние соответствия проверены независимо и применимы к новой модели?
- Какие отказы обусловлены локальной записью, а какие контекстом документа?
- Какие свойства композиционны и какие требуют совместных правил?
- Какие старые задачи остаются недоступны и могут изменить исторический вывод?

Три участника GPT-6 Luna medium подготовили и взаимно рецензировали материалы.
Редактор дополнил существующий аудит; root объединил пересекающиеся объяснения
критериев/измерения и отделил мир миграции реализации. Продуктовый код не
менялся, новые платформенные эксперименты не выполнялись. ReuseDecision:
`extend_existing`; отдельный отчет дублировал бы существующего владельца.

Доказательство консилиума: `ev_2e9390fadf3e441291e4ed849fe84284`; checkpoint
`trace_22e591faf03f494bb12475732d2371e5`. AC1-AC3 PASS для исследовательского
разбора; это не меняет статус PARTIAL продукта.

No winner selected. No solution recommended.
