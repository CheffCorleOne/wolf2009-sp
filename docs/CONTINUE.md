# Точка продолжения

Репозиторий: https://github.com/CheffCorleOne/wolf2009-sp, branch `main`.
Рабочая копия находится отдельно от установленной игры. Цель — открытый native
движок всей SP-кампании Wolfenstein 2009: Android ARM64, затем iOS/WebAssembly.
Полная кампания пока не запускается; проходимых миссий нет. Goal остаётся active.
Готовность не оценивать процентом распознанных ресурсов или компонентных тестов.

## Рабочие границы

- Пользователь разрешил REA, subagents, сборки, эмулятор, управление ПК,
  запуск игры для исследования и публикацию engine source/Android releases.
  Dota остановлена; повторного подтверждения этих действий не требуется.
- Оригинальная установка, configs/registry и существующие сохранения — только
  чтение. Изменяемые PC-эксперименты используют отдельные evidence/runtime-game/SP
  и профиль. Коммерческие assets/EXE/DLL, decompiled proprietary code, raw evidence,
  toolchains, credentials и личные абсолютные пути не добавлять в Git.
- Приоритет — общий C++ engine/game runtime и восстановление поведения.
  APK служит клиентом проверки. Не возвращаться к ручной одной сцене или
  постоянной упаковке APK вместо разработки движка.

## Последний checkpoint — каталоги DECL

Этап поверх969a12e добавляет CompiledDeclarations/CompiledDeclarationCatalog,
compiled frontend SourceDefinitions и game-private SourceDeclarationRuntime.
Профиль2 LE pools/2 LF sections проверяет exact byte spans/EOF/budgets; rawId
не type. Catalog сохраняет duplicates/порядок/owning immutable entries.
AddCompiledRecord принимает только unchanged entry, сохраняет payload/pools/
packet provenance. Kind0 string,1 floatpool,2 intpool,3 inlinefloatbits,4 int32.
numericWords содержит точные bits, без выдуманных decimal SourceTokens.
Unsupported variants — явные child barriers; known malformed pool span отвергается.
Plain-source profile сохранён. Подробности: DECLARATIONS.md.

Global найден в original assets.pk4, exact maps/game/global/global.decls,
2,175,123B/FNVd79bab8fa3531c10,3246 entries/1191entityDef/21model/pools0,1016.
Global SPK не содержит DECL; loose .decls files не найдены в проверенном SP/base.
Trainyard:2,112,750B/FNVc8d044282ad8a99e,3269entries/974entityDef/31model,
pools0,1467. Empty:357,867B/641records/pools0,47. Не подменять эти источники.

В native game оба packet загружаются независимо через обычный API8 idFile;
для global QA извлекает только точный ZIP member в собственную fixture directory.
Map/Explicit требуют typed DECL path, Global — safe relative VFS .decls path.
Import1005+1212 model/entity records,13 отказов,8836checks PASS. Это DATA,
без native registry/Spawn/game-clock effects, epoch0/no mount-pin claim.
Global/map namespaces НЕ объединяются автоматически. Missing selected tree
не означает runtime absence. Native QA callback теперь принимает ДВАconstchar*.

Проверки: main63/63 CTest PASS; transport92synthetic/4standalone tests,
catalog167synthetic/2tests, frontend348synthetic/2tests +old81/49regression PASS.
Native first6a72ca2ff86e и combinedd9fd9b18b28b PASS: sourceempty/player/BRAI/
V1/V2/mesh/GLES. CoreV2 now747checks/declaration_probes1; descriptors0.
Body GPU24433/41950pixels/startBMP4be877c8…7026 неизменён. ARM64production
build PASS, QAOFF; физического ARM runtime proof нет. Новый APK не упакован.
Logs/hashes — evidence/compiled-integration и указанная runtime directory.
Одна transient sparse-checkout.lock при параллельной prepare была устранена
последовательной подготовкой; checkout/pin не менялся. Prepare mutates shared
engine checkout: выполнять один раз, независимые ABI builds — после него.

RE ledgers: compiled-decl-reader, compiled-decl-properties,
compiled-decl-numeric-values, compiled-global-location. Reuse отдельно сравнивает
overlap global/map в compiled-catalog-overlap. Следующий unlock — source-backed
load/owner/merge plan, затем исходные entity bindings. Не останавливаться на
catalog inspector и не объявлять кампанию готовой по imported record counts.

## Предыдущий animation checkpoint969a12e

Этап поверх опубликованного5921a3c добавляет immutable animation sets,
native source-frame control, multi-clip entities и model animation references.
Текущий HEAD/status сверять через Git; exact native hashes лежат в ignored evidence.

EngineAnimationSet создаёт обычный modelDef из1..64 exact logical keys и qualified
V2 aliases одной actual модели/полной ordered hierarchy. Нет flags/frame commands,
multipoint, implicit rig или выдуманных расширений. SourceControlledEntity
выполняет HoldFrame(zero-based source index), PlayOnce и автоматический HoldLast
через обычные RunFrame/Think/Present/idAnimator. Lookup — GetSpecificAnim.
Source frame выбирается напрямую по block/local coordinate, без FPS conversion.

Patch19 разрешает V2-only SetFrame с preflight до Reset/PushAnims и ForceUpdate
для same-ms/zero-weight replacement. Private frame теперь int во всех game TUs;
legacy WriteShort/ReadShort wire сохранён. V1 SetFrame/source Cycle отвергаются.
Source save/restore unsupported; fatal save refusal не вызывать внутри успешного
live-session QA: gameLocal.Error останавливает session. Длинный V2 clip>32767
runtime не проверен; authored legacy40001 проверен.

Обе modelDef factories проверяют полный immutable text БЕЗ parse перед FindType.
Иначе изменённый SetText+Invalidate мог заменить animation data живой entity
ещё до отказа. QA проверяет Find/Create/Spawn/bound SetModel, channels/raw palette/
state. Forget скрывает discovery, сохраняя immutable ownership record до shutdown;
bound entities работают и unchanged set можно зарегистрировать снова.
Entities держат дополнительные MD5 refs. Освобождать entities до native decl/model,
затем FlushUnusedAnims перед unmount. Upstream SpawnEntityType allocation-exception
rollback не гарантирован. Контракт: src/idtech4a_anim/SETS.md.

SourceDefinitions::ResolveModelAnimations — отдельный owning data-only query:
полный прежний dependency gate, per-name DAG merge/provenance/unsupported child
barriers и бюджеты. Старый Resolve побайтно сохранён. Locker даёт4 pure refs и
2 opaque frame1 sound blocks; расширения не переписываются, gameplay flags false.

## Проверки предыдущего animation checkpoint

- Main portable build + CTest build/core-windows-zig:55/55 PASS. SourceDefinitions:
  81 прежних +49 новых synthetic checks, прежний trainyard и exact locker proof.
  Standalone2/2 PASS. Дополнительных root CMake targets для refs не нужно.
- Полная цепочка19 patches/32 upstream files; checker и13 migration/idempotence/
  local-edit tests PASS. Старые патчи не переписывать; новые hooks — следующим
  патчем с сохранением preimages. См. tools/prepare-engine.ps1.
- Native x86_64 build PASS. First run05c9083f1bdb PASS; final combined247405040756
  PASS: original empty/player/source collision, BRAI, V1/V2, mesh/CPU skin/GLES.
- Controlled body335/41 +locker24/21:14524 checks,19 poses,10 snapshots,
  335 обычных game frames, max joint7.63e-6/bounds0. Same-ms/source255/crossfade,
  two entities, ceil-end hold, refusals, Forget/reactivation и legacy save PASS.
- Ordinary V2:270264 checks,78 poses,83 snapshots,1622 frames; preparse_refusals3
  в каждом из5 probes. Core742 checks/26 descriptor reads,5 ordinary+2 controlled
  callbacks,4 independent epochs. V1:179663 checks/27 legacy controls. Descriptors0.
- GLES body всё ещё использует supplied poses, НЕ entity-driven GPU. Texture24433/
  pose41950 pixels, start BMP SHA4be877c8…7026 совпал с прежним. Readbacks GL0;
  старые startup GL enum warnings не новый regression.
- ARM64 production build PASS, ANIMATION ON/all QA OFF, QA exports отсутствуют,
  ELF LOAD align16KiB. Физического ARM64 runtime proof нет.

Exact artifacts/logs: evidence/animation-set и evidence/engine-runtime/247405040756.
Все native/test shell sessions этого checkpoint завершены. См. tests/engine-runtime,
tests/engine-animation-set, tests/source-definitions и module docs.

## Сохранённая основа и ограничения

- idTech4A++ pin ce49510f3b0c04969eab8ebe8238416e84e4f0bb, ignored engine/.
  Core/game GetGameAPI8; original Windows Gamex86 ABI19 не загружается.
  Legacy C++98, private adapters C++11; STL/layouts между DSO не пересекаются.
- Все1434 архивов и геометрия27 карт уже проверены; inventory не повторять.
  PROC/MODL/TXTR работают через native world/model/image loaders.
- Source HKXS world/index подключён к CM handle0 без native brush/vertex/polygon
  fallback. Original empty/player/masks/cache/pins PASS. DeclaredDoom3BitsV1 — наша
  compatibility policy, не retail filter. Other bodies/filters/cylinder response
  ещё требуют qualification. Trainyard535 bodies включают triggers/phantoms;
  bounds не превращать в solids. См. COLLISION.md.
- Rounded support/Bullet pin d3afe76132fc76b0f9b2359cbaef47a3766e7fbb.
  CastAll: один BVH, bounded source-order prefix, uncertainty/metadata проверяются
  также за prefix. Indeterminate не считать clear.
- Native BRAI host: OnSpawn/OnUsed/Wait/UseTarget/SetGlobalVariable/shared int32.
  633 parsed graphs/154 task types не полное выполнение. Diagnostic native-ms
  clock не retail float32 batching/scale/pause/reset. См. BRAINS.md.
- MeshV2: MODL+exact named SKEL, LOD/geometry policy, независимые epochs/pins.
  Body52/head27/locker2 проверены. No-LOD slot0 — explicit policy2, не source LOD.
  MODL bind, SKEL references и animation poses — разные данные.
- SplineClipData/SourceRigAnimation используют GPL codec HavokLib pin
  ef5d5c6f5ab8d7f32cb4e8983300b8db61e4dc6b. Только explicit full local N==B,
  no binding table/identity scale. Partial/remapped/additive/head-face/automatic
  rig selection пока нет. Timed sampling direct seconds; frame helpers не
  bijective. Naive frame*frameDuration ломает shared block endpoints.
- V2 wire:4 source roles MODL/meshSKEL/HKXA/animationSKEL, все identities/epochs/
  model hierarchy проверяются. Readonly idFile владеет capability/pins независимо
  от registry. V1 aliases/sampler сохранены.

## RE и следующий шаг

Локальные frozen ledgers: hkxa-runtime-binding/hkxa-node-policy,
source-locker-binding, searchable-locker и animation-key-bridge под evidence/.
HKXA names/animations same-index; absent/exact-identity binding→NULL remap,
ordinary copy i→i требует N==B. Binding hint не равен node BlendType;
partial fill/rig association из count или имён не выводить.

Actual container_locker_foot_01 — rvAnimatedContainer; visual-template
env_locker_foot_01_anim — rvSearchable, оба parent idAnimated. Actual Activate
выбирает anim1=open, снимает usable, обрабатывает persist/giveDelay1200 и AF/
ragdoll/highlight. Это не toggle; Searchable focus/auto-close — другая ветка.
Idle/open/idle_open/close хранят2/24/2/21 frames. Полный locker пока не поддержан.

ModelDef asset token проходит FindType32 animBlendTree. Для conditional implicit
default доказана цепочка SingleKeyframe technique→Node Keyframe→общий canonical
extensionless HKXA key. Это НЕ буквальная замена.md5anim→.hkx и НЕ fixed-frame node.
Authored tree имеет приоритет. Frozen namespace ledger — animation-tree-namespace;
папки.abt недостаточно: original loader использует также compiled global.decls/
map DECL. Их framing/value формат теперь реализован, см. checkpoint выше.
Load/duplicate precedence и полный retail runtime namespace ещё не квалифицированы.

Следующий unlock — полный selected declaration catalog/override qualification,
затем declaration-driven clips и source entity bindings. GPL idAnimated полезен,
но CycleAnim→SetCycleCount сейчас не подходит source. Private nonvirtual handlers
требуют narrow hooks. Не дублировать idEventDef: CancelEvents сравнивает адрес.
Completion требует playback generation/cancel/reentry защиты; entity generation
одной недостаточно. Start_anim=idle не переводить автоматически в HoldFrame0.
Actual source side effects не игнорировать при admission.

Далее: source level/entity ownership, collision bodies/filters, animation/BRAI
actions, AI/weapons/missions/saves. Не подменять кампанию очередной visual-сценой.

## Инструменты и Android

SDK по стандартному tools/android-sdk под USERPROFILE, NDK30.0.16248370,
CMake3.22.1/Zig0.15.2. Native максимум2 jobs; agents/source замораживать перед build.
QA: build/engine-android-x86_64; production: build/engine-runtime-android-arm64-v8a.
Build-EngineRuntime.ps1 -WithWolfAnimation включает production providers;
-WithAnimationV2Checks добавляет controlled QA. Runtime scripts документированы.
REA6.8.0/skill v49 установлены; setup/doctor без причины не повторять.
Один владелец binary metadata session, cached evidence читать до новых queries.

APK остаётся0.9.0-engine-dev/code10/source42edc34, SHA256
8f7d4e76fe106fea2050f16c3c84678069707b2e368ebc72846e33ed038b9fd4.
Новые engine commits в APK не упакованы. Target emulator-5556/Android15 x86_64,
перед тестом adb devices. QA работает в unique /data/local/tmp directory без
установки/foreground приложения. Физического телефона пока нет. GLES3 baseline;
iOS/WASM ещё не портированы.
