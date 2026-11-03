# Geometry-only skinned mesh binding v2

V2 загружает исходный weighted MODL по явно выбранным SKEL container,
точному имени skeleton object, LOD и vertex declaration profile. HKXA не
читается и не подставляется как фиктивная source identity. Полученная
геометрия импортируется в настоящий `idRenderModelMD5` через обычный model
manager. Animation playback, actor placement и head attachment не подразумеваются.

`ReadSkeletonData` поддерживает тот же bounded 32-bit little-endian
`Havok-5.6.0-r1` packfile layout, что canonical `HavokPack`. Он выбирает
единственный exact named `hkaSkeleton` в named `Animation Container`,
проверяет arrays/fixups/types/unique names и один ordered tree до 256 joints.
Нет выбора по первому объекту, числу joints, имени модели или имени карты.
Все 12 исходных words reference hkQsTransform и дополнительный bone word
сохраняются. Derived rigid reference poses имеют отдельный qualification
flag; unknown scale/nonfinite reference не выдаются за известную позу.
Geometry importer использует только SKEL names/parents и собственные MODL
bind transforms. Reference pose не заменяет bind и не задаёт retargeting.

`SkinnedVertexProfile::Weighted64` допускает только проверенную inline64
declaration с четырьмя palette indices/weights; `WeightedOrRigid64` также
разрешает существующую rigid64 declaration. Палитры, absolute16 indices,
signed first weight/tangent metadata, UV, raw colour и explicit LOD сохраняются.
Это format profiles; число 52/27 и humanoid joint names не являются способом
определения формата. Старые greeter body/head wrappers сохраняют прежние
ограничения и отдельную attachment/idle логику.

Portable API находится в `SkinnedModelData.h`: `SkinnedMeshRequestV2`,
`SkinnedMeshDataV2`, `SkinnedMeshSourceInfoV2`, `LoadSkinnedMeshV2`.
V1 request/source-info остаются прежними; общий `PreparedSkinnedGeometry`
позволяет обоим путям использовать один geometry preparation/native importer.
V2 cache namespace — `wolf2009/<mount>/md5r/v2/lodN/<key>/<model>`. Полный ключ
включает оба canonical paths, exact skeleton name, declaration profile, LOD и
оба mount epochs. Hash collision проверяется полным request; FNV не является
аутентификацией. V1 namespace/hash сохраняются побайтно.

C++98-compatible core registration:

```cpp
bool Wolf2009_RegisterSkinnedMeshV2(const char* modlPath,
    const char* skelContainer, const char* exactSkeletonName,
    int levelOfDetail, int geometryProfile, idStr& nativeName);
```

Profile numbers: 1 — weighted64, 2 — weighted/rigid64. Resident models держат
leases только MODL/SKEL; одинаковые mount labels pin-ятся один раз. Source-info
содержит две реальные byte identities и полный named request. V1 getters
отказывают для V2; существующий source-animation v1 producer явно не принимает
такую модель без нового explicit clip binding. Его wire/provider не меняются.
Appearance registration использует общий immutable native skin/material
preflight, повторно читая и сверяя правильные две V2 source identities.
Source skeleton reference data не удерживаются как лишняя копия всей mesh.

`EngineSkinnedMeshV2.cmake` подключается после `EngineSkinnedModel.cmake`.
Он добавляет `SkeletonData.cpp` в renderer bridge. Opt-in
`WOLF2009_MESH_V2_QA` добавляет только C++11 core QA object и host source macro;
нового game/API8/dlsym interface нет. Entry:

```cpp
bool Wolf2009_RunSkinnedMeshV2Checks();
```

Private runtime fixture inputs: trainyard MPK, global SPK,
`streampacks/allies_kreisau_agt02.spk`, original material catalog.
QA uses its own `qav2`, `qav2global`, `qav2char` mounts, normal model manager,
actual native dynamic snapshots, original body52/standalone head27, character
LOD0/1, two independent snapshots, shared authored appearance, source hierarchy,
UV/RGBA/corners/bounds, purge/reload, stale epoch refusal and final lease release.
It does not render a GPU frame or attach a head to an actor.

Portable tests in `tests/engine-skinned` include independent own 3/7-joint
packfile/MODL fixtures, malformed/unknown reference cases, exact selection/key
checks and an HKXA-read trap. Owner-supplied original checks compare
HumanoidMale/EmptyMale distinct reference arrays against identical bind
geometry from the same MODL, plus character LOD0/1 and standalone head27.
Actual Android15 x86_64 runtime passed: 193461 checks, 10 supplied poses,
23775 triangle corners, maximum position error `7.62939453e-6`, bounds error
zero. Model identity/hierarchy, original body52/head27, character LOD0/1,
shared base versus independently resolved authored skin, UV/RGBA, two snapshots,
reload/stale epochs and released source leases passed in the ordinary model
manager. The original selected head is the standalone trainyard LOD0 resource
(72 vertices/97 triangles), not an attached or high-detail actor head.
Use runtime scripts with `-WithMeshV2Checks -RendererGameRoot <own copy>`;
the build switch adds QA only, and the test requires its native success marker.

The explicit `SkinnedLodPolicy` adds two separate admissions. `Authored=1`
preserves all existing callers and golden cache names. Policy2,
`SingleResourceWithoutLodRecords`, requires absence of the root LOD table and
every mesh LOD field, and requested compatibility slot0. It does not invent
authored LOD0. Mixed/incomplete records, another slot and unknown policies are
refused. Request/source data retain both policy and actual record presence;
the policy2 cache uses a distinct full-key domain. A new native registration
overload takes this policy explicitly while the previous overload keeps1.

The original locker qualifies for policy2/weighted-or-rigid64: its literal
model declaration selects the exact MODL and two-bone SKEL. Portable checks
now pass407 synthetic/9795 source checks. Native mesh QA expanded to198890
checks,12 supplied poses and24447 corners, including its163vertices/112triangles.
Its actual animation playback is checked by the separate
[native animation v2 adapter](../idtech4a_anim/V2.md). Actor loot/search behavior
does not follow from these geometry/animation checks.
