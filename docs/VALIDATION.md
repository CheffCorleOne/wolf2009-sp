# Проверки от 3 ноября 2026

Проверенная среда: Windows x64, Node.js 24.22.2, Zig 0.15.2,
miniz из idTech4A++ `ce49510f3b0c04969eab8ebe8238416e84e4f0bb`.

| Проверка | Результат |
|---|---|
| C++ archive self-test | 23 passed, 0 failed |
| Node format + native integration suites | 17 passed, 0 failed |
| PROCB7, PBBF и статические MD5R1 | 16 self-tests. Геометрия 27 MPK, 3 715 889 triangles, 0 ошибок |
| DDS, ENTS4, материалы и ZIP | 22, 26, 17 и 22 self-tests соответственно. Декодированы 56 139 сущностей в 27 картах |
| CPU preview | 15 self-tests. На складе использованы 53 текстуры, визуально проверен результат |
| Android viewer 0.3 | Обе ABI собраны. Эмулятор Android 15 x86_64 открыл склад (18 492 треугольника). Поворот, сброс камеры и возврат после Home проверены |
| Android viewer 0.4 | Два эмулятора API 35 x86_64 отрисовали 53 DDS на 179 из 232 поверхностей склада. При одинаковой камере изменились 3 869 пикселей области рендера относительно режима без материалов. Поворот, сброс вида и возврат после Home проверены |
| Точки появления, HKXS и движение | 17, 33 и 25 синтетических тестов соответственно. `wolf2009-walk` на оригинальной `empty.mpk` остановился на X=168, Z=0. Ходьба при частичном покрытии склада отклонена |
| Платформы, размещение моделей и VFS | 26 тестов линейного перемещения, 22 проверки переноса пассажира, 20 проверок расстановки и 28 проверок подключения пакетов |
| Старт BRAI и коллизии моделей | 22 проверки прямого события появления и 14 проверок локальной геометрии. Для управляющей связи выполнено сравнение в оригинальном Windows runtime при изменении одного байта отдельной копии карты |
| CMake ядра | Все 4 CTest suites прошли на Windows x64 (Zig/miniz). Модули Android x86_64 с системной zlib из NDK прошли суммарно 366 проверок в эмуляторе API 35, включая вагон, материалы и начальные действия BRAI |
| Изменённый idTech4A++ | Полные сборки `doom3` ARM64 и x86_64. Реальная файловая система двигателя в эмуляторе читала основной MPK и дополнительные SPK через `idFile`; проверены размеры и CRC |
| Android 0.6 | На обоих эмуляторах событие появления запустилось один раз, вагон проехал 320 единиц за 12,8 секунды. На тестовом эмуляторе проверены возврат из фона, поворот взгляда, повтор поездки и отказ при попытке заменить уровень неподдерживаемой картой |
| Android 0.5 | Два эмулятора API 35 x86_64 на оригинальной `empty` прошли старт, ходьбу до X=168, удержание у стены и обратный ход. На складе проверены отказ при неполных коллизиях и вход в осмотр |
| Geometry CLI integration | 4 passed. PBBF aggregation, vertex compaction, preview, отсутствие ENTS, output preservation/isolation |
| Все SPK/MPK через нативный C++ reader | 1 434/1 434, 2 487 чанков, 81 796 записей, 0 ошибок |
| SP assets.pk4, miniz и legacy backend | 6 421 записей, 55 336 498 inflated bytes, CRC passed |
| SP shaders.pk4, оба backend | 1 044 записи, 1 058 644 inflated bytes, CRC passed |
| MP pak0.pk4 как стандартный ZIP regression, оба backend | 18 504 записи, 584 115 541 inflated bytes, CRC passed |
| Custom/standard synthetic ZIP, stored/deflate, comments через границы буфера | Оба backend passed |
| Malformed footer/CRC/размеры/таблицы/zlib/лимиты | Отклонены ожидаемым образом |
| Engine patch, повторное применение к чистым закреплённым исходникам | Три полученных файла совпадают с проверенным checkout |
| REA/Ghidra + реальный GetGameAPI | Doctor healthy; pseudocode получен; подробности в REA-SETUP.md |

Нативные ZIP-пробы компилируют именно `framework/Unzip.cpp` из checkout
с обеими настройками `_MINIZ`. Только platform/allocator headers заменены
тестовыми Windows x64 shims. Renderer и весь engine в этих пробах не линкуются.

```powershell
npm test
.\tools\build-native.ps1
npm run test:native
node tools/check-engine-patch.mjs
.\build\native\wolf2009-inspect.exe --self-test
```

Повтор полного SPK/MPK validation без извлечения игровых ресурсов:

```powershell
$archives = Get-ChildItem 'C:\path\to\Wolfenstein\SP\base' -Recurse -File |
    Where-Object Extension -In '.mpk','.spk'
$paths = @($archives.FullName)
for($i = 0; $i -lt $paths.Count; $i += 50) {
    $batch = @($paths[$i..([Math]::Min($i + 49, $paths.Count - 1))])
    & .\build\native\wolf2009-inspect.exe @batch
    if($LASTEXITCODE -ne 0) { throw 'Archive validation failed' }
}
```

Подробные локальные результаты: `evidence/native-streams-summary.json`,
`evidence/native-streams.log`, `evidence/native-zip.log`,
`evidence/inventory.json`, `evidence/package-sample.json`.
Их не следует включать в публичный репозиторий автоматически.

Итого Node suites теперь 20 тестов; исходные 17 сохранены. Подробнее о новом
этапе в GEOMETRY.md. Проверен CPU-рендеринг статической геометрии карты.

Остаются непроверенными desktop CMake/system-zlib сборка, полный engine runtime,
gameplay, ARM64 runtime, iOS и WebAssembly. Полная валидация
контейнеров не доказывает декодирование семантики всех ресурсов.
