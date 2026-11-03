# Точка продолжения

Проект: `C:\Users\temir\source\wolf2009-sp`, отдельный от установленной игры.
Публикация: https://github.com/CheffCorleOne/wolf2009-sp, branch `main`.
Оригинальная установка и существующие сохранения используются только для чтения.
Изолированная PC-копия и профиль находятся в `evidence/runtime-game/SP`.

Цель остаётся прежней: полноценная одиночная кампания Wolfenstein 2009 на
открытом нативном движке, Android первым, затем iOS/WebAssembly. Пользователь
просит работать над общим C++ engine/game runtime и reverse engineering.
APK служит проверочным клиентом. Полная кампания пока не запускается;
не выдавать structural coverage или headless import за проходимую игру.

## Авторизация и рабочие границы

Владелец разрешил REA, subagents, сборки, эмулятор, запуск игры для исследования,
полное управление ПК и публикацию исходников/Android releases на GitHub.
Dota остановлена. Дополнительное подтверждение этих действий не нужно.
Игровые assets, EXE/DLL, локальные evidence и toolchains в Git не добавлять.
Работать из папки проекта, не из установленной игры. Использовать изолированные
копии для изменяемых экспериментов; хеши сохранений уже сверялись после PC probes.

## Текущий checkpoint общего движка

- Основа idTech4A++ закреплена на `ce49510f3b0c04969eab8ebe8238416e84e4f0bb`.
  Цепочка из 15 патчей воспроизводит 29 изменённых upstream файлов.
  9 migration/idempotence/local-edit tests прошли. Checkout в `engine/` ignored.
  `tools/prepare-engine.ps1` применяет всю цепочку; отдельные overlapping patches
  вручную не накладывать. `node tools/check-engine-patch.mjs` проверяет состояние.
- Штатные Android common/game/session init, libgame/GetGameAPI8, devmap,
  Frame и Shutdown уже проверены. Это GPL game module; оригинальный Windows
  Wolf2009 ABI19 DLL не загружается. Подробности: ENGINE.md.
- `SupportGameplayMap` теперь соединяет owning HKXS world/index с native CM
  handle zero. Generated map содержит worldspawn и исходный ENTS start; CM
  не содержит vertices/polygons/brushes. Source fallback к brushes запрещён.
  `DeclaredDoom3BitsV1` — явная GPL compatibility policy, не retail runtime filter.
  Неполные metadata/coverage, дополнительные bodies и cylinder response ambiguity
  отвергаются до binding. Source snapshots/identity/epoch и mount leases живут
  до FreeMap. Actual original-empty proof: `evidence/engine-runtime/f71799d467bb`.
  6 traces, exact contents3/material, cached feature reuse, wrong-key/externalCM/
  unmount refusal, native player stop X167.7497 и normal teardown PASS.
  Portable support-map profile: 108 checks. См. COLLISION.md и tests/engine-support.
- RoundedConvex/Bullet subset, SupportCollisionWorld/Index и CollisionFilter
  остаются общим ядром. Vendor pin/byte manifest и обратимость локальных edits
  проверены. 1352 public geometry checks; runtime fixture floor/sphere/player,
  pivot hit и certified small negative arc прошли. Широкая свободная отрицательная
  дуга пока Indeterminate. Никогда не превращать false TOI/uncertainty в clear.
  All27 geometry/hierarchy complete не означает gameplay completeness:
  cylinder selector, raw/runtime filters и extra body/entity binding ещё различаются.
- Native BRAI literal host добавляет реальные `idWolfBrainTask` и
  `idWolfBrainGlobals` в GPL game. Whole-group preflight, Think→Tick, generation
  references, CanUseKey→latch и shared initialized int32 dictionary.
  OnSpawn/OnUsed/Wait/UseTarget/SetGlobalVariable, только bounded literals.
  Actual run `evidence/engine-runtime/d0695c25d9e9`: cross-entity timed delivery,
  shared writes7/1, key gate, reset, generation reuse, rejected unsupported groups,
  65-task/provider-last quota и clean teardown PASS. 59 portable checks.
  Clock/defaults scope diagnostic; retail expressions/globalVars/save lifetime и
  большинство 154 task types впереди. Structural reader охватил все633 BRAI;
  это не выполнение всех графов. См. BRAINS.md и tests/engine-brain.
- Source-backed weighted body импортирован в настоящий `idRenderModelMD5`.
  Explicit MODL/HKXA/SKEL/LOD request, 52 named joints, source epochs/pins,
  immutable aliases, native joint quaternion conversion, owning source identity.
  Actual native CPU run `evidence/engine-runtime/8c4f32656015`: 11 poses,
  13629 triangle corners, max position error7.63e-6, UV0, native hierarchy/bounds,
  independent instances/caches, purge/reload/stale epoch/refreshed epoch PASS.
  Portable95 checks и mainLOD0/characterLOD0/LOD1 source comparisons PASS.
  Initial run имел explicit checker: diffuse0/fallback2, GPU0.
  Production `RegisterSkinnedAppearance` добавлен для original authored skin→
  entity customSkin→immutable material clones. Combined native run
  `evidence/engine-runtime/c09b600f6d02` PASS: 385482 checks,14 poses,32619 corners,
  original1024²TXTR native RGBA CRC daf354cd, defaultA unchanged, source collision,
  BRAI и world EGL regression в одном процессе. Warehouse texture/flat BMP
  побайтово совпали с previous vertex-colour proof592c0ea2e781.
  Не заявлять GPU character rendering по CPU snapshots/image decode.
  Отдельный actual GPU proof теперь прошёл: `evidence/engine-runtime/26224c856f53`,
  source body1333v/2110t/52j+original1024²TXTR/nativecustomSkin, two suppliedposes,
  textureA/B24433pixels/pose41950pixels, allreadbacksGL0. Три кадра просмотрены,
  native world/model teardown released mounts. Это body renderer proof, без
  головы и game-entity playback; не расширять его на готового NPC/освещение.
  Animation manager/entity playback, head27, facial/copy timing ещё не подключены.
- Полный portable CMake core прошёл 46/46 CTest suites. Current QA x86 build:
  `build/engine-android-x86_64`; production ARM build:
  `build/engine-runtime-android-arm64-v8a`. Native source поддерживает C++98
  baseline; новые portable consumers — отдельные C++11 OBJECT targets.
  QA definitions/exports opt-in, production defaults OFF. Current ARM build
  прошёл полностью; QA exports отсутствуют, ELF LOAD align16KiB. Физического
  ARM64 runtime proof пока нет.

## Проверки и следующие задачи

Не повторять полную инвентаризацию 1434 архивов или успешные broad tests без
новых изменений. Для следующих шагов нужны source entity/class bindings,
штатное runtime animation playback, другие collision bodies и retail filters,
затем AI/оружие/миссии/сохранения. Не возвращаться к ручной отдельной сцене
вокзала или постоянному перевыпуску APK вместо общего движка.

REA установлен и подключён. Skill
`C:\Users\temir\.agents\skills\reverse-engineer-anything\SKILL.md` v49
уже применялся; source architecture analysis не требует нового REA session.
Оригинальные hashes/адреса и raw RE findings находятся в ignored evidence;
сводка — RESEARCH.md / REA-SETUP.md. Использовать результаты прежде чем
открывать новое Ghidra workspace. CLI fallback: `tools/rea.ps1 doctor`.

## Android клиент

Публичный engine APK всё ещё `0.9.0-engine-dev`, code10, source `42edc34`,
SHA256 `8f7d4e76fe106fea2050f16c3c84678069707b2e368ebc72846e33ed038b9fd4`.
Он запускает текстурированную original empty и Doom3-baseline player/touch,
прошёл SurfaceView/Home-resume/exit на Android15 x86_64. Старый0.8 — отдельный
viewer с поездкой. Новые engine commits не считать установленными в APK.
Активный QA target: `emulator-5556`; проверять `adb devices` перед запуском.
ARM64 проверяется сборкой, физического Android-устройства у владельца пока нет.
Для современного Android выбран GLES3 baseline; iOS/WASM ещё не портированы.
