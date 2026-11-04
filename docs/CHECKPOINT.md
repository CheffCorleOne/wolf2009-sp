# Проверенный этап: trainyard в engine APK0.10, 2026-11-04

Рабочая ветка — `integration/trainyard-runtime-20261104`, поверх проверенного
игрока/MP40 `99d901f` и source trainyard `e38f8e8`. Исходная установка
и сохранения не изменены; коммерческие данные остаются вне Git.

Обычный source-qualified `devmap` загружает мир trainyard, исходного игрока
и выбранный вагон. `idMover` с compound source collision перевозит игрока
нативной физикой до исходного `train_stop`; arrival выдаётся один раз.
В кадре обычной камеры видны исходные геометрия и diffuse textures.
Три неподдержанных modulation-декали явно пропущены отдельным customSkin;
исходные геометрия, DDS и strict importer сохранены.

Общие HKXS/ENTS body ownership, readonly API8 body binding, source identity/
mount leases, response profiles, native mover и static skin вынесены из
сценария карты. Body plan проверен на всех 27 картах: 11 953 тела,
4 702 точные instance-связи и 7 224 неподключённых resource bodies.
Это подготовка общей загрузки, а не автоматический запуск остальных миссий.

Проверено на Android 15 x86_64:

- `09cab1a843c5`: trainyard 1438 game checks / 790 frames; мир 29 812 shapes,
  вагон 86 shapes; delta вагона 24.000004, игрока 23.999634; arrival один раз;
- настоящая game camera: 5 visible wagon surfaces → 0 hidden → 5 restored,
  A/B 173 682 pixels, восстановление RGB без отличий; камера/время/физика те же;
- `b1eef2682ddf`: игрок 51 checks / 399 frames, два тестовых movers 146 / 204,
  MP40 924 + 924 checks, прежние texture/fire/reload GPU проверки PASS;
- отдельный legacy combined `14a95ae2ecb4`: actor 293 / entity 243 / AF 518,
  BRAI/V1/V2/mesh/GLES PASS. Legacy save-wire и source wide-TRM проверяются
  раздельно; отказ записи wide hull в legacy record сохранён;
- map shutdown освобождает players/weapons/views/movers/skins/profile/session,
  animation descriptors — 0; все native процессы корректно завершились.

Portable core 74/74 CTest PASS; после последних admission guards повторены
targeted 2/2. Source body plan 2/2, static-skin policy 2/2, Node 15/15 PASS.
24 патча / 41 upstream file точно совпадают с подготовленным checkout.
Production ARM64 и x86_64 пересобраны с animation ON / всеми QA OFF.
APK0.10 установлен в выделенный эмулятор; хеш извлечённого установленного APK
совпадает с артефактом: `2b75bc10322aebdc913225471b0bbe744fe3e3f015a259d38420b0b32daec008`.
Проверены загрузка исходной trainyard, маршрут left1.2s / forward1.8s /
left1.8s, дополнительное движение/обзор/прыжок и Home → возврат в тот же
процесс. Старый сбой длинной грани вагона не повторился; новых failure captures
и AndroidRuntime ошибок на этом маршруте нет. Это ограниченный smoke test,
а не проверка всей физики карты. Физический ARM64 ещё не проверен.

В общем rounded solver добавлены проверка другой кандидатной нормали на тех
же двух witness points и независимый сертификат разделяющей плоскости для
Distance/Overlap. Исходные tolerance/radii/filters сохранены. Rounded tests
1372 checks и collision-index315 checks/23 linear comparisons PASS.
Node inventory + APK verifier:20/20 PASS. APK ELF/ZIP16KiB, подпись и source-only
allowlist PASS; игровых ресурсов в нём нет. Обычный SAF выбирает только empty,
trainyard требует [developer staging](ANDROID.md).

**Проходимых миссий пока нет.** После поездки ещё отсутствуют дверь/встречающий,
бой, AI/navigation и продолжение BRAI: 2235 ENTS не имеют native gameplay
binding, восемь поздних узлов wagon brain не исполняются. World response,
cylinder envelope и физика используют явно обозначенные native policies;
retail parity не заявляется. Следующий сценарий — цепочка после прибытия,
общий entity dispatcher и первый противник в том же runtime.

Подробности и повторение: [TRAINYARD.md](TRAINYARD.md).

## Предыдущий этап: игрок и MP40

Работа возобновлена. Изменения Gemini из `c730d3d` интегрированы как `676dce3`,
затем исправлены и дополнены Codex. Рабочая ветка:
`integration/player-mp40-20261104`. Последний опубликованный baseline `b72c4bf`.

Обычный source-qualified devmap создаёт native source player с исходными
health100/mass200, trainyard slots0/1/11 и отдельными stand/crouch hulls
50 vertices/144 edges/96 faces. На оригинальной `empty` проверены движение,
стена, скольжение, crouch/blocked stand, jump, native death/respawn.
Стартует `weapon_none`; явная dev-команда выдаёт видимый MP40.

MP40 использует исходные 49-joint rig,7 surfaces,4808 vertices/6488 triangles,
три базовые поверхности с текстурами и четыре скрытые upgrades, source-time
idle/fire/reload. Проверены32 патрона, interval120ms, попадание/урон5, промах,
hold/release, empty click, reload/ammo conservation и reset. Один controller
исполняет source frame commands. Reload проигрывает исходный sample; POI/eye
имеют явно ограниченную native presentation policy.

Фактический Android15 x86_64 run `80c55add17dc` PASS:

- native hull214 checks; player51 checks/399 frames;
- weapon924 checks/402 frames без view и924/400 с оригинальным view;
- session24 checks, input guards, reentry, dev equip/view lifetime;
- обычный `game->Draw`:3 gun surfaces, texture A/B37816 pixels,
  restored0, fire30519/reload58152; native palette и reload emitter PASS;
- map shutdown: players/weapons/views/profile/session0, animation descriptors0;
- согласованная core/game ABI-пара;23 patches/40 files exact PASS.

Отдельный regression run `ae9480525c5a` сохранил actor293/entity243/AF518,
BRAI/V1/V2/mesh/GLES и descriptors0. Пересобранный portable core72/72 CTest
и Node15/15 PASS. Sound standalone3/3 PASS с actual MP40/trainyard inputs.

Это рабочий engine test, ещё не проходимая кампания и не обновление APK0.9.
ARM64 production core/game также собраны с QA OFF; запуск на физическом
телефоне ещё не проверен. Body остаётся собственным
bootstrap placeholder; retail camera, spread, filters/keepDistance, полноценные
weapon effects, AI, миссии, Veil и saves впереди. См. [детали](PLAYER-WEAPON.md).

## Исторический checkpoint перед паузой — b72c4bf

Ниже сохранён итог прежней паузы. Упомянутые там неприменённые MP40/wide-TRM
работы теперь интегрированы; текущий результат описан выше.

На этом историческом этапе разработка была приостановлена по просьбе владельца.
Возобновлённый этап описан выше. Цель остаётся прежней: открытый нативный движок всей
SP-кампании Wolfenstein 2009, сначала Android. **Проходимых миссий пока нет.**
Исходный код опережает опубликованный developer APK0.9; новый APK на этом этапе
не собирался. Оригинальная PC-установка и её сохранения не изменены.

## Проверенный результат

Общий C++ runtime читает исходные ENTS и effective compiled declarations,
создаёт настоящий native класс для выбранного `rvAnimatedContainer`, сохраняет
имя и размещение, подключает оригинальные модель, анимации и два звука.
В обычном игровом цикле проверены:

- независимые экземпляры исходных объектов917/921;
- `start_anim` с конечным исходным числом повторений2^30, без переполнения;
- Use/Activate, `anim1=open`, снятие usable, звук первого кадра и конечная поза;
- исходная задержка1200ms и реальная выдача GPL `idItem` в inventory игрока;
- session persistence при повторном создании, generation-safe opener и
  удаление объекта с ожидающим событием;
- отдельный AF-профиль: после анимации подключается настоящий `idPhysics_AF`,
  WORLD hinge и два joint modifiers; проверены движение крышки, ограничения,
  сохранность корня, независимое delayed give и удаление активной физики.

Основной совместный Android15 x86_64 прогон `acf35e9254fe` завершился PASS:
293 actor-binding checks/10 отказов/9 poses;243 entity checks/235 frames;
518 AF checks/134 frames. Общие player/BRAI/V1/V2/mesh/GLES проверки тоже PASS,
дескрипторов после shutdown0. Source/native joint error в actor-binding
не превышает9.54e-7. Это тест игрового модуля; отдельного GPU-подтверждения
именно нового контейнера здесь нет.

Переносимый набор прошёл72/72 CTest. Последующий отдельный SourceRagdoll
прогон прошёл56 synthetic checks, включая границу допустимого AF retarget,
и оригинальный HKXR2 bodies/1 hinge/2 bones. Патчи движка:20 по32 upstream
файлам, exact prepared checkout проверен. Последняя ARM64 production сборка
предшествует этому checkpoint; новую ревизию ARM64 и физический телефон
ещё нужно проверить.

## Явные ограничения совместимости

- Это создание объектов через game-private factory и тестовый harness;
  автоматическая загрузка всей карты/кампании ещё не реализована.
- NativeNearUnitPlacementV1 сохраняет raw authored matrix, но для native
  physics/render нормализует небольшой масштаб, максимум0.5%. У921 исходное
  XY-сжатие0.2754%, измеренная верхняя граница смещения0.0922 game units.
  Это ограниченная аппроксимация, не полноценный affine renderer.
- AF-профиль использует одну подвижную крышку и фиксированный WORLD hinge.
  Пять collision walls исходного root не подключены: collision coverage
  явно неполна. Source rounded box заменён консервативной native оболочкой;
  excess≈0.18013 game units. NativeRestDelta допускает source-to-native
  anchor projection≤0.5 game units; измерено0.336277. Source mass5kg и torque
  cap сохранены, inertia/damping/gravity — явная native совместимость.
- Проверенное управляемое изменение угла крышки≈0.35495°, drift native
  anchor0. Отдельный эффект gravity в этом прогоне практически нулевой;
  соответствие движения оригинальному Havok не доказано.
- Session dictionary не равен retail save format; native item bounds query
  не равен исходному mask0x9000 broadphase. Original loot classes, highlight,
  actor-use notification и reverse AF→animation ещё не закончены.
- Native sound cache загрузил оригинальные SFX/SNDS, но headless callbacks
  поглощают вывод. Звук через динамики устройства и retail mixer не проверены.
- Оружие, противники, миссии, Veil, оригинальные UI/save/load и полноценные
  переходы кампании остаются в разработке.

## Сохранённые следующие работы

1. [MP40 LOD proposal](../proposals/mp40-single-root-lod/README.md) готов отдельно,
   ещё не применён.10/10 isolated CTest PASS: view rig49 joints,7 surfaces,
  4808 vertices/6488 triangles,37 poses, ошибка≤3.82e-6. Fire/reload commands,
   native/GPU и материалы остаются отдельными задачами.
2. [Wide trace-model draft](../proposals/native-wide-trm/README.md) не закончен.
   Stand/crouch игрока — исходные ConvexVertices hulls, не fallback capsule:
  50 unique native-f32 points,96 triangles/144 edges, radius0. Нужны limits
  64/160/96 и161-bit CM caches с сохранением legacy save wire.
3. Source player audit: `player_marine` действительно использует `idPlayer`.
   Можно повторно использовать GPL movement/inventory/lifecycle. Trainyard
   выбирает `weapon_none`, fists и pendant; базовые default MP40 slots не
   являются стартовым вооружением этой миссии. Source keepDistance2 и native
   clip epsilon0.25 требуют отдельного явного решения.
4. После player/weapon compatibility подключить общий импорт supported ENTS,
   source frame events, BRAI/миссии и проверяемое прохождение первого участка.

Полные binary/runtime evidence и коммерческие ресурсы остаются локально в
ignored `evidence/`. Исходники, инструкции и authored proposals сохранены в Git;
оригинальные EXE/DLL/PK4/SPK/MPK, выгрузки и credentials не публикуются.
