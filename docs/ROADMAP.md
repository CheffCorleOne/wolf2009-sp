# План нативного SP-порта

## Целевые решения

- Сначала Android ARM64, графический прототип на OpenGL ES 3.x.
- Начальные версии проверять на современной системе Android и фиксировать
  точные ABI/API/graphics requirements после рабочего renderer-прототипа.
  Поддержка всех устройств пока не заявляется.
- Общий C++ core и декодеры не должны зависеть от Android/Windows API.
- Vulkan рассматривается после рабочего baseline и замеров. Его наличие
  само по себе не решает задачи форматов и игрового поведения.
- iOS потребует отдельного platform/graphics backend и проверки на Apple
  toolchain; WebAssembly — отдельной сборки и потокового доступа к данным.
  Нельзя рассчитывать загрузить все гигабайты игры в память браузера.
- APK проверен в эмуляторе x86_64. Тест ARM64 на физическом устройстве впереди.

## Этапы и проверяемые результаты

| Этап | Результат и критерий готовности | Состояние |
|---|---|---|
| 0. Инструменты/идентификация | REA+Ghidra readiness, hashes, инвентаризация | Выполнено; MCP подключён после перезапуска |
| 1. Контейнеры | Чтение PK4/SPK/MPK, реальные файлы и повреждённые входы | Оба патча проверены. idTech4A++ читает именованные ресурсы через `idFile`, ядро собрано для ARM64 и x86_64 |
| 2. Одна статическая сцена | Геометрия, текстуры, расстановка моделей и сравнение с PC | Показан текстурированный склад, размещена модель вагона на вокзале, есть парные измерения PC |
| 3. Нативный Android viewer | ARM64 APK, GLES3, свободная камера, загрузка своей карты | Склад с 53 текстурами проверен на Android 15 x86_64. Собраны ABI arm64-v8a и x86_64 |
| 4. Мир и движение | Коллизии, player controller, карта/переходы | Native player на source empty теперь использует owning HKXS index без brush fallback. Source masks/cache/leases проверены. Geometry/hierarchy 27 карт прочитаны; runtime filters, другие тела и переходы требуют привязки |
| 5. Анимация и взаимодействия | Skeleton/bind pose, animation, doors/triggers/weapons, наблюдения против PC | Source body импортирован в настоящий MD5 renderer; native weighted snapshots и fractional source poses проверены. Animation manager/entity playback, head/face, оружие и противники впереди |
| 6. Игровая логика SP | AI, миссии, Veil, сохранения, звук и интерфейс | Structural BRAI:633 ресурса. В native game работает bounded literal OnSpawn/OnUsed/Wait/UseTarget/SetGlobalVariable host. Большинство действий, retail globals/clock/save и mission entity import ещё не реализованы |
| 7. Кампания и оптимизация | Проходимость миссий, regression captures, Android hardware tests | Не начат |
| 8. Другие платформы | iOS и WebAssembly backends + отдельные runtime checks | Не начат |

## Текущий приоритет — общий runtime

По просьбе владельца главный объём работы теперь приходится на reverse
engineering и переносимый C++ engine/game code. Android host 0.9 уже запускает
штатный runtime и служит для проверки изменений ядра. Следующие APK нужны
после существенного продвижения движка; создание платформенной обвязки не
заменяет восстановление исходной игровой логики.

Автономный viewer 0.8 остаётся проверочным инструментом. Следующий основной
результат — загрузка игровых данных через штатные подсистемы закреплённого
idTech4A++, с повторным использованием GPL Doom3 game module там, где его
поведение подходит. Это не готовая совместимость с Wolf2009 ABI 19.

Работа разделена между тремя независимыми направлениями:

1. Штатный `common->Init`, загрузка GPL `libgame.so`, session, `devmap`,
   кадры и освобождение ресурсов. Первый тест использует собственные
   минимальные GPL-ресурсы и не требует коммерческой игры.
2. Оригинальные PROC/MODL/TXTR через `idRenderWorld`, model manager,
   material/image loaders. Проверять `empty`, `trainyard` и `mtw_ware`,
   отдельно отмечать отсутствие BSP/PVS и неподдержанные material stages.
3. Точные декодированные HKXS/ENTS через штатные collision/clip,
   `idPhysics_Player`, mover и trigger/target lifecycle. Неполную коллизию
   отвергать, не заменять её границами render-модели. Сначала проверить
   собственную комнату и полностью декодируемую `empty`.

Первый общий engine checkpoint прошёл на Android x86_64: native game/physics
на собственной комнате и оригинальной `empty`, штатные world/model/image
loaders и opaque GPU A/B на `empty`/warehouse, включая нормальный teardown.
Portable transforms/hulls и triangle-buffer readers прошли все 27 карт.
К ним добавлены общий rounded support backend, BVH, сферы/капсулы и явный
cylinder virtual-envelope profile. Android shell этого runtime уже проверен.
Source collision уже подключена к native player для строго принятой `empty`,
а ограниченный BrainTask host — к настоящим GPL entities. Следующий результат —
исходные игровые entity bindings, анимация через штатный game lifecycle и
квалификация фильтров/других тел на картах кампании. Полнота reader не означает
проходимую карту. [Границы физики](COLLISION.md).

Общий BRAI reader сохраняет узлы, связи, условия и непрозрачные поля для
всех карт. Обработчики действий добавляются по измеренному поведению
оригинала, с положительными и отрицательными тестами. Успешный разбор
графа не означает, что он уже исполняется. Подробности — [BRAINS.md](BRAINS.md).

Критерий следующего этапа — доказанный engine/game lifecycle, реальные
миры и модели через обычные интерфейсы, движение и взаимодействие через
игровой модуль. Headless-тест отдельно от видимого кадра и Android APK.

`GetGameAPI` и наблюдаемое поведение оригинала исследовать отдельно, сохраняя
адреса/хеши/выводы в локальном evidence. Не подменять missing gameplay модулем
Quake4 или загрузкой Windows DLL. Прохождение всей кампании требует большого
объёма отдельной реконструкции; срок сейчас оценить надёжно нельзя.

## Checkout и выпуск

`tools/prepare-engine.ps1` готовит sparse checkout idTech4A++ с патчем
для архивов. Текущий автономный Android viewer собирается с JDK 21 и NDK r30.
Интеграция игрового цикла с idTech4A++ ведётся в `src/idtech4a_boot`,
`src/idtech4a_render`, `src/idtech4a_gameplay` и `tests/engine-runtime`.

Публичный репозиторий содержит наши исходники, тесты, документацию и патч.
Локальные evidence, toolchains, build outputs и коммерческие игровые файлы
не входят в публикацию. Исходники опубликованы в
[CheffCorleOne/wolf2009-sp](https://github.com/CheffCorleOne/wolf2009-sp).
