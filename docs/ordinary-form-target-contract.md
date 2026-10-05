# Ordinary Form Contract

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

Цвета поддерживают непрозрачный RGB, automatic и именованные ссылки
StyleColors.ButtonTextColor, StyleColors.ButtonBackColor,
StyleColors.ButtonBorderColor. BorderColor по умолчанию automatic, текст и
фон ссылаются на соответствующие цвета стиля. Альфа-канал, другие имена
стиля и другие виды платформенных цветов явно отвергаются. Проверены
15 отдельных присваиваний с обратным чтением и строгая сборка из XML;
12 getters созданной формы совпали с заданными значениями. Доказательства:
`ev_59bffaa579c3408aa501cc0716a4ae76`,
`ev_3f6d1fc6961e4480ab402ab91a64649a`.

Font поддерживает automatic, абсолютный шрифт с непустым faceName и
StyleFonts.TextFont. Именованные поля height, bold, italic, underline,
strikeout сохраняют различие между отсутствием и явно заданным значением.
Масштаб scale и признак его переопределения scaleOverride независимы:
конструктор по описанию и конструктор по исходному шрифту могут давать
одинаковый масштаб с разным признаком переопределения. Сырая маска в XML
не допускается. Поддержаны размер с точностью до десятых и целый масштаб,
включая 0. WindowsFont, другие ссылки стиля и переопределения automatic
или стиля пока явно отвергаются. Проверены 11 значений через объект кнопки
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
Description; Separator не имеет дополнительных полей, Submenu может содержать
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
localized string операции и допускает пустую запись или одну локализацию
ru. Другие языки и несколько локализаций строго отвергаются, без потери
данных. Unicode, экранирование XML и переводы строк подсказки сохраняются.

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
вариантов: ev_bbb81122b09942cbb28bd51a53f6b545. Полный эталон прошел Border
и остановился на следующем поле CommandBar; его полный цикл еще не завершен.


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
ev_0f3bc8e79b0e41248bd8ff6318a0f28f. Независимое ревью и актуальный CTest
11/11 PASS: ev_5589b98b384a4d8bb57e223c75885ca0. Это не объявляет весь
CommandBar или полный эталон готовыми: следующий неописанный контракт
находится в общей metadata записи элемента.


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
обработчика. Старый текстовый Action не принимается. События Button.Click и
Form.OnClose сохраняют свои отдельные доказанные ограничения.

Text, ToolTip и Explanation самого CommandBarButton остаются отдельными
свойствами. Отсутствие элемента и явно пустой элемент различаются, включая
флаги хранения. Подписи действия не подменяются переопределениями кнопки.
Публичного представления сырой записи или запасного пути сборки нет.

Собственный XML с тремя независимыми подписями действия и явно пустыми
переопределениями кнопки прошел сборку EPF, строгий экспорт Designer
8.5.1.1343 и повторный dump. Канонические XML и Module.bsl совпали точно.
Свидетельство: ev_b9d6405cb8d04d7bbae6eb3867838125.

Общий контракт остается PARTIAL. Вариант без явного Text проходит сборку
и строгий экспорт, но Designer добавляет автоматический текст, который
текущий decoder отвергает. Этот текст отличается от Action.Text, поэтому
нормализовать его к подписи действия нельзя. Кроме того, значение 2 в
завершающей части записи пункта меню после сохранения GUI пока не описано.
Оно встречается и у нетронутого подменю, не является доказанной маской
измененных подсказок. Эти случаи явно отвергаются; полный авторский эталон
пока не проходит цикл. Font и остаточная запись самого Action поддержаны
только в доказанном состоянии по умолчанию.
