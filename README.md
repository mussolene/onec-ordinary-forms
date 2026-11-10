# onec-ordinary-forms

[![CI](https://github.com/mussolene/onec-ordinary-forms/actions/workflows/ci.yml/badge.svg)](https://github.com/mussolene/onec-ordinary-forms/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mussolene/onec-ordinary-forms?include_prereleases)](https://github.com/mussolene/onec-ordinary-forms/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Утилита `oof` и библиотека C++ для чтения, редактирования и сборки обычных форм 1С через именованный `Form.xml`, модуль BSL и ресурсы, удобные для Git.

## Быстрый старт

Скачайте [архив v1.0.0-rc.1 для Linux x86_64](https://github.com/mussolene/onec-ordinary-forms/releases/download/v1.0.0-rc.1/onec-ordinary-forms-v1.0.0-rc.1-linux-x86_64.tar.gz) и сверьте его с [SHA256SUMS](https://github.com/mussolene/onec-ordinary-forms/releases/download/v1.0.0-rc.1/SHA256SUMS). Двоичный пакет рассчитан на Ubuntu 26.04 x86_64 и системные libxml2, zlib и ICU этого выпуска.

```sh
tar -xzf onec-ordinary-forms-v1.0.0-rc.1-linux-x86_64.tar.gz
cd onec-ordinary-forms-v1.0.0-rc.1-linux-x86_64
./oof build examples/fixtures/minimal/Form.xml work/Form.bin
./oof dump work/Form.bin work/roundtrip/Form.xml
```

Первый результат получается без установленной 1С и собственных входных файлов: пример содержит надпись и кнопку, его модуль пустой. Сравните исходный и полученный XML, измените подпись и повторите сборку. Кандидат выпуска не означает 100% покрытия платформы.

Для сборки из исходников нужны CMake 3.20+, компилятор C++20, libxml2 2.14.6+, zlib и ICU 60+ (компонент `uc`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build/sidecars/onec-form-native/oof build examples/fixtures/minimal/Form.xml work/Form.bin
build/sidecars/onec-form-native/oof dump work/Form.bin work/roundtrip/Form.xml
```

Для своей формы выполните `oof dump input/Form.bin work/Form.xml`, отредактируйте пакет и вызовите `oof build work/Form.xml output/Form.bin`. Исходный BIN для сборки не нужен. Рядом с XML обязателен `<имя XML без расширения>/Module.bsl`, даже если модуль пустой.

## Пакет исходников

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.gif
```

Публичный XML объявляет `ordinaryFormVersion="2.1"`. Он содержит именованные свойства, реквизиты, команды, события и дерево `ChildItems`, но не совместим с XML управляемых форм. Модуль и картинки хранятся отдельными файлами. Схемы и правила модели описаны в [контракте OrdinaryForm](docs/ordinary-form-target-contract.md).

CLI обрабатывает одну форму за вызов. Обе команды принимают `--json` для диагностики, `--type-schema <types.xsd>` для имен типов конфигурации и `--metadata <configuration-root>` для разрешения ссылок по выгрузке метаданных. Это необязательное обогащение имен и ссылок: UUID и `typeName` типов берутся из XML, сборка не требует полной конфигурации или базы.

## Ручной цикл с Git и Designer

1. Выгрузите свою обработку или отчет в файлы через Designer и найдите `Form.bin` нужной обычной формы.
2. Выполните `oof dump`, просмотрите диагностику и сохраните XML, модуль и ресурсы в Git.
3. Измените пакет, проверьте `git diff` и выполните `oof build`.
4. Поместите собранный `Form.bin` в штатную выгрузку, соберите EPF/ERF через Designer и проверьте его строгой повторной выгрузкой.
5. Повторите `oof dump` для результата Designer и сравните XML, модуль и ресурсы перед фиксацией изменений.

Для последнего платформенного шага в репозитории есть [tools/platform_validate_epf.sh](tools/platform_validate_epf.sh). Ему нужны лицензированная среда 1С и Docker; подробности находятся в [руководстве разработчика](docs/development.md#проверка-результата).

## Состояние и ограничения

Целевая платформа: 1С 8.5.1.1343, Linux. Открытые задачи: [старые варианты колонок Table, #48](https://github.com/mussolene/onec-ordinary-forms/issues/48), [14 кодеков состояния выполнения, #49](https://github.com/mussolene/onec-ordinary-forms/issues/49) и [ActiveXControl вне целевой области, #50](https://github.com/mussolene/onec-ordinary-forms/issues/50). Точные границы приведены в [контракте модели](docs/ordinary-form-target-contract.md) и [плане полного покрытия](docs/repository-target-state.md).

`oof --coverage --json` показывает классификацию дескрипторов. Нулевые остатки и `descriptorCoverageComplete=true` сами по себе не доказывают полную сохранность формы. Предупреждения и `reconstructionComplete="false"` означают неполное чтение; пересборка сохраняет только представленные моделью данные. Успешный цикл собственного reader/writer также требует независимой проверки Designer.

CLI не исполняет BSL и предназначен для доверенного локального каталога. Не добавляйте частные EPF/ERF, дампы, лицензии, секреты и отчеты из `work/` или `scan-output/` в Git. Пределы ввода и правила путей описаны в [контракте](docs/ordinary-form-target-contract.md#актуальное-состояние-и-границы-cli-2026-10-05).

Для интеграции и изменений кода: [разработка](docs/development.md), [архитектура](docs/architecture.md). Лицензия: [MIT](LICENSE).
