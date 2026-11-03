# Буферы треугольников HKXS

`src/wolf2009/HavokTriangleMesh.*` — отдельный переносимый reader исходных
triangle subparts и принадлежащих им буферов. Он использует проверенные
sections/fixups из `HavokPack` и измеренный профиль Havok-5.6.0-r1, pack-v5,
32-bit little-endian. Реализация не зависит от Havok SDK или платформенного
API и не выполняет обход графа физических тел.

`DecodeHavokTriangleMeshes(bytes, selectedOffsets, limits)` принимает явные
offsets объектов `hkpStorageExtendedMeshShape`. Пустой список перечисляет
все typed storage objects файла. В одном HKXS могут находиться одновременно
мир и ресурсы моделей; это перечисление не устанавливает их активность,
размещение в ENTS или участие в столкновениях.

В результате сохраняются:

- исходные offsets storage, subpart record и owned MeshSubpartStorage;
- 216 байт storage header, 64 байта record, 80 байт owned header и fixups;
- четыре исходных слова scale; координаты возвращаются без применения scale;
- исходные float4 vertex bytes, xyz в игровых единицах и отдельные W bits;
- исходные index arrays, порядок трёх индексов и все байты хвоста stride;
- raw array descriptors, capacity flags и material metadata без назначения
  их значениям contents, масок или состояния сущностей;
- fingerprint исходных байтов и выбранного subpart, не криптографическая
  аутентификация версии игры.

Измеренный storage profile использует pointer/count по offsets +148/+152
для 64-байтных triangle records и +192/+196 для массива ссылок на owned
`hkpStorageExtendedMeshShapeMeshSubpartStorage`. В record читаются число
треугольников +16, vertex stride +24, число вершин +28, index stride +52
и index type +56. Остальные поля остаются raw.

| Поле owned header | Сохраняемый массив | Размер элемента |
|---|---|---:|
| +8 | float4 vertex bytes | 16 |
| +20 | исходные u16 index words | 2 |
| +32 | исходные u32 index words | 4 |
| +44 | raw material metadata bytes | 1 |
| +56 | raw material metadata words | 4 |
| +68 | raw material metadata words | 2 |

Для каждого массива сохраняются count, исходное capacity/flags слово,
разрешённый pointer offset и payload. Material metadata намеренно остаётся
числовыми словами/байтами: например, один элемент не предполагается
автоматически материалом каждого треугольника.

Поддержаны literal index triples для type 1/u16 и type 2/u32: stride должен
вмещать три индекса и быть кратным их размеру. Размер owned index array
должен точно совпадать с `numTriangles * indexStride`. Padding, в том числе
значения больше числа вершин, не интерпретируется как дополнительный индекс.
W компонента вершины сохраняется побитно, даже если это NaN; только xyz
считаются координатами и обязаны быть конечными и укладываться в бюджет.

`buffersDecoded` означает чтение измеренного vertex/index layout.
`identityScaleBufferProfile` дополнительно требует точный исходный scale
`(1,1,1,1)` и отсутствие дополнительного inactive index buffer. Неизвестные
strides/types и scale сохраняются с `unsupportedProfiles`; reader не
подбирает запасной формат. Противоречивые counts, выходы за границы,
пропущенные обязательные fixups, неверные индексы и превышение бюджетов
вызывают исключение.

`completeBufferCoverage` относится только к выбранным typed buffers и
измеренному identity-scale profile. Даже при `true` не установлены winding,
alternate-triangle policy, welding, collision contents, поза тела и runtime
фильтры. Этот этап не подключает треугольники к native loader или контроллеру.

Проверка всех 27 исходных MPK завершилась без parser errors и неизвестных
buffer profiles: 289 storage objects, 45 377 subparts, 360 050 vertices и
334 308 triangles. Сохранены 1 337 232 байта stride tails и 226 165 байт raw
material payloads. Во всех этих исходных subparts встретились u32 indices
со stride 16; u16/stride 8 проверены отдельно синтетическими тестами.

| Явно выбранный reference storage | Subparts | Vertices | Triangles |
|---|---:|---:|---:|
| Trainyard, offset 360832 | 2 919 | 17 983 | 14 238 |
| Warehouse, offset 45360 | 247 | 1 740 | 1 650 |

All-source trainyard содержит восемь storage objects: 2 993 subparts,
18 483 vertices и 14 670 triangles. Эти цифры отличаются от явно выбранного
world storage, поскольку другие source objects не исключались по догадке.
Warehouse bounds совпали с ранее проверенным независимым metadata probe:
минимум `(-4127.999854, -4271.999870, -207.999992)`, максимум
`(-2792.754887, -2192.751667, 184.999995)` в игровых единицах.

Новые tests в `tests/HavokTriangleMesh.cpp` используют самостоятельно
созданный packfile из общего `tests/HavokTestFixture.h`. Они проверяют
u16/u32, stored order, padding, raw W/material values, explicit selection,
unknown profiles, nonidentity scale, malformed pointers/counts/indices и
лимиты памяти/числа элементов. CLI принимает MPK paths; `--survey` продолжает
обработку остальных входов после отдельной ошибки. Тестовые reference offsets
являются данными проверки исследованной версии, не выбором тела в gameplay.

Host C++11 tests и read-only trainyard/warehouse checks прошли. Новый reader
также скомпилирован для ARM64 Android API 26 с `-std=c++11`; это проверка
компиляции, без утверждения об ARM64 runtime. Публикационный allowlist этого
этапа находится в `tests/engine-gameplay/triangle-source-manifest.json`.

Обычная CMake-сборка `src/wolf2009` включает `wolf2009-havok-triangles` и
синтетический CTest. При заданных `WOLF2009_TRAINYARD_MPK` и
`WOLF2009_SP_BASE` добавляются две проверки исходных карт. После сборки:

```powershell
.\build\archive\wolf2009-havok-triangles.exe
.\build\archive\wolf2009-havok-triangles.exe 'C:\path\to\trainyard.mpk'
```

## Семантика отдельного triangle query

`HavokTriangleQuery` добавляет независимую C++11 реализацию измеренных правил
retail triangle leaf. Для исследованного Wolf2.exe (SHA в RESEARCH.md):
child shape `0x1038c230`, material lookup `0x1038bae0`, triangle ray
`0x103a96e0`. `flipAlternateTriangles` — signed byte; он меняет
выбор второго/третьего индекса. `triangleOffset` — signed int32 индекс в
welding array, он не сдвигает vertex/index/material buffers. Geometry, raw
child filter word и welding получают отдельные статусы. Material index type,
его byte stride и signed material stride читаются по проверенным retained
arrays. Отсутствующий material-index base возвращает исходный raw word0.
Этот raw word ещё не объявляется игровой contents mask.

Отдельный segment ray двухсторонний, с исходным относительным edge epsilon,
строгой верхней границей fraction и установленными endpoint rules. Radius,
extrusion и welding не участвуют в исследованном ray body. Они сохраняются
в результате и остаются существенными для других физических запросов.
Непустой welding payload текущий reader не сохраняет: индекс/границы
известны, выбранное значение явно unavailable. Реализация использует double
и не заявляет побитную эмуляцию оригинальных x86 float вычислений.

Проверены 44 синтетических случая и все 27 исходных MPK: 320 559 выбранных
world triangle occurrences, 320 427 пар лучей через центры конечных
невырожденных треугольников, 640 854 попадания. Для 132 вырожденных cores
луч возвращает miss; contracted arithmetic не должна создавать им ложную
ненулевую нормаль. Физическую форму с ненулевым радиусом при этом нельзя
выбрасывать: вырожденный core всё ещё может иметь объём после добавления
радиуса. Все 320 559 child filter words разрешились; extrusion нулевой,
welding arrays пустые, radius ненулевой. Radius-free native surface
candidates — **0**. Поэтому успешные лучевые проверки не включают source
triangle surfaces в обычную CM collision и не доказывают sweep/contact
совместимость или nearest-hit запрос по целому миру.

CMake собирает `wolf2009-havok-triangle-query`; без аргументов он выполняет
синтетические проверки, с MPK paths — проверку своих исходных данных.
