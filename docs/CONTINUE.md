# Точка продолжения

Проект расположен отдельно от установленной игры. Оригинал используется
только для чтения. В локальном workspace есть все исходники и результаты.

1. Перезапуск выполнен; REA MCP подключён и проверен реальным Ghidra search.
   Работать именно в этой папке проекта, а не в папке игры.
2. Прочитать README, RESEARCH, ROADMAP и VALIDATION. Не повторять полную
   установку и успешные проверки без причины.
3. Проверить присутствие REA MCP tools. Если они не появились — прочитать
   installed `reverse-engineer-anything` skill и launch error. CLI fallback:
   `tools/rea.ps1 --version` и `tools/rea.ps1 doctor --provider ghidra --json`.
4. Использовать локальные evidence как отправную точку; original Gamex86
   SHA/entry address указаны в RESEARCH. Результат первой Ghidra-пробы
   описан в docs/REA-SETUP.md.
5. Текущий native APK `0.9.0-engine-dev`, code10, source `42edc34`,
   SHA256 `8f7d4e76fe106fea2050f16c3c84678069707b2e368ebc72846e33ed038b9fd4`.
   Установлен на оба AVD; `5554` получил приватные исходные assets/empty,
   но не запускался, чтобы не мешать владельцу. Нажать **Запустить empty**.
   `5556` прошёл native player/touch/surface, настоящий Home/resume и exit0.
   APK 0.8 с поездкой остаётся отдельным старым viewer release.
6. Главный приоритет изменён по просьбе владельца: общий engine/game
   runtime, а не последовательное дописывание сцены вокзала. Три направления
   — `src/idtech4a_boot` (настоящий common/game lifecycle),
   `src/idtech4a_render` (штатные world/model/image loaders),
   `src/idtech4a_gameplay` (точные collision данные и GPL game lifecycle).
   Не выдавать CPU bridge или headless init за видимый игровой порт.
   Уже прошли настоящие Android x86_64 common/game init, synthetic mover /
   player / trigger lifecycle и source-qualified original `empty` через
   native collision с остановкой на X167,7497. Рендерер штатно загрузил
   три оригинальных мира, модели и DDS. Обычный opaque GLES3 pipeline
   вывел empty/warehouse, прошёл DDS→white A/B и normal GL/game/FS teardown.
   GPU scope — x86_64 SwiftShader ES3.0, 512². См. ENGINE.md и tests/engine-runtime.
7. Общий BRAI reader `BrainGraph` проверен на 633 ресурсах из всех карт,
   включая 14 147 узлов 154 типов и 13 158 связей. Условия сохраняются,
   но generic interpreter ещё не реализован. Старый узкий OnSpawn handler
   использует общий parser с прежними ограничениями. См. BRAINS.md.
8. Незавершённая разработка viewer 0.9 содержит управление вдоль вагона и
   отдельный body/head decoder. Не смешивать её с публичным APK 0.8.
   Сравнения с оригиналом описаны в PLAYER.md. Общие проверки по пакетам
   запускаются через CMake с `WOLF2009_TRAINYARD_MPK` и `WOLF2009_SP_BASE`.
9. Portable world transforms/hulls и triangle buffers проверены на всех 27
   MPK и опубликованы. Они ещё не подключены к native gameplay importer.
   Следующий общий этап — расширение collision/material policies и SP logic
   поверх настоящего game lifecycle. Android shell и production engine/game
   для ARM64 и x86_64 уже собраны, native APK проверен на `5556`. Не возвращаться
   к отдельной реализации каждого движения сцены. Физический ARM64 ещё не проверен.

Предпочтения владельца: Android-устройства пока нет; широкая совместимость
современных Android важнее привязки к одному телефону. Выбор API оставлен
разработчику. Установленная PC-кампания запускается. Пользователь разрешил
установку REA, исследование файлов и разработку локального открытого порта;
публикация движка на GitHub и Android releases явно разрешены 3 ноября 2026.
Репозиторий создан: https://github.com/CheffCorleOne/wolf2009-sp, branch main.
Пользователь также разрешил запуск оригинальной игры для исследования.
На текущем этапе он играет в Dota: работать без захвата ввода, переключения
окон и запуска Windows-игры. Разрешены фоновая сборка с малым числом jobs
и проверки в headless `emulator-5556`; видимый `5554` не активировать.
Изолированная PC-копия загрузила `empty` и `trainyard`, данные записала
в отдельный профиль. Результаты измерений — в [PLAYER.md](PLAYER.md).
Существующие сохранения владельца сверены по хешам после эксперимента.
Для экспериментов создана отдельная локальная копия SP в
`evidence/runtime-game/SP`; копия и игровые ресурсы исключены из Git.

Git-ready root не включает `engine/`, `references/`, `evidence/`, `build/`
или локальные toolchains. Основной код reader находится в `src/wolf2009`;
engine checkout восстанавливается через закреплённый патч.
