# Ordinary Form Contract

## Целевая приемка 2026-10-26

Полное покрытие проверяется для 1С 8.5.1.1343, без ActiveXControl.
Справочный каталог сохраняет исключенный тип; отчет `oof --coverage --json`
отделяет 26 справочных типов от 25 целевых и показывает незавершенные
классификации. Флаг descriptorCoverageComplete не заменяет независимую
проверку семантики платформой. Полные критерии приведены в
[целевом состоянии](repository-target-state.md#текущая-цель-полное-покрытие-8511343).

CalendarField.ToolTip использует общий локализованный слот подсказки и пустую
строку по умолчанию. CheckBox.BorderColor использует общий кодек цвета,
ChoiceField.Font и CommandBar.Font используют общий FontValue и кодек шрифта.
Неподдержанные соседние поля сохраняют предупреждение о неполноте.
RadioButton и InputField также поддерживают Font и BorderColor, а ListBox
поддерживает BorderColor. Используются те же типизированные общие кодеки.
Font у ListBox не объявлен в текущем API и не вводится как искусственное
публичное свойство. Профиль редактора таблицы остается отдельной границей.


CalendarField и ProgressBar также поддерживают Font и BorderColor,
UsualGroup поддерживает Font и BorderColor, TrackBar поддерживает BorderColor.
У UsualGroup default шрифта отличается: `Font kind="automatic" bold="true"`.
Именованный FontValue различает отсутствие bold, явно false и true;
остальные неподтвержденные переопределения автоматического шрифта отклоняются.

Общие TextColor, BackColor, FieldTextColor, FieldBackColor, ButtonTextColor и
ButtonBackColor поддерживаются там, где пара контрол/свойство подтверждена
платформой. В этой серии закрыты 32 пары и три информационных цвета календаря:
HolidayColor, SelectionBackColor, SelectionTextColor. ColorValue сохраняет opaque
RGB, automatic и подтвержденные именованные styles; alpha и неизвестные styles
отклоняются. Неизвестные соседние поля не маскируются нормализацией цветов.

CalendarField сохраняет обе границы периода отображения (Date либо Undefined),
ShowCurrentDate, CalendarNavigation, EnableStartDrag и EnableDrag. Дата,
совпадающая со служебным значением Undefined, явно отклоняется. CurrentDate
является временным выбранным значением модели в памяти. Явные runtime-only
свойства отклоняются при чтении и записи публичного XML, исключены из XSD и
не записываются в Form.bin. Текущее значение нельзя молча выдать за default.

Border поддерживается у CalendarField, CheckBox, ChoiceField, InputField,
LabelDecoration, ListBox, PictureDecoration, ProgressBar, RadioButton, TrackBar
и UsualGroup. Именованное значение рамки учитывает default владельца; UsualGroup
имеет отдельный внутренний формат. Rounded разрешен только у UsualGroup,
ширина должна равняться 1. Это расширение не означает поддержки всех вариантов
рамок платформы. CalendarField.SelectionMode принимает DateSelectionMode.Single,
Multiple и Interval. SelectedDates является временной коллекцией; публичный XML
и BIN не сохраняют ее содержимое.

`SelectedDates` доступно только для чтения как свойство: назначение и сброс
возвращают `read_only`, даже пока getter живой коллекции не реализован.
Это не запрещает методы самой коллекции. Справка 8.5.1.1343 описывает
`Count`, `Get`, `Contains`, `Add`, `Delete` и `Clear`.

Нативная проверка 12 календарей, по четыре на режим Single, Multiple и
Interval, обнаружила различие между датой как ключом и сохраняемым временем.
`Contains` и `Delete` сопоставляют день без времени. В Multiple повторное
добавление того же дня сохраняет первое время, в Single заменяет выбранное
значение вместе со временем. Interval заполняет промежуточные дни; при
добавлении 6 ноября в 12:34:56 к выбранному 4 ноября промежуточное 5 ноября
тоже получает время 12:34:56. После удаления 6 ноября и перехода в Single
остается 5 ноября, а не первый элемент. Поэтому обычный массив дат не
реализует этот контракт. В данном запуске начальное выделение и выделение
клона соответствовали текущему дню базы, 4 ноября 2026 года; это не
константное исходное значение модели. Кодек и getter остаются незавершенными.

CheckBox, ChoiceField, RadioButton, LabelDecoration и PictureDecoration также
сохраняют Transparent. PictureDecoration поддерживает Font и BorderColor через
те же именованные типы значений, что и остальные подтвержденные владельцы.
CheckBox сохраняет булевое ThreeState с исходным значением false. Редактор
колонки таблицы сохраняет прежние ограничения.
Chart сохраняет локализованные ValueLabelFormat и PercentLabelFormat,
включая различия LF, CRLF и CR. Designer удаляет пустые языковые варианты
при сохранении формы.
Chart сохраняет LabelDelimiter, MaxSeriesCount, MaxSeriesPercent,
BeginGaugeAngle, EndGaugeAngle, GaugeThickness и GaugeBushThickness.
Числовые границы проверяются при чтении и записи.
GanttChart сохраняет OnIntervalEditEnd, HTMLDocumentField сохраняет
DocumentComplete, GeographicalSchemaField сохраняет Selection, а
GraphicalSchemaField сохраняет Selection и OnActivate. Назначения проверены
в 1С и строгой выгрузкой Designer; вызов через интерфейс отдельно не проверен.
Реквизит формы поддерживает именованный тип valueTree. Отдельные реквизиты
проверены в 1С и строгой выгрузкой Designer. Табличное поле поддерживает
источник ValueTree и свойство Tree; для ValueTable это свойство недоступно.
Dendrogram сохраняет события Selection и DetailProcessing вместе с узлами
и связями. Назначения проверены платформой; вызов событий интерфейсом
отдельно не проверен.
Chart сохраняет именованные события Selection и DetailProcessing совместно
с сериями, точками и значениями. Вызов событий интерфейсом отдельно не проверен.
Chart сохраняет 13 булевых свойств, включая автоматические пределы и
SeriesInRows. Связанные поля пределов проверяются на согласованность.
Платформенные пробы охватывают три размера и три типа диаграммы;
именованная сборка других типов Chart остается отдельной задачей.
Form сохраняет 13 именованных событий с независимыми обработчиками.
Назначения проверены на платформе; фактический вызов всех событий через
интерфейс отдельно не проверен.
SpreadsheetDocumentField сохраняет полосы прокрутки, Enabled, ShowSelection,
EnableStartDrag, EnableDrag, Border и BorderColor, а также семь подтвержденных
событий. Парные поля полос прокрутки проверяются на согласованность.
Порядок обхода элемента хранится в Position/TabOrder. Изменение через API
сдвигает соседние элементы на той же странице; порядок ChildItems сохраняется.
`ZOrder` определяется порядком соседей в `ChildItems`. Writer сохраняет этот
порядок как порядковый номер в записи геометрии; отдельное скалярное поле
в XML не требуется. Изменение через API сохраняет независимый `TabOrder`.
Оба успешных случая, перестановка и назначение текущего порядка, возвращают
`applied`. Скалярного исходного значения для сброса нет.

Общее свойство `Name` читает `ControlNode.name`, которое сохраняется
в XML-атрибуте `name` и общей записи элемента. При целевом режиме
совместимости `DontUse` API доступен только для чтения, включая назначение
того же имени. Нативная проверка 12 полей подтвердила 36 отказов без изменения
имени и поиска элемента. Справка допускает запись в старых режимах
совместимости до 8.3.5 включительно; такой режим API этим контрактом
не реализован. Редактирование имен в исходном XML сохраняется.

`Form.CurrentControl` классифицирован как состояние выполнения и исключен
из публичного XML. На 12 полях нативная десериализация возвращает корневую
панель вместо выбранного поля. Назначение работает и до открытия формы;
назначение корневой панели после выбора поля не меняет активный элемент.
Кодек живой ссылки и API смены фокуса пока не реализованы. `GetPropVal`,
`SetPropVal` и сброс явно возвращают `unsupported_surface`. Эта
классификация хранения не означает готовности поведения активного элемента.

`Form.FormOwner` имеет начальное значение `Undefined` и является временной
связью. Нативная проверка 12 элементов подтвердила назначение и стабильное
чтение владельца, сброс через `Undefined` и отсутствие владельца у клона
после сериализации. `Null`, число и строка отвергаются платформой.
Кодек хранения не требуется. Кодек живой ссылки на другую форму или ее
элемент пока не реализован: API явно возвращает `unsupported_surface`,
публичный XML не принимает `FormOwner`. Проверка ссылки на другую форму
в этом пакете не завершена из-за повторной инициализации модуля стенда.

`ListBox.CurrentRow` является временной ссылкой на элемент списка значений.
На 12 заполненных списках подтверждены 36 выборов строк, стабильное чтение,
исходное `Undefined`, сброс выбора после удаления строки и `Undefined` у
нативного клона. Назначение `Undefined`, `Null`, числа и строки отвергается
с ошибкой отсутствующего ключа. Чужой элемент принимается без исключения,
но не становится текущим; точное сопоставление его ключа пока не установлено.
Кодек живой ссылки и связь текущего значения реквизита со списком пока
не реализованы: API возвращает `unsupported_surface`, XML отвергает свойство.

`HTMLDocumentField.Document` является доступным только для чтения временным
DOM типа `ВнешнийОбъект` на целевой Linux-платформе. У 12 локальных документов
подтверждены стабильная идентичность, чтение заголовка, изменение DOM и отказ
замены свойства. Нативный клон получает другой DOM с пустым заголовком и без
добавленного маркера. Скалярного значения по умолчанию нет; при загрузке
возможна ошибка готовности документа. Кодек живого DOM не реализован,
getter возвращает `unsupported_surface`, setter и сброс возвращают `read_only`.
Публичный XML не содержит `Document` или скрытой копии DOM. Наличие такого
свойства не зависит от исключенного из целевого набора `ActiveXControl`.

`Data` использует существующий `DataPath` и классифицирован как
`persisted_editable`, `binding_record`. Getter прямой привязки возвращает
имя реквизита по его ID. Вложенные пути и контекст RadioButton без собственной
привязки пока явно отклоняются. Setter и сброс Data остаются неподдержанными:
нативная перепривязка меняет также состояние редактора, которое нельзя
воспроизвести одной заменой DataPath.

`ValueType` классифицирован как `persisted_editable`, `control_info`.
У связанного контрола getter читает тип реквизита, setter и сброс запрещены,
включая реквизит с пустым типом. Непривязанный ChoiceField возвращает пустой
тип; назначение корректного описания типа принимается без изменения.
Для непривязанного InputField тип хранится в собственном именованном
`ValueType`. Setter этого типа в модели пока не реализован: нативные
изменения оставляют дополнительное состояние редактора.

Кодек InputField поддерживает отсутствие DataPath при явно заданном локальном
ValueType: одиночные String, Numeric и Date-only с прежними ограничениями
квалификаторов. Тип по умолчанию не выдумывается; пустые и смешанные типы
остаются неподдержанными. Несуществующая привязка не подменяется локальным
типом. Проверены 12 несвязанных полей на платформе, начальные типы и 48
нативных восстановлений; строгая выгрузка сохранила XML и модуль побайтно.
Профили с остаточным состоянием после изменений типа остаются явно неполными
с OOF1140. Непривязанные числовые поля с локальным ValueType сохраняют
MinValue и MaxValue через общий кодек типизированных значений, без создания
фиктивного реквизита или DataPath.

Вложенная Panel сохраняет Enabled, ToolTip, Font, Transparent, ShowTabs,
Picture, PictureSize, PanelPictureLocation и OnCurrentPageChange. Для ShowTabs
проверены все 13 значений. Картинки поддерживают ссылки на библиотеку и файлы
GIF, PNG, JPEG и BMP; для размера и положения проверены допустимые режимы. Корневая панель
формы сохраняет только ранее подтвержденный набор свойств.

`Form.Controls` и `Panel.Pages` используют общий `FormItemCollectionRef`:
идентификатор владельца и вид коллекции. `form_item_refs` разрешает эту
ссылку через существующее дерево `ChildItems`, без второй копии объектов.
`Controls` обходит контролы от родителя к потомкам, пропускает страницы
и корневую панель формы. `Pages` возвращает прямые страницы своей панели
в порядке дерева. Физический порядок объектов во внутренних векторах
не определяет порядок коллекции. Поврежденное или неполное дерево,
включая панель без именованных страниц, возвращает `nullopt`.
Замена и сброс коллекций запрещены; в XML отдельных `Controls` и `Pages`
нет. `CurrentPage` является временной ссылкой на страницу и в XML не входит.

Нативный пакет 8.5.1.1343 охватывает 12 панелей, 24 контрола и 24 страницы:
порядок, доступ по имени и индексу, идентичность повторного чтения,
запрет замены и выбор текущей страницы. Выполнены 172 положительные проверки;
отдельное наблюдение подтверждает отсутствие корневой панели в `Controls`.
Добавление, удаление и очистка коллекций этим пакетом не проверялись и
нового API для этих операций не вводилось.
Table сохраняет Enabled, ToolTip, Font, BorderColor и FieldBackColor через
общий механизм свойств; ограничения редакторов колонок остаются прежними.
Table сохраняет HeaderBackColor, HeaderTextColor, FooterBackColor,
FooterTextColor, SelectionBackColor, SelectionTextColor, AlternationRowBackColor,
HeaderFont и FooterFont с платформенными значениями по умолчанию.
Table сохраняет AutoInsertNewRow, Footer, Header, EnableStartDrag, EnableDrag,
ChangeSettingOfColumns, ChangePositionOfColumns, ChangeRowSet, HorizontalLines,
VerticalLines и UseAlternationRowColor. ChoiceMode таблицы относится к
состоянию времени исполнения и не сохраняется в Form.xml.
Table сохраняет 22 именованных события. OnCurrentParentChange доступно при
источнике ValueTree; вызов событий в интерфейсе отдельно не проверен.
`Table.Columns` возвращает `TableColumnsRef` с владельцем Table.
`table_columns` разрешает ссылку в существующий `TablePayload.columns`,
сохраняя порядок и именованные колонки без копии коллекции. Замена и сброс
коллекции запрещены. Содержимое сохраняется прежним именованным `Columns`;
самостоятельного скалярного значения свойства нет. Ссылки отдельных колонок
и полный API изменения коллекции этим интерфейсом не вводятся.
Платформенный пакет из 12 таблиц с колонками Code и Extra подтвердил
стабильность коллекции, порядок, доступ по индексу и имени, поиск и все
36 запретов замены собственными колонками, чужими колонками и Undefined.
LabelDecoration сохраняет Hyperlink и Shortcut, PictureDecoration сохраняет
Shortcut. Сочетание F2 с Alt и Ctrl проверено при открытии формы в режиме
гиперссылки; физическое нажатие клавиш еще не проверено.
LabelDecoration также сохраняет VerticalAlign, MarkNegatives и ScrollingText.
Для ScrollingText проверены DontUse, Slow, Normal, Fast и VeryFast; внутренний
код 1 отклоняется. Визуальное отображение этих свойств отдельно не проверено.
InputField и ChoiceField сохраняют общие события выбора, открытия, очистки,
настройки и изменения; InputField также сохраняет AutoCompleteText и TextEditEnd.
Редакторы колонок таблицы сохраняют прежние ограничения.
PictureDecoration сохраняет DragStart, DragCheck, DragEnd и Drag; ListBox
сохраняет Selection, OnActivateRow и OnCheckChange. Назначения проверены
на платформе, фактический вызов через интерфейс отдельно не проверен.
Обработчики LabelDecoration.Click и PictureDecoration.Click сохраняются как
именованные события; их назначения проверены после выгрузки и повторного
открытия. Фактический щелчок и вызов обработчиков в интерфейсе еще не проверены.

Caption для Button, CheckBox, RadioButton, UsualGroup и LabelDecoration
поддерживает обычную строку и именованные переводы. Для непустых ru/en эти пять
типов прошли полный цикл Designer и проверку значений при запуске. Пустые
переводы и другие сочетания языков требуют отдельного подтверждения.
Caption самой формы также поддерживает именованные переводы; непустые ru/en
прошли проверку Designer и запуск на синтетической форме. Form.AutoTitle
сохраняется как булево свойство с default true; значение false проверяется на
формах с заданным и пустым заголовком.

PictureDecoration также сохраняет Hyperlink, UseContextMenu, Zoomable,
EnableStartDrag, EnableDrag, NonselectedPictureText и PictureSize. Поддержаны
шесть режимов PictureSize: RealSize, Stretch, Proportionally, Tile, AutoSize и
ByFontSize. Внутренние парные значения размера должны совпадать. Для
NonselectedPictureText поддерживаются обычная строка и именованные переводы
`Item` с кодами языков. Предел составляет 65536 переводов на свойство;
превышение отклоняется. Пустой перевод сохраняется в модели и Form.bin, но
Designer 8.5.1.1343 удаляет его при выгрузке даже при наличии непустого `en`.
Поэтому полная идентичность XML через Designer для этого случая не подтверждена.
Остальные события PictureDecoration остаются отдельными задачами.

UsualGroup и TrackBar сохраняют Transparent. ProgressBar сохраняет ShowPercent,
ShowStyle (ProgressBarSmoothingMode.Smooth, Broken, BrokenTilt) и Orientation.
TrackBar сохраняет MarkingAppearance (DontShow, TopLeft, BottomRight, BothSides)
и Orientation. Orientation допускает только Auto, Horizontal и Vertical.
Числовые LargeStep и MarkingStep не входят в это расширение.

ChoiceField сохраняет ChoiceButton, ClearButton, SpinButton, OpenButton и ReadOnly
как булевы свойства с default false. ChoiceField также сохраняет ChoiceListHeight
и ChoiceListWidth, InputField сохраняет ChoiceListWidth. Эти размеры являются
signed int32 с default 0; дробные и выходящие за диапазон значения исходников
отклоняются. Редакторы колонок таблицы не получают эти свойства автоматически.
Поддержка списков и дополнительных картинок остается отдельной задачей.

CheckBox и RadioButton сохраняют HorizontalAlign (Left, Center, Right),
VerticalAlign (Top, Center, Bottom) и TitleLocation (TitleLeft, TitleRight).
HorizontalAlign.Auto и Justify отклоняются для этих владельцев в API и XSD,
поскольку платформа их не принимает. ThreeState и числовая привязка CheckBox
остаются за границей этой итерации.

CalendarField поддерживает восемь именованных событий: Selection, DragStart,
DragEnd, Drag, OnActivateDate, OnPeriodOutput, OnChange и DragCheck. События
хранятся в Events самого контрола как имена обработчиков. Кодек проверяет
владельца, уникальность, известный тип действия и непустой обработчик;
неизвестные записи и нарушенные счетчики отклоняются. Порядок внутреннего
хранения не определяет порядок именованных событий в XML.
CheckBox, RadioButton и TrackBar также сохраняют OnChange через ту же
дескрипторную проверку и именованные Events своего контрола.

CLI подготавливает файлы в приватных временных файлах рядом с выходом,
сохраняет режим существующих файлов и откатывает уже установленные файлы
при обрабатываемой ошибке. Ошибка удаления временных файлов сообщается явно.
Несколько файлов не образуют атомарную транзакцию при аварии процесса.

## Актуальное состояние и границы CLI, 2026-10-05

Все 38 контролов эталонной формы читаются в именованную проекцию. Исходный
разбор содержит 63 предупреждения OOF1140 и `reconstructionComplete=false`.
Проекция собирается без исходного `Form.bin`, проходит строгую выгрузку
Designer; XML, `Module.bsl` и повторный BIN собственного writer совпадают.
Это подтверждает доступные именованные данные и повторяемость цикла, но не
полное восстановление произвольной формы. Свидетельство:
`ev_87eeb5b904f74e03b7392b2bb3692d34`.

CLI ограничивает размер каждого входного файла 64 MiB и суммарный размер
файлов-картинок пакета 64 MiB. Парсер list-stream ограничивает разбор
1 048 576 токенами и 256 уровнями вложенности. Парсер контейнера ограничивает
обход числом блоков min(размер входного файла в байтах, 1 048 576); заголовок
блока ограничен 64 байтами. Эти пределы ограничивают расход ресурсов на входных
данных, но не доказывают отсутствие всех уязвимостей.

Пакетные `Module.bsl` и файлы картинок должны разрешаться внутри каталога формы.
CLI отвергает выходные символические ссылки. Пакетные входные и все выходные
файлы с несколькими жесткими ссылками также отвергаются. Входная символическая
ссылка допустима только при разрешении в обычный файл внутри пакета. Границей
служит канонический каталог пакета рядом с явно указанным XML; ссылки в
родительских каталогах этого входного пути разрешены.
CLI читает и сохраняет `Module.bsl`, но не исполняет BSL. Рабочий
каталог следует считать доверенным: CLI не изолирует процесс и не защищает
от гонок при параллельной замене файловой системы.

This repository has no external consumers to protect. Breaking old command
surfaces, XML shapes, helper scripts, compatibility aliases, and fallback paths
is allowed when it makes the product architecture cleaner.

There is one product: a platform-like `OrdinaryForm` object graph.

There is one release path:

```text
Form.bin -> ListInStream -> OrdinaryForm -> Form.xml + Module.bsl + Items/*
Form.xml + Module.bsl + Items/* -> OrdinaryForm -> ListOutStream -> Form.bin
```

Everything outside that path is research, diagnostics, or temporary scaffolding.

## Product Model

`OrdinaryForm` owns the form, controls, nested controls, attributes, commands,
events, positions, bindings, identities, typed values, defaults, module text,
picture references, and platform-like `GetPropVal` / `SetPropVal` behavior.

`Form.bin` is not the model. It is only a container for the serialized form
stream and module stream.

Public `Form.xml` is not a dump format. It is the editable projection of
`OrdinaryForm`.

## Concept Registry

The only owner of public ordinary-form model decisions is the executable
`OrdinaryFormConceptRegistry`.

Platform schemas, help/API extracts, runtime bindings, descriptor joins, oracle
streams, corpus checks, and Designer validation are evidence adapters. They do
not decide public XML concepts on their own and they do not form a trust
hierarchy.

Storage names are evidence, not public names. API names are evidence, not
storage slots. Runtime types are evidence, not public object names. Negative
search results in a help index are evidence about that index only.

Every accepted public concept must have one registry row:

```text
API/help name -> public XML name -> runtime identity -> storage member/codec -> proof
```

Example:

```text
ПолеВвода / Поле ввода -> InputField
-> 381ed624-9217-4e63-85db-c4c3cb87daae
-> TextBox / txt
-> oracle/corpus proof
```

`TextBox` cannot be mapped by type alone because `InputField`, `ChoiceField`,
and `ListBox` share that storage shape. The registry row, not the storage type,
owns that distinction.

## Add Concepts This Way

When XML cannot rebuild a value, add the missing named object-model concept.
Do not patch old payloads.

A new concept is accepted only after the registry declares:

- API/help name and type, or why it is storage-only;
- public XML name and schema owner;
- runtime identity: GUID, collection identity, event/command identity, or
  value-object identity;
- storage member, descriptor family, slot/default/write rule, or value codec;
- proof command or corpus/oracle validation.

If this row is incomplete, the concept stays diagnostic.

## Delete These, Do Not Preserve Them

Remove release-facing code or docs that require:

- source `Form.bin` as build input;
- baseline diffs;
- payload patching;
- raw/list-stream preservation;
- hidden raw object models;
- fallback binary blobs;
- compatibility profiles;
- old command aliases whose only purpose is not breaking old behavior.

Temporary diagnostics are fine under `scan-output/`, but they must not become
release inputs or public XML.

## Public Package

`Page` is identified in public XML by its name and owning `ChildItems` tree.
Its typed internal ID is allocated during parsing and is not written to XML.
Control IDs remain platform IDs. A public `Page.id` is rejected rather than
accepted through a compatibility branch. Current XML identity verification:
`ev_8340649070f5401787475122583e122c`.

Page `Visible` and `Enabled` are independent Boolean properties with default
`true`. XML omits default values and writes `false` explicitly. The schema
permits each property once, between `Title` and `ChildItems`. Verification:
`ev_df9462158f2a4c42a53b31741b9c99fb`. These properties are supported by the current recursive Panel/Page codec
within the limits described below.

Page has an optional named `Position`, using the existing rectangle and
`Bindings` vocabulary. Absence remains implicit; an explicit position survives
XML serialization. The internal boundary codec currently supports fixed
Left/Top and explicit Right/Bottom constraints to the owning Form or Panel.
Width and Height are differences between endpoint coordinates. Missing owner
constraints, other targets, proportional constraints and unsupported flags
are rejected instead of reconstructed from a baseline stream.

Changing the observed Right constraint changes the runtime width of a bound
child while its saved rectangle remains fixed. This establishes a layout
effect, not the complete formula for every page variant. A subsequent Bottom
experiment confirms a four-pixel height change for a four-unit constraint
change, with the saved rectangle fixed. A larger change encounters another
size limit whose formula remains unproven. The root writer
now uses this named boundary encoder. Verification:
`ev_d8864750203a47bfa4413ae13c294c97`,
`ev_d1df9f1b84274f029da48a13d8776302`,
`ev_e2fddecde6c9498c8f119fd3d812cc7b`.
Recursive Panel/Page document serialization now supports the default Panel
style, named position and explicit child pages. Unsupported Panel properties,
page pictures and direct children without a Page are rejected.

The ordinary-control geometry codec uses an explicit owner, page index and
local sibling ordinal. Within a Panel, platform target zero resolves to that
Panel in named bindings, including proportional targets. At root it resolves
to Form. A nested binding to Form is rejected until its separate storage
representation is established. Page and ordinal mismatches, invalid owners,
incoming IDs and source edges fail explicitly. The document codec now uses this same geometry codec for recursive
Panel/Page tables. Runtime Bottom evidence:
`ev_7efcc3e0e5ae4378b70db7f71010f4fc`.

The editable package is:

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.*
```

Everything in this package must describe named `OrdinaryForm` concepts. If a
low-level platform value is needed for rebuild, promote it into a named concept
with a descriptor-backed serializer.


The Page table codec now handles named Name, localized Title, Visible and
Enabled properties in both directions, preserving order and validating
identity, counts and language uniqueness. Position and children remain separate
owner surfaces. Unsupported property variations are rejected. The root writer
uses this codec for its standard page. Current macOS and fresh Linux suites
pass 11/11, and strict Designer root reconstruction from XML and module passes:
`ev_de0a60c9770b4bfa911e743a39c8acab`,
`ev_0c9e829731ff45df8ec194f0d5464c58`.

A controlled Designer experiment identifies a tab-picture descriptor within
Page metadata. Clearing a standard picture removes its UUID and changes Bottom
constraints for all three pages, without changing saved rectangles or child
geometry. The complete descriptor is not implemented; such pages fail
explicitly. Evidence: `ev_1c4a7ce42e2d453e9479eaafa15bdad8`.
Recursive Panel/Page document serialization now supports the default Panel
style, named position and explicit child pages. Unsupported Panel properties,
page pictures and direct children without a Page are rejected.


Primary document serialization now supports explicit root Pages with named
metadata, localized titles, explicit positions and the existing four leaf
control codecs. Logical order is local to each Page; physical records are
sorted by control ID. Incoming dependency checks cover the root page graph,
including Form and cross-page control targets. DataPath and control events
remain attached by object identity. A single canonical default page has an
implicit representation, while any changed metadata or geometry remains an
explicit named Page. Unsupported styles, pictures and nested Panel controls
still fail explicitly through this same primary codec.

Current proof builds a new two-page XML and module without a source Form.bin,
loads it in Designer and performs a strict dump. Canonical XML and module match
exactly before and after the platform; rebuilding that XML produces an identical
Form.bin. macOS and fresh Linux tests pass 11/11. Evidence: `ev_5c2f6193f49143a5bb3332a79358e659`.
This establishes root-page integration, not complete recursive Panel support
or complete support for all properties of the four leaf control types.

## Проверенный рекурсивный кодек Panel, 2026-10-03

Основной путь читает и собирает вложенные Panel с явными Page. Поддержаны
Name и Position панели; Name, локализованный Title, Visible, Enabled и
Position страниц. Дочерние Button, LabelDecoration, строковый InputField и
Boolean CheckBox используют те же кодеки, что корневые элементы. Порядок
задается отдельно для каждой страницы; физические записи сортируются по ID.
Реквизиты, DataPath и события учитываются по всему дереву. Зависимости
проверяются в пределах владельца; ссылка на содержащую панель имеет
именованный targetId. Неподтвержденные варианты явно отвергаются.

Синтетический XML с двумя уровнями панелей собран без исходного Form.bin,
принят строгой выгрузкой Designer и повторно разобран. Канонические XML и
Module.bsl совпадают; повторная сборка дает идентичный новый Form.bin.
Независимый образец Designer с тремя страницами также прошел сборку из XML
и строгую выгрузку с неизменным каноническим XML. В публичном Module.bsl
используется LF, в потоке контейнера CRLF. Проверки macOS и Linux amd64:
11/11; независимая проверка границы модуля: 1/1. Доказательства:
`ev_2f5eb63c5307437cbb7a2ddc9a05909a`, `ev_0c8313118a2340f7ab9f1ac1804a974a`.

Покрытие типов: 5/25 частично (20%), 0 полностью. Это не процент готовности
продукта. Следующая серия исследует Button: одно измененное свойство на опыт
относительно исходного элемента, затем InputField и остальные типы.

## Текущий исполняемый набор свойств Button

Поддержаны Caption, Enabled, MultiLine, ToolTip, HorizontalAlign,
VerticalAlign, PictureLocation, PictureSize, BorderColor, ButtonTextColor и
ButtonBackColor, Font, Picture, MenuMode, Shortcut и Buttons, а также Name,
Position, подтвержденные Bindings и Click.
Выравнивания по умолчанию Center, подсказка пустая. Неподдержанные члены
перечислений и чужой тип, даже с default member, отвергаются при сборке.
Именованный XML с Right/Bottom и многострочной подсказкой прошел строгий
Designer цикл; XML, модуль и повторно собранный бинарник совпали.
Доказательство: `ev_ac8d0f224078451688420991ae1a0f4f`.

PictureLocation поддерживает Left и Right, default Left. PictureSize
поддерживает RealSize, Stretch, Proportionally, Tile, AutoSize и ByFontSize,
default RealSize. Right и ByFontSize прошли строгий Designer цикл и проверку
значений через объект кнопки в выполнении. Доказательства:
`ev_a042ad82e22642eabe0a0c117af4e6c1`,
`ev_364cb10e1ffd4ee38697aa5f43b6e274`.

Picture является ссылкой на PictureAsset с внешним файлом GIF, PNG, JPEG
или BMP в Form/Items/<ИмяЭлемента>/Picture.*. Именованный параметр
transparent задает прозрачный фон; по умолчанию false. CLI build читает
файл, dump извлекает его без изменения байтов. В XML нет байтов, base64
или внутреннего дескриптора. Отсутствующая картинка сохраняется без ресурса.
Стандартная библиотека поддерживается именованной ссылкой
`<Picture standardName="PictureLib.Write"/>`: доступны 294 канонических
имени из справки 8.5.1.1343. Они не создают файловые ресурсы. Ссылка
задает либо standardName, либо идентификатор PictureAsset, одновременно
оба значения запрещены. Общие картинки, коллекции, наборы вариантов и другие
дескрипторы пока явно отвергаются. Проверка сигнатуры определяет формат,
но не заменяет полную проверку исправности файла изображения.
Восемь сочетаний формата и прозрачности прошли выполнение и строгий
Designer цикл, XML, модуль, картинки и повторная сборка совпали.
Доказательство: `ev_0dad2a12da664bd1ba19ac13b4a3db68`.
Все 294 стандартные ссылки проверены чтением свойства самой платформой,
строгим Designer циклом и совпадением XML, модуля, файлов и Form.bin.
Доказательство: `ev_469ffac216e842bca054b2223ff569f1`.

Общий кодек цветов поддерживает непрозрачный RGB, automatic и именованные
ссылки из единого двустороннего реестра: десять ранее поддержанных
StyleColors, 146 проверенных WebColors и 25 WindowsColors. Ссылки сохраняют
платформенное имя, а не его текущее RGB. Полнота справочного каталога цветов
не утверждается; неизвестные имена и идентификаторы отклоняются.
BorderColor по умолчанию automatic, текст и фон кнопки ссылаются на
соответствующие цвета стиля. Альфа-канал явно отвергается. Проверены
15 отдельных присваиваний с обратным чтением и строгая сборка из XML;
12 getters созданной формы совпали с заданными значениями. Доказательства:
`ev_59bffaa579c3408aa501cc0716a4ae76`,
`ev_3f6d1fc6961e4480ab402ab91a64649a`.

Font поддерживает automatic, абсолютный шрифт с непустым faceName,
пять именованных WindowsFonts и StyleFonts.TextFont. Именованные поля height, bold, italic, underline,
strikeout сохраняют различие между отсутствием и явно заданным значением.
Масштаб scale и признак его переопределения scaleOverride независимы:
конструктор по описанию и конструктор по исходному шрифту могут давать
одинаковый масштаб с разным признаком переопределения. Сырая маска в XML
не допускается. Поддержаны размер с точностью до десятых и целый масштаб,
включая 0. Общий кодек сохраняет измененные высоту, начертание, гарнитуру
и масштаб также у automatic, WindowsFonts и StyleFonts.TextFont.
Другие ссылки стиля явно отвергаются. Исходные 11 значений проверены через объект кнопки
и строгий Designer цикл с совпадением XML, модуля и повторной сборки.
Доказательство: `ev_5ead98d8bd3f41838b7b9365624bdb14`.

MenuMode поддерживает DontUse (default), Use и UseExtra. Публичный XML
использует `<MenuMode type="MenuMode" member="UseExtra"/>`.
Кодек самостоятельно создает внутреннюю структуру пустого меню для двух
режимов с меню. Состав меню представлен отдельной именованной коллекцией
Buttons, описанной ниже. Неизвестные поля и внутренние форматы отвергаются.
Три режима проверены чтением свойства самой платформой и строгим Designer
циклом с совпадением XML, модуля, файлов картинок и повторной сборки.
Доказательство: `ev_f1ab4a6da7c24a8f9c601f4810aa0280`.

Shortcut является типизированным значением с именованной клавишей Key
и булевыми Alt, Ctrl, Shift. Поддержаны все 80 имен платформенной XDTO-схемы,
включая навигационные клавиши, и все восемь сочетаний модификаторов.
Например, `<Shortcut Alt="false" Ctrl="true" Shift="false"><Key>A</Key></Shortcut>`.
None без модификаторов является default и опускается в XML; None с
модификаторами сохраняется. Неизвестные клавиши, версии, флаги, поля
и сырые числовые атрибуты отвергаются. Каталог и XSD имеют одного владельца
в метамодели. Типизированный XML доступен на четырех ранее описанных
поверхностях Shortcut, но бинарный кодек в этой итерации расширен только
для Button. Полная обратная сборка 640 вариантов проверена самой платформой
и строгим Designer циклом с совпадением XML, модуля, файлов и Form.bin.
Доказательство платформенного результата: `ev_e13a60309c9d495f9a71c40408ef0b03`.
Заключительная проверка macOS и Linux проходит 11/11:
`ev_e25203fb73a94b68bac708ef95feea4c`.

Buttons содержит именованные CommandBarButton с типом Action, Submenu или
Separator. Поддержаны Name, Type, Text, Explanation, ToolTip, Enabled,
Checked, ChangesData, Representation, Shortcut, Picture, Action, Order и вложенные
Buttons. Action содержит обработчик, имя и локализованные Text, ToolTip и
Description; Separator допускает только ClientInterfaceVariant, Submenu может содержать
рекурсивную коллекцию. Имена уникальны в своей
коллекции. Это отдельные пункты меню, а не контролы ChildItems или глобальные
Commands. Русские имена находятся в appinfo OrdinaryFormPalette.xsd.

Внутренние идентификаторы действий и коллекций вычисляет основной кодек;
они не выходят в публичный XML и не требуют исходного Form.bin. Файловые
картинки пункта сохраняются в именованном пути
Items/<ControlName>/Buttons/<ItemName>/Picture.<format>; для вложенности
повторяются сегменты Buttons/<ItemName>. Выход из пакета запрещен.

Платформа проверила все именованные свойства 13 пунктов, включая два уровня
подменю, стандартную и восемь файловых картинок. Строгий Designer PASS,
XML, модуль, 16 файлов картинок и повторная Form.bin совпали побайтно.
Доказательство: `ev_5c2beb1afcd94946ac66c6b2ad1997ec`.
Свежие macOS и Linux проверки проходят 11/11:
`ev_50bee3be5b524b7191ab27622bd9b859`.

Order у Submenu принимает DontOrder, Ascending и Descending. Свойство
сохраняется в именованной модели и собирается основным кодеком; оно
недопустимо у Action и Separator. Свежий строгий Designer цикл с разными
Order у двух вложенных подменю сохранил XML, модуль, 16 файлов картинок
и Form.bin побайтно: `ev_6edd447ff1d04cc7afb27d6ef5953cd4`.

Ограничения этого шага явные: DefaultButton у Action внутри Button.Buttons
не меняет сохраняемые данные в повторном платформенном опыте; у отдельной
CommandBar контекст отличается: `ev_10332bde2d004bfb86df758564d7295c`.
Непустое меню при MenuMode DontUse отвергается: сама платформа удаляет меню
и запрещает доступ к Кнопки: `ev_287e571c861d453c82ea61695f1610ce`. В отдельной минимальной форме
подтверждено настоящее выполнение Button.Click и пункта Button.Buttons.Action
через клавиатуру в контейнере. Обработчики записали PASS|DirectButton и
PASS|MenuAction, без переприсвоения действий после загрузки формы:
`ev_67fec8ed7768429c9879869188f02168`.

Это 16/16 прямых свойств Button в перечисленных вариантах, не полная
поддержка типа. API GetPropVal
с вычислением default пока остается требованием целевой модели.


InputField.AutoChoiceIncomplete (АвтоВыборНезаполненного) поддержан для
текущего строкового профиля поля ввода. Default false, именованный XML
принимает true/false через общий слой свойств. Строгий Designer цикл
сохранил XML, Module.bsl и повторную Form.bin побайтно; getter загруженной
формы подтвердил true: `ev_21083e5a888b40a5973ea6f921d64923`.
Это расширяет текущие Enabled и ReadOnly, но не доказывает полную
поддержку InputField или остальных типов данных поля.


Для строкового InputField текущий сохраняемый логический профиль расширен
до 16 свойств: Enabled, ReadOnly, AutoChoiceIncomplete, AutoMarkIncomplete,
Wrap, ChooseType, MarkNegatives, ChoiceButton, OpenButton, ClearButton,
SpinButton, ChoiceListButton, Transparent, MultiLine, ExtendedEdit и
PasswordMode. Wrap и ChooseType по умолчанию true; Enabled true;
остальные свойства по умолчанию false. Каждое новое свойство исследовано
отдельным изменением, затем проверяется совместный вариант.

ChoiceIncomplete и MarkIncomplete являются состояниями времени исполнения
и не включены в сохраняемый профиль. TextEdit остается unclassified:
runtime сериализация сохраняет false, но документ Конфигуратора
нормализует его до true. Primary encoder отвергает явное TextEdit,
пока корректное документное сохранение не подтверждено. ListChoiceMode,
строковые свойства и другие типы реквизитов требуют следующих отдельных
опытов. Это частичная поддержка InputField, не готовность всего типа.

Подтверждение сохраняемого профиля: strict Designer, побайтные
XML/Module.bsl/Form.bin и 256 getters PASS,
`ev_0daca28cc3f34400918867a57ee0c378`. Отличие runtime сериализации TextEdit
от документного пути: `ev_7182607052084d37bd6a3e4fc26fc8b2`.


Строковый профиль InputField дополнен именованными ToolTip и Format.
Оба свойства по умолчанию пустые; явно заданная пустая строка
нормализуется к отсутствующему XML-свойству. Кодек использует существующие
операции локализованных строк. Format допускает пустую запись или одну
локализацию ru; другие варианты отклоняются без потери данных. ToolTip
поддерживает именованные переводы для 14 типов обычных элементов; редакторы
колонок таблицы сохраняют прежние ограничения. Unicode, экранирование XML и
переводы строк подсказки сохраняются. Designer 8.5.1.1343 может удалить пустой
перевод при выгрузке; это ограничение требует отдельной проверки для каждого
свойства и не считается доказанным полным совпадением XML.

Подтверждение: 108 getters после холодного открытия, строгий Designer
цикл и точные XML/Module.bsl/Form.bin PASS,
`ev_2bdc0ddae6254cb9996c92955e39b803`. Всего подтверждены 18 сохраняемых
свойств строкового поля. Сохранение Format не доказывает форматирование
Число или Дата, их профили пока не реализованы.


В нативном кодеке строкового InputField добавлены HorizontalAlign
(Left, Center, Right, Justify, Auto), VerticalAlign (Top, Center, Bottom)
и ChoiceListHeight (int32). Предварительные значения по умолчанию:
Auto, Top и 0. XML использует именованные свойства; ChoiceListHeight
валидируется как xs:int, дроби и переполнение явно отвергаются.

Статус этих трех свойств PARTIAL. Нативные тесты и точная повторная
сборка набора из 17 полей PASS: `ev_562bc4b6285f480bbd1ff5fdb0763ceb`.
Полные области значений и сохранение Конфигуратором не подтверждены:
оба запуска Designer остановлены отсутствием лицензии до опыта.
Доказательства: `ev_057d336011424892a233be3c7dc40689`,
`ev_07a32ab4ca6447cfba02ef7e25467fc4`.
Это 21 реализованное свойство текущего строкового профиля,
из которых платформой подтверждены 18. Ни отрицательные границы высоты,
ни ее дробная нормализация не объявляются поддержкой платформы.


Платформенная проверка HorizontalAlign, VerticalAlign и ChoiceListHeight
впоследствии завершена: строгий Designer цикл дает точные XML,
Module.bsl и Form.bin; 357 getters после нового открытия PASS.
Доказательство: `ev_3ad96e1591394018b80658d2a5820c99`.
Текущий строковый профиль содержит 21 подтвержденное сохраняемое свойство.
Предыдущий статус PARTIAL трех свойств снят, сам тип остается PARTIAL.
Высота в XML является нормализованным int32: дроби и переполнение
отвергаются. Setter платформы отдельно показал усечение 1.5 до 1
и оборачивание значений за границами int32; XML не воспроизводит
эти неявные преобразования.


LabelDecoration поддерживает Enabled (default true) и ToolTip
(default empty), CheckBox поддерживает ToolTip (default empty).
Текст сохраняет Unicode и переводы строк; текущая область локализации
допускает пустую запись либо один язык ru. Несколько языков и неверные
записи явно отвергаются без потери данных.

Нативные тесты macOS, строгий Designer, точные XML/модуль/Form.bin
и 18 getters после нового открытия семи контролов PASS:
`ev_c0900fb5429b490c98226f3b852aa9dc`. Это поддержка трех отдельных
свойств, не полное покрытие LabelDecoration или CheckBox.

## Основной кодек CalendarField, ProgressBar и PictureDecoration

Три изолированных патча объединены с существующими свойствами надписи,
флажка и поля ввода. CalendarField сохраняет Enabled; ProgressBar и
PictureDecoration сохраняют Enabled и ToolTip. Default Enabled true и
ToolTip empty нормализуются как отсутствующие свойства. Position и Visible
используют общий кодек геометрии. Эта начальная партия не включала даты,
данные индикатора и содержимое картинки; актуальные расширения описаны ниже.

Независимые записи платформы выявили пропущенные части ранних кодеков
индикатора и картинки. Исправлены полные внутренние записи default-свойств;
они не выводятся в публичный XML и не копируются из исходного Form.bin.
Тесты читают независимые записи: Calendar сохраняет исходную геометрию,
Picture меняет только ID и имя для тестовой формы с предыдущей кнопкой;
Progress проверяет исходные свойства в адаптированном геометрическом
контексте. Неизвестные значения и события отклоняются без fallback.

Свежие XML-only формы: три календаря, четыре индикатора и четыре поля
картинки. Строгий Designer сохранил точные XML, модуль, Form.bin и нулевой
набор ресурсов. Отдельные загрузки дали 30 PASS проверок свойств. Ошибка
раннего тестового XML перенесла Hidden.Visible в Default; пересоздание
форм по именам устранило ошибку проверки, кодек геометрии не менялся.
Нативные тесты macOS 11/11 PASS. Доказательство:
`ev_21d9b6ab4e284348a1987e84d030db75`. Общая цель PARTIAL: 8/25 типов
частично (32%), 0 полностью.

Итоговая проверка этой партии: macOS 11/11 и Linux amd64 Release 11/11
PASS, независимый просмотр текущего кода и платформенных результатов PASS.
Доказательство: `ev_5221cb1f10f042b484fbfb810686046b`.

## Выравнивание, шрифт, стандартная картинка и границы индикатора

| Именованное свойство | Редактируемый контракт | Проверка |
|---|---|---|
| LabelDecoration.HorizontalAlign | Auto, Left, Center, Right; Justify явно отклоняется | Полные независимые записи, строгий Designer, 8 getter-проверок четырех надписей |
| CheckBox.Font | Существующий FontValue; automatic и absolute; явно false отличается от отсутствующего поля | XML-only, строгий Designer, полное сравнение шрифтов трех флажков после загрузки |
| LabelDecoration.Font | Общий FontValue и общий блок свойств контрола; automatic, absolute и StyleFonts.TextFont | Строгий Designer, повторные XML/модуль/BIN, getter равен штатному конструктору платформы; ev_7b3b800f167242b8ae4db92c867390e2 |
| PictureDecoration.Picture | Стандартная PictureLib reference без внешнего ресурса | XML-only, строгий Designer, Empty/Write/ExecuteTask getters |
| ProgressBar.MaxValue/MinValue/Step | Только int32, defaults 100/0/1; дроби и переполнение отвергаются | Обе границы диапазона в XML и потоке; 18 getters шести индикаторов |

Используются существующие дескрипторы, типизированные значения и единый
сериализатор. Сырые потоки и baseline не появляются в публичном XML.
Значения по умолчанию опускаются. Строгие XML/module/asset/rebuild циклы,
macOS 11/11 и Linux amd64 Release 11/11 PASS; независимые проверки кода
не нашли дефектов. Доказательство: `ev_0ee9debb8425427190f119dc979e5813`.

Неподдержанные события и данные индикатора отклоняются;
эти ограничения не заменяют полную цель. Охват остается 8/25 PARTIAL
(32%), 0 FULL.

## Внешняя картинка PictureDecoration

PictureDecoration.Picture использует тот же PictureRef и PictureAsset,
что кнопка. Ресурс хранится в Form/Items/<ElementName>/Picture.gif;
XML содержит именованную ссылку и описание ресурса, байты в XML не
встраиваются. Чтение извлекает файл и сохраняет признак прозрачности.
Запись получает ресурс из документа, а не из исходного Form.bin.

Полная сериализация формы подтвердила изменение только внутренней записи
картинки и счетчика. После восстановления запись картинки совпала.
Свежая XML-only сцена одновременно содержит Empty, PictureLib.Write,
внешнюю GIF и прозрачную GIF. Строгий Designer сохранил точные XML,
Module.bsl, два ресурса и повторно собранный Form.bin. После запуска
четыре getter-сравнения прошли, обе выгруженные GIF совпали с исходными
байтами. macOS и Linux amd64 Release: 11/11 наборов PASS; независимая
приемка кода PASS. Доказательство: `ev_fad2f43a38b04fc2b284e9266769b01f`.

Доказанный внешний формат этой сцены: GIF. Другие форматы используют
общий существующий кодек картинки, отдельного платформенного подтверждения
PictureDecoration для них эта партия не дает. Общий охват остается
8/25 PARTIAL (32%), 0 FULL.

## Числовая привязка индикатора и совместная форма

ProgressBar.DataPath поддерживает прямую ссылку на локальный Attribute с
единственным числовым TypeDomain. Используются существующие числовые
квалификаторы и единая таблица связей. Индикатор без DataPath остается
допустимым. Неизвестный реквизит, составной путь, UUID метаданных и
нечисловой или составной домен явно отклоняются без fallback.

Свежая XML-only сцена содержит связанные и свободные индикаторы вместе
с полем ввода и флажком. Строгий Designer сохранил точные именованные XML,
модуль и числовые квалификаторы 10/2/nonNegative. Повторная сборка XML
совпала с исходной XML-сборкой Form.bin. Сырой контейнер, выгруженный
Designer, отличается байтами; его точное совпадение не заявляется.
Холодная загрузка подтвердила четыре имени связей и числовой тип значения.
После записи Amount=37 реквизит равен 37, значение индикатора до открытия
формы остается 0. При естественном открытии формы запись Amount=37 в
ПередОткрытием дает значение индикатора 37 в ПриОткрытии. Запись через
значение индикатора также меняет Amount; восстановление возвращает оба
значения к 0. Действия для проверки назначены динамически, поддержка
сохраняемых событий формы этим не заявляется. Доказательство жизненного
цикла: `ev_fd69f2907d084ace85fa08666835a51c`. Чтение квалификаторов
в рантайме не проверялось. Доказательство: `ev_df4033cc188248018944b06688bd731b`.

Отдельная смешанная форма восьми поддержанных типов с вложенной страницей,
кнопкой и внешней GIF прошла строгий цикл XML/module/assets/rebuild и
холодную проверку. Проверка InputField и CheckBox записывает значение через
элемент и проверяет обновление связанного реквизита. Немедленное обратное
обновление буфера элемента при записи реквизита до открытия формы не
включено в доказанный контракт. Для строки учитывается явно заданная
фиксированная длина 64. Нативные наборы macOS и Linux amd64 Release:
11/11 PASS; независимая проверка кода связей PASS. В наборах модель/поток/XML
теперь 90 именованных функций-сценариев вместо 89. Итоговая проверка:
`ev_377a460b1e9245c2a1f706a5f9be91dd`. Охват остается
8/25 PARTIAL (32%), 0 FULL.

## Начальный кодек RadioButton

| Свойство | Поддержка | Проверка |
|---|---|---|
| Caption, Enabled, ToolTip | Именованные свойства; defaults empty/true/empty | Независимая полная запись, отдельные изменения и восстановления, холодные getters Unicode |
| Position, Visible | Общая геометрия | Строгий Designer и холодный Visible=false |
| DataPath, значение выбора группы, события | Не поддержаны; явный отказ | Отрицательные проверки кодека |

Свежая XML-only сцена трех переключателей прошла строгую выгрузку и
холодный запуск. Публичные схемы уже содержат именованные свойства и
совпадают с генерацией новой метамодели. Полные нативные наборы: 11/11
PASS; независимое ревью полной записи PASS. Доказательство:
`ev_ce17ee3d14044deaa51a7c81882510bb`. Охват типов: 9/25 PARTIAL (36%), 0 FULL.

Совместная XML-only сцена девяти типов после объединения переключателя
и числовой привязки индикатора прошла строгий Designer, точные сравнения
XML/модуля/одного ресурса и повторной XML-сборки, затем холодные проверки
свойств и трех связей. Нативные наборы 11/11 PASS; модель/поток/XML:
20+44+27 = 91 именованный сценарий. Доказательство:
`ev_5df9a5d6ecc4452495452bc78f769922`.


## Верхняя граница идентификаторов формы

Designer принимает и сохраняет заголовок, в котором верхняя граница ID больше
максимума объявленных контролов и реквизитов. Собственный парный опыт меняет
только это поле; после открытия публичные привязки совпадают с базовой формой.
Reader принимает такую границу только ниже UINT32_MAX, с OOF1140 и
`reconstructionComplete=false`. Значение ниже существующего ID, переполнение
и неверный тип остаются ошибками.

Синтетические ID страниц назначаются после максимума объявленных объектов.
Явная таблица замен обновляет страницы и PageRef до добавления в документ;
события и ресурсы получают ID после страниц. Идентификаторы контролов и
реквизитов не меняются. Writer вычисляет заголовок из именованной модели,
не сохраняет исходную дополнительную границу. Повторные XML, страницы,
события и ссылки стабильны. Проверка: ev_6eda18a93c6641ef91be32d8dd4958fe.

## Дополнительная входящая зависимость CommandBar

Для CommandBar непосредственно на форме подтверждена допустимая платформой
запись: при основной привязке Bottom к Form.Bottom без пропорциональной цели
входящий список Top дополнительно содержит одно собственное ребро Bottom.
Парные собственные формы отличаются только этим ребром, обе проходят строгий
Designer и возвращают одинаковые публичные привязки после открытия.
Designer сохраняет дополнительную запись. Свидетельство:
ev_e4f8b34c16804f0f84cc9c6fdad4b812.

Reader разрешает только эту форму записи с OOF1140 и
`reconstructionComplete=false`. Основная именованная привязка сохраняется;
writer строит входящий граф из именованных привязок без сохранения остатка.
Это неполная именованная проекция, не доказательство точного восстановления.
Все остальные входящие ребра, ссылки, счетчики и структура проверяются строго.

## Прямые привязки CalendarField и TrackBar

Оба контрола используют общий `DataPath` и таблицу связей реквизитов.
Связь необязательна. CalendarField принимает один тип Date с датой и временем,
TrackBar принимает один Numeric с длиной 10, точностью 0 и запретом
отрицательных значений. Вложенные пути, отсутствующие реквизиты и другие
квалификаторы отклоняются. Другие типы пока не подтверждены.

Собственная форма с обеими привязками прошла строгий Designer. Повторные XML
и модуль совпали, сборка повторного XML дала исходный бинарный результат
нашего writer. Побайтовое равенство контейнера Designer и нашего writer
не заявляется. Свидетельство текущей проверки указано в плане завершения.

## Начало периода CalendarField

BeginOfDisplayPeriod сохраняет локальную дату с точностью до секунды или
Неопределено. Именованные DateValue и UndefinedValue используются без
исходного Form.bin. Неопределено является значением по умолчанию и
опускается в каноническом XML. Кодек и XSD отклоняют часовой пояс, дробные
секунды и явную дату 0001-01-01T00:00:00, совпадающую с маркером пустого
значения. Общий тип даты сохраняет эту раннюю дату для других свойств.

Отдельная свежая XML-only сцена прошла два строгих цикла Designer и
холодное чтение даты, времени, явного и отсутствующего значения
Неопределено. Именованный XML и модуль совпали; различие контейнера
ограничено счетчиком ревизии и служебными временными отметками.
Доказательство: `ev_70c476b87c874b4fb49968e9917fd508`. Реальная проверка
libxml2 подтвердила 9 допустимых и 28 недопустимых вариантов схемы:
`ev_32e7076e10f04a33bd54044ae10c818e`.

Год 4000 принят конструктором Дата и началом периода; значение сохранено
в подтвержденном поле потока и восстановлено. Ограничение литерала даты
языка запросов 3999 сюда не переносится. Верхняя граница года платформы
не установлена; четыре цифры являются ограничением формата.
Доказательство: `ev_0f651290685043838d2f3dd9886797cf`. Эта проверка предшествовала
интеграции CommandBar. Текущий охват типов
составляет 10/25 PARTIAL (40%), 0 FULL; добавление свойства CalendarField
не увеличивает число подтвержденных типов.

## CommandBar: кнопки и действия

| Понятие | Контракт | Проверка |
|---|---|---|
| Enabled, ToolTip, Position | Именованные свойства панели и общая геометрия | Две панели, Unicode, строгий Designer и холодные getters |
| Buttons, Action, подменю | Общая типизированная модель меню; действие принадлежит кнопке | Независимая полная запись, пары владельцев и ID, вложенность, сохранение трех обработчиков |
| Secondary | Булево, по умолчанию true; false задает основную панель | Собственные независимые изменения свойства и холодная загрузка |
| CommandBarButton.DefaultButton | Единственное действие верхнего уровня на основной панели; вложенное действие, Separator, Submenu и Button.Buttons отклоняются | Сборка из XML, холодные getters, строгий Designer, повторный XML и модуль |
| Вызов действия, остальные свойства и события | Пока не подтверждены | Не заявлять полную поддержку панели |

Используются существующие XML и меню кнопки, публичная схема описывает
Buttons как CommandBarButtonsType. Доказательства:
`ev_85604d13606142ebb92db2cce54fa289`, `ev_7202bb5ad7e04b6b9b3a935d879cf2ad`.

Совместная XML-only сцена десяти типов прошла строгий Designer и холодные
проверки: вложенная страница/кнопка, меню командной панели, три привязки и
один внешний GIF. XML, модуль, ресурсы и повторная сборка совпали.
Нативные наборы 11/11 PASS; модель/поток/XML: 20+46+28 = 94 сценария.
Сохранение Action проверено, фактическое нажатие кнопки не проверялось.
Доказательство: `ev_4060308b9ad8474aac691623e66eecd7`.

DefaultButton является свойством конкретного CommandBarButton, а не сырым
идентификатором формы. Ссылка заголовка формы вычисляется по владельцу действия,
ID пункта вычисляется по обходу меню. UUID источника действий меняется при
загрузке платформой и не входит в публичный XML. Несогласованная пара ссылки,
отсутствующий пункт и неверный владелец отвергаются при чтении.

Текущий доказанный платформенный цикл DefaultButton использует явный Text
действий: первое действие true, второе false; XML и модуль после строгой
выгрузки совпадают. С пустым Text платформа меняет локализацию заголовка,
повторный разбор пока завершается OOF1115. Это открытое ограничение, а не
успешный полный цикл. Свидетельство: ev_1e73b334e3b241179a5bf1f7099c2907.

## SpreadsheetDocument, Field и вложенные редакторы

Сериализуемый XDTO SpreadsheetDocument представляет документ отдельно от
визуального Field. Field предоставляет свойства и методы документа.
`Range.SetControl` назначает редактор диапазону, а `Column.Control` возвращает
InputField, ChoiceField или CheckBox. Свидетельство:
`ev_861809b639e44e988e50f0bc546c98ed`.

Табличный документ содержит собственную сетку ячеек, области, оформление,
объединения, группировки и настройки печати. Схема платформы
`schemas/platform/8.5/moxel/moxel_root-69-http_v8.1c.ru_8.2_data_spreadsheet.xsd`
разделяет содержимое Cell, Format, ViewSettings и другие объекты документа.
Внутренний Cell.control имеет тип base64Binary; публичный XML должен описывать
редактор именованным типом и свойствами, а не копировать этот бинарный блок.

Нужно различать три связи:

- редактор значения ячейки или области SpreadsheetDocument;
- редактор колонки Table, доступный через Column.Control;
- элемент формы, встроенный в ячейку поля табличного документа или закрепленный
  за ее геометрией. Методика платформы описывает эти два режима размещения:
  https://its.1c.ru/db/content/metod8dev/src/developers/platform/metod/spreadsheet/i8102592.htm

Их поддержка не доказывается поддержкой текста ячеек. Начальные профили Table
с редактором InputField по умолчанию и SpreadsheetDocumentField с текстовыми
ячейками остаются частичными до отдельной проверки остальных редакторов,
типизированных значений, свойств, событий и размещения.

Выделение областей меняется в среде выполнения и точно восстанавливается
в снимке контрола без изменения ячеек (`ev_d2ced7dc39ad4b7eaf16884adb38a5fd`).
Однако заданные в исходном XML две области и текущая ячейка R2C3 не сохранились
при холодном открытии ни исходной сборки, ни сборки после строгой выгрузки:
оба варианта открылись с единственной областью R1C1. Содержимое четырех ячеек
сохранилось. Точная граница сброса между загрузкой и открытием не установлена
(`ev_799482ef2677413daaf207e87689bf00`).

Поэтому публичная SelectionArea не входит в текущий доказанный контракт
сохраняемой формы. Это не запрет на будущую поддержку настроек вида:
платформенная XSD содержит saveViewSettings и viewSettings, но их связь
с сохранением выделения именно в обычной форме еще требует отдельной проверки.
Текущий кодек должен собирать проверенное состояние по умолчанию и отклонять
неподдержанные варианты без сохранения сырых записей или неявного fallback.

## Контекст формы обработки, 5 октября 2026

Стандартное расширение формы обработки представляется именованным
`DataProcessorFormExtension`, а связь с основным реквизитом формы определяется
`MainAttribute attributeId`. Это отдельная связь, она не выводится из флагов
реквизита. Конкретный объект метаданных имеет `TypeDomain.Entry term="object"`
с обязательным `typeUuid`. Он отличается от ссылочного типа `reference`.
`unknown` с UUID больше не является альтернативным путем сборки объекта.

Текущий двунаправленный кодек поддерживает стандартные настройки расширения,
один выбранный объектный реквизит и стандартные флаги этого реквизита.
Отсутствующий основной объект, ссылочный или составной тип, нестандартные
флаги и неизвестное расширение отвергаются при чтении и сборке. Настройки
расширения не публикуются индексными полями или скрытым потоком.

Пример структуры, UUID типа нужно брать из GeneratedType.TypeId соответствующего
объекта метаданных:

```xml
<MainAttribute attributeId="2"/>
<DataProcessorFormExtension/>
<Attributes>
  <Attribute id="2" name="ProcessorObject">
    <TypeDomain>
      <Entry term="object" typeUuid="11111111-1111-1111-1111-111111111111"/>
    </TypeDomain>
  </Attribute>
</Attributes>
```

Расширения других объектов и нестандартные настройки формы обработки пока
не поддержаны. Значение старого флага Attribute.Main не заменяет MainAttribute;
его полная платформенная семантика требует отдельного подтверждения.

Платформенный цикл нового контекста: основной объект создан, native и строгий
Designer PASS, канонический XML, модуль и ресурсы совпали. Побайтовое равенство
после сохранения Designer FAIL из-за изменяемого внутреннего счетчика;
в изолированном опыте это единственное различие потока. Поправка +1 для
расширения была проверена и отвергнута, поскольку 4 также становится 5.
Свидетельство: ev_69e50e16ad99422c8f7c2275bbd4afd6.


## Главная панель формы и общие свойства Panel

Form.Panel является постоянным типизированным PanelPayload. У главной панели
нет отдельного ID или имени, она не добавляется в коллекцию контролов.
Form.ChildItems остается единственным владельцем корневых страниц и элементов.
Публичный XML использует вложенное свойство:

```xml
<Panel>
  <AutoTabOrder>false</AutoTabOrder>
  <BorderColor kind="styleReference" styleName="StyleColors.BorderColor"/>
  <TextColor kind="absolute" red="44" green="55" blue="66"/>
  <BackColor kind="absolute" red="77" green="88" blue="99"/>
</Panel>
```

Те же четыре свойства поддержаны у обычного контрола Panel. По умолчанию
AutoTabOrder=true, цвета automatic; значения по умолчанию можно опускать.
Главная и вложенные панели имеют независимые PropertySet. Отдельный тип XSD
FormPanelType допускает только доказанные свойства, без ID, имени, Events
и ChildItems. Неизвестные свойства не теряются при сериализации, а отклоняются.

Цветовой кодек повторно использует существующие automatic, непрозрачный RGB
и известные именованные стили. Отдельный native опыт подтвердил
StyleColors.BorderColor; внутренний ID -22 в XML не выводится. Повтор цвета
фона в потоке вычисляется из BackColor, несогласованный повтор отвергается.
Неподдержанная прозрачность или неизвестный стиль отклоняются явно.

Сборка из XML без исходного BIN, native getters главной и вложенной панелей,
строгая выгрузка Designer и равенство XML/модуля проверены для RGB и стиля.
Свидетельство: ev_c6e491c3a4774324882a0f00c2d79519. Независимое ревью и свежий
CTest: ev_147e669863cf40b8a1345742cf99e470. Это не полная приемка Panel:
остальные свойства, события и всеобщий обратный цикл остаются открытыми.


## Цвета CommandBar

CommandBar поддерживает именованные BorderColor, ButtonTextColor и BackColor.
BorderColor и BackColor по умолчанию automatic; ButtonTextColor по умолчанию
StyleColors.ButtonTextColor. Явный automatic для текста отличается от этого
стилевого значения и сохраняется. Имя TextColor для CommandBar не используется.
Все три свойства используют существующий ColorValueType и общий цветовой кодек.

Непрозрачный RGB и известные именованные стили проверяются при сборке;
неизвестные стили, alpha и неверный тип значения отклоняются. Именованный стиль
StyleColors.BorderColor подтверждено платформой для рамки CommandBar.
Сборка только из XML, getters после загрузки, строгий Designer и равенство
канонического XML/модуля проверены: ev_d97633f9fe8d4c6f8594caa32b0f9414.
Остальные свойства оформления CommandBar этим разделом не объявлены готовыми.


## Типизированная рамка Border

BorderValue является именованным значением PropertyValue. Абсолютная рамка
содержит ControlBorderType и целую толщину; стилевая рамка использует
существующий StyleReference. Примеры публичного XML:

```xml
<Border kind="absolute" borderType="Double" width="2"/>
<Border kind="styleReference" styleName="StyleBorders.ControlBorder"/>
```

Absolute требует borderType и width и не допускает style attributes;
styleReference требует ссылку и не допускает абсолютные поля. Пустые ссылки,
неизвестные имена типов, лишние атрибуты, элементы и текст отвергаются.
Default CommandBar.Border: absolute, WithoutBorder, width=0, без ссылки.
Он опускается в XML; default конструктора платформы width=1 этим не заменяется.
Толщина хранится целым числом 0..5, WithoutBorder допускает максимум 1.

Общая модель/XSD знает девять ControlBorderType. CommandBar поддерживает
WithoutBorder, Single, Double, Embossed, Indented, Underline, DoubleUnderline
и Overline. Rounded у CommandBar явно отклоняется: native setter оставляет
предыдущее значение. В общей модели Rounded требует ширину 1 для будущих
доказанных контекстов. Это не объявляет другие контролы готовыми к Border.

CommandBar использует descriptor-backed serializer без исходного BIN.
Поддержаны абсолютные рамки и доказанный StyleBorders.ControlBorder.
Неизвестные стили, фабрики и состояния не заменяются на default. Собственные
native setters, XML-only сборка, getters, строгий Designer и равенство
канонического XML/модуля проверены: ev_5583621bb36a4f63ba0f790fb51727d5.
Для двух записей из эталона (Underline/1 и Overline/1 в состоянии создания)
подтверждено восстановление через официальный XDTO: целая Border имеет
именованное начертание, хотя исходный getter ТипРамки возвращает Неопределено.
ПрочитатьXML и назначение Рамка восстанавливают enum без изменения XDTO целой
рамки. Decoder принимает только эти точные записи, encoder собирает обычную
именованную рамку. Исходный аномальный getter не сохраняется; контракт здесь
является канонизацией именованного XDTO значения, а не равенством всех getter.
Другие сочетания отсутствующей фабрики отклоняются. Свежая сборка только из
XML, getters, строгий Designer и совпадение повторных XML/модулей для обоих
вариантов: ev_bbb81122b09942cbb28bd51a53f6b545. На момент этого опыта разбор
эталона тогда остановился на следующем поле CommandBar. Позднее все 38
контролов стали читаться в именованную проекцию; актуальное состояние указано
в начале документа. Эта история не утверждает полного восстановления формы.


## Канонизация состояния создания CommandBar

Два наблюденных внутренних состояния записи CommandBar, 1 и 2, представляют
одинаковые именованные свойства в проверенных сценариях. При загрузке
Enterprise переводит 1 в 2; строгий Designer может сохранять 1. Публичный XML
не содержит это поле. Decoder принимает только эти точные значения, encoder
выводит каноническое 2, остальные части записи проверяются строго.

На собственных пустой панели и первичном меню с DefaultButton проверены
официальные getters, назначение ToolTip, реальная активация Enter, свежая
сборка только из XML, strict Designer и точное равенство канонических
XML/модуля: ev_8a2f4c85b1a844e398b2e9bfc1c1df3b,
ev_c752edff621447a3b6c7f3f32ed56f2c. Это ограниченное правило нормализации,
оно не разрешает другие значения поля или неизвестные свойства CommandBar.


## AutoFill, Transparent, ButtonBackColor и два enum CommandBar

CommandBar хранит AutoFill и Transparent как bool с default false,
ButtonBackColor как ColorValue с default automatic. ButtonsAlignment имеет
тип CommandBarButtonAlignment, члены Left, Center, Right и default Left.
Orientation имеет тип Orientation, члены Auto, Horizontal, Vertical и
default Auto. Default значения опускаются при каноническом выводе XML.

```xml
<AutoFill>true</AutoFill>
<ButtonsAlignment type="CommandBarButtonAlignment" member="Center"/>
<Orientation type="Orientation" member="Horizontal"/>
<Transparent>true</Transparent>
<ButtonBackColor kind="absolute" red="11" green="44" blue="77" alpha="255"/>
```

Модель и XSD проверяют точный type/member двух enum только на поверхности
CommandBar. Неподтвержденные числовые коды, неправильные bool и неподдержанная
прозрачность RGB отвергаются кодеком. Shared ColorValue и обработка остальных
контролов не заменены отдельным форматом или fallback.

Проверены 72 сочетания свежей сборки из XML, три cold native варианта,
строгий Designer и точное равенство повторных XML/модуля:
ev_0f3bc8e79b0e41248bd8ff6318a0f28f. Независимое ревью и CTest
11/11 PASS: ev_5589b98b384a4d8bb57e223c75885ca0. Это подтверждает перечисленные
варианты CommandBar, но не все его свойства. В той последовательности работ
следующим оставался общий контракт записи метаданных элемента.


## CommandBar.ActionSource, подтвержденный контракт 2026-10-05

Источник действий выражается именованной ссылкой в расширении CommandBar:

```xml
<ActionSource formId="1"/>
<ActionSource controlId="7"/>
```

Отсутствие свойства или пустой ActionSource означает исходное Undefined.
Ссылка на форму обязана указывать на текущую Form. Ссылка на контрол обязана
разрешаться в Table или HTMLDocumentField. Эти два типа приняты платформой
в отдельной проверке всех 25 типов в состоянии по умолчанию; остальные
23 типа отвергнуты. Другие состояния и внешние ActiveX этим опытом не доказаны.

Внутренний кодек отображает Undefined в UINT32_MAX, ссылку на форму в 0,
ссылку на контрол в его ID, строго меньше UINT32_MAX. Это поле metadata[2],
не таблица ContextMenu и не признак FirstInGroup. Decoder после обхода
графа проверяет разрешение ссылки и тип источника. Неизвестные соседние поля
продолжают отвергаться. Сырые индексы не входят в публичный XML.

XSD ограничивает структуру и типы ID; взаимоисключение formId/controlId
и разрешение ссылок дополнительно проверяют XML reader и объектная модель.
Присвоение Undefined через runtime setter после Table отвергнуто платформой;
контракт CLI подтвержден свежей сборкой состояния Undefined из XML,
без обещания такой операции над уже открытой платформенной формой.

Четыре варианта из именованного XML прошли cold getters, строгий Designer
и точное сравнение канонических XML и модуля после повторного разбора.
Фактура: ev_0be98646c09244de8aac2ce0b22899ce,
ev_43f9a2e5b728452bbffa1c37b9e77072.


## Именованное действие меню, частичный контракт 2026-10-05

CommandBarButton.Action использует единственный структурированный формат:

```xml
<Action handler="RunHandler" name="MenuAction">
  <Text><Item language="ru">Выполнить</Item></Text>
  <ToolTip><Item language="ru">Запустить обработку</Item></ToolTip>
  <Description><Item language="ru">Выполняет выбранную операцию</Item></Description>
</Action>
```

handler обязателен и непустой; name обязателен и допускает пустую строку.
Три локализованных значения обязательны, пустое значение выражается пустым
элементом. Переиспользуется LocalizedStringValue, язык не заменяется именем
обработчика. Старый текстовый Action не принимается. Button.Click сохраняет обработчик при допустимых отличиях имени и
локализованных подписей Action от handler; непредставленные metadata
дают OOF1140 и неполное восстановление. Form.OnClose сохраняет строгую
политику пустых metadata.

Text, ToolTip и Explanation самого CommandBarButton остаются отдельными
свойствами. Отсутствие элемента и явно пустой элемент различаются, включая
флаги хранения. Подписи действия не подменяются переопределениями кнопки.
Публичного представления сырой записи или запасного пути сборки нет.

Собственный XML с тремя независимыми подписями действия и явно пустыми
переопределениями кнопки прошел сборку EPF, строгий экспорт Designer
8.5.1.1343 и повторный dump. Канонические XML и Module.bsl совпали точно.
Свидетельство: ev_b9d6405cb8d04d7bbae6eb3867838125.

При отсутствии явного Text decoder принимает пустое локализованное значение
либо одну непустую вычисляемую подпись с языком #. Она не становится
переопределением Text. Writer сохраняет отсутствие свойства и пустое
вычисляемое значение; платформа восстанавливает подпись от Name. Смена
handler эту подпись не меняет. Подпись не нормализуется к Action.Text.
Произвольные языки, несколько подписей и лишние поля отвергаются.
Свидетельство собственных опытов: ev_566434fc892443c8b4aec4acd3139ffb.
Полный собственный цикл без явного Text прошел строгий Designer и повторный
dump, XML и Module.bsl совпали точно: ev_0ff1168f57b54032a4a4dab3c97d06b6.

CommandBarButton.ClientInterfaceVariant хранит именованный контекст оформления
команды: Version8_0 и Version8_2_OrdinaryApp. Последний является значением
по умолчанию и опускается в XML. Это контекст интерфейса IClientInterfaceForCommand,
а не подтвержденное свойство объекта CommandBarButton в языке 1С.
Соответствие двум наблюдаемым числовым значениям основано на наследовании
интерфейса платформы, использовании поля при выборе оформления, перечне
ClientInterfaceVariant в платформенной XSD и собственном сохранении Designer.
Другие варианты явно отвергаются, не заменяются значением по умолчанию.

Контекст ClientInterfaceVariant применим также к Separator. Его значение
Version8_0 проходит именованный XML и повторное сохранение Designer; default
Version8_2_OrdinaryApp опускается. Поле Representation у Separator не является
активным свойством: getter Отображение возвращает Неопределено для всех четырех
сохраненных вариантов и обоих контекстов интерфейса. Нестандартное известное
значение читается с OOF1140 и reconstructionComplete=false; writer строит
каноническое состояние из именованной модели. Неизвестное значение перечисления
остается ошибкой, Representation в XML Separator запрещен. Собственный опыт:
ev_4476d09c4c8945faaa0bbeafa8580317. Каноническое меню из восьми Separator
прошло строгий Designer, XML, модуль и повторный BIN совпали:
ev_55e71f4cc77d47658558a4feb9ef43cf. Полный эталон после этой правки прошел
строгий Designer и повторную сборку именованной проекции:
ev_87eeb5b904f74e03b7392b2bb3692d34.

Собственное меню, сохраненное GUI, прошло dump, сборку только из XML и модуля,
загрузку EPF и строгую выгрузку Designer 8.5.1.1343. Повторные канонические
XML и Module.bsl совпали точно. Свидетельство:
ev_7777ea230a9949fb898a315cdac481d1. Второе поле контекста пока принимается
только в доказанном состоянии по умолчанию.

На момент этой проверки общий контракт оставался PARTIAL. Разбор авторского
эталона проходил проверку меню, затем останавливался на OOF1106 по пути
$/1/2/2/2/2/1/7/7/7. Диагностика указывала точное поле записи действия вместо
общей коллекции меню. Назначение второго поля контекста требовало проверки.
Font и остаточная запись самого Action были подтверждены только в доказанном
состоянии по умолчанию. Позднее эталон стал читаться как неполная именованная
проекция; актуальное состояние приведено в начале документа.


## Чтение и сборка с предупреждениями

По актуальному пользовательскому контракту неполное покрытие свойств не
должно само по себе запрещать чтение или сборку. Известные именованные
свойства сохраняются; непредставленные данные сопровождаются предупреждением.
Сборка создает файл из доступной объектной модели, предупреждая о данных,
которые не удалось восстановить. Такой результат не подтверждает точного
восстановления исходной формы. Сырые поля, baseline и синтетические контролы
не являются способом восполнения данных.

Корневой атрибут reconstructionComplete=false обозначает состояние
восстановления источника. Это диагностические сведения документа, а не
свойство платформенного контрола или переключатель сериализаторов. При
полном восстановлении атрибут опускается. XSD разрешает и проверяет атрибут;
парсер XML переносит его в модель. Сборщик не блокирует false, а возвращает
предупреждение. CLI пишет успешный результат и диагностику в одном JSON.

`Form.Style` сохраняется основным кодеком `root_record` как `StyleValue`.
Исходное значение Auto представлено пустым `StyleValue{}`; в XML оно
опускается. `<Style/>` явно задает Auto, а
`<Style styleId="b36c7fda-5c22-4377-bbab-edc7b2bfb412"/>` ссылается на
именованный стиль конфигурации. Модель проверяет канонический ненулевой UUID,
но не разрешает внешний каталог стилей. Определение цветов и шрифтов
принадлежит конфигурации и не копируется в XML формы.

На 8.5.1.1343 проверены 12 назначений именованного стиля открытой форме,
12 копий с сохранением этого стиля и 12 сбросов в Auto. Именованный стиль
создан в Designer с измененным цветом. Отдельные 12 форм, собранные из
именованного XML, подтвердили getter без назначения Style после загрузки.
Строгая выгрузка Designer сохранила XML и модули всех 12 форм побайтово.
Нативное назначение именованного стиля неоткрытой копии ValueFromString
завершалось SIGSEGV; успешные проверки назначения относятся к открытой форме.
Свидетельства: `ev_615fa1ced2204e2483d0fb8f3883fbf0`,
`ev_ab3ae8efbd6c4e0b82b645df04aa2d9e`.

`PivotChart.DataSource` и `PivotChart.Fields` классифицированы как
временные свойства без кодека хранения. Пакет из 12 сводных диаграмм
подтвердил оба допустимых источника: шесть результатов запроса и шесть
построителей отчета. Getter источника возвращает назначенный объект,
ссылка на `Fields` сохраняет идентичность и видит три реальных поля:
два измерения и числовой ресурс. Замена `Fields` запрещена платформой.
У нативного клона источник равен `Undefined`, поля и коллекции ролей пусты.

Исходное значение `DataSource` равно `Undefined`; скалярного исходного
значения `Fields` нет. Кодеки живых ссылок на запрос, построитель и поля
пока не реализованы. Getter обоих свойств и изменение источника явно
возвращают `unsupported_surface`, замена и сброс `Fields` возвращают
`read_only`. Эти ссылки не допускаются в публичный XML и не подменяются
таблицей значений, пустой коллекцией или копией нативного потока.
`Chart.DataSource` также является временной ссылкой на живой объект,
доступной только для записи, без скалярного исходного значения.
В отличие от сводной диаграммы, назначение источника обычной диаграмме
оставляет сохраняемое состояние: `Values dataMode="Source"` и необязательное
`sourceDescription`. Оно поддерживается отдельным именованным кодеком.
Клон сохраняет размеры и запрет изменения счетчиков, но не связь
с исходной таблицей или диапазоном и не исходную матрицу значений.
Кодек живого источника остается незавершенным; setter и сброс явно
возвращают `unsupported_surface`, getter возвращает `write_only`.

`GraphicalSchemaField.CurrentItem` является временной ссылкой на элемент
работающей схемы: `runtime_only`, без кодека хранения, исходное `Undefined`.
На 12 полях выбор действия `ProbeItem` подтвержден getter по типу и имени;
назначение элемента из исходной схемы и из `GetSchema` проверено отдельно.
Нативный клон сохраняет два элемента самой схемы, включая `ProbeItem`,
но выбранный элемент клона равен `Undefined`. При назначениях поток формы
меняет только счетчик изменений. Равенство объектов элементов не является
доказательством выбора: оно возвращало false при правильных типе и имени.

Живой кодек элемента и именованное содержимое графической схемы пока
не реализованы. Getter, setter и сброс в модели явно возвращают
`unsupported_surface`. Текущий элемент не записывается в публичный XML.
Свидетельства: `ev_7058a0fdc0f44e24a83ee392d63a5f9e`,
`ev_ab3ae8efbd6c4e0b82b645df04aa2d9e`.

`PivotChart.PivotChartType` сохраняется как именованное перечисление
`PivotChartType`: `Column` и `Column3D`, исходное значение `Column3D`.
Основной кодек записывает и сверяет тип сводной диаграммы и связанный тип
ее внутреннего renderer. Несовпадение этой пары или неизвестное значение
перечисления отклоняется. Проверены 24 нативных назначения на 12 диаграммах
и отдельная холодная загрузка шести Column и шести Column3D из именованного
XML без повторного назначения свойств. Строгая выгрузка и повторная сборка
сохранили канонический XML и модуль побайтно; BackColor не изменился.

Переключение типа в живой платформе также меняет отдельное внутреннее поле
renderer с пока неизвестным назначением. Оно не выведено в публичный XML
и не нормализуется как часть PivotChartType. Такой профиль остается
`reconstructionComplete=false` с OOF1140; по существующему контракту
предупреждение сохраняется при сборке. Точное восстановление этого
дополнительного состояния пока не доказано.

Профильные предупреждения реализованы для Form, Panel, LabelDecoration, Button,
PictureDecoration, UsualGroup, RadioButton, CheckBox, ListBox, InputField,
CalendarField, TrackBar, ProgressBar, Splitter, TextDocumentField,
SpreadsheetDocumentField, PivotChart, GraphicalSchemaField и
GeographicalSchemaField. Дополнительно реализованы ChoiceField, Table,
Chart, GanttChart и Dendrogram. Это покрытие непредставленных полей известных
профилей, а не всех вариантов этих контролов. Неизвестное содержимое
географической схемы также дает предупреждение: сохраняются именованный
контрол и геометрия, само содержимое пока не восстанавливается.

Неизвестный профиль входного потока Form или Panel дает предупреждение;
известные цвета, AutoTabOrder, страницы и привязки сохраняются. При расхождении
повторной записи BackColor сборщик использует именованное свойство и предупреждает
о неполном восстановлении. Это не разрешает неизвестные свойства публичного XML:
их словарь по-прежнему проверяет XSD.

Арность, структурные маркеры и корректность известных типизированных значений
остаются обязательными. Перечень профильных предупреждений и незавершенных
свойств выше относится к данным, которые представлены не полностью, а не к
читаемости контролов эталона. Полный эталон читается в именованную проекцию с
предупреждениями; это не означает полного восстановления. Актуальные результаты
см. в начале [карты исследований](research-map.md).


## Стандартное действие меню Close

В элементе CommandBarButton с type=Action задается ровно один вариант:
Action с обработчиком или StandardAction со стандартной командой.
Подменю и разделитель не могут содержать эти варианты.

```xml
<CommandBarButton name="CloseFromForm" type="Action">
  <StandardAction command="Close" commandBarId="4" source="Form"/>
</CommandBarButton>
```

commandBarId ссылается на именованную CommandBar, чья корневая коллекция
задает контекст команды. Доступен также context="Default" без commandBarId.
Источник принимает Form, AllSources или Control; последний задается ссылкой
sourceControlId на существующий контрол.
Сериализованная идентичность коллекции разрешается внутри кодека и не
выводится в XML; перенумерация коллекции не меняет ссылку на именованный
контрол. Код команды задает StandardMenuCommandDescriptor.
Оба варианта прошли сборку CLI, загрузку и строгий экспорт Designer.

Поддержана команда Close с контекстами Default и CommandBar. Это не
универсальная поддержка всех стандартных команд: неизвестные команды,
контексты и отсутствующие ссылки отклоняются. Проверки расширенного контекста
описаны в [карте исследований](research-map.md#общие-свойства-и-standardaction-2026-10-05).
Нулевая служебная запись конца непустой корневой коллекции, наблюдаемая
в Designer, читается с предупреждением о неполном восстановлении; известные
именованные действия сохраняются и доступны для сборки.


## Размер и шрифт колонки Table

Вложенный объект Column поддерживает сохраняемую ширину макета и шрифт текста:

```xml
<Column name="Code">
  <DataPath>Code</DataPath>
  <Header><Item language="ru">Код</Item></Header>
  <Width unit="form">140</Width>
  <TextFont kind="absolute" faceName="Arial" height="10"/>
  <Control type="InputField"/>
</Column>
```

Width является неотрицательным десятичным числом с обязательной единицей form.
Это единицы сохраняемого макета; свойство Width API платформы измеряется в
символах и зависит от шрифта. Прямое равенство этих значений не предполагается.
При отсутствии Width сохраняется прежняя каноническая ширина модели 210.
TextFont использует общий FontValue; автоматический шрифт опускается в XML.
Он относится к тексту колонки, отдельно от Font вложенного редактора Control.

В модель входит авторитетное значение ширины макета. Производные поля
платформенного формата проверяются как числа и нормализуются; при загрузке
и строгом сохранении платформа пересчитывает их с учетом шрифта. Публичный
XML содержит только именованные значения. Числа потока с экспонентой читаются
без промежуточного преобразования через double; XML сохраняет xs:decimal.

На целевой платформе проверены 12 колонок с ширинами 0, 100, 140 и 210 и
тремя шрифтами. Все чтения и строгий цикл Designer прошли, XML и модуль
совпали. Полный набор свойств Columns еще не завершен; эта возможность
не закрывает их классификацию. Свидетельство:
`ev_7ae706b44fb34425b0ef6ac66b518f3d`.

## Типы редактора колонки Table

Control типа InputField поддерживает необязательный ValueType через общую
модель TypeDomain. Подтверждены пустой тип и один тип String, Numeric,
Date-only, Boolean или ValueList. Enabled и ReadOnly принимают Boolean.
Внешнее описание типа колонки и описание встроенного редактора обязаны
совпадать. Парные записи редактора проверяются по варианту типа; неправильные
маркеры, идентификаторы, счетчики и известные булевы значения отвергаются.

Пять непустых типов прошли собственный цикл build, строгий Designer, dump и
повторный build. XML, модуль и итоговый BIN совпали; предупреждений нет.
Свидетельство: ev_17db9b8108c44204951eea250aa5284c.

У ChoiceField и CheckBox внутри колонки сохраняется ограниченный профиль
свойств по умолчанию. Чтение другого корректного оформления может дать
неполную проекцию с предупреждением. Это не означает сохранение такого
оформления. Общая XSD 1.0 описывает структуру Control; зависимость допустимых
свойств от атрибута type дополнительно проверяют XML parser и валидатор модели.

## Имена картинки кнопки выбора

Для InputField и ChoiceField API принимает ChoiceButtonPicture и его
платформенный синоним SelButtonPicture. Оба имени обращаются к одной картинке.
В XML используется только ChoiceButtonPicture, а в палитре сохраняются оба
имени. Прямая запись отдельного значения псевдонима недопустима.

## Выделение текста документа

TextDocumentField.SelectedText и InputField.SelectedText относятся к
состоянию времени исполнения и не входят в Form.xml. Модель не воспроизводит
выделение в интерфейсе или замену текста в редакторе.

## Видимость колонки таблицы

TableColumn поддерживает Visible, значение по умолчанию true. Выключенная
видимость сохраняется именованным свойством. Для таблицы ValueTree коллекция
Columns может быть пустой; для ValueTable требуется хотя бы одна колонка.
Полный набор свойств колонок остается открытым.
На платформе проверены 12 таблиц с видимыми и скрытыми колонками и 12 таблиц
ValueTree с пустой и непустой коллекцией. Строгая выгрузка сохранила XML и
модуль. Свидетельство: `ev_3a56d6ca0095481387063031942ce0cd`.

## Фиксация колонок

Table поддерживает FixedLeft и FixedRight как сохраненные целые количества
закрепленных колонок, по умолчанию 0. Платформа проверяет допустимость при
назначении, но после скрытия или изменения ширины колонки может сохранить
прежние значения фиксации. Поэтому документ не отклоняет такое сохраненное
состояние лишь из-за текущего числа видимых колонок. Назначение через модель
не воспроизводит все проверки интерфейса платформы.

Для малых ширин кодек записывает точный десятичный атом в компактной
экспоненциальной форме, если она короче. XML остается десятичным. Показатель
внутренней записи ограничен диапазоном от -4096 до 4096; значение за этой
границей отклоняется. На наборе из 12 таблиц Designer открыл ширины до
10^-320. Некоторые значения Designer меняет при сохранении, в том числе при
повторном открытии своего же Form.bin. Поэтому точное совпадение XML для
таких значений не гарантируется. Свидетельство:
`ev_f58e1f33cb7f4c9fbeabaff659dea5ff`.

## Режим выделения строки таблицы

RowSelectionMode использует TableBoxRowSelectionMode.Cell или Row. Это
отдельное свойство от SelectionMode, которое задаёт одиночное или множественное
выделение строк. На целевой платформе оба свойства доступны для записи.

## Редактируемое значение контрола

Общее `ControlExtension.Value` является доступом к текущим данным контрола,
а не сохраняемым исходным значением формы: `runtime_only`, без кодека хранения.
Вид значения `variant`; универсального скалярного исходного значения нет.
Справка 8.5.1.1343 ограничивает свойство контролами со связью с данными;
для поля ввода или таблицы, назначенных владельцем формы, оно недоступно.

На 12 связанных полях ввода и трех локальных полях String/Numeric/Date
назначенное значение подтверждалось getter; связанные реквизиты менялись
вместе с ним. Еще 12 контролов охватили CheckBox, ChoiceField и RadioButton:
после назначения getter и связанный реквизит совпали. Их исходные значения
различаются: CheckBox возвращает false, ChoiceField и RadioButton без выбора
возвращают `Undefined`. В нативных копиях текущие данные потеряны; в снимках
до и после смешанного пакета менялся только счетчик сериализации.

Живой кодек доступа к данным и ограничения владельца формы пока не реализованы.
Getter, setter и сброс модели явно возвращают `unsupported_surface`.
`Value` исключен из публичного XML. Назначения внутренних свойств документов
и схем рассматриваются отдельно от этого доступа к текущим данным.

## Состояние строк таблицы

CurrentRow, CurrentData и SelectedRows относятся к работающей форме и не
записываются в Form.xml. Модель объектов строк и коллекции выделения пока
не реализована. CurrentColumn также относится к состоянию времени исполнения;
модель текущей колонки еще не реализована.

`CurrentRow` и `CurrentData` имеют вид `variant`: справка 8.5.1.1343
определяет их конкретные типы через расширение табличного поля для источника
данных. `CurrentData` также может быть `Undefined` в иерархическом дереве.
Эта классификация не завершает живые кодеки строк и данных.

`CurrentParent` также является состоянием выполнения без кодека хранения.
Тип значения зависит от данных таблицы, поэтому скалярного общего исходного
значения нет: иерархический список справочника возвращает пустую ссылку,
а проверенное дерево значений возвращало `Undefined`.
На 12 разных формах списка иерархического справочника назначение группы
подтвердилось getter по равенству ссылок. Клон формы вернул `Undefined`.
Единственное изменение потока до и после назначения находится в известном
счетчике сериализации `$/1/10`; последовательность A, A, B, A не сохраняет
выбранную группу в потоке. Соседний элемент дерева справочника неначального
родителя не подтвердил и не засчитывается как успешный случай.
Кодек живого родителя пока не реализован: getter, setter и сброс модели
возвращают `unsupported_surface`. Свойство не записывается в публичный XML.

## Активизация по умолчанию и свертка

`Position.DefaultControl` является сохраняемым булевым свойством, доступным
для чтения и записи. Исходное false опускается в XML. Назначение true
снимает выбор только у соседей той же группы: отдельная корневая страница,
отдельная страница вложенной панели или вся общая область панели при
`SeparateByPage=false`. False и сброс снимают собственный выбор без
назначения другого элемента. Модель и кодек отвергают несколько выбранных
элементов в группе, отсутствующие ссылки и ссылки на другую страницу.
Сборщик сохраняет идентификатор выбранного контрола у владельца страницы;
публичный XML содержит только именованный DefaultControl у самого элемента.
Отдельный кодек геометрии без документа не принимает этот флаг.

На 12 полях в пяти группах проверены назначения, сброс и 168 живых плюс
168 восстановленных флагов. Холодная сборка первичного XML сохранила все
12 ожидаемых флагов, включая панель с общей областью. Строгая выгрузка
сохранила канонический XML и модуль побайтно.

`Position.Collapse` является временным свойством `ControlCollapseMode` со
значениями None, Left, Right, Top и Bottom; исходное значение None.
GET читает состояние Position; SET и RESET изменяют его и возвращают
статусы временного изменения. Неверный тип или член перечисления отвергается
до изменения; прямая конструкция модели также проверяется валидатором.
Любое явное значение Collapse, включая None и nullopt, запрещено при записи
публичного XML. После сброса временного свойства запись снова разрешена.

На 12 контролах, включая панели, поля ввода, разделители обеих ориентаций,
кнопки, поля таблиц и табличных документов, все 72 назначения принимались
и читались точно. Нативная копия возвращает None; отдельного сохраняемого
поля режима не обнаружено. Видимые через API координаты не изменились
до открытия формы. Это проверка значения свойства и его хранения;
визуальный эффект свертки в открытом окне отдельно не проверялся.

## Общая область страниц панели

Если `SeparateByPage` равно `false`, все страницы панели используют одну
общую область. Ее позиция задается одинаковым `Position` на каждой странице;
несовпадение позиций отклоняется при сборке. При выключении распределения
в конфигураторе платформа предупреждает об удалении элементов с других
страниц, поэтому изменение режима требует учитывать состав элементов.

При уже заданном `SeparateByPage=false` контролы всех закладок образуют
одну физическую последовательность. Кодек использует общий порядковый номер
и TabOrder; после чтения общие ChildItems располагаются на первой странице.
API TabOrder и ZOrder используют ту же группу. Перестановка ZOrder
канонизирует общие ChildItems и сохраняет независимый TabOrder каждого элемента.

## Прокручиваемые страницы панели

`ScrollPageMode=true` сохраняется с раздельными страницами и без явного
`ShowTabs`. После включения режима платформа возвращает для `ShowTabs` значение
`Неопределено`, пока вкладки явно не назначены. Для сохраненной панели без явного `ShowTabs` модель возвращает это значение
как вычисляемое. Побочные действия изменения режима еще не реализованы.

## Выбор незаполненного поля ввода

`IncompleteChoiceMode` задается перечислением `IncompleteChoiceMode` со
значениями `OnEnterPressed` и `OnActivate`. Значение по умолчанию в проверенной
платформе 8.5.1.1343: `OnEnterPressed`.

## Режим выбора из списка

Для поля ввода `ListChoiceMode` и `ChoiceListButton` сохраняются раздельно.
При присваивании режима через `SetPropVal` кнопка принимает то же булево
значение; последующее изменение кнопки не меняет режим. `ChoiceList` относится
к состоянию выполнения и не записывается в Form.xml.

`InputField.ChoiceList` и `ChoiceField.ChoiceList` возвращают стабильный
`ValueListRef`. `create` проверяет и копирует `ValueListValue`, а `replace`
проверяет новое содержимое до копирования и меняет тот же объект списка.
`get` дает только константный доступ. Присваивание ссылки нескольким полям
сохраняет общий объект; замена ссылки у одного поля не меняет остальные
ссылки. Старый список освобождается после исчезновения последнего владельца.
Копирование документа разделяет внешние ссылки состояния выполнения;
сохраняемые `ValueListValue` продолжают копироваться глубоко.
Вложенные живые ссылки не допускаются в типизированных значениях, поэтому
через них нельзя создать цикл владения. Скалярного сброса коллекции нет;
для нового списка создается и присваивается отдельная ссылка.

Проверка 8.5.1.1343 охватывает шесть InputField и шесть ChoiceField:
пустые исходные списки, повторный getter, добавление, удаление, очистку,
общий объект после присваивания и старые ссылки после замены. Все 36
назначений Undefined, Null и Array отклонены без изменения прежнего списка.
Для InputField проверены включенные и выключенные режим и кнопка списка,
для ChoiceField также проверен пустой тип. Проверялись строковые профили,
поведение интерфейса выбора и остальные типы реквизитов этим пакетом
не подтверждены. Поддержанные значения содержимого и их ограничения
определяются общим кодеком ValueList.

## Обновление диаграммы

`RefreshEnabled` относится к состоянию выполнения и не записывается в
Form.xml. На проверенной платформе начальное значение равно `true`.
`ShowTitle` и `ShowLegend` доступны в 8.5 и сохраняются в Form.bin. Для
`LegendArea` и `PlotArea` поддержаны положение, четыре границы, проверенные
булевы свойства, цвета, рамки и шрифты, а также формат таблицы данных,
выравнивание и тип шкалы радарной диаграммы. Остальные свойства областей и
вложенные шкалы еще не реализованы; их наличие в потоке отмечается как
неполное восстановление.

GanttChart сохраняет `PeriodicVariantUnit` и `PeriodicVariantRepetition`.
Единица имеет тип `TimeScaleUnitType` и восемь подтвержденных членов от
`Second` до `Year`, исходное значение `Month`. Кратность по умолчанию равна
1; сохраняемое значение является целым числом от 1 до 2147483647. Числовой
ввод через `SetPropVal` усекается к целому до проверки диапазона. Именованный
XML и прямой `PropertySet` требуют целого значения. Проверены 12 диаграмм,
пустые и заполненные коллекции, четыре режима масштаба и строгая выгрузка
Designer с совпадением XML и модуля. Полная модель диаграммы Ганта остается
отдельной незавершенной областью.

GeographicalSchemaField поддерживает `Scale` и `ScaleKeeping` как состояние
времени выполнения. По умолчанию используются 1 и
`GeographicalSchemaShowMode.AllData`; допустимы также `ScaleDefined` и
`SpecifiedArea`. Числовой ввод масштаба усекается к целому и проверяется
в диапазоне 1..2147483647. Заданные значения доступны через API модели,
но отклоняются при записи XML/BIN; публичная схема не содержит этих
сохраняемых элементов. Проверка 12 непустых карт подтвердила сохранение
объектов карты и сброс настроек отображения при восстановлении формы.
`ShowedArea` также является состоянием выполнения без кодека хранения.
На 12 непустых картах прямое назначение четырех границ подтверждено getter
и снимками. Нативное восстановление сбрасывает границы в нули;
повторное включение `SpecifiedArea` их не возвращает. Обычная холодная
загрузка подтвердила это на 12 картах. Строгая выгрузка Designer также
сбросила границы и режим отображения. Независимое `LongitudeShift`
сохранилось и не должно выводиться из границ области.

Живой объект `GeographicalSchemaShowedArea` пока не реализован: getter,
setter и сброс возвращают `unsupported_surface`, скалярного исходного
значения нет, свойство исключено из публичного XML. Полная именованная
модель данных карты, включая сохраняемое смещение долготы, остается
незавершенной и требует диагностики неполного восстановления.
Открытие десериализованного клона в отдельном опыте завершилось падением
1С с кодом 139; успешная холодная загрузка проверена отдельно.

InputField поддерживает числовой `DataPath` с одиночным числовым типом:
длина до 64, точность до длины, любой или неотрицательный знак. При длине
0 точность канонически равна 0. XML приводит ввод к этому состоянию,
неканонический бинарный TypeDomain и прямая модель отклоняются. Проверены
14 полей в 1С 8.5.1.1343 с холодной загрузкой и строгой выгрузкой Designer,
совпадением XML и модуля. Это не означает завершения `MinValue`, `MaxValue`
или всех вариантов `TypeRestriction`.

`InputField.MinValue`, `MaxValue` и типизированные значения диаграммы
используют один кодек `typed-value`. Он поддерживает `Number`, `String`,
`Boolean`, `Date`, `Null`, `Undefined`, `UUID`, известный встроенный `Type`,
а также `Color`, `Font`, `Border`, `TypeDescription`, `Array`, `Structure`,
`Map`, фиксированные варианты трех коллекций, `ValueList` и обычную
`FormattedString`. Значения могут быть вложенными.
XML содержит именованный дочерний элемент соответствующего типа;
цвет, шрифт, рамка и описание типов используют существующие схемы значений.
Исходные границы равны `Undefined`; явное исходное значение
не записывается. Поддерживаются прямая привязка к одиночному числовому
реквизиту и непривязанное поле с локальным одиночным числовым `ValueType`,
без `ListChoiceMode`; неподдерживаемый контекст отклоняется.
Обе физические копии каждой границы сверяются до нормализации. Неизвестные
типы явно отклоняются, без замены на `Undefined`.

Проверены 12 полей с восемью типами значений и два поля с исходными
границами: сборка именованного XML, холодное открытие, чтение 28 границ,
строгая выгрузка Designer и побайтовое совпадение канонического XML и модуля.
Отдельно проверены 12 непривязанных числовых полей с 12 типами значений:
холодное открытие собранного из именованного XML объекта, чтение 24 границ
без назначения свойств после загрузки, строгая выгрузка Designer и побайтовое
совпадение XML и модуля. В каждом поле Data пуст, а ValueType числовой.
Копирование живой формы через ValueFromString остается отдельным непокрытым
контекстом: в 12 копиях getter границы был недоступен. Причина не установлена;
успешная холодная загрузка не считается доказательством этого сценария.

Доступность свойств InputField проверяется по типу связанного реквизита или
явному локальному ValueType. Mask, MultiLine, ExtendedEdit и PasswordMode
доступны только для одиночного строкового типа без ListChoiceMode; чтение,
назначение и сброс для числового, датового типа и режима списка отклоняются.
Getter отсутствующей Mask
возвращает пустую строку. Для известных одиночных String, Numeric и Date
getter отсутствующего TypeRestriction возвращает пустое описание типов.
`TypeLinkItem` сохраняется как `persisted_readonly`, `control_info`,
`integer32`, с исходным значением 1. Getter модели читает сохраненный номер;
setter и сброс возвращают `read_only`. Положительный номер в XML имеет
представление int32, а его допустимость зависит от конкретного источника.

Прямая связь задается именованным `<TypeLink attributeId="10"/>` на локальный
реквизит формы с одиночным объектным типом. Источник и его UUID проверяются
структурно. Соответствие UUID внешней конфигурации и допустимость номера для
этого источника требуют проверки в платформе с той же конфигурацией и данными.
Неначальный номер без связи отклоняется. Неактивный индекс в бинарном потоке
без источника платформа игнорирует; модель читает исходное значение 1.
Пути через реквизиты объектов метаданных и источники обработчиков пока
не представлены этим прямым видом связи и явно отклоняются.

Проверены 12 полей с прямым источником и номерами 1, 2, 3 в базе с сохраненной
и примененной по F7 конфигурацией: 12 холодных getter и 12 нативных клонов
сохранили значения. Строгая выгрузка Designer дала побайтовое совпадение XML
и модуля с исходной именованной сборкой. Отдельный опыт показал сброс части
больших номеров в 1; структурная валидность XML не заменяет проверку источника.
Текущие доказательства: `scan-output/type-link-primary-valid-cold-native-20261104/summary.json`.

Полные формы справочника пока не поддержаны: выгрузка из примененной базы
упирается в неподдержанное расширение формы справочника. Это отдельная
граница от прямой связи поля и не позволяет объявлять инструмент завершенным.

`MinValue` и `MaxValue` классифицированы как `persisted_editable`,
`control_info`, `typed-value`, с исходным `Undefined`. Эта классификация
описывает доказанное хранение свойства. Таблицы и деревья значений,
прочие объекты платформы, файловые картинки внутри `ValueList` и объектные
ключи `Map` еще не покрыты полностью. Классификация дескриптора не означает
завершения всех вариантов типа `Произвольный` или готовности всего продукта.

`InputField.Mask` использует основной кодек `control_info`, исходное значение:
пустая строка. Непустая маска строкового редактора сохраняется в двух копиях,
которые декодер строго сверяет; длина редактора в этом профиле равна нулю,
а именованный ValueType сохраняет собственные квалификаторы. Назначение
непустой Mask через API устанавливает Wrap=false. Сброс Mask очищает маску
и сохраняет текущее значение Wrap. Проверены 12 назначений масок, включая
специальные символы, несколько вариантов через точку с запятой и экранирование.
Холодное открытие именованного XML без повторного назначения свойств
подтвердило 12 масок, пустые Data, исходный ValueType и Wrap=false.

После нативного удаления маски длина редактора может оставаться нулевой
при ненулевой длине ValueType. Такое остаточное состояние пока явно
отклоняется, без потери значения и без нормализации неизвестной истории.
Назначение TypeRestriction меняет данные редактора, хотя getter ValueType
сохраняет прежний тип. Копия через ValueFromString возвращает пустое
TypeRestriction, а getter Mask в ней недоступен.

`InputField.TypeRestriction` классифицирован как `runtime_only`, `none`:
значение ограничения доступно в живой модели, но не сохраняется как значение
этого свойства в Form.bin. Это подтверждено отдельно от нативной копии:
холодная загрузка 12 полей с четырьмя состояниями длины редактора (64, 8, 0
без маски и 0 с маской) вернула пустое TypeRestriction и исходный String(64).
Строгая выгрузка сохранила отличия длины редактора. Поэтому этот внутренний
параметр не является сериализованным значением TypeRestriction.

На 12 строковых, числовых и датовых полях проверены 60 назначений: строковое,
числовое, датовое, составное и пустое описание типов. Каждое читается обратно
без изменения ValueType и Wrap. API возвращает `applied_runtime_only`,
сброс возвращает `reset_runtime_only` и восстанавливает пустое описание.
Описание проверяется общим валидатором типизированных значений и его бюджетом;
некорректное описание объектного типа без UUID отклоняется до изменения модели.
Явно заданное временное значение, включая пустое, отвергается при записи
публичного XML; TypeRestriction также запрещен при чтении Form.xml.
После сброса временного свойства именованная модель снова сериализуется.
Это классифицирует свойство, но не закрывает остаточные состояния редактора:
их смысл и обратная сборка остаются незавершенными, без скрытой нормализации.

В C++ `PropertyValue` является наследником `std::variant`, а его базовый
тип доступен как `PropertyValue::Variant`. Это позволяет хранить рекурсивные
коллекции по значению, с глубоким копированием и без циклических ссылок.
Для `std::variant_size` и `std::variant_alternative` используется базовый тип.
`ArrayValue`, `StructureValue` и `MapValue` сохраняют собственный признак
`fixed`; сериализатор не подменяет его признаком родительской коллекции.
`Structure` содержит именованные `Field`, `Map` содержит `Entry` с `Key`
и `Value`, массив содержит `Item`. Сырых потоков или двоичных заглушек нет.

`Map` и `FixedMap` сравниваются по ключам и значениям независимо от порядка
записей. Публичный XML упорядочивает записи по общему каноническому ключу:
это устраняет перестановки после строгой выгрузки Designer. Поддерживаются
простые ключи; числовые ключи `int64_t` и `DecimalValue` с одинаковым
значением считаются одним ключом. Имена полей `Structure` должны быть
непустыми корректными строками UTF-8 без одинаковых строковых ключей;
эта модель не вводит API вставки и поиска полей структуры.

Общий `TypedValueBudget` ограничивает документ 1000000 узлами значений
и глубиной 64, корневое значение имеет глубину 0. Считаются также ключи
соответствий и элементы `TypeDescription`. Проверка документа предшествует
глубоким копиям и сериализации. XML учитывает общий бюджет во время чтения,
кодек хранения проверяет размеры и глубину до создания дочерних значений.
Обе копии границ поля проверяются до их полного сравнения и нормализации.

Общий формат подтвержден нативными назначениями 12 значений в 12 полях
и 12 диаграммах. Для 11 поддержанных непустых образцов текущий C++ кодек
восстановил нативную запись побайтово.
Холодный пакет именованного XML, где двенадцатый случай является пустым
массивом, прошел 36 проверок значений, строгую выгрузку Designer и
побайтовое сравнение XML и модуля. Для соответствий значения проверены
по ключам; три сериализованных `FixedMap` отличались порядком записей,
что отдельно сохранено в результатах проверки. Такой порядок не влияет
на канонический XML или сравнение значений модели.

`ValueList` содержит именованные `ValueType`, `AvailableValues` и `Items`.
У каждого `Item` есть числовой `id`, типизированный `Value`, `Presentation`,
`Check` и необязательная `Picture`. Порядок элементов сохраняется;
идентификатор соответствует `GetID` и не зависит от позиции.
`lastAssignedId` хранит историю выдачи идентификаторов и опускается,
когда совпадает с максимальным живым ID. Новый пустой список имеет
внутреннюю историю -1, пустой список после очистки может сохранять историю.
Отсутствие `AvailableValues` отличается от вложенного пустого списка.

`Presentation` допускает строку или форматированную строку.
`FormattedStringValue.value` в C++ содержит обычную строку либо
`LocalizedStringValue`: первый вариант использует публичный `Value/String`,
второй сохраняет прежний `Value/Item` с языками. Обычный типизированный
объект форматированной строки использует строковый вариант; представление
элемента списка с разметкой требует `formatted=true`. Для `Picture`
поддержаны отсутствие и известное имя библиотеки, без нового хранилища
ресурсов. Файловые картинки явно отклоняются до реализации контекста
вложенных ресурсов. Неизвестные вложенные типы не заменяются на `Undefined`.

Пакет из 12 нативных вариантов проверяет новый и очищенный список,
удаление, вставку и перенос элементов, ограничения типов, доступные
значения, разметку и библиотечную картинку. C++ кодек восстановил все
12 записей побайтово. Холодное открытие именованного XML на 12 полях
и 12 диаграммах подтвердило 36 значений и 57 проверок идентификаторов;
строгая выгрузка сохранила XML и модуль побайтово.

`Form.CaptionPicture` использует общий кодек картинок. Пустое значение
не записывается; библиотечная картинка задается через `standardName`,
файловая через ссылку на `PictureAsset`. Файл `CaptionPicture.gif`,
`CaptionPicture.png`, `CaptionPicture.jpg`/`CaptionPicture.jpeg` или
`CaptionPicture.bmp` хранится рядом с `Module.bsl`. Произвольные имена
файлов в корне этого каталога и выход за пределы пакета не разрешаются.

Программное присваивание через `SetPropVal` принимает пустую или известную
библиотечную картинку, как требует справка платформы. Designer допускает
также картинку из файла; поэтому XML/BIN и чтение API поддерживают такой
ресурс. При сбросе или замене картинки удаляется только отсоединенный
ресурс, который больше нигде не используется. Общий ресурс сохраняется.

В 8.5.1.1343 проверены 12 библиотечных назначений, сохранение GIF вручную
в Designer и холодное открытие именованных XML с библиотечной и файловой
картинками. Строгая выгрузка сохранила XML, модуль и файл побайтово.
Другие источники картинки, включая картинки конфигурации и фрагменты
файлов-коллекций, не подтверждены. Дескриптор классифицирован как
`persisted_editable`, `root_record`, `picture`; ограничения источников
картинок остаются явными ограничениями домена.

`Chart.GaugeChartQualityBands` является сохраняемой коллекцией, доступной
через API только для чтения. Getter возвращает принадлежащую диаграмме
ссылку `ChartCollectionRef`, замена и сброс коллекции возвращают `read_only`.
Содержимое представлено в `ChartPayload.gauge_quality_bands` и именованном
XML: `GaugeChartQualityBands/GaugeChartQualityBand`. Пустая коллекция
опускается. Каждая запись содержит `Begin`, `End`, `BackColor`, `Text`,
`ToolTip`; границы являются точными десятичными числами, `Begin <= End`.
Исходная полоса имеет границы 0 и 10, цвет RGB(50,205,50) и пустой текст.

Кодек сохраняет порядок полос, отрицательные и большие границы, цвет,
обычный текст и переводы. Единственный русский перевод сохраняется как
`Item`, а не превращается в обычную строку. Целочисленные служебные копии
границ не заменяют точные значения. Размер полос учитывается в общем
бюджете коллекций диаграмм до копирования потока и выделения вектора.
Лишние поля, противоречивые представления текста и неизвестные версии
отклоняются; сырые записи и исходный бинарник для восстановления не нужны.

На 8.5.1.1343 проверены 12 диаграмм с 24 полосами, отдельные 12 числовых
граничных случаев и холодная сборка именованного XML с 84 проверками.
Строгая выгрузка Designer и повторная сборка сохранили XML и модуль
побайтово, коллекции совпали с нативным снимком. При нативном клонировании
многоязычный строковый getter может стать пустым, хотя сериализованные
переводы сохраняются без изменений. Кодек сохраняет переводы и не
имитирует выбор языка платформенным контекстом.

Коллекции `Chart.Series` и `Chart.Points` сохраняют историю выдачи ID
через необязательный атрибут `lastAssignedId`. Он обозначает последний
выданный идентификатор, включая уже удаленные элементы. Без атрибута
используется максимальный действующий ID, не менее 1 для серий (резерв
итоговой серии) и 0 для точек. Избыточное значение по умолчанию опускается.
История сохраняется также у пустой коллекции.

Значение не может быть меньше действующих ID или больше
9223372036854775807. XSD, модель и двусторонний кодек проверяют эти границы.
Большие значения платформа преобразует в отрицательные, поэтому они
отклоняются до записи. Холодная проверка 12 диаграмм и отдельный пакет
пустых/непустых коллекций подтвердили последующую выдачу ID без повторного
использования удаленных. Это необходимая часть модели коллекций;
программное изменение `SeriesCount` и `PointCount` описано ниже.

`Chart.ActivePoint` сохраняется в основном кодеке `control_info` как
именованная ссылка `ChartPointRef` на точку того же контрола. В XML
`<ActivePoint>20</ActivePoint>` ссылается на ID точки, а `<ActivePoint/>`
задает отсутствие активной точки. Без явного свойства getter возвращает
первую точку или `Undefined`, если точек нет. Внутренняя позиция не является
ID точки и не публикуется в XML. Чужие и отсутствующие ссылки отклоняются.
Кодек поддерживает явную матрицу и режим `Source`.
Холодная сборка 12 диаграмм с разреженными ID точек дала 24 успешные
проверки getter и размеров. Строгая выгрузка Designer и 24 сравнения
именованных контролов сохранили состояние без изменений.

На 12 диаграммах платформы 8.5.1.1343, включая шесть круговых,
выбор и отсутствие выбора пережили нативное копирование. Числовые значения
setter отвергает. Назначение объекта точки показало смещение результата
и зависимость допустимости от числа серий. Это поведение еще не выяснено,
поэтому `SetPropVal` и сброс `ActivePoint` возвращают `unsupported_surface`;
поддержка хранения и getter не означает завершения API изменения выбора.
Свидетельства: `ev_57540942459f4ad7a27867ba8c066e4f`,
`ev_e9ccd7c06cea45099084ab03a3c5eff0`.

`Chart.Values` поддерживает атрибут `dataMode` со значениями `Sample`,
`Explicit` и `Source`. Это именованное состояние данных объектной модели,
а не дополнительное свойство программного API платформы. Без атрибута
пустое измерение подразумевает `Sample`, непустая матрица подразумевает
`Explicit`. Отличающееся от этого правила состояние записывается явно;
матрица, расшифровки и подсказки сохраняются в обоих режимах.

В режиме `Source` коллекции серий и точек сохраняют размеры, а `Values`
не содержит ячеек. Атрибут `sourceDescription` хранит описание диапазона
табличного документа; пустое описание можно опустить. Описание допустимо
только в режиме `Source`. Платформа не сохраняет живую связь с объектом
источника, поэтому XML не обещает восстановление этой связи или исходных
числовых значений. События, линии тренда и полосы качества сохраняются.
`SetPropVal` и сброс `SeriesCount`/`PointCount` возвращают `read_only`
в режиме `Source`, включая попытку присвоить прежнее значение.
На 8.5.1.1343 холодная сборка 12 диаграмм дала 49 успешных проверок;
строгая выгрузка Designer и повторное чтение сохранили именованные данные.

Режим нельзя вычислять только по размерам: непустая демонстрационная
матрица остается `Sample`, а очищенная вручную диаграмма может быть
пустой в режиме `Explicit`. Холодная проверка подтвердила оба случая.
При последующем увеличении платформа создает демонстрационные числа
в `Sample` и `Undefined` в `Explicit`. При первой ручной записи в
`Sample` остальные числовые значения обнуляются, расшифровки и подсказки
сохраняются. При изменении размеров модель должна сохранить фактический
режим до изменения коллекций. Неверные режимы явно отклоняются.


`SetPropVal` для `Chart.SeriesCount` и `Chart.PointCount` изменяет именованные
коллекции и их матрицу. Отдельные скалярные свойства счетчиков в XML
запрещены, `GetPropVal` вычисляет их по коллекциям. Сброс задает ноль.
При уменьшении сохраняются история ID, оставшиеся значения, расшифровки
и подсказки. При росте ID продолжаются от последнего выданного, подписи
новых элементов зависят от `AutoSeriesText` и `AutoPointText` и их позиции.
Удаленная активная серия заменяется последней оставшейся; отсутствие
серий дает `Undefined`. Число точек активную серию не меняет.

Режим данных сохраняется до изменения размеров. Новые ячейки `Explicit`
получают `Undefined`, новые ячейки `Sample` получают демонстрационные
числа с точностью до сотых от 0 до 20 включительно. Последнее является
реализацией демонстрационного заполнения по наблюдениям платформы;
тождество ее генератору, распределению и последовательности не доказано.
Два дескриптора классифицированы как `persisted_editable`,
`collection_record`, `integer`, с исходным нулем. Полный контракт связанных
источников данных остается неподтвержденным. При наличии такого источника
API явно возвращает `unsupported_surface`.

Принимаются числа, булевы значения и десятичные строки с точкой или
запятой. Отрицательные дроби отклоняются до усечения, положительные
усекаются. Ошибка входа или нехватка ресурсов не меняет документ.
`max_chart_collection_items` ограничивает сумму числа серий, точек,
ячеек и линий тренда всех диаграмм документа величиной 1000000; превышение возвращает
`resource_limit` до копирования и выделения новой матрицы. Это защитный
лимит продукта, а не установленная верхняя граница чисел платформы.


`GetPropVal` для `Chart.Series` и `Chart.Points` возвращает
`ChartCollectionRef`: владельца диаграммы и вид коллекции. Ссылка
обозначает принадлежащий объект, а не копию его элементов; текущие
серии и точки остаются в соответствующих коллекциях `ChartPayload`.
Ссылка сохраняет значение после роста, обнуления и повторного заполнения.
`SetPropVal` и `ResetPropVal` возвращают `read_only`, даже если передается
та же ссылка. При пустой диаграмме getter также возвращает ссылку.

В публичном XML эти объекты представлены существующими `Series` и
`Points`. Отдельная скалярная запись или ссылка в property bag запрещена.
Дескрипторы имеют `persisted_readonly`, `collection_record` и собственные
кодеки коллекций; `DefaultKind::none` означает отсутствие отдельного
скалярного значения по умолчанию. Содержимое изменяется через модель
коллекций, включая `SeriesCount` и `PointCount`. Стабильность ссылок и
запрет замены подтверждены на 12 диаграммах платформы 8.5.1.1343.


`Chart.SummarySeries` поддерживает необязательные `Text` и `Value`.
Отсутствие `Text` означает автоматическое название; наличие сохраняет
явное назначение, включая пустую строку и стандартное название `Сводная`.
Поддержаны обычный и локализованный текст. `Value` использует общий
общий кодек типизированных значений, описанный выше. Отсутствие `Value`
соответствует Undefined; отдельного признака его явного назначения нет.

При выключенном признаке явного текста кодек требует каноническое
автоматическое название. Противоречивый текст, неверный признак и
неподдержанное типизированное значение явно отклоняются. Они не заменяются
пустыми значениями. На 12 диаграммах, шести пустых и шести с данными,
холодное открытие и строгая выгрузка Designer подтвердили сохранение
текста, локализации, признака явного названия и скалярных значений.
Обычная и сводная серии используют общий реестр 21 свойства. В него входят
оформление, видимость в диаграмме и легенде, форматы подписей и таблицы,
накопление, ось значений, редактирование значений и коллекция `Trendlines`.
`SummarySeries` является вычисляемым признаком владельца. `Indicator`
вычисляется как `GraphicalRepresentationType=Line`; назначение `true`
устанавливает `Line`, а `false` устанавливает `Auto` при любом прежнем виде.
Эти два производных свойства отдельно в XML не записываются.

`ChartSeries.text` в C++ имеет тип `PropertyValue`, чтобы сохранять обычный
и локализованный текст. XML сохраняет существующий элемент `Text`.
`Details` использует тот же ограниченный набор типизированных скаляров,
что и `Value`; непокрытые типы объектов и объектные ключи соответствий
остаются отдельной задачей.

У `ColorPriority` есть подтвержденное различие владельцев. Для обычной
серии это временное состояние: setter и getter работают до сохранения,
а после открытия формы платформа возвращает `false`. Поэтому свойство
доступно через API модели, но не входит в XML обычной серии. У сводной
серии это сохраняемое свойство и оно входит в XML. Общий реестр сохраняет
оба случая явно, без дублирования остальных свойств серии. Проверены
явные данные, демонстрационные данные и пустые точки; свидетельство
`ev_612f08f59f24474e92f5bb692afc2409`.

Каждый владелец содержит упорядоченную коллекцию `Trendlines` с элементами
`Trendline`. У линии тренда есть 13 именованных свойств: `Line`, `Color`,
`EquationArea`, `Text`, `Details`, `ApproximationDegree`,
`ApproximationType`, `Marker`, `StockChartUsedPointValue`, `Factor`,
`ShowInLegend`, `ShowEquation`, `ShowDeterminationFactor`.
`EquationArea` использует общий реестр 12 свойств `ChartLabelArea`.
Внутренние идентификаторы обеспечивают стабильность ссылок во время жизни
документа и не публикуются как атрибуты XML линий тренда.

Платформа подтвердила необычные правила коллекции на 24 владельцах:
`Insert` с переданным аргументом возвращает Undefined и оставляет коллекцию
прежней. Назначение `EquationArea` также оставляет прежний объект и его
поля. Модель воспроизводит эти правила; изменение оформления выполняется
через свойства самой области. Удаление по индексу меняет порядок коллекции;
поиск удаленной линии возвращает отсутствие допустимой ссылки.
Свидетельство: `ev_e7be26eed41e40709c5e8b42dc32fd42`.

`Chart.ColorPalette` использует 17 значений платформенного перечисления.
Платформа хранит исходную палитру отдельно от ее текущих проекций в памяти.
Объектная модель читает исходное значение, которое получает форма после
открытия. Допустимые текущие проекции проверяются и приводятся к исходному
значению при сборке; отдельные служебные состояния в XML не выводятся.
Причинная проверка исходной палитры на 24 диаграммах:
`ev_c608d75cb3304e54866e0695b02078a2`.
Именованный цвет серии сохраняется как именованный цвет, включая стиль,
системный и Web-цвет. Производные RGB-значения автоматических цветов не
являются отдельными свойствами публичной модели. Холодные проверки Pie,
Line и Column подтвердили восстановление такого кэша платформой без
изменения изображений, getter и строгой выгрузки. Свидетельства:
`ev_f89270efbc32488ca2206c151fbc3673`,
`ev_78116657c0db43488b0de809ce358fe7`.


`Chart.PlotArea.ScaleLines` представлен значением `LineValue`:
`type`, `member`, знаковая 32-битная `width` и булев `gap`.
Используется общий кодек линии и существующие дескрипторы оформления
областей. В XML это именованный элемент `ScaleLines`; технические признаки
потока не публикуются. Отсутствие элемента означает сплошную линию
диаграммы толщиной 1 без отступа. Getter, setter и сброс области используют
тот же кодек; неверное значение отклоняется до изменения модели.

Поддержаны 25 видов из четырех семейств: `ChartLineType`,
`SpreadsheetDocumentDrawingLineType`, `SpreadsheetDocumentCellLineType`
и `GeographicalSchemaLineType`. Имена следуют программному API;
в платформенной XDTO-схеме географический тип назван
`GeographicalSchemeLineType`. Порядок числовых значений семейств различается
и хранится в общем реестре, из которого формируются сведения палитры.

Проверенные положительные ограничения толщины: 50 для видимых линий
диаграммы и географической схемы, 3 для рисунка и ячейки табличного
документа. Для `None` и двойной линии ячейки максимум равен 1.
Отступ разрешен только для семейства ячейки табличного документа.
Платформа принимает отрицательную толщину; модель сохраняет ее как
знаковое значение, кодек безопасно переводит его в платформенные 32 бита.
Граничные значения, отказы и все 25 видов проверены на платформе;
это не исчерпывающий перебор всех числовых значений.

Неизвестные семейства, виды, признаки, неверная структура или несогласованное
значение отклоняются. Пара семейство/вид и зависимые ограничения проверяются
моделью дополнительно к XSD. Свойство `ScaleLines` допустимо только у `PlotArea`.
Полное закрытие области построения требует также объектов ее шкал.


`PlotArea` содержит четыре именованных объекта шкал: `PointsScale`,
`ValuesScale`, `AdditionalValuesScale` и `SeriesScale`. Общий тип
`ChartScale` поддерживает `ScaleStep`, `TitleText`, `LabelFormat` и девять
свойств-перечислений, перечисленных ниже.
Принадлежащие ссылки `ChartScaleRef` стабильны при изменении данных;
замена и сброс самих объектов возвращают `read_only`.
Для доступа к полям используется ключ
`ObjectKey{control, id, ChartAreaKind::plot, ChartScaleKind::<kind>}`.
Другие сочетания владельца и вида области отклоняются.

Шаг хранится точным десятичным значением, без `double`, исходное значение
равно 0. Setter принимает числа, булевы значения и десятичные строки
с точкой или запятой, внешними пробелами и знаком плюс; отрицательные
значения преобразуются в 0. Пустая строка, экспонента, внутренние
разделители, дата, Null и Undefined отклоняются без изменения модели.
В исходном XML шаг должен быть неотрицательным. Текст и формат поддерживают
обычную и локализованную строки, исходные значения пусты.

Формат основной шкалы значений имеет два согласованных представления
в платформенном потоке. Сериализатор выводит оба из одного именованного
`ValuesScale.LabelFormat`, decoder проверяет их совпадение. Положение
записей шкал вычисляется по грамматике, независимо от размеров матрицы.
Неподдержанные измененные поля шкал не нормализуются в значения по умолчанию.
Поддержано 21 свойство шкалы: 20 скалярных свойств и принадлежащая ей
область TitleArea с отдельным контрактом из 12 свойств.

На 1С 8.5.1.1343 обнаружен сбой отображения: назначение шага `1e-28`
основной шкале значений первой диаграммы в проверенной форме принимается
setter и читается getter, но последующее обновление UI завершает процесс
с `std::length_error: vector::reserve`. Ошибка воспроизведена чистым API
платформы на форме с 12 круговыми диаграммами, без нового сериализатора.
Значение сохраняется точно; инструмент не подменяет его произвольным
ограничением. Этот случай не считается успешной проверкой отображения.


Перечисляемые свойства шкалы используют один реестр типов, значений
и исходных состояний:

| Свойство | Тип платформы | По умолчанию |
| --- | --- | --- |
| ScaleLocation | ChartScaleLocation | Auto |
| ScaleMarkLocation | ChartScaleMarkLocation | Auto |
| ShowTitle | ShowChartScaleTitle | Auto |
| TitleTextMode | ChartScaleTitleTextSource | Auto |
| ScaleLabelLocation | ChartScaleLabelLocation | Auto |
| LabelOrientation | ChartLabelsOrientation | Auto |
| GridLinesShowMode | ChartGridLinesShowMode | Auto |
| TitlePlacement | ChartScaleTitlePlacement | SpecialArea |
| ShowInChart | ShowInChart | Auto |

Тип перечисления и допустимый участник проверяются в API, модели и XML.
Сброс возвращает исходное состояние. Корневое `Chart.ShowTitle` остается
булевым свойством, а одноименное свойство шкалы принимает перечисление
`ShowChartScaleTitle`; владелец определяет контракт.

Шесть зависимых значений потока выводятся из этих именованных свойств:
видимость подписей трех основных шкал, ориентация подписей шкалы точек
в двух местах и видимость сетки основной шкалы значений. Эти значения
не являются отдельными свойствами публичного XML. При чтении несовпадение
с именованным состоянием явно отклоняется до общей проверки профиля.
Все значения проверены на 12 диаграммах и четырех шкалах; смешанный
пакет из 432 значений прошел холодное открытие и строгую выгрузку Designer.


Новые перечисляемые свойства шкалы используют существующее разреженное
хранилище `PropertySet`. Три ранее опубликованных поля `scale_step`,
`title_text` и `label_format` сохраняют прежнее представление в C++.
Дескриптор однозначно выбирает место хранения; размещение фиксированного
поля в разреженной части или неизвестный ключ отклоняются. Сериализатор
обходит дескрипторы в стабильном порядке, независимо от порядка назначения.


Оформление четырех шкал использует общие типы значений: GridLine и
ScaleLine принимают Line либо Undefined; GridLineColor, ScaleLineColor
и LabelColor принимают Color; LabelFont принимает Font. Эти шесть полей
хранятся в существующем разреженном PropertySet. По умолчанию обе линии
равны Undefined, цвета automatic, шрифт automatic без переопределений.
Публичная nullable-линия задается как `<GridLine><Line type="ChartLineType"
member="Dotted" width="2" gap="false"/></GridLine>` либо
`<GridLine><Undefined>undefined</Undefined></GridLine>`.

Внутренние значения заданных линий вставляются в запись шкалы и сдвигают
ее следующие поля. Единый расчет положения полей используется при чтении
прежних свойств, оформлении и согласовании зависимых значений. Числовые
смещения, признаки присутствия и идентификаторы цветов в XML не выводятся.
Платформенные опыты оформления и ссылочных значений:
`ev_8acb08b31fe04c098e2f7fc1cfb2359c`,
`ev_5a399212306c4804804215808cc216eb`,
`ev_afbdbf8284434954a3e84bf3bcd4a80c`,
`ev_72ad24d074db40e5bb6491bfbaa60aa6`.

Партия оформления собрана из именованного XML и открыта в 1С без исходного
бинарного шаблона. На 12 диаграммах проверены 288 значений оформления и
432 ранее поддержанных перечисляемых значения. Все 720 сравнений прошли,
после строгой выгрузки Designer XML и модуль совпали побайтово.
В опыте с измененным шрифтом штатный конструктор сохраняет масштаб 120
без признака scaleOverride; эти два именованных поля независимы.
Доказательство: `ev_c3cc13bfda7b428982765872da13a303`.


LabelAngle хранит точное десятичное значение со знаком. Платформа принимает
проверенные углы за пределами указанного в справке диапазона, включая
-361 и 361, без ограничения. MaxLabelRows хранит знаковое 32-битное целое.
В объектном API Decimal, целое, Boolean и числовая строка преобразуются
общим механизмом: половины округляются от нуля, затем результат приводится
к signed32 по модулю 2^32. Например, -2.5 дает -3, а 2147483647.5 дает
-2147483648. XML требует уже приведенное целое xs:int. Undefined, Null,
дата и неверная числовая строка отвергаются до изменения модели.
ScaleStep сохраняет отдельное правило ограничения отрицательного значения
нулем. Доказательства числовых правил:
`ev_3d34d434ab2340d9a2f349bb710d28e2`,
`ev_9ed24409c9af4f2e8adc67afacdbc1ff`.

Вложенная `Scale.TitleArea` имеет платформенный тип ChartLabelArea,
ОбластьПодписиДиаграммы. Ее нельзя заменить или сбросить. Она отличается
от корневой Chart.TitleArea и поддерживает следующие именованные свойства:

| Свойство | Тип | Исходное значение |
| --- | --- | --- |
| Location | ChartLabelLocation | Auto |
| Left, Top | Decimal | 0.5 |
| Font | Font | automatic |
| TextColor, BackColor, BorderColor | Color | automatic |
| Transparent | Boolean | true |
| Border | Border | WithoutBorder, width 1 |
| Marker | ChartMarkerType | Auto |
| Orientation | ChartLabelsOrientation | Auto |
| Angle | Decimal | 0 |

Left и Top в объектном API ограничиваются интервалом от 0 до 1 с точным
десятичным хранением. XML требует значение внутри этого интервала.
Angle сохраняет знаковое десятичное значение. Текст заголовка остается
свойством Scale.TitleText; поля Text, Placement, Right и Bottom у вложенного
TitleArea не вводятся. Публичный XML использует фактически принимаемые
платформой имена Location: LeftTop, LeftBottom, RightTop, RightBottom
и LeftAndTopSpecified. Отличающиеся английские имена из справки
не считаются дополнительными допустимыми значениями.
Доказательство: `ev_06107dec37cd4359b5847f1cdd35f46b`.

В C++ вложенная область адресуется как
`ObjectKey{control, id, ChartAreaKind::title, ChartScaleKind::<kind>}`.
Ссылка ChartAreaRef содержит владельца, вид области и вид шкалы; последний
размещен в прежнем выравнивании структуры. Все 12 полей используют
существующий PropertySet шкалы. Фиксированный размер ControlNode
не увеличивается, порядок присваивания не влияет на XML или бинарную запись.

Три корневые ссылки Chart.PlotArea, Chart.LegendArea и Chart.TitleArea
классифицированы как persisted_readonly. Они имеют отдельные именованные
кодеки и XML-типы, вычисляются из владельца и отвергают замену и сброс.
Их Font и Color проверяются общими кодеками до изменения состояния,
включая измененные ссылочные шрифты и неизвестные имена цветов.
Присваивания шрифтов трех областей подтверждены платформой:
`ev_94a8a40786da4cb1b61ee63cb5a10b51`.

Полная партия собрана из именованного XML и холодно открыта на 12 диаграммах.
Все 1536 сравнений прошли: прежние свойства шкал, новые числа, 12 полей
вложенной области, шрифты корневых областей, устойчивость ссылок и запрет
их замены. После строгой выгрузки Designer XML и модуль совпали побайтово.
Доказательство: `ev_baa5e255ffbd4cda95fe5635f2ffee36`.

Русская аннотация MaxLabelRows использует подтвержденное платформой имя
МаксимальноеКоличествоРядовПодписей. Опечатка Количестов из справки
не принимается платформой и не переносится в публичную палитру.
Проверены оба написания на всех 48 шкалах:
`ev_46614fd79e53432997e3436e3ebbaa47`.
