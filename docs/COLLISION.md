# Столкновения: общее C++ ядро

Ресурсы читаются из своей PC-копии игры. Геометрия не создаётся заново:
сохраняются исходные опорные точки, радиусы, размещение, идентификаторы
объектов и сырые слова фильтров. Собственные простые сцены нужны для
проверки физики без включения игровых ресурсов в открытый репозиторий.

## Данные и запросы

`WorldCollisionGeometry` обходит выбранное статическое тело HKXS.
Для convex-vertex shapes сохраняет также все дополнительные SIMD lanes:
оригинальная support-функция просматривает четыре вершины каждого блока.
Исходные plane-ray данные остаются отдельными; они могут не совпадать с
support-геометрией. Это различие не исправляется увеличением допуска.

`WorldTriangleGeometry` размещает треугольники, а
`HavokPrimitiveSupport` читает сферу и капсулу с их исходным радиусом.
`SupportCollisionWorld` объединяет эти представления с проверкой источника,
объекта, экземпляра, transform и wrapper radius. Радиусы обёрток не суммируются
с радиусом листа. Неразрешённые объекты остаются явными пропусками.

`RoundedConvex` выполняет distance, overlap и continuous cast для
`convex(corePoints) + sphere(sourceRadius)`. Внутри используется закреплённая
часть Bullet: 11 cpp и 41 header, scalar double. Полный physics world Bullet
не подключается; игрок и игровой lifecycle остаются в GPL game module.
Лицензия, побайтовый upstream manifest и локальный патч находятся в
`third_party/bullet_narrowphase`.

Вращение движется по кратчайшей quaternion дуге, перенос — линейно.
Для строго допущенных coordinate-axis вращений отдельный algebraic
certificate использует invariant projection и interval enclosure настоящего
scalar lerp. Он не предполагает точность sin/cos или ошибку их производных.
Нативный адаптер отдельно выражает actor относительно настоящего pivot и
разбивает полный поворот на участки до 90 градусов. Совпадающие точки и
вырожденные ядра допустимы; отсутствие определённой нормали не подменяется
выдуманным контактом.

Отрицательный результат итерационного алгоритма не доказывает свободный
проход. Результат `Separated` для cast требует независимого положительного
сертификата разделяющей плоскости на всём интервале движения. Нехватка
итераций, неоднозначное касание и неизвестные фильтры остаются
`Indeterminate`. Исходный radius, численный допуск и clearance игрока — три
разных величины.

`SupportCollisionIndex` владеет BVH и геометрией. Его bounds получены из той
же canonical rigid pose, что используется узкой фазой; сырые bounds reader
не используются для отбрасывания кандидатов. Учитывается промежуточное
вращение, а не только два крайних положения. Сохраняются ближайший известный
контакт и его источник даже при неполном покрытии выбранного тела.

Для контроллера существует явная политика `NonBlockingPhysical`: если
clearance-запрос неопределён, повторяется cast физической формы без clearance.
Пропустить препятствие разрешено только при положительном сертификате
разделения всего физического движения. Это позволяет двигаться вдоль пола,
сохраняя остановку у стены и исходные радиусы.

## Цилиндры

По умолчанию cylinder остаётся неразрешённым. Явный профиль
`RetailDefault16VirtualEnvelope` представляет выпуклую оболочку всех 32
восстановленных виртуальных вершин плюс исходный convex radius.
Кольцо нерегулярное; используются исходные perpendicular vectors и
пошаговая float32 арифметика. Поле cylRadius и convex radius имеют разные роли.

Функции проверенной Windows-копии воспроизвели 34 запроса к собственному
тестовому объекту: получены 32 вершины, координаты совпали с моделью.
При направлении 21 градус оригинальный selector выбирает вершину, которая
не максимизирует dot среди всех доступных вершин. Поэтому envelope явно
сохраняет `matchesNativeSupportSelection=false`; его геометрические запросы
не удостоверяют точное поведение retail collision response. Профиль и
наблюдавшиеся параметры не теряются после уничтожения исходного world.

## Фильтры и привязка к игре

`CollisionFilter` восстанавливает отдельные правила original ray collection,
ray root и contact pair. Нулевое child word для ray допускает запрос;
для контакта оно требует поиска root. Это разные правила. Runtime userdata,
ignore IDs и порядок mask priority нельзя получить из одного HKXS.
Неизвестное состояние даёт `Unknown`, а доказанный запрет сохраняется.

Декодирование геометрии выбранного статического тела не означает подключение
остальных rigid bodies, entity runtime, welding и materials. Для source
worlds `nativeRunnable` и `sceneClearanceVerified` остаются false.
Native bridge подключает собственную модель `wolf2009/_qa/rounded-support`
и отдельно квалифицированные source worlds. QA-модель содержит rounded floor
и sphere и не включает game assets. Bounds/contents, translation, position,
contacts, rotation и FreeMap проходят общий CM интерфейс без изменения game-import ABI.
Неразрешённый запрос не превращается в успешный native-only проход.

`SupportGameplayMap` импортирует source HKXS и ENTS одного ресурса/архива.
Виртуальная `.map` содержит worldspawn и выбранный исходный player start,
а `.cm` — ровно одну пустую `worldMap`. В ней нет вершин, полигонов и кистей:
столкновения выполняет owning support index, подключённый к handle0 до
`idClip::Init`. Отказ source `.cm` запрещает native brush/render fallback.

Первый профиль `DeclaredDoom3BitsV1` сохраняет только известные комбинации
source contents и проверяет точное равенство флагов native material. Geometry,
hierarchy, contents, child filters и contact metadata должны быть полны;
другие rigid bodies и world triggers требуют отдельных entity bindings.
Этот профиль не подменяет original BitsCollisionFilter runtime context.
Для `empty` сохранены все 10 shapes и raw contents3/solid_opaque. Native
player остановился на X167.7497; отдельные point/actual-TRM traces подтвердили
материал, биты1/2 и исключение маски8.

Архив удерживается source lease до `FreeMap`: Unmount живого источника
отклоняется. Remount получает новый process-local epoch; source identity
также включает HKXS/ENTS bytes и interpretation profile. Повторный LoadMap
проверяет эту identity, включая имена с допустимым native `.map` suffix,
и сохраняет feature IDs. Подменённый cache key не меняет рабочий binding.
Обычный shutdown освобождает index и lease до закрытия файловой системы.

В Android x86_64 проверены движение настоящего GPL player по этому support
floor и остановка перед sphere, contacts/contents, positive pivot hit,
certified малая отрицательная дуга и обычный FreeMap. Точечный rotation trace
возвращает Identity endAxis по native convention; trace model получает
реально повернутую ось. Некоторые геометрически свободные вращения пока
остаются Indeterminate: общий no-hit rotation certificate ещё неполон.

## Проверки

Обычная сборка `src/wolf2009` включает синтетические CTest suites. Переданные
`WOLF2009_TRAINYARD_MPK` и `WOLF2009_SP_BASE` добавляют проверки своей копии.
Отдельный probe BVH можно запустить так:

```text
wolf2009-support-collision-index --source-map YOUR_MAP.mpk
wolf2009-support-collision-index --cylinder-envelope --source-map YOUR_MAP.mpk
node tools/check-bullet-subset.mjs
node tools/check-engine-patch.mjs
```

Сырые отчёты, дизассемблирование, оригинальные бинарники и извлечённые
геометрические данные сохраняются только в ignored `evidence/` и `build/`.
Сводка фактически выполненных проверок — в [VALIDATION.md](VALIDATION.md).
