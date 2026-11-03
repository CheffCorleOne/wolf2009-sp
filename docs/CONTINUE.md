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
  Цепочка из 17 патчей воспроизводит 32 изменённых upstream файла.
  11 migration/idempotence/local-edit tests прошли. Checkout в `engine/` ignored.
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
  Clock static follow-up: `evidence/brain-clock-retail/FINDINGS.local.md`.
  Unit=ms доказана: Session→Frame→float32 game accumulator→trunc int→Network/Timer.
  Existing fixed16ms diagnostic clock не retail-equivalent: batching/local scale/
  stop/pause/reset/serializedfloat отличаются. Старый setter200839f0 — gameEdit,
  не обычный frame producer. Не переименовывать diagnostic-ms в retail-compatible
  без integration/runtime proof.
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
  HKXA теперь подключён к native animation manager: см. следующий пункт.
  Теперь есть отдельные native source visual entities и geometry-only head27;
  attachment, facial/copy timing и исходная actor logic ещё не подключены.
- `src/idtech4a_anim` подключает один qualified52-joint/182-frame clip к обычным
  idMD5Anim/idAnimBlend/idAnimator без bake. Core→game передаёт readonly idFile
  descriptor с exact metadata/bytes/leases через прежний API8. Private STL и
  layouts между DSO не пересекаются; file удерживает source pins независимо
  от discovery registry. Consumer освобождает modelDefs/anim refs и вызывает
  FlushUnusedAnims перед source unmount. Aliases immutable, stale epochs refuse.
  Native first `evidence/engine-runtime/bfc575ba153f` и combined
  `evidence/engine-runtime/61e7e1c5fe66` PASS: game156533checks/15poses/snapshots,
  native crossfade, bounds, legacy27controls, final7542ms tick, maxjoint1.526e-5.
  Core50checks: reload/identity/scopedremove/pins, normaldescriptors0.
  Wrapper Build-EngineRuntime со всемиQA switches тожеPASS; его финальные
  binaries повторно прошли source animator/runtime в
  `evidence/engine-runtime/55546f042f42`.
  Portablewire377/source239 PASS. ARM production sourceAnimationON/QAOFF build
  PASS, sourceQAexportabsent. Это предыдущий animator checkpoint; новый
  source visual entity proof описан ниже. Retail clock claim отсутствует.
  Profile: explicit1cyclePlayAnim, noRootExtraction/rate/loops/autoENTSframe148.
  Новые `SourceVisualEntity` проходят обычные native RunFrame/Think/Present/
  ModelCallback. Explicit registered modelDef, own private spawn args, rigid
  placement с float roundtrip, native-ms one-shot, no clipmodel/root extraction.
  Inputs preflight до spawn, bound SetModel отказ до освобождения palette.
  QA533ba87039ac: entity23130checks/506frames/twoinstances; raw palette проверяется
  ДО любых forced/callback/GetBounds calls; endpoint TH_ANIMATE inactive,
  world joints, snapshots/bounds, deletion/render entry и generation reuse PASS.
  Total animator179663checks/35poses/25snapshots, legacy27, Core50/descriptors0.
  ModelDefs/unused cached anims удерживают leases до обычного release/flush;
  нет levelowner/save/AI/classname/ENTS frame translator. Native SpawnEntityType
  allocation-exception rollback по-прежнему не гарантирован upstream.
- Additive mesh-v2: MODL + exact named SKEL + LOD/profile/2epochs, отдельный
  immutable namespace, без HKXA reads/placeholder. Generic SkeletonData retains
  raw reference words + qualification, ordered tree≤256; MODL bind независим.
  V1 aliases/hash/source-info/wire сохранены, v1 animation producer v2 отвергает.
  Portable198synthetic/9299source +oldV1 PASS. Native193461checks/10poses/
  23775corners, maxposition7.63e-6/bounds0: body52, selected standalonehead27
  72v97t, detailLOD0/1, UV/RGBA/appearance, cache/reload/epochs/pins PASS.
  Native CPU snapshot хранит BASE shader; effective customSkin remap проверять
  отдельно, как в старом renderer. Firstf3567 QA oracle был неверен и исправлен;
  production менять для этого не потребовалось. См. MESH_V2.md.
- Общий SplineTransformCodec — checked scalar C++11 adaptation GPL HavokLib
  ef5d5c6, provenance сохранён. Source input owns bytes/explicit absolute TRS
  ends, blocks/masks/rawoffsetfacts; scale policy explicit, floats opaque.
  Standalone2/2 PASS185synthetic/59928sourcechecks/3clips: greeter373times exact
  vs narrowdecoder ~1e-15, realpickup335frames/2blocks, idle41frames.
  НЕ generic rig/order/additive/native provider: trainyard HKXA container имеет
  187 animations, bindings0; отсутствие binding не доказывает identity map.
  Reuse static audit `evidence/hkxa-binding/FINDINGS.local.md` frozen/manifested.
  Source boundary consumer: transformStart=block+mask; floatStart=block+floatOffset.
- Новый combined actual runtime4aedf1adf84b PASS: sourceempty/player+BRAI+
  source visual/animator+meshv2+oldCPUskin+GLESworld/body в одном процессе,
  source lifetime and animation descriptors0 onnormalShutdown. GPU body по-
  прежнему suppliedposes, не entity-driven GPU proof. QA binaries x86 и
  production ARM64 fullbuild PASS; newAPK не выпускался.
- Полный portable CMake core прошёл 50/50 CTest suites. Current QA x86 build:
  `build/engine-android-x86_64`; production ARM build:
  `build/engine-runtime-android-arm64-v8a`. Native source поддерживает C++98
  baseline; новые portable consumers — отдельные C++11 OBJECT targets.
  QA definitions/exports opt-in, production defaults OFF. Current ARM build
  прошёл полностью; QA exports отсутствуют, ELF LOAD align16KiB. Физического
  ARM64 runtime proof пока нет.
  Contact collection теперь CastAll: oneBVH, bounded source-order prefix,
  full uncertainty/metadata checks даже beyondprefix. Host315checks/23linear,
  46/46core CTests и native ownfixture5b7b9f2f1b44/sourceemptyc28f7879f099 PASS.

## Проверки и следующие задачи

`SourceDefinitions` добавлен в переносимое ядро: entityDef/model lexer,
наследование с provenance, сохранение opaque commands, раздельные бюджеты
storage/resolution и transactional AddMember. Прошли 81 synthetic checks в
8 группах и selected source proof: 8 members / 100 declarations. Main и
standalone CMake — 2/2 PASS; Android ARM64/x86_64 syntax PASS.
Контекст взят из trainyard worldspawn0:
`mapSpecificDef=model_trainyard_base`, без вывода из имени карты.
Static retail parser подтвердил case-insensitive MapDef, пропуск при пустом
контексте и unconditional child skeleton override. Наш профиль допускает exact
uppercase placeholder с обязательным named context и остаётся data-only:
`gameplaySupported=false`. См. tests/source-definitions и
`evidence/mapdef-inheritance/FINDINGS.local.md`.
Default skeleton EmptyMale и animation skeleton Humanoid имеют 52 одинаковых
имени и parents, но reference arrays различаются. Current MODL bind читается
из MODL, а SamplePose — из 52 tracks. Не выводить automatic retargeting из имён.

Не повторять полную инвентаризацию 1434 архивов или успешные broad tests без
новых изменений. Для следующих шагов нужны source entity/class bindings,
generic clip/rig association и actor controllers, другие collision bodies и retail filters,
затем AI/оружие/миссии/сохранения. Не возвращаться к ручной отдельной сцене
вокзала или постоянному перевыпуску APK вместо общего движка.

Текущая параллельная работа следующего этапа: engine_boot пишет НОВЫЕ
SplineClipData.{h,cpp}+tests/spline-clip (data-only exact named HKXA reader,
без native/rig inference), engine_reuse исследует runtime clip↔skeleton
consumer в отдельном evidence/hkxa-runtime-binding. Не включать незавершённые
новые файлы в текущую публикацию автоматически; frozen codec/mesh/visual
checkpoint завершён и проверен отдельно.

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
