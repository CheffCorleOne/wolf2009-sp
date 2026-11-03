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

Успешное чтение всех записей не доказывает проходимость карты, разрешение всех
parent closures или полноту runtime namespace. Приоритет global/map/loose,
reload и замена индексов требуют отдельного source-backed load plan. Missing
в выбранном catalog не даёт права автоматически создать default blend tree.

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
