# Игровые каталоги DECL

`CompiledDeclarations` читает сохранённые каталоги Wolfenstein 2009.
`CompiledDeclarationCatalog` индексирует именованные объявления, а
`SourceDefinitions::AddCompiledRecord` разбирает model/entityDef для последующих
игровых привязок. Обычный `AddMember` остаётся отдельным frontend текстовых `.def`.
Загрузка данных не регистрирует native types и не создаёт игровые сущности.

## Формат и владение

Наблюдаемый пакет состоит из двух LE word pools и двух разделов записей.
Каждый pool начинается с u32 count. Раздел содержит LF-строку с числом групп;
группа — исходное имя файла, default type ID и число записей. В записи идут
raw ID, byte length и ровно столько байт payload. LF, записанный после текста
объявления, уже включён в byte length; дополнительный разделитель не пропускается.

Профиль `TwoSectionLfTextV1` задан вызывающим кодом. Это наше имя профиля,
встроенного magic/version в этом заголовке нет. Decoder требует canonical u32
decimal/LF metadata и точный EOF, сохраняет оба pools, оба раздела, порядок,
слова и все byte spans. Payload остаётся непрозрачным массивом байт; проверка
именованного текстового envelope выполняется отдельным catalog слоем.

Group default type может отличаться от явного типа payload. Raw ID не является
универсальным типом или порядковым номером: наблюдаемый consumer использует его
как requested typed index лишь при отдельном режиме и resolved type4. Эти числа
не передаются в GPL `RegisterDeclType`, чей namespace отличается от retail.

Catalog копирует документы до публикации, сохраняет duplicates и обе исходные
формы имён. Lookup возвращает Missing/Unique/Ambiguous. Явный профиль имён может
fold ASCII, заменить slash и удалить суффикс после последней точки; это не
переписывание `.md5anim` в `.hkx`. Private immutable origin позволяет проверить,
что полученный entry не был изменён перед импортом. Это контроль владения и
provenance, не криптографическая аутентификация.

Лимиты transport:64 MiB source,1 млн pool words,20 тыс. groups,100 тыс. records,
4 MiB payload на запись и128 MiB conservative retained storage. Catalog имеет
собственные лимиты работы, копий и результатов lookup. Source frontend также
учитывает полные удерживаемые документы, а не только размер выбранного record.

## Типизированные свойства entityDef

Compiled entityDef хранит `key type aux value`, например строковый spawnclass
или integer health. Plain-source key/value parser для этого формата не подходит.

| Kind | Данные |
| --- | --- |
| 0 | Строковый token |
| 1 | Float32 words из второго pool, index измеряется словами |
| 2 | Int32 words из первого pool |
| 3 | Inline float32 bits |
| 4 | Inline signed int32 |

Aux сохраняется полностью; в исследованном native value хранится его low byte.
Для принятого array profile проверяются count1..255 и полный index/count span.
Scalar profile использует aux1, string profile aux0. Unsupported combinations
сохраняются как `CompiledOpaque` и блокируют одноимённого родителя. Их qualified
`value` пуст, а исходный tuple остаётся в provenance. Неподтверждённое inherit
отвергается, чтобы зависимость не исчезла из графа.

`numericWords` владеет точными словами числовых полей. Float/pool значения не
превращаются в выдуманные SourceTokens с ложными offsets. Nonfinite float bits
могут сохраняться как данные; admission для physics/render/gameplay остаётся
обязанностью соответствующего consumer. Составные integer getters отдельно
не заявляются доказанными. Наследование сравнивает decoded words, а не равенство
двух индексов из разных pools. Неизвестные значения из разных документов не
сливаются как одинаковые.

## Подключение к игре и покрытие

Game-private `SourceDeclarationRuntime` читает через обычный API8 `idFile`,
закрывает файл до parsing и возвращает owning catalog/frontend. Typed DECL
resource и selected global `.decls` имеют явные roles. Native type registration,
entity Spawn и изменение игрового clock не выполняются. Epoch0 означает, что
обычный idFile не предоставил mount-epoch capability; mount pins не заявляются.

Проверенные источники: trainyard DECL2,112,750B/3269 records и empty DECL357,867B/
641 records. В выбранном global SPK нет DECL; основной global каталог найден
отдельно в `assets.pk4`, exact member `maps/game/global/global.decls`,2,175,123B/
3246 records. Это разные источники с разными pools; данные сохраняются раздельно.

## Единый набор и правила замены

`CompiledDeclarationNamespace` применяет восстановленные правила ordinary
source-file owners. Перед каждой группой снимается seen08 у её владельца.
Уже защищённая запись protected20 пропускает замену раньше проверки владельца;
исключение — тип model34. Затем допускается unseen запись того же владельца,
а foreign-owner и повтор в одной группе пропускаются. Принятая запись получает
seen08; отдельный режим также ставит protected20. Обычный режим его не снимает.
Имена владельцев сравниваются с ASCII case fold, без slash/stem преобразования.

Набор сохраняет все исходные candidates, решения и предыдущие записи, включая
owning pools и flags. Раздел применяется атомарно, с лимитами памяти и работы.
Индекс объявлений использует явный case/slash/last-dot profile; raw tokens и
source offsets сохраняются. `SourceDefinitions` получил такой же необязательный
профиль declaration names; прежний literal profile остаётся default. Имена полей,
logical animations и asset references не приобретают stem-преобразование.

Game-private `SourceDeclarationSetRuntime` принимает явный ordered section/mode
plan по загруженным packets и строит effective frontend. На real global/trainyard
проверены два плана: protected global + ordinary map и все sections ordinary.
Оба дают4890 выбранных объявлений, включая1555 entityDef и48 моделей. В первом
плане1621 protected skips и4 model replacements, во втором1625 replacements.
Конфликт `streamtech.map_heap_size` остаётся integer140 из global либо string
"140" из карты согласно выбранным gates; числовая строка не превращается в int.

Эти два плана проверяют механизм reader. Ниже описан отдельно наблюдавшийся
startup profile. Другие cvar branches и внешние lifecycle flag resets ещё
требуют квалификации. Профиль начинается с пустого ordinary-owner набора:
implicit/default owners, bit10/native-text state и raw-ID native indices сюда
не импортируются. В выбранном entity graph разрешены 1 553 closures; два объявления
отказывают из-за отсутствующих `tesla_prong_arcs` и `trigger_multi`. Эти отсутствия
проверяются отдельно, без подстановки default parent. Полнота runtime namespace
не заявляется.
Missing не даёт права автоматически создать default blend tree.

Проверки: tests/compiled-declarations, tests/compiled-catalog и
tests/compiled-definitions. Native probe включён в существующий
`-WithAnimationV2Checks`; production loader доступен с `-WithWolfAnimation`,
QA exports выключены в production. Коммерческие inputs и raw evidence в Git
не включаются.

Фактический checkpoint: main CTest63/63 PASS; transport92, catalog167 и
frontend348 synthetic checks, fresh trainyard/empty inputs. Android15 x86_64
native game probe прошёл8836 checks и13 отказов: map1005 и global1212 model/entity
imports, owning lifetime, typed values и раздельные namespaces. Совместный
source-empty/player/BRAI/animation/mesh/GLES run прошёл с descriptors0. ARM64
production собран с QA OFF; физический ARM64 runtime ещё не проверен.

Новый namespace checkpoint: main65/65 CTest PASS, namespace60 synthetic checks
в11 группах и fresh original global/map test. Android15 x86_64 combined runtime
прошёл9810 set checks/7 отказов и8838 packet checks/13 отказов, effective1603
model/entity imports и owning facts после удаления обоих handles. Shared player,
BRAI, animation, mesh и GLES checks также PASS; descriptors0. ARM64 production
собран с QA OFF. Исходные классы ещё не зарегистрированы, проходимых миссий нет.

## Наблюдавшийся compiled startup

В изолированном оригинальном PC-процессе с `com_SingleDeclFile=1` наблюдалась
загрузка английского trainyard. `global_vo_english.decls` читается только section0
с protection; затем обе sections `global.decls` с protection. Обе sections map
locale и main map читаются ordinary. Два map DECL имеют одно logical resource
name, но находятся в разных пакетах, поэтому caller должен выбрать их явно.
У global locale непрочитанная section1 пуста. Наблюдение не доказывает все reload,
locale/cvar ветки, ввод или совместимость всей миссии.

`Wolf2009_LoadSourceDeclarationStartup` требует явные `CompiledLocalizedV1`,
четыре exact resource requests и разные load identities. Он строит семь
applications и возвращает owning namespace/frontend через прежний API8.
Отсутствующий input, неверная role/policy или непустая пропускаемая section1
отвергаются. Filename/language fallback, native class registration и Spawn нет.

Дополнительные locale inputs: global49906B/FNV845a8a07071ae26e и
trainyard188406B/FNVe675921b8a577fd7. Это `lipSync` catalogs, не sound shaders;
их тела сохраняются как opaque data. Четыре inputs дают7317 candidates,
5687 selected keys,797 lipSync, прежние1555 entityDef и48 model.
Наблюдавшийся порядок даёт1626 protected skips и4 model replacements.

Native combined run прошёл5711 startup checks/8 отказов,8844 packet checks/
13 отказов и9810 set checks/7 отказов. Owning facts остаются после удаления
input/set handles; native registry/clock не меняются. Общие source player,
BRAI, animation, mesh/GLES и shutdown PASS, descriptors0. ARM64 production
собран с QA OFF; физический ARM64 runtime и новый APK не проверялись.
QA-only callback теперь принимает четыре пути; production API8 не менялся.
