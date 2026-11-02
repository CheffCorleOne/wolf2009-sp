# Android: нативный просмотр статического мира

Первый Android APK содержит Java Activity, GLES3 renderer на C++ и общий
`Archive`/`Geometry` core. Это **просмотрщик геометрии**, а не запущенная
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
3. Нажать **Открыть MPK** и выбрать один `.mpk` из своей копии SP-карты.
   Для первого теста подходит `SP/base/maps/game/mtw/mtw_ware.mpk`.
4. Приложение открывает URI только для чтения, копирует выбранный файл в
   закрытый временный кэш, проверяет контейнер и декодирует мир в фоне.
   Временная копия удаляется после разбора. Исходный файл не изменяется.
5. Один палец поворачивает камеру, два пальца сдвигают её; щипок и `−`/`+`
   меняют масштаб. **Камера** сбрасывает вид; **Орбита/Полёт** переключает
   режим. **Вперёд/Назад** приближают орбиту или перемещают свободную камеру.

SAF позволяет выбрать файл без storage-permissions. Приложение не запрашивает
доступ ко всему накопителю или сети, не сохраняет URI-разрешение и не содержит
скрытого произвольного file-path intent. Если используется облачный провайдер
документов, скачивание выбранного файла выполняется самим провайдером.

Лимит импорта — 512 МиБ на MPK. Native limits: до 128 МиБ inflated chunk,
2 млн decoded vertices/triangles и 256 МиБ суммарных render buffers. Превышение
или неверный формат дают сообщение об ошибке; предыдущая сцена остаётся.

Для `mtw_ware` ожидаются 26 438 world vertices, 18 492 triangles и 232 surfaces.
Все PBBF chunks индексируются вместе. Вершины уплотняются **после** фильтрации
`_area*`, поэтому local-space props не искажают границы сцены и auto-fit камеры.

## Проверка 0.3.0

Сборка APK, подпись, ABI, отсутствие storage/network permissions и 16-КиБ
alignment проверены инструментами. В эмуляторе Android 15 (API 35, x86_64)
загружен `mtw_ware.mpk`. Перетаскивание пальцем изменило вид, сброс камеры
восстановил исходное изображение, а после переключения приложений карта
осталась открытой. Запуск на физическом ARM64-устройстве ещё не проверен.

Renderer использует depth buffer, indexed triangle lists, условный цвет
материала и flat directional shading. UV передаются, но реальные DDS textures
пока не подключены. Нет материалов оригинального D3D renderer, ENTS placements,
анимаций, коллизий, игрового управления, миссий, AI, сохранений или звука.

## Официальные источники

- [Android SDK и command-line tools](https://developer.android.com/studio).
- [NDK r30 LTS и опубликованные контрольные суммы](https://developer.android.com/ndk/downloads).
- [Системный выбор документов / SAF](https://developer.android.com/training/data-storage/shared/documents-files).
- [Требования к 16-КиБ страницам](https://developer.android.com/guide/practices/page-sizes).
