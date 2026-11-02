# Статическая геометрия: этап 0.2

Реализован независимый C++11 decoder `MD5RProcVersion 7` и PBBF, опирающийся
на собственные ресурсы игры. Декодируются positions/UV, ссылки материалов,
обычные indexed triangle lists и границы meshes. Core не вызывает Windows
или Android API. CLI и отдельный CPU rasterizer нужны для проверки данных.

## Поддерживаемая структура

PROC содержит текстовые декларации `VertexBuffer`, `IndexBuffer`, `Model`,
`Mesh`, `PrimBatch`, `DrawBuffers`, `DrawIndexedTriList`, `Bounds`.
Лексер понимает комментарии, строки, вложенные блоки и непрозрачные секции
`bin N` с точным количеством байт. `MemSize` не считается размером файла.

Ссылки `.MD5RBin` разрешаются относительно имени PROC через виртуальный
PBBF index. Все повторные PBBF chunks в MPK агрегируются. Нулевые shadow/trace
buffer declarations без payload допустимы; данные требуются только для
обычной render-геометрии.

| Render vertex layout | Stride | Position | Normal/tangent/binormal | Color | UV |
|---|---:|---|---|---|---|
| Packed `Dec_11_11_10N` | 40 | float4 @0 | packed uint32 @16/20/24 | @28 | float2 @32 |
| Unpacked | 64 | float4 @0 | float3 @16/28/40 | @52 | float2 @56 |

Команда draw имеет поля `minVertex vertexCount firstIndex triangleCount`.
`firstIndex` измеряется индексами, `triangleCount*3` задаёт их количество.
В проверенных ресурсах индексы абсолютны внутри VB: `minVertex` не добавляется.
16-битные индексы проверены на всех картах; 32-битный reader есть, но реальных
32-битных world buffers в этой выборке не найдено. Неизвестные используемые
форматы и неподтверждённый marker `ZeroBasedIndices` явно отклоняются.

Проверяются declared counts, byte lengths, finite components, ranges,
индексы, mesh bounds с tolerance 0.02 игровых единицы, лимиты памяти/синтаксиса.

## Экспорт и preview

По умолчанию экспортируются только `_area*` world models. Submodels
находятся в локальных координатах; их нельзя ставить в origin без ENTS.
После фильтрации вершины уплотняются, индексы переназначаются. Это сохраняет
корректные границы сцены для камеры. `--include-submodels` — отдельный
диагностический режим с предупреждением, не проверенное размещение props.

```powershell
.\tools\build-native.ps1
.\tools\build-preview.ps1
npm run test:geometry
.\tools\preview-scene.ps1 -GameRoot 'C:\path\to\Wolfenstein' -Map game/mtw/mtw_ware
.\build\native\wolf2009-scene.exe --validate 'C:\path\to\map.mpk'
```

Экспорт создаёт новый файл эксклюзивно, отказывает в перезаписи и записи
в распознанную установку `/SP/base/`. Preview также не перезаписывает BMP.
Папка `evidence/scene-previews` исключена из Git; изображения и OBJ содержат
производные игровых данных и не включаются в source archive.

Rasterizer: Z-up auto-fit camera, perspective projection, z-buffer,
flat directional shading и условный цвет материала. Текстуры не загружаются.
Нулевые треугольники пропускаются; near-plane clipping не реализован,
auto-fit держит проверенные сцены перед камерой. Это CPU diagnostic,
не Android/GLES engine renderer и не интерактивное прохождение.

## Проверено

- Независимый исследовательский parser и нативный C++ decoder прошли все
  27 PROC: 3 623 models, 33 367 ordinary draw surfaces, 3 715 889 triangles;
  отсутствующих буферов и ошибок диапазонов/bounds нет.
- `empty`: 132 exported vertices / 70 triangles.
- `mtw_ware`: 26 438 exported world vertices / 18 492 triangles / 232 surfaces.
  Preview 1280×720, azimuth45/elevation35: 106 478 покрытых пикселей,
  61 фактически нулевой треугольник, near-plane skips0. Изображение проверено.
- 9 C++ geometry self-tests; 10 CPU renderer self-tests; 3 новых CLI
  integration tests с полностью синтетическими данными. Проверены повторные
  PBBF chunks, compaction, ненулевой первый индекс, file preservation,
  output isolation и непустое изображение.

Полные локальные артефакты: `evidence/geometry-agent`,
`evidence/geometry-preview`, `evidence/scene-previews`.

REA MCP после перезапуска реально подключён к Ghidra. В EXE найден маркер
`MD5RProcVersion` @`0x105d7118` и функция-читатель @`0x10135a60`.
Evidence IDs: `ev_cc94959cbcde964f0a7eb24c2be69c9f835611ce7870bbb8dbe34e733c776ac6`
и `ev_500392cb6aa659f18ec57e998f69c391524bf53de30f40455744e768c3dfa364`.
Сохранены локальные `geometry-binary.bundle.json` и
`wolf2-geometry.snapshot.json`; сессия закрыта. Декомпилированные реализации
не добавлялись в исходники проекта.

## Граница результата

MODL/MD5R исследован частично: inline data и различие LoadVertexFormat/runtime
format подтверждены на нескольких props. Production MODL decoder, skinning,
bind pose, точное packed-normal преобразование, ENTS placement, BSP/PVS,
материалы/текстуры, коллизии, gameplay и Android runtime не реализованы.
Сравнение итоговой сцены с запущенной PC-игрой ещё не проводилось.
