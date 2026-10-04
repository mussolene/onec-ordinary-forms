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
Buttons. Action задает обработчик модуля; Separator не имеет дополнительных
полей, Submenu может содержать рекурсивную коллекцию. Имена уникальны в своей
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
используют общий кодек геометрии. Даты календаря, данные индикатора, сама
картинка и события этих контролов пока явно отклоняются.

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
