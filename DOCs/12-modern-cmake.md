# 12. Практикум по современному CMake и Conan 2

Сборочная система намеренно показывает несколько возможностей CMake 3.28. Все
экспериментальные режимы выключены по умолчанию, поэтому обычная сборка остаётся
предсказуемой.

## Карта проекта

| Файл | Что демонстрирует |
|------|-------------------|
| `CMakePresets.json` | configure/build/test/package/workflow presets |
| `cmake/ProjectOptions.cmake` | usage requirements через `INTERFACE`-цель |
| `cmake/Dependencies.cmake` | `find_package()` и резервный `FetchContent` |
| `cmake/CompilerSupport.cmake` | compile checks без привязки к одному компилятору |
| `cmake/Packaging.cmake` | установка по компонентам и архивы CPack |
| `Src/*/CMakeLists.txt` | цели принадлежат своим каталогам, связи выражены target-based API |
| `conanfile.py` | Conan 2 recipe, `CMakeDeps`, `CMakeToolchain`, layout и packaging |

Внутренние цели доступны через стабильные alias-имена: `opc::project`,
`opc::core`, `opc::adapters`, `opc::app`. Настройки компиляции не записываются
глобально, а приходят через `opc::project_options`.

## Presets: рекомендуемый путь

```bash
cmake --list-presets=all
cmake --workflow --preset dev
```

Отдельные стадии того же workflow:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Другие учебные профили:

```bash
cmake --workflow --preset asan  # AddressSanitizer + UndefinedBehaviorSanitizer
cmake --preset unity            # unity build и precompiled headers
cmake --build --preset unity
ctest --preset unity

cmake --preset release
cmake --build --preset release
cpack --preset release          # TGZ и SHA-256
```

`compile_commands.json` создаётся в каталоге конкретного preset. Его понимают
clangd, clang-tidy и многие IDE.

## Полезные переключатели

| Cache variable | Default | Назначение |
|----------------|---------|------------|
| `OPC_BUILD_TOOLS` | `ON` | собирать `opc-map` |
| `OPC_ENABLE_WARNINGS` | `ON` | строгий переносимый набор warnings |
| `OPC_WARNINGS_AS_ERRORS` | `OFF` | добавить `-Werror` / `/WX` только своим целям |
| `OPC_ENABLE_SANITIZERS` | `OFF` | ASan + UBSan для GCC/Clang |
| `OPC_ENABLE_IPO` | `OFF` | IPO/LTO после `check_ipo_supported()` |
| `OPC_ENABLE_UNITY_BUILD` | `OFF` | объединять `.cpp` своих целей |
| `OPC_ENABLE_PCH` | `OFF` | использовать `target_precompile_headers()` |
| `OPC_ENABLE_CLANG_TIDY` | `OFF` | запускать clang-tidy из compile pipeline |
| `OPC_DEPENDENCY_PROVIDER` | `AUTO` | `AUTO`, `CONAN` или `FETCHCONTENT` |

Пример точечной настройки:

```bash
cmake --preset dev \
  -DOPC_WARNINGS_AS_ERRORS=ON \
  -DOPC_ENABLE_CLANG_TIDY=ON
```

Санитайзеры, PCH, unity и IPO — инструменты с разными компромиссами, а не
обязательный «максимальный» профиль. Например, unity build ускоряет full build,
но может скрывать отсутствующие include и увеличивать потребление памяти.

## Два способа получать зависимости

`AUTO` сначала ищет config packages через `find_package()`. Если их нет,
open62541 и Catch2 загружаются через `FetchContent`. Полностью автономный режим:

```bash
cmake --preset dev -DOPC_DEPENDENCY_PROVIDER=FETCHCONTENT
```

`CONAN` запрещает fallback, поэтому ошибка интеграции обнаруживается сразу.
Нужен Conan 2:

```bash
python3 -m pip install --user conan
conan profile detect --force
conan install . -s build_type=Debug -s compiler.cppstd=23 --build=missing
conan build . -s build_type=Debug -s compiler.cppstd=23 --build=missing
```

`CMakeToolchain` передаёт CMake сведения о compiler/runtime, а `CMakeDeps`
создаёт config packages для `find_package(open62541)` и
`find_package(Catch2)`. Recipe также умеет собрать устанавливаемый пакет:

```bash
conan create . -s build_type=Release -s compiler.cppstd=23 --build=missing
```

Если нужно управлять стадиями вручную, после `conan install` используйте preset,
имя которого Conan напечатал в конце команды (обычно `conan-debug`).

## Что считать современным стилем

- свойства задаются цели, а не всему directory scope;
- зависимости выражаются `target_link_libraries()` с `PRIVATE`/`PUBLIC`;
- include path использует `$<BUILD_INTERFACE:...>`;
- внешние библиотеки не получают warnings/sanitizers проекта;
- configure, build, test, install и package остаются отдельными стадиями;
- dependency provider можно заменить, не меняя граф собственных целей;
- in-source build завершится понятной ошибкой и не загрязнит репозиторий.
