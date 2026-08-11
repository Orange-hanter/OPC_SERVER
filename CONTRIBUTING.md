# Contributing

## Перед кодом

1. Прочитать [DOCs/08-engineering-standards.md](DOCs/08-engineering-standards.md).
2. Прочитать релевантные [ADR](DOCs/adr/README.md).
3. Если решение архитектурное — сначала ADR (draft), потом код.

## Workflow

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/opc-map validate DOCs/examples/demo-plant.modbusproj.json
```

Форматирование:

```bash
clang-format -i $(git ls-files '*.cpp' '*.hpp' | grep -v Lib/)
```

## Слои

Не подключайте `adapters/` из `core/`. Composition root — только `Src/app` / `main`.  
Инженерный CLI (`tools/opc-map`) не должен линковаться с runtime poller/UA.

## Коммиты / PR

- Одно логическое изменение на commit по возможности.
- PR описывает: мотивацию, какие ADR затронуты, как тестировали.
- Чеклист: [DOCs/10-quality-gates.md](DOCs/10-quality-gates.md).

## Язык

- Документация: русский.
- Идентификаторы кода: английский.
