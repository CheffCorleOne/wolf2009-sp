# Игрок и MP40

Native source session подключена к обычным `devmap`, `SpawnPlayer`,
`idPhysics_Player` и `game->Draw`. API8 сохранён; Windows DLL не загружается.
Явный data-only `wolf2009-session.cfg` выбирает четыре compiled catalogs,
исходного игрока, стартовое снаряжение и точные stand/crouch HKXS bodies.
Без конфигурации остаётся прежний Doom gameplay path.

Trainyard выбирает `player_marine`, health100/mass200, slots0/1/11 и
`weapon_none`. Base defaults с MP40 не подменяют старт миссии. Дев-команда
`wolf2009DevEquipMP40` создаёт fresh retained view/presentation owner и выдаёт
32 clip/32 reserve; это отдельная тестовая выдача, не исходный pickup.

Обе формы игрока используют точные native-f32 support points:50 vertices,
144 edges,96 triangular faces. Общий trace-model ABI64/160/96 и161-bit CM
caches собраны в core и game согласованно. Legacy save record остаётся
32/33/16; wide trace не сериализуется в старый формат.

MP40 view загружает именованные исходные MODL/SKEL/HKXA через readonly VFS:
49 joints,7 surfaces,4808 vertices,6488 triangles. NativeBaseDiffuseV1 выводит
три базовые поверхности с original sleeve/hand/gun diffuse; четыре upgrades
скрыты. Исходные shader facts и все hide names сохранены. Несовпадающее
унаследованное имя enemy sleeve не создаёт отсутствующую геометрию.
Idle/fire/reload имеют80/5/48 frames и исходную длительность.

`SourceWeaponController` — единственный scheduler frame commands. Frame1
`weapon_attack` делает native ray/Damage с исходным scalar5; clip32 и interval120ms
сохранены. Reload переносит ammo в конце source clip. Исходные POI/eye/sound
команды доходят до отдельного presentation owner. NativeWeaponLocalAimAndPointTrackingV1
отслеживает animated wrist и сглаживает weapon-local aim; retail camera solver
этим не восстановлен. Source reload sample запускается на SND_CHANNEL_WEAPON.

## Проверка

Android15 x86_64, `80c55add17dc`, согласованные libdoom3/libgame, PASS:

| Проверка | Результат |
|---|---|
| Wide native trace/cache/rotation |214 checks |
| Source player |51 checks/399 frames: walk, wall slide, crouch, blocked stand, jump, death/respawn |
| MP40 без visual |924 checks/402 frames |
| MP40 с original visual |924 checks/400 frames: full clip, hit/miss, hold/release, empty/reload/reset |
| Session |24 checks: source input guards, repeated begin, visible dev equip, ownership |
| Настоящий game camera/GLES |3 gun surfaces, original texture contribution37816 pixels, restore0 |
| Source animation/GPU |fire30519/reload58152 changed pixels, original49-joint palette |
| Reload presentation |frame20: POI3/eye1/sound1, original sample/native emitter active |
| Shutdown |players/weapons/views/profile/session0, animation descriptors0 |

Синтетические fixture assertions исправлены по native geometry conventions:
CM epsilon применяется вдоль нормали наклонной грани; positive idRotation
поворачивает clockwise. QA ceiling создаётся как concrete idSpawnableEntity.
Это исправления тестовых ожиданий, не подгонка production collision.

Повторный тест использует `tests/engine-runtime/Test-EngineRuntime.ps1` с
явными `-Serial`, `-BuildDirectory`, owner-supplied `-RealEmptyGameRoot`,
`-RendererGameRoot`, `-WithSourceSessionChecks -WithOffscreenChecks`.
Нужна сборка с WOLF2009_ANIMATION_V2_QA и WOLF2009_RUNTIME_GL_QA.
Никакие исходные assets или локальные кадры не входят в Git.

## Границы

Это автоматизированный engine scenario на original empty, не миссия trainyard.
Player body использует собственный bootstrap placeholder. Native movement,
eyeheight/POI, clock, scalar damage, ammo slots и base diffuse — явные
compatibility policies. Retail keepDistance2/filters, camera recoil/spread,
shader parity, muzzle/impact effects, complete weapon sound и pickup behavior
требуют отдельной реконструкции. Headless sink не доказывает слышимый звук.
Сохранения/AI/миссии/Veil не реализованы; проходимых миссий0. APK0.9 не обновлён.
ARM64 production core/game собраны с отключёнными QA; физический ARM runtime
на этом этапе не проверялся.
