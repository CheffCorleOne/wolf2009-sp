# Wolfenstein (2009) SP — открытый нативный порт

Исследование форматов и начало разработки совместимого движка одиночной
кампании. Первая целевая платформа — Android ARM64, затем другие платформы.

**Сейчас работают загрузчики контейнеров, декодер статического мира PROCB7
и диагностический просмотр геометрии. APK и запускаемой кампании пока нет.**

## Что готово

- Переносимый C++11 reader SPK/MPK: zlib, таблицы файлов, выравнивание,
  контроль границ и памяти, чтение отдельных записей.
- Патч обоих ZIP-backend в idTech4A++ для оригинальных SP PK4 с изменённой
  сигнатурой концевой записи. Архивы игры не изменяются.
- Read-only инвентаризация установленной игры на Node.js без зависимостей.
- PROCB7/PBBF: вершины и UV в форматах 40/64 байта, индексы, материалы,
  проверки границ, OBJ-экспорт статического мира и C++ CPU preview.
- Геометрия всех 27 MPK прошла нативный декодер: 3 715 889 треугольников,
  ошибок нет. Отрендерена геометрия склада `mtw_ware` без текстур и props.
- Проверены все 1 434 оригинальных SPK/MPK: 2 487 чанков, 81 796 записей,
  ошибок нет. Оба ZIP-backend проверены на оригинальных SP PK4 и контрольном MP PK4.
- На Windows x64 прошли 23 archive, 9 geometry, 10 rasterizer self-tests
  и 20 Node/integration tests.

За основу платформенного слоя выбран
[idTech4A++](https://github.com/glKarin/com.n0n3m4.diii4a),
commit `ce49510f3b0c04969eab8ebe8238416e84e4f0bb`.
Его Android/GLES-поддержка полезна, но совместимость Wolf2009 SP предстоит
реализовать отдельно. Исходный `Gamex86.dll` — Windows x86 и не является
переносимым игровым модулем.

## Структура

| Путь | Содержимое |
|---|---|
| `src/wolf2009/` | Исходный код C++ reader и standalone CMake target |
| `patches/` | Изменения к закреплённой версии движка |
| `tools/` | Инвентаризация, подготовка checkout и нативные пробы |
| `tests/` | Синтетические тесты форматов и интеграции ZIP |
| `docs/` | Исследование, план, результаты проверок, точка продолжения |
| `engine/` | Локальный sparse checkout основы, исключён из Git |
| `evidence/` | Локальные полные отчёты, исключены из Git |
| `build/` | Локальные сборки, исключены из Git |

## Быстрая проверка

Node.js 22+; для этих тестов установка npm-пакетов не требуется:

```powershell
npm test
```

Windows, Git и Zig 0.15.2:

```powershell
.\tools\prepare-engine.ps1
.\tools\build-native.ps1 -Zig 'C:\path\to\zig.exe'
npm run test:native
.\build\native\wolf2009-inspect.exe --self-test
```

Первый просмотр своей установленной копии (только статический мир):

```powershell
.\tools\build-preview.ps1 -Zig 'C:\path\to\zig.exe'
npm run test:geometry
.\tools\preview-scene.ps1 -GameRoot 'C:\path\to\Wolfenstein' -Map game/mtw/mtw_ware
```

OBJ и BMP появятся в новой локальной папке `evidence/scene-previews`.
Preview использует CPU, flat shading и условные цвета материалов.
Он проверяет геометрию и не заменяет будущий GLES renderer.

Другие хосты с CMake и zlib development package:

```sh
cmake -S src/wolf2009 -B build/archive
cmake --build build/archive
ctest --test-dir build/archive --output-on-failure
```

CMake/system-zlib путь пока не запускался в этой среде. Проверенная сборка
использовала Windows x64, Zig и miniz из закреплённого checkout движка.

Инвентаризация своей установленной копии:

```powershell
node tools/inventory.mjs 'C:\path\to\Wolfenstein' evidence
```

Игра при этом не запускается и не изменяется. Результаты появляются только
в указанной папке отчётов. Полная проверка SPK/MPK описана в
[проверках](docs/VALIDATION.md).

## Следующий этап

Материалы/текстуры, ENTS placements и renderer adapter для Android.
MODL/MD5R пока исследован частично; анимация, коллизии и SP-логика впереди.
[Исследование](docs/RESEARCH.md), [план](docs/ROADMAP.md),
[продолжение работы](docs/CONTINUE.md).
Настройка инструментов: [REA и Ghidra](docs/REA-SETUP.md).
Реализованные форматы: [геометрия](docs/GEOMETRY.md).

Новый код распространяется под GPL-3.0-or-later. Сохранены уведомления
заимствованной ZIP-реализации в патче. Игровые ресурсы и оригинальные
бинарники в этот репозиторий не входят; пользователь предоставляет свою копию.
Исходники опубликованы: [CheffCorleOne/wolf2009-sp](https://github.com/CheffCorleOne/wolf2009-sp).
Это экспериментальная версия движка; полноценная кампания и Android runtime
ещё разрабатываются. Игровые ресурсы в репозитории отсутствуют.
