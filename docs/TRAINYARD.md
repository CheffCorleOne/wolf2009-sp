# Нативный старт trainyard

Оригинальная карта теперь загружается обычным source-qualified `devmap` в
общий C++ engine/game runtime. Создаются исходный игрок и выбранный вагон;
его OnSpawn запускает нативный pusher. Игрок едет за счёт контактов физики,
вагон останавливается у исходного `train_stop`, arrival выдаётся один раз.
Это начало карты; дальнейшая миссия и бой ещё не реализованы.

Engine APK0.10 содержит этот runtime для arm64-v8a и x86_64. В выделенном
Android15 x86_64 эмуляторе проверены загрузка, ограниченный маршрут ходьбы,
обзор, прыжок и возврат из фона. Обычный SAF-импорт пока выбирает только empty;
для trainyard нужен явный [developer staging](ANDROID.md). Полная миссия,
дверь и встречающий в этом APK ещё не работают.

## Общие механизмы

`SourceMapBodies` владеет исходными HKXS/ENTS и строит точные связи тел мира,
экземпляров и неподключённых ресурсов. Имена и raw motion/filter words
сохраняются; классификация не означает activation/dynamics. Batch всех27 SP
карт:11953 bodies,56139 ENTS,27 worlds,4702 exact instance links,7224 resource
names,0 other unbound. Это проверка связей, не запуск всех уровней.
Набор включает 26 карт `maps/game` и одну служебную `maps/tools/mv2`.

`SourceCollisionBinding` передаёт через существующий readonly API8 filesystem
явный запрос тела и возвращает CM alias с policy/epoch/bytes/fingerprint.
Игровая сторона держит descriptor lease; CM binding повторно проверяет
источник и удерживает свой mount pin. Source collision не заменяется render
bounds или brush boxes. Идентичность треугольника включает occurrence,
region, shape/record offsets, subpart и triangle index; siblings не теряются.

Профиль `NativeSourceWordsCylinderEnvelopeV1` отдельно допускает source child
words и native response materials, сохраняя исходные поля и qualification
flags. Полный uint32 word материала и root gate остаются разными уровнями:
для trainyard OR child words0x0227403f, root0x0207403f. Native query использует
свой явный mask profile; оригинальные userData/veto правила не восстановлены.
Цилиндры используют opt-in envelope32 восстановленных virtual vertices плюс
исходный radius. Его selector не равен retail Havok; это явно обозначенная
native compatibility. Строгий прежний default продолжает отказывать.

`NativeSourceMover` использует настоящие `idMover`/`idPhysics_Parametric`,
compound CM и native passenger contacts. Подготовка читает owning ENTS/BRAI;
OnSpawn доставляет владелец уровня после сборки. Поддержан восстановленный
direct OnSpawn→MoveToEnt profile, исходная скорость/цель, защита повтора,
независимые экземпляры и однократный arrival. На endpoint синхронно публикуется
предусмотренный native snap; покадровой коррекции игрока или платформы нет.
Поздние BRAI outputs сохраняются и не объявляются исполненными.

`NativeSourceStaticSkin` преобразует только исходные explicit remap pairs,
проверяет native/source XYZ/UV/RGBA и удерживает оригинальные источники.
У вагона skin `box_car_full_in` существует, но его full_out/full_in пары не
совпадают с десятью material names текущего MODL. Сохраняется явный identity
customSkin с authoredNoOp=true; missing/unsupported skin не подменяется.
Отдельный opt-in `BaseDiffuseWithOmittedVerifiedModulationV1` пропускает только
проверенные классы AmbientDecalModulate/2xModulate через частный customSkin.
Это три поверхности/100 треугольников: их opaque checker fallback перекрывал
рабочие материалы. Остальные семь поверхностей/3358 треугольников используют
исходные diffuse textures. Все десять геометрических поверхностей, исходные
ColorMap DDS, UV и цвета сохранены; strict importer и материалы других
экземпляров не изменяются. Правильная композиция этих декалей ещё не реализована.
Material catalog загружается до первого импорта моделей и ранней проверки
`.map`, которая происходит до game::InitFromNewMap.

## Выбранный исходный экземпляр

Source profile `MeasuredTrainyardIntroV1` выбирается явно в локальном
`wolf2009-session.cfg`. Ordinary Doom/source empty пути сохранены.
Source player берётся из compiled startup: health100, mass200, отдельные
stand/crouch hulls50/144/96, slots0/1/11, selected weapon_none.

Вагон ENTS2129 `starting_boxcar_model` не имеет одноимённого HKXS body.
Он использует `models/vehicles/box_car_a`:66 boxes,8 hulls,12 cylinders,
все86 raw child words0 и template root0. В согласованном isolated original
runtime query измерено единственное принятое non-world body с root/contents3,
и тот же script ray вернул этот вагон на полу Z295.52. Поэтому именно этому
экземпляру задан явный native response3; общий default func_mover не выведен.
Source zeros/COM/swept state сохраняются. Модель подключена в проверенном
local shape frame; COM не используется как смещение геометрии.

Selected func_mover declaration содержит только spawnclass=idMover,
noDefaultPhysics1, rotateForever0, classname=func_mover. Профиль проверяет эти
поля. Raw bind=starting_boxcar сохраняется; отсутствующий родитель не
выдумывается. Для этого измеренного экземпляра явно применяется установленное
world placement (256.38,-80.02,256), identity axis. Исходный BRAI reference
`trainyard/movetrain.brain` разрешается в точный typed resource
`BRAI/brains/trainyard/movetrain`.

## Фактически проверено

Android15 x86_64, run `9463fe061801`, PASS:

- обычный devmap,535 body records/2238 ENTS, мир29812 shapes;
- 1438 game checks/790 frames, source player и86-shape compound wagon;
- wagon delta24.000004, rider delta23.999634, floor origin Z295.588898;
- source speed25, native endpoint12800ms, arrival at Y239.98, one delivery;
- actual `game->Draw`:1380 surfaces, из них5 visible wagon; hidden1375/0;
- geometry presence A/B173682 pixels; restored RGB0, unchanged camera/time/
  physics, без переноса камеры и отдельного preview world;
- map shutdown: players/weapons/views/movers/skins/profile/session0,
  animation descriptors0.

Отдельная independently authored mover QA `d095870590ad`:146 checks/204frames,
два независимых pushers, пассажир, duplicate spawn, exact endpoint, arrival,
остановка при потере owner. Portable core74/74 CTest; targeted source binding
tests повторены после guards для неизвестных storage metadata/foreign decls.
Final source-session regression `b1eef2682ddf`: player51/399 frames,
mover146/204, MP40924+924 checks и actual first-person GLES (source texture
pixels37816, restored0, fire30519/reload58152), shutdown0. Отдельный combined
legacy run `14a95ae2ecb4` сохраняет actor293/entity243/AF518, BRAI/V1/V2/mesh/GLES.
Source wide-TRM и legacy save-wire QA запускаются отдельно: попытка записать
wide hull в legacy record корректно отказывает. ARM64 production core/game
собраны с QA OFF; физического ARM-теста и нового APK нет.

Повторение: сборка native QA с animation V2+GLES, затем
`tests/engine-runtime/Test-EngineRuntime.ps1` с явными `-Serial`,
`-BuildDirectory`, owner-supplied `-RendererGameRoot`, `-WithTrainyardChecks`
и при необходимости `-WithOffscreenChecks`. Оригинальная установка readonly.
Raw logs/кадры/assets остаются локально и в Git не входят.

## Что ещё отсутствует

2235 ENTS не имеют native gameplay bindings;8 поздних узлов wagon BRAI пока
не исполнены. Дверь/встречающий, бой/AI/navigation, Veil, objectives, original
save/load и переходы кампании впереди. Native pusher переносит actors, но
compound mover не делает собственного world sweep (`PUSHFL_CLIP` требует
convex trace model). Retail clock/physics/filter/camera parity не заявляется.
Player body всё ещё собственный bootstrap placeholder. В выбранном вагоне
три modulation layers явно пропущены; прочие неподдержанные shader compositions
сохраняют checker fallback. Присутствие геометрии на GPU не доказывает retail
texture/shader parity.

Это проверенный engine milestone, не проходимая миссия и не новый APK.
