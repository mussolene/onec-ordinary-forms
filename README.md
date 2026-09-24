# onec-ordinary-forms

Проект развивает утилиту для разбора, редактирования и сборки обычных форм 1С в именованный XML и отдельный `Module.bsl`. XML-словарь проекта версии 2.1 вдохновлён читаемостью управляемых форм, но не совместим с форматом их выгрузки. Поддержка XML-модели сама по себе не означает, что каждый элемент можно записать в `Form.bin`.

## Сборка и работа

Нужны CMake 3.20+, компилятор C++20 и libxml2. Из корня репозитория:

```sh
make build
make test
make smoke
build/sidecars/onec-form-native/oof --help
```

Сборка включает единственный путь утилиты: библиотеку `liboof` и команду `oof`.

Запись в `Form.bin` пока ограничена исследованным вариантом потока 27/18 и сценарием не более чем с одной кнопкой. Это не означает поддержку произвольных форм или всех свойств XML-модели.

Выгрузка и сборка используют соседний каталог модуля. Для `Form.xml` в `work/Form.xml` модуль находится в `work/Form/Module.bsl`; для пустого модуля всё равно создайте пустой файл:

```sh
build/sidecars/onec-form-native/oof dump input/Form.bin work/Form.xml
${EDITOR:-vi} work/Form.xml
${EDITOR:-vi} work/Form/Module.bsl
build/sidecars/onec-form-native/oof build work/Form.xml output/Form.bin
```

CLI предоставляет команды `dump` и `build`; `--help` описывает их параметры. Элементы и свойства редактируются в XML, обработчики в `Module.bsl`. Автотесты XML проверяют разбор, модель и обратную сериализацию XML; они не подтверждают запись такого содержимого в `Form.bin`. Поддержка бинарного кодека ограничена реализованными сочетаниями формы и свойств, а неподдержанное значение должно приводить к диагностике. Успешная сборка не заменяет строгую проверку платформой. Для неё используйте локальный EPF и `tools/platform_validate_epf.sh`, если доступна лицензированная среда.

## Синтетический источник

Минимальная форма для проверки XML-пути:

```xml
<Form id="1" name="Main" ordinaryFormVersion="2.1">
  <ChildItems>
    <Button id="2" name="Run">
      <Position/>
      <Enabled>false</Enabled>
      <Caption>Run</Caption>
      <Events><Click id="3">RunClick</Click></Events>
    </Button>
  </ChildItems>
</Form>
```

Сохраните рядом `Form/Module.bsl`, например с содержимым:

```bsl
Процедура RunClick(Элемент)
    Сообщить("Пример");
КонецПроцедуры
```

Этот минимальный сценарий соответствует синтетической интеграционной форме CLI. Он иллюстрирует именованный XML для обычной формы, а не совместимость с XML управляемых форм. CLI проверяет XML по схеме 2.1, генерируемой из метамодели; одно лишь соответствие схеме не доказывает поддержку бинарного потока.

## Структура реализации

- `sidecars/onec-form-native/include/oof/model` и `src/model`: объектная модель и метамодель типов, свойств и значений.
- `include/oof/source` и `src/source`: разбор и сериализация публичного XML; сюда относится генерация описаний схемы.
- `include/oof/storage` и `src/storage`: потоки и кодеки значений.
- `include/oof/form_bin.hpp`, `src/form_bin.cpp`: контейнер `Form.bin` и граница загрузки/сохранения.
- `src/cli/main.cpp`: тонкая оболочка команд CLI.
- `tests`: тесты модели, XML, хранения и интеграции CLI.

Текущее архитектурное направление и следующий порядок расширения описаны в [`docs/architecture.md`](docs/architecture.md), процесс разработки в [`docs/development.md`](docs/development.md), миграционная граница в [`docs/repository-target-state.md`](docs/repository-target-state.md). Продуктовый контракт находится в [`docs/ordinary-form-target-contract.md`](docs/ordinary-form-target-contract.md).
