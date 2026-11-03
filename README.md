# Wolfenstein (2009) SP — открытый нативный порт

Исследование форматов и начало разработки совместимого движка одиночной
кампании. Первая целевая платформа — Android ARM64, затем другие платформы.

**Developer APK 0.9 запускает настоящий GPL engine/game runtime на Android.**
Выберите `SP/base` своей PC-копии и нажмите **Запустить empty**. Движок
читает оригинальные `assets.pk4` и `empty.mpk`, выводит текстуры исходной
тестовой карты, рассчитывает ходьбу и столкновения. Управление — кнопками
и касаниями сцены. Кампания, оружие, противники и звук миссий пока недоступны.
[Скачать engine APK](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.9.0-engine-dev) · [Инструкция](docs/ANDROID.md).

Прежний [viewer 0.8](https://github.com/CheffCorleOne/wolf2009-sp/releases/tag/v0.8.0-alpha)
остаётся отдельным проверочным инструментом: показывает поездку и открытие
двери в начале `trainyard`. В 0.9 приоритетом стал общий native runtime.

## Что готово

- Переносимый C++11 reader SPK/MPK: zlib, таблицы файлов, выравнивание,
  контроль границ и памяти, чтение отдельных записей.
- Патч обоих ZIP-backend в idTech4A++ для оригинальных SP PK4 с изменённой
  сигнатурой концевой записи. Архивы игры не изменяются.
- Read-only инвентаризация установленной игры на Node.js без зависимостей.
- Общее C++ ядро rounded collision: исходные support points и radii, distance,
  overlap, continuous cast и BVH. Реальные HKXS данные проверены отдельно от
  игровой привязки; фильтры и неопределённые контакты учитываются явно.
  [Устройство и текущие границы](docs/COLLISION.md).
- Исходная HKXS world collision подключена к штатному `idClip` через owning
  support index. Оригинальная `empty` прошла native player, masks, cache и
  освобождение ресурсов без преобразования в brushes. Остальные карты требуют
  квалификации фильтров, дополнительных тел и игровых сущностей.
- Ограниченный native BRAI host выполняет OnSpawn/OnUsed, Wait, UseTarget и
  SetGlobalVariable через настоящие GPL entities. Проверены общие int32 globals,
  keyed Use, generation-aware ссылки и отказ неподдержанных графов до выполнения.
  Это ещё не импорт логики миссий. [Профиль исполнения](docs/BRAINS.md).
- Переносимый reader `entityDef`/`model` сохраняет наследование и provenance
  полей. Контекст MAPDEF передаётся из исходного worldspawn; неизвестные команды
  анимаций сохраняются как данные. Отдельная таблица разрешает полные logical
  имена анимаций с наследованием; неподдержанная дочерняя запись блокирует
  родительскую. Frame-command blocks сохраняются, расширения не переписываются.
  [Профиль resolver](tests/source-definitions/README.md).
- Каталоги DECL карты и global теперь читаются общим C++ кодом, включая
  типизированные строки, int32, float32 и проверенные word-pool значения.
  Owning snapshots сохраняют исходные записи, числовые биты и provenance.
  Они подключены к игровому модулю через существующий API8 filesystem.
  Единый набор применяет восстановленные owner/duplicate/protected gates по
  явному плану чтения и сохраняет все кандидаты. По наблюдению оригинального
  `trainyard` добавлен startup loader четырёх global/map и языковых каталогов.
  Регистрация исходных игровых классов и их поведение ещё в разработке.
  [Формат, подключение и границы](docs/DECLARATIONS.md).
- Source weighted body и исходные spline poses импортируются в штатный
  `idRenderModelMD5`. Native CPU snapshots, UV, цвета и bounds сравнены с source
  decoder. Исходный authored skin выбирает оригинальный TXTR через native
  `customSkin`; отдельные material declarations сохраняют внешний вид других
  экземпляров. Source HKXA также подключён к штатным `idMD5Anim`/`idAnimator`:
  прямой spline sampling, native PlayAnim/blending/bounds и ресурсные leases
  проверены на одном52-joint profile. Ограниченная visual entity также прошла
  обычные native Think/Present, callback, endpoint и generation-safe teardown.
  Это ещё не импорт поведения исходных NPC.
  Original body с текстурой и двумя source poses также прошёл native GLES
  readback в эмуляторе; голова и игровое поведение ещё не подключены.
  [Проверки и границы](tests/engine-skinned/README.md).
  [Source animation runtime](src/idtech4a_anim/README.md).
- Geometry-only mesh binding v2 читает MODL с явно выбранным named SKEL без
  зависимости от анимации. Original body52, standalone head27, character LOD0/1,
  native snapshots/UV/цвета/skin и source leases прошли Android runtime.
  [Контракт загрузчика](src/idtech4a_render/MESH_V2.md).
- Общий C++11 spline codec на основе GPL HavokLib читает TRS curves с проверкой
  границ, несколькими блоками и явной scale policy. Проверены три оригинальных
  клипа; skeleton mapping, float/face tracks и retail actor binding остаются
  отдельной работой. [Provenance и проверки](tests/spline-codec/README.md).
- Named HKXA reader сохраняет binding facts и отдаёт общий codec input.
  Переносимый rig sampler принимает явно выбранный скелет и полное покрытие
  дорожек; проверены аналитические и оригинальные позы, включая несколько
  блоков. V2 native provider уже прошёл body182/41/335 и открытие locker2
  через обычный animator/SourceVisual, с проверкой source leases и reload.
  [Reader](tests/spline-clip/README.md) · [Rig profile](tests/rig-animation/README.md).
- Общий animation-set controller переключает несколько qualified clips одной
  модели, удерживает source frame и заканчивает one-shot точной конечной позой.
  Проверены original body335/41 и locker24/21, same-tick команды, два независимых
  объекта, native Think/Present/bounds и сохранность resource leases.
  Это механизм движка; исходные interaction/loot/AI ещё не перенесены.
  [Контракт и проверки](src/idtech4a_anim/SETS.md).
- PROCB7/PBBF: вершины и UV в форматах 40/64 байта, индексы, материалы,
  проверки границ, OBJ-экспорт статического мира и C++ CPU preview.
- Геометрия всех 27 MPK прошла нативный декодер: 3 715 889 треугольников,
  ошибок нет. На складе `mtw_ware` проверена геометрия статического мира.
- Проверены все 1 434 оригинальных SPK/MPK: 2 487 чанков, 81 796 записей,
  ошибок нет. Оба ZIP-backend проверены на оригинальных SP PK4 и контрольном MP PK4.
- Desktop-инструмент читает материалы из `SP/base/assets.pk4` и DDS из карты.
  Для склада он вывел 53 разные текстуры. Реализованы чтение ENTS4 и загрузка
  статических моделей MD5R1. Расстановку моделей по сцене ещё предстоит проверить.
- Нативный Android GLES3 viewer отрисовал склад в эмуляторе Android 15 x86_64.
  На 179 из 232 поверхностей вывел 53 разные текстуры из карты, остальные
  53 поверхности отображаются условным цветом.
  Проверены поворот камеры, сброс вида и возврат в приложение. В APK также
  есть arm64-v8a. Проверка на физическом телефоне ещё впереди.

За основу платформенного слоя выбран
[idTech4A++](https://github.com/glKarin/com.n0n3m4.diii4a),
commit `ce49510f3b0c04969eab8ebe8238416e84e4f0bb`. Патч открывает
ресурсы SPK/MPK через собственный интерфейс `idFile` движка.
Проверена сборка ядра для Android ARM64. [Сборка и проверка движка](docs/ENGINE.md).
В Android x86_64 уже прошли штатные game lifecycle и физика игрока,
загрузка исходной `empty`, native world/model/image loaders и реальный
opaque GLES3 кадр с проверенным вкладом DDS. В APK 0.9 эти подсистемы
подключены к обычному Android SurfaceView через закреплённый Q3E API.
Проверены импорт, физика игрока, касания, возврат из фона и выход процесса.
ARM64 проверен сборкой; физического устройства для проверки пока нет.
Его Android/GLES-поддержка полезна, но совместимость Wolf2009 SP предстоит
реализовать отдельно. Исходный `Gamex86.dll` — Windows x86 и не является
переносимым игровым модулем.

## Структура

| Путь | Содержимое |
|---|---|
| `src/wolf2009/` | Общие C++ декодеры контейнеров, геометрии, текстур и сущностей |
| `android/` | Android Activity, импорт своей карты и нативный GLES3 viewer |
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
Этот скрипт рисует геометрию условными цветами. Для текстурированного CPU
preview укажите свою карту и пакет материалов, затем отрендерите полученный OBJ:

```powershell
.\build\native\wolf2009-pack.exe 'C:\path\to\mtw_ware.mpk' 'C:\path\to\assets.pk4' evidence\warehouse
.\build\native\mesh-preview.exe evidence\warehouse\scene.obj evidence\warehouse\world.bmp --mtl evidence\warehouse\scene.mtl
```

Другие хосты с CMake и zlib development package:

```sh
cmake -S src/wolf2009 -B build/archive
cmake --build build/archive
ctest --test-dir build/archive --output-on-failure
```

Общее ядро собрано через CMake на Windows x64 с Zig и miniz из закреплённого
checkout, а также для Android x86_64 с системной zlib из NDK. Консольные
тесты Android выполнены в эмуляторе, включая ходьбу по оригинальной `empty.mpk`.

Инвентаризация своей установленной копии:

```powershell
node tools/inventory.mjs 'C:\path\to\Wolfenstein' evidence
```

Игра при этом не запускается и не изменяется. Результаты появляются только
в указанной папке отчётов. Полная проверка SPK/MPK описана в
[проверках](docs/VALIDATION.md).

## Следующий этап

Основная работа — подключение ресурсов к штатным renderer, collision и
game lifecycle idTech4A++, с повторным использованием GPL Doom3 module.
Общий загрузчик графов поведения проверен на 633 ресурсах из всех карт.
Большинство игровых действий ещё предстоит восстановить.
[Графы поведения](docs/BRAINS.md) · [Игрок и сравнение с PC](docs/PLAYER.md).
[Исследование](docs/RESEARCH.md), [план](docs/ROADMAP.md),
[продолжение работы](docs/CONTINUE.md).
Настройка инструментов: [REA и Ghidra](docs/REA-SETUP.md).
Реализованные форматы: [геометрия](docs/GEOMETRY.md).

Новый код распространяется под GPL-3.0-or-later. Сохранены уведомления
заимствованной ZIP-реализации в патче. Игровые ресурсы и оригинальные
бинарники в этот репозиторий не входят; пользователь предоставляет свою копию.
