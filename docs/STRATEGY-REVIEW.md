# Подход к разработке, 2026-11-04

После исчерпания квоты Antigravity владелец вернул реализацию Codex и разрешил
параллельные задачи без режима экономии. Gemini сейчас не используется.
Его полезные изменения проверены и интегрированы; новая работа идёт в общем
C++ runtime, затем проходит фактический engine/game тест.

## Принятый результат Gemini

В отдельном worktree Gemini выполнил аудит snapshot `6d747fd` и подготовил
commit `c730d3d`; Codex интегрировал его как `676dce3`. Взяты:

- source session wiring и native game QA entrypoints;
- ранний отказ повторного BeginSourceSession с сохранением active session;
- guard stock Next/Prev/Select/NextBest/Drop для исходного player inventory;
- dev MP40 equip и явный режим source-session в Android runner.

Ревью и запуск нашли ошибки парсера idDict/idLexer, reserved VFS path,
startupLibrary key и source map name. Они исправлены. Нормальный `devmap`
теперь действительно создаёт source player. Код не принимался по одному
отчёту Gemini или успешной синтаксической проверке.

Codex завершил MP40 view/presentation, исходные текстуры/rig/idle/fire/reload,
единый frame-command scheduler, reload audio и GPU-проверку обычного game camera.
Сценарий source empty/player/MP40 прошёл на Android15 x86_64 (`80c55add17dc`),
включая полный clip, damage, reload и cleanup. Отдельный прежний combined
runtime (`ae9480525c5a`) и переносимый набор72/72 также PASS.
Подробности: [игрок и оружие](PLAYER-WEAPON.md).

## Архитектура и критерий работы

Расширение закреплённого GPL idTech4A++ остаётся практической основой:
уже работают native filesystem/render/game/physics. Существующие GPL
движение, clip, animator, event queue и renderer используются там, где
соответствуют выбранной совместимости. Независимый C++ source compatibility
layer читает оригинальные данные и реализует недостающие SP классы/действия.
Исходный Windows game module ABI19 не загружается через Doom API8.

Работа принимается законченным игровым сценарием: исходные ENTS/startup
данные → реальные native entities → поведение → кадр/эффект → cleanup.
Component decoders, admission profiles и число тестов сами по себе не
означают готовность миссии. Если поведение оригинала неизвестно, оно
исследуется отдельно; native approximation обозначается прямо.

Начало trainyard уже прошло этот путь: world collision, исходные игрок/вагон,
native pusher/passenger, OnSpawn/MoveToEnt/arrival и actual game camera.
Общие body ownership/binding, mover и static skin описаны в
[TRAINYARD.md](TRAINYARD.md). Следующий сценарий — общая доставка событий
после прибытия, дверь/встречающий, затем противник и последовательность миссии.
Trainyard стартует с weapon_none; готовый MP40
не выдаётся ему автоматически как выдуманное стартовое снаряжение.

## Организация

Root сохраняет архитектуру, проверку binary/runtime evidence, интеграцию
и итоговую приёмку. Независимые агенты получают ограниченную подсистему и
отдельные файлы; одновременно не более двух compiler jobs. Общие patches,
CMake и session lifecycle меняются согласованно. Повторный полный прогон
выполняется при новом общем риске или изменении, а не ради счётчика.

Приоритет — рабочий engine/game код. APK упаковывается после значимого
этапа или по прямой просьбе. Оригинальная установка остаётся readonly;
assets/evidence/decompiled proprietary implementations в Git не входят.
Полной кампании, AI/navigation/Veil/retail save пока нет. Срок полной
реализации и процент готовности по компонентным тестам не обещаются.
