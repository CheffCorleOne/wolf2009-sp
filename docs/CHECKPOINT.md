# Состояние проекта перед паузой

Разработка приостановлена по просьбе владельца. Продолжать только после его
нового сообщения. Цель остаётся прежней: открытый нативный движок всей
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
