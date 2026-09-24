# Research Map

This repository keeps research notes at the level of verified approach,
sanitized tool behavior, and public references. Do not commit private
processors, customer exports, full platform dumps, disassembly output, license
configuration, OACS state, or host-specific paths.

## Direction

The ordinary form model is built from three evidence sources:

- platform export/import behavior;
- public documentation and prior art for ordinary form controls;
- small sanitized observations from platform libraries.

Public articles and repositories are useful for vocabulary and comparison, but
the serializer should be treated as correct only after a platform round trip
confirms it.

## Validation Loop

1. Export a private EPF/ERF with platform tooling into an ignored directory.
2. Decode `Ext/Form.bin` into the object XML package.
3. Rebuild the form stream from object XML.
4. Ask the platform to read or export the rebuilt processor.
5. Compare stable XML and runtime behavior.

For ordinary `Form.bin` writer work, Designer batch export is stricter than a
metadata-only load/check cycle and is the preferred acceptance check.

## Leak Scan Checklist

Before publication or commit, check current content and staged changes for:

- host paths and private mounted volumes;
- `.agent/oacs`, `.oacs`, local databases, key files, and `.env`;
- `nethasp.ini` contents, license server addresses, and license tokens;
- passwords, passphrases, private keys, API tokens, and GitHub tokens;
- EPF/ERF/CF/DT files, platform archives, generated dumps, and customer
  metadata.

Recommended local commands:

```bash
git status -sb
git diff --cached --check
git diff --cached --name-only
git diff --cached | rg -n -i '(/Users/|/home/[^/[:space:]]+|/private/|/var/folders/|password\s*=|passphrase\s*=|secret\s*=|token\s*=|private key|NH_SERVER_ADDR|NH_TCPIP|NHS_SERVER)'
gitleaks detect --source . --redact --no-banner
```

Documentation may mention placeholder values such as
`<local-nethasp.ini>`. Do not record real local values.

## Platform Areas

The platform binary scan has been useful for names and neighboring concepts,
not as a replacement for round-trip validation.

Known useful areas:

- ordinary form designer libraries for control and property vocabulary;
- UI/metadata libraries for shared form concepts;
- core stream/container libraries for `ListInStream` and `ListOutStream`;
- formatting and picture libraries for value serializers.

Keep raw binary strings, symbols, traces, and dumps in ignored local work
directories. Promote only compact verified conclusions into OACS.

## План сравнения одиночных изменений, 2026-09-24

Статус: итерация завершена частично. AC1 PASS: новая сессия Designer повторно
открыла сохраненные EPF с Button; Caption `Launch` и состояние Enabled=false
прочитаны в форме. AC2 PARTIAL: сохранение без изменений дало различие только по
позиционному пути `$/1/10` (3 -> 4); варианты Caption и Enabled изменили ожидаемые
листья и также `$/1/10` (3 -> 5). Роль этой позиции в текущем эксперименте не
подтверждена, различие оставлено видимым. AC3 PARTIAL: строгая выгрузка Designer
успешна для всех четырех EPF, но `oof dump` завершился `OOF1114` для каждого по
пути Button base record `$/1/2/2/1/2/1/0`; именованное свойство не определено и
кодек продуктовой модели не прошел обратную проверку. Отчет находится в
`scan-output/button-property-pilot-actual-diag-20260924T125045Z-23059/report.json`.
Сравниватель показывает только позиционные отличия логического потока формы и
не приписывает им смысл свойств. Пакетный автоматический генератор вариантов
свойств не реализован.
Историческая сверка после пилота: значение Button base record в позиции 17,
отличающее этот вход от канонического, уже записывалось как `2` в `53abed1`.
Техническое имя старого writer `baseStyleState` не доказывает именованное
свойство платформы. Позиция `$/1/10` уже трактуется текущим кодеком как счетчик
сериализации, начиная с `14a49a2`; пилот сам по себе эту семантику не проверял.
Перед следующим экспериментом использовать [разбор повторов](ordinary-form-pattern-audit.md#повторный-разбор-истории-от-2026-09-24).

Целевой охват: 25 типов текущего каталога без внешнего ActiveX.

Критерии исследования перед массовой автоматизацией:

- AC1: один созданный элемент и измененное свойство действительно сохраняются
  в документе формы и читаются после закрытия и повторного открытия.
- AC2: повторное сохранение без правок отделяет служебные изменения от изменения
  исследуемого свойства. Неизвестные различия не скрываются нормализацией.
- AC3: соответствие подтверждается несколькими значениями и обратным изменением,
  затем воспроизводится единственным продуктовым кодеком и строгой выгрузкой Designer.

Последовательность для каждого эксперимента:

1. Зафиксировать версию платформы и синтетическую исходную форму. Создать один
   элемент через механизм, сохраняющий документ формы. Сохранить и открыть заново.
2. Дважды сохранить форму без правок. Зафиксировать различия между этими контрольными
   сохранениями. Идентификаторы и счетчики считать шумом только после проверки их роли.
3. Из одной исходной формы получить независимые варианты, меняя одно свойство.
   Для логического значения проверить оба состояния; для перечисления все допустимые
   значения; для строки пустое значение, ASCII и кириллицу; для числа несколько
   допустимых значений. Отдельно проверить возврат к значению по умолчанию.
4. После каждого сохранения открыть форму заново и прочитать свойство. Сравнить
   внутреннюю структуру потоков, а не абсолютные смещения байтов. Сохранить связь
   с именем элемента, свойством, значением и версией платформы в закрытом отчете.
5. Проверить взаимодействия: второй элемент того же типа, другой родитель,
   страницы, зависимые свойства, события и привязки. Одно свойство может менять
   несколько записей; отсутствие разницы может означать несохраняемое свойство.
6. При неоднозначности исследовать конкретный путь чтения/записи в библиотеке
   нужной версии. Использовать существующие результаты Ghidra и точечные пробы.
7. Перенести подтвержденное правило в именованную объектную модель и дескриптор
   единственного кодека. Собрать форму из XML без исходного бинарного шаблона,
   проверить строгой выгрузкой Designer и чтением измененного значения.

Первый пилот: Button, свойства Caption, Enabled и геометрия. После выполнения
AC1-AC3 повторить на InputField и Table, прежде чем масштабировать перебор.
Прежнее наблюдение OACS о несохранении добавленных во время исполнения элементов
нужно перепроверить на выбранном способе сохранения. Сериализация живого объекта
через `ЗначениеВСтрокуВнутр` сама по себе не доказывает сохранение в `Form.bin`.

Существующие `platform_scom_formdocument_probe.sh` и Ghidra-инструменты являются
исследовательскими средствами, а не готовым редактором свойств или пакетным
генератором вариантов. Удаленную реализацию и зависящие от нее скрипты не возвращать.
Исходные формы, библиотеки, трассы и подробные различия остаются вне Git; в OACS
попадают только обезличенные выводы и ссылки на свидетельства. Отладчик подключать
для конкретного неразрешенного перехода, а не как обязательный этап каждого случая.
