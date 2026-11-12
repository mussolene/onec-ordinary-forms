# Разработка и интеграция

## Сборка и проверки

Нужны CMake 3.20+, компилятор C++20, libxml2 2.14.6+, zlib и ICU 60+ (компонент `uc`). Старые версии libxml2 некорректно проверяют длинные значения `xs:decimal`. На macOS префикс Homebrew можно передать через `-DCMAKE_PREFIX_PATH=$(brew --prefix libxml2)`; для отдельной установки ICU используйте `-DICU_ROOT=/path/to/icu`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
ctest --test-dir build --build-config Release --output-on-failure -L smoke
```

Эквивалентные цели из корня: `make test` и `make smoke`. CI использует эти же цели. Наборы CTest проверяют модель, контейнер, потоки, кодеки, XML, метаданные и CLI; метка `smoke` выбирает проверку версии и интеграцию CLI. Успешный CTest не заменяет платформенное доказательство. Порядок проверки кандидата описан ниже.

## Вызов из внешнего инструмента

```sh
build/sidecars/onec-form-native/oof dump input/Form.bin work/Form.xml --json
build/sidecars/onec-form-native/oof build work/Form.xml output/Form.bin --json
build/sidecars/onec-form-native/oof --coverage --json
```

`dump` и `build` принимают необязательные `--type-schema <types.xsd>` и `--metadata <configuration-root>`. Первый параметр предоставляет декларации имен типов, второй читает XML выгрузки метаданных для разрешения имен и идентификаторов ссылок. Эти параметры необязательны и дополняют разрешение имен и ссылок. UUID и `typeName` типов берутся из XML; основной writer не зависит от полной конфигурации. Исходный BIN, база 1С и платформа для `build` не нужны.

Проверяйте код завершения и массив `diagnostics` в JSON, включая предупреждения успешной команды. Код `0` означает успешную операцию, `1` означает ошибку обработки или параметров, `64` означает неизвестную команду или неверное число операндов. Библиотечная диагностика может содержать неполную модель, но CLI ее не выпускает. JSON `ok=true` означает успех команды, а не независимую нативную приемку формы.

## Подключение библиотеки C++

Существующий CMake target `oof::liboof` подключается через `add_subdirectory` этого репозитория и `target_link_libraries`. Это статическая библиотека; используйте CMake target, чтобы передать зависимости libxml2, zlib и ICU. Отдельный установленный пакет `find_package(oof)` сейчас не определен.

Основные точки входа:

| Заголовок | Вызов и контракт |
| --- | --- |
| [`oof/form_bin.hpp`](../sidecars/onec-form-native/include/oof/form_bin.hpp) | `oof::load_form_bin(bytes, form_name, metadata_context)` возвращает `Result<model::OrdinaryFormDocument>`; `oof::save_form_bin(document)` возвращает `Result<std::vector<std::uint8_t>>`. |
| [`oof/source/form_xml.hpp`](../sidecars/onec-form-native/include/oof/source/form_xml.hpp) | `oof::source::parse_form_xml(xml, metadata_context, types)` и `serialize_form_xml(document, types)` преобразуют именованный XML и модель. Версия задана `ordinary_form_xml_version = "2.1"`. |
| [`oof/model/ordinary_form.hpp`](../sidecars/onec-form-native/include/oof/model/ordinary_form.hpp) | `OrdinaryFormDocument::get_prop_val`, `set_prop_val`, `reset_prop_val` работают с `ObjectKey`, `PropertyId` и типизированным `PropertyValue`; `validate()` проверяет инварианты. `module()`, `set_module()`, `assets()` и `add_asset()` предоставляют модуль и ресурсы. |
| [`oof/source/metadata_xml.hpp`](../sidecars/onec-form-native/include/oof/source/metadata_xml.hpp) | `parse_configuration_type_schema(xsd)` строит `ConfigurationTypeSchema`; `parse_metadata_context(xml_documents)` строит `MetadataContext`. Это необязательный контекст, а не исходная форма для пересборки. |
| [`oof/result.hpp`](../sidecars/onec-form-native/include/oof/result.hpp) | `Result<T>::ok()`, `value()`/`take_value()` и `diagnostics()` передают результат и диагностику. Предупреждения могут сопровождать успешный результат. |

Библиотечные функции XML обрабатывают текст и объектную модель. Чтение и запись соседних файлов модуля и ресурсов организует вызывающая программа; готовый пример находится в [`src/cli/main.cpp`](../sidecars/onec-form-native/src/cli/main.cpp). Не вызывайте `value()` при неуспешном результате.

## Контракт исходного пакета

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.gif
```

Для произвольного `Example.xml` CLI ищет модуль в `Example/Module.bsl`. Файл обязателен даже для пустого модуля. Ссылки на картинки разрешаются внутри того же пакета. `dump` создает XML и соседние файлы; `build` читает их и сериализует новый контейнер формы.

Публичный `Form.xml` версии 2.1 использует английские имена объектов и свойств, типизированные значения и вложение `ChildItems`. Это формат обычных форм проекта, а не XML управляемых форм. Модуль хранится в BSL, картинки в отдельных ресурсах. Формат не допускает сырых потоков, индексных записей и скрытого бинарного источника для пересборки. Полный контракт находится в [ordinary-form-target-contract.md](ordinary-form-target-contract.md).

## Изменения модели

Перед добавлением свойства найдите существующего владельца в модели, дескрипторах, XML и сериализации. Установите публичное имя, применимость, доступность, сохраняемость, тип, значение по умолчанию и платформенный кодек. Расширяйте существующие дескрипторы и общие кодеки, когда совпадает семантика. Недостающее сохраняемое значение получает именованное понятие модели и схему.

Завершайте семейство сохраняемых объектов одновременно в reader и writer. Для применимых пар контроль/свойство проверяйте default, допустимые варианты, границы и отказ на неправильном типе. Общую нативную проверку объединяйте в один пакет минимум с десятью контролами; полный корпус выполняйте после завершения семейства, а не каждого свойства. Явно классифицируйте свойства состояния выполнения, которые не должны попадать в XML/BIN. Не маскируйте неизвестное поле сырым потоком и не объявляйте неполную проекцию полной. Остатки, включая старые варианты колонок `Table` и кодеки состояния выполнения, перечисляются в [контракте](ordinary-form-target-contract.md) и [плане](repository-target-state.md).

## Проверка результата

Собственный цикл `BIN -> XML -> BIN -> XML` проверяет согласованность reader/writer. Совпадение его результатов не доказывает совместимость с платформой: обе стороны могут содержать одну ошибку. Для изменения формата нужны независимо созданный платформой образец, строгая выгрузка Designer, повторное чтение результата и проверка значений платформенными getters там, где это требуется контрактом.

Целевая среда: 1С 8.5.1.1343, Linux, без `ActiveXControl`. Для проверки EPF/ERF используйте лицензированный работающий контейнер и новый каталог результата внутри `scan-output/`:

```sh
OOF_PLATFORM_CONTAINER=licensed-container \
  tools/platform_validate_epf.sh work/result.epf scan-output/validation-current
```

Скрипт выполняет строгий Designer `/DumpExternalDataProcessorOrReportToFiles`. Он не принимает существующий каталог результата. Предварительная проверка метаданных или успешное создание базы не подтверждают корректность `Form.bin`. Полный ручной цикл с Designer и Git описан в [README](../README.md#ручной-цикл-с-git-и-designer).

`oof --coverage --json` учитывает классификацию дескрипторов. Даже нулевые остатки и `descriptorCoverageComplete=true` не заменяют независимые платформенные проверки и проверку реального корпуса. Кандидат выпуска с ограниченным покрытием должен явно указывать оставшиеся границы.

Держите частные EPF/ERF, клиентские дампы, лицензии, секреты и локальные пути за пределами Git и OACS. Временные данные размещайте в игнорируемых `work/`, `scan-output/` или системном временном каталоге. CLI не исполняет BSL и требует доверенного локального каталога; пределы файлов и правила путей приведены в [контракте](ordinary-form-target-contract.md#границы-cli).

## Проверка кандидата выпуска

После завершения изменений вычислите отпечаток исходников и сверьте его с [манифестом доказательств](release-evidence.json):

```sh
python3 tools/release_gate.py --source-digest
make release-gate RELEASE_TAG=vX.Y.Z-rc.N
```

Замените `vX.Y.Z-rc.N` фактической версией кандидата, согласованной с `OOF_VERSION` и манифестом. Текущий манифест исторический, поэтому проверка свежего main должна отказать до получения нового подтверждения. Не обновляйте только digest ради прохождения gate.

Проверка сопоставляет версию CLI, исходники, покрытие и доказательства манифеста, запускает CTest и отличает кандидата с явными ограничениями от полного выпуска. Workflow [Release](../.github/workflows/release.yml) выполняет ту же проверку перед упаковкой. Архив Linux x86_64 содержит `oof`, схемы, документацию, минимальный пример и скрипт строгой проверки Designer; рядом публикуются манифест и `SHA256SUMS`.
