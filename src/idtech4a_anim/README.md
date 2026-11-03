# Native source animation

Этот opt-in adapter подключает один проверенный HKXA/SKEL profile к штатным
`idMD5Anim`, `idAnimBlend`, `idDeclModelDef` и `idAnimator`. Он использует
существующий source-qualified weighted MODL importer и его 52-joint hierarchy.
HKXA остаётся spline-данными: каждый запрошенный native time в миллисекундах
преобразуется в секунды и передаётся canonical `GreeterAnimation::SamplePose`.
Промежуточный MD5 clip с запечёнными 24 Hz frames не создаётся.

Первый profile ограничен исходным
`models/characters/_humanoid_male/srp_tyd_nml_train_door_open.hkx` и явно
выбранным `humanoid_male` SKEL. Он поддерживает один явный `PlayAnim` cycle,
native channel blending и независимо созданные animator instances. Root
остаётся частью sampled pose, `GetOrigin`/movement delta возвращают ноль.
Looping, multipoint clips, изменение playback rate, root extraction и
автоматическое толкование исходного ENTS `frame` отвергаются до изменения
blend state. Head/facial clips, actor/event dispatch, retail float32 clock
accumulation и автоматический source entity/modelDef binding сюда не входят.

Сначала core регистрирует уже загруженный квалифицированный native model:

```cpp
idStr clipAlias;
Wolf2009_RegisterSourceAnimation(modelAlias, clipAlias);
```

Та же операция доступна console command
`wolf2009SourceAnimation <registered model alias>`. В GPL game module
`Wolf2009_CreateSourceModelDef(clipAlias, optionalNativeSkinAlias, outModelDef)`
создаёт обычный `DECL_MODELDEF`. Consumer вызывает обычные `SetModel` и
`PlayAnim` с явно выбранным start time. Alias модели, полная hierarchy и
текущая source identity проверяются; одинаковых 52 joint names другой модели
недостаточно. Native custom skin остаётся per-entity выбором.

Core и game обмениваются readonly `idFile` через существующий API8 filesystem.
Namespace `wolf2009/_animation/anim/<key>.md5anim` зарезервирован за runtime
descriptor. Wire v1 содержит policy, joint/frame count, IEEE64 duration и
frame duration, все три source epochs/byte counts/FNV identities, model alias,
точные MODL/HKXA/SKEL paths, clip/skeleton names и исходные HKXA/SKEL payloads.
Каждая строка ограничена 4096 bytes, payload — 64 MiB, весь envelope —
128 MiB + 64 KiB. Trailing bytes, неизвестные profiles и несогласованные
shared-mount epochs отвергаются. FNV — только local cache/byte identity,
не аутентификация; hash aliases сравниваются с полным ключом.

При открытии descriptor core pin-ит все различные mounts, повторно читает и
сверяет все три оригинальных ресурса и текущую квалифицированную модель.
Открытый `idFile` владеет своими metadata, bytes и leases независимо от
registry. `Wolf2009_RemoveAnimationBinding` удаляет только discovery entry;
открытый descriptor остаётся действительным. `idMD5Anim` владеет этим file до
обычного освобождения cached animation. Reload транзакционен и допускается
только для того же полного binding/epoch. Старый alias после remount не
перепривязывается. Закрытие animation/model owners требуется до FS shutdown.
Личные C++11 provider types и STL не пересекают API8 DSO boundary.

`idAnimBlend` семплирует исходный clip по реальному requested time. Его
длительность в native units равна `ceil(duration * 1000)`; для данного клипа
это 7542 ms. Последний tick, включая переход сразу за end time, вычисляет
точный clamped source endpoint. Quaternions конъюгируются при переводе в
native convention. Source animator bounds вычисляются после native blending,
joint modifications и visual offset по реальным skinned vertices; endpoint
bounds не объединяются вместо bounds текущей позы. Default MD5 text parsing,
frame interpolation и multipoint policy сохраняются.

`EngineAnimation.cmake` добавляется после существующих core, GPL BASE game,
renderer и stream targets. `WOLF2009_ANIMATION` и `WOLF2009_ANIMATION_QA`
по умолчанию OFF. Новые provider/QA objects используют C++11; upstream
animator/renderer sources остаются C++98. API8 import/export structs не
меняются. QA-only game symbol имеет plain C signature:

```cpp
extern "C" int Wolf2009_RunSourceAnimatorQA(const char* clipAlias,
                                            const char* otherModelAlias);
```

Core driver объявлен в `EngineAnimationQA.h`:

```cpp
typedef int (*Wolf2009AnimatorQaEntry)(const char*, const char*);
bool Wolf2009_RunAnimationChecks(Wolf2009AnimatorQaEntry gameProbe);
```

Driver использует собственные `qa_anim`, `qa_anim_skel`, `qa_anim_other`
mounts и packages `maps/game/trainyard/trainyard.mpk`,
`maps/game/global/global.spk`. Active world `main` сохраняется. Game probe
создаёт и удаляет две явные visual entities и проверяет дробные времена,
ceil-end update, source/native hierarchy, actual dynamic snapshots и bounds,
два независимых animator/cached meshes, native crossfade, reload, refusal
без blend effects и обычный authored MD5 control. Обычная cleanup освобождает
только QA definitions/models; `FlushUnusedAnims` закрывает unused providers.
Core проверяет stale epoch, scoped registry removal и descriptor pins.

Portable проверки находятся в `tests/engine-animation`: synthetic wire
roundtrip/invalid inputs и optional `--source-base` проверка настоящего HKXA,
SKEL и MODL byte identity. Последняя подтверждает source codec/timing и
различие с 24 Hz resampling, но не заменяет native animator QA и не заявляет
GPU rendering или retail actor playback. Runtime evidence дополняется только
после фактического общего engine/game запуска.

Фактический Android15 x86_64 запуск прошёл: 156533 game checks, 15 поз и
15 dynamic MD5 snapshots, один native crossfade и 27 legacy MD5 controls.
Максимальная ошибка joints — 1.526e-5, bounds — 0. Негридовое время4390ms
отличилось от 24Hz interpolation на0.001506 единицы; endpoint7542ms и пропуск
последнего tick обработаны. Проверены сохранение двух instances, hierarchy/
model identity, transactional reload, отказ Cycle/SetFrame/root-extraction до
изменения animator и сохранение активного legacy animator при таком отказе.
Ещё50 core checks покрыли full-key aliases, immutable descriptor после scoped
removal, stale epoch/remount, pins и normal cleanup. После Shutdown открытых
descriptors0. QA export присутствует в x86 проверочной libgame и отсутствует
в production ARM64. Обе engine/game ABI собраны; ARM64 на устройстве не запускался.

Для сборки/runtime проверки используйте `tests/engine-runtime` с
`-WithAnimationChecks -RendererGameRoot <own copy>`. Build script отдельно
принимает `-WithWolfAnimation` для production без QA. В combined проверке
source empty, Brain host, animator, CPU model/appearance и GLES body/world
checks прошли в одном процессе с нормальным teardown. GPU body проверка по-
прежнему получает supplied source poses; она не доказывает game-entity playback.
Ограниченный visual entity binding теперь проходит обычный `idAnimatedEntity`
lifecycle: [контракт и проверки](SOURCE_VISUAL.md). Native QA расширен до
179663 checks, 35 pose comparisons и 25 snapshots; 23130 из этих checks
проверяют две entities в 506 native frames, включая raw palette до forced
callback, endpoint/TH_ANIMATE, world placement и generation-safe teardown.
Это не импорт исходных actor classnames, AI, head attachment или source frame
semantics. Generic track decoding находится отдельно в
[SplineTransformCodec](../../tests/spline-codec/README.md); v1 provider пока
сохраняет свой один qualified clip и прежний wire.
