# Android: нативный просмотр статического мира

Android APK 0.4 содержит Java Activity, GLES3 renderer на C++ и общий
`Archive`/`Geometry`/`Material`/`Dds`/`ZipArchive` core. Это **просмотрщик
статического мира с diffuse-текстурами**, а не запущенная
одиночная кампания. Игровых ресурсов, Windows DLL и коммерческих библиотек
в приложении нет.

## Требования и сборка

- Android 8.0 / API 26 и выше, ABI `arm64-v8a` или `x86_64`.
- OpenGL ES 3.0. Производительность и пиковая память зависят от карты и GPU.
- Сборка проверена на Windows x64 с Temurin JDK 21.0.13+9, Android API37.0,
  build-tools37.0.0 и **NDK r30 LTS / 30.0.16248370**.
- Нативная библиотека собирается с 16-КиБ ELF alignment; APK проверяется
  `zipalign -P 16` и подписывается локальным debug-ключом.

```powershell
.\tools\install-android-sdk.ps1
.\tools\build-android.ps1
```

SDK по умолчанию находится в `tools/android-sdk` пользовательского профиля,
а не в каталоге игры. Installer получает точные package URLs и опубликованные
контрольные суммы из официального Google repository XML. Для выбранных
архивов Google публикует SHA1; они проверяются, а локальный SHA256 дополнительно
записывается в `downloads/installation-receipt.local.json`. SDK/NDK и их
архивы в исходный репозиторий не входят.

Сборка не требует Android Studio, Gradle или сторонних Maven-зависимостей:
используются NDK clang, javac, D8, aapt2, zipalign и apksigner. Нативный core
линкуется с системным NDK zlib. `android/CMakeLists.txt` также описывает
нативный target для будущей IDE/Gradle интеграции; CMake-путь отдельно не запускался.

Результат: `build/android/wolf2009-geometry-debug.apk`.
Отчёт сборки: `build/android/build-report.local.json`.
Debug keystore создаётся только в `build/android/debug.keystore`, исключённом
из Git; его нельзя включать в публичный source archive. Новый ключ другого
checkout не сможет обновить уже установленный APK без переустановки приложения.

## Использование

1. Установить debug APK на совместимое устройство или эмулятор.
2. На старте отображается синтетический куб, созданный кодом.
3. Нажать **Материалы** и выбрать `SP/base/assets.pk4` из своей копии игры.
   Каталог `.guide`/`.mtr` разбирается в памяти; при ошибке прежний каталог
   остаётся. Если материалы импортированы после карты, карту нужно открыть заново.
4. Нажать **Карта** и выбрать один `.mpk` из своей копии SP-карты.
   Для первого теста подходит `SP/base/maps/game/mtw/mtw_ware.mpk`.
   Без каталога материалов карта доступна в прежнем flat-режиме.
5. Приложение открывает URI только для чтения, копирует выбранный файл в
   закрытый временный кэш, проверяет контейнер и декодирует мир в фоне.
   Временная копия удаляется после разбора. Исходный файл не изменяется.
6. Один палец поворачивает камеру, два пальца сдвигают её; щипок и `−`/`+`
   меняют масштаб. **Камера** сбрасывает вид; **Орбита/Полёт** переключает
   режим. **Вперёд/Назад** приближают орбиту или перемещают свободную камеру.

SAF позволяет выбрать файл без storage-permissions. Приложение не запрашивает
доступ ко всему накопителю или сети, не сохраняет URI-разрешение и не содержит
скрытого произвольного file-path intent. Если используется облачный провайдер
документов, скачивание выбранного файла выполняется самим провайдером.

Лимит импорта — 512 МиБ на MPK и 128 МиБ на PK4. Native limits: до 128 МиБ inflated chunk,
2 млн decoded vertices/triangles и 256 МиБ суммарных render buffers. Превышение
или неверный формат дают сообщение об ошибке; предыдущая сцена остаётся.

Для `mtw_ware` ожидаются 26 438 world vertices, 18 492 triangles и 232 surfaces.
Все PBBF chunks индексируются вместе. Вершины уплотняются **после** фильтрации
`_area*`, поэтому local-space props не искажают границы сцены и auto-fit камеры.

## Diffuse bindings 0.4

Материалы загружаются из всех `.guide` перед всеми `.mtr`; каталог публикуется
атомарно и затем используется только для чтения. ZIP reader проверяет структуру
PK4 и CRC выбранных текстовых entries. Карта индексирует все TXTR chunks
текущего MPK. `MaterialCatalog::Resolve` даёт diffuse path и постоянный UV scale;
изображения по одному virtual path декодируются только один раз.

DDS BC1/BC2/BC3 преобразуются общим core в RGBA8. На GL thread создаются
RGBA8 textures с repeat и mipmaps; исходные RGBA8 остаются в ограниченном
CPU-кэше для повторного upload при потере GL context. Встроенные `_white`
и `_black` явно создаются как 1×1. DDS из `assets.pk4` в этом increment не
используются: это архив деклараций, а diffuse изображения ищутся только
в TXTR выбранной карты.

Лимиты: не больше 4096×4096, 64 МиБ на декодированное изображение, 96 МиБ
суммарных retained RGBA8 и 256 уникальных текстур. Учитывается также
`GL_MAX_TEXTURE_SIZE`. Неразрешённые материалы, отсутствующий DDS,
неподдерживаемый формат, превышение лимита или отказ GPU оставляют surface
в flat-режиме. Счётчик показывает реальные GPU-ready bindings, DDS,
solid textures, flat и причины fallback. A8 не трактуется как полноценный
opaque diffuse, чтобы не выдумывать семантику маски.

Для `mtw_ware` host-путь даёт 232 world bindings, 180 разрешённых материалов,
179 textured bindings / 53 уникальных DDS, 53 flat, 25 partial и 1 отсутствующий
diffuse; retained RGBA8 — 2 146 688 байт. Ошибок декодирования или лимита нет.
`partial` означает, что известный diffuse используется, а остальные свойства
материала намеренно не воспроизводятся.

Дополнительный host-тест адаптера `android/native/TextureBindingTest.cpp`
прошёл 10 synthetic checks: порядок guide/material sources, golden DDS pixel
и UV scale, дедупликация, partial diffuse, missing/corrupt DDS, лимиты,
явные solid colors, режим без каталога и исключение local submodels.

## Проверка 0.3.0

Сборка APK, подпись, ABI, отсутствие storage/network permissions и 16-КиБ
alignment проверены инструментами. В эмуляторе Android 15 (API 35, x86_64)
загружен `mtw_ware.mpk`. Перетаскивание пальцем изменило вид, сброс камеры
восстановил исходное изображение, а после переключения приложений карта
осталась открытой. Запуск на физическом ARM64-устройстве ещё не проверен.

Renderer использует depth buffer, indexed triangle lists, известный diffuse
с условным flat directional shading и flat fallback. Это opaque diffuse-only:
alpha-test/blend, normal/specular, оригинальные shader techniques и точная
цветовая/световая модель не имитируются. Нет ENTS placements,
анимаций, коллизий, игрового управления, миссий, AI, сохранений или звука.

## Проверка 0.4.0

Проверен финальный APK с SHA256
`71f70804917054635450469a28ac16545250b8024ec756fbd2c2cd251ef6243a`.
На двух AVD с Android 15 / API35 / x86_64 импортированы оригинальные
`assets.pk4` и `mtw_ware.mpk` через SAF. GLES renderer сообщил 179/232
GPU-ready diffuse bindings, 53 DDS, 53 flat, 25 partial, 52 unresolved
и 1 missing; ошибок декодирования, лимита или GPU upload не наблюдалось.

Контрольный A/B flat/diffuse при одинаковой камере изменил 3 869 пикселей.
Перетаскивание, сброс камеры и возврат после переключения приложений прошли.
Повреждённая карта и неподходящий PK4 дали видимые ошибки; прежняя сцена
и каталог сохранились. Повторная загрузка вернула те же счётчики.
В APK сохранены license notices GPL, Android NDK и LLVM.

Это проверка конкретной карты и двух x86_64 emulator instances, а не всей
кампании или всех устройств. Физический ARM64 runtime, принудительная потеря
GL context и точное совпадение изображения с оригинальным PC renderer
отдельно не проверялись.

## Официальные источники

- [Android SDK и command-line tools](https://developer.android.com/studio).
- [NDK r30 LTS и опубликованные контрольные суммы](https://developer.android.com/ndk/downloads).
- [Системный выбор документов / SAF](https://developer.android.com/training/data-storage/shared/documents-files).
- [Требования к 16-КиБ страницам](https://developer.android.com/guide/practices/page-sizes).
