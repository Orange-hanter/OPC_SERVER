# Contributing

## Перед кодом

1. Прочитать [DOCs/08-engineering-standards.md](DOCs/08-engineering-standards.md).
2. Прочитать релевантные [ADR](DOCs/adr/README.md).
3. Если решение архитектурное — сначала ADR (draft), потом код.

## Workflow

```bash
cmake --workflow --preset dev
./build/dev/tools/opc-map/opc-map validate DOCs/examples/demo-plant.modbusproj.json
```

Другие профили и Conan 2: [DOCs/12-modern-cmake.md](DOCs/12-modern-cmake.md).

Форматирование:

```bash
clang-format -i $(git ls-files '*.cpp' '*.hpp' | grep -v Lib/)
```

## Слои

Не подключайте `adapters/` из `core/`. Composition root — только `Src/app` / `main`.  
Инженерный CLI (`tools/opc-map`) не должен линковаться с runtime poller/UA.

## PR checklist

См. [DOCs/10-quality-gates.md](DOCs/10-quality-gates.md).

CI на PR обязателен: workflow **CI** должен быть зелёным.

## Releases

Не публикуйте бинарники вручную с ноутбука как «официальный» релиз.  
Релиз = SemVer-тег → workflow **Release** (см. [DOCs/11-ci-and-releases.md](DOCs/11-ci-and-releases.md)).

Перед тегом обновите [CHANGELOG.md](CHANGELOG.md).

## Язык

- Документация: русский.
- Идентификаторы кода: английский.
