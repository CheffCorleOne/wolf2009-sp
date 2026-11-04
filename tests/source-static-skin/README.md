# Native static source skin

`NativeSourceStaticSkin` is a game-private owner for an explicit static MODL,
already loaded native model, owner material archive and authored source skin.
It uses the existing `SkinCatalog::Read/Apply`; selected duplicate, wildcard or
model directives refuse. Matching material pairs retain the exact source data.

Preparation compares every source/native triangle corner XYZ, scaled UV and
encoded RGBA. It accepts changed targets only through the existing strict
`BuildDiffuseMaterial` policy, with identical UV scale/vertex colour encoding.
Targets have retained original DDS and source-qualified material identities.
Unmatched and identity-mapped surfaces preserve their existing materials.
Replacement materials and the native customSkin have separate immutable names;
canonical/foreign declarations are never overwritten.

The owner retains the source archive, typed model/PBBF and selected texture
files/bytes/fingerprints. It keeps shared native geometry pointer/count and
declaration snapshots. `Preflight` detects purged/replaced native models,
changed shared geometry/declarations and broken file liveness. The caller must
destroy source entities before releasing the prepared owner at map shutdown.

Actual source selection for the trainyard wagon is
`models/vehicles/box_car_full_in` in `skins/vehicles.skin`, with explicit pairs
`box_car_full_out -> box_car_full_in` and `box_car_full_in -> box_car_full_in`.
The similarly named `skins/models/vehicles/box_car_full_inside` is another skin.
Original declarations/assets are not embedded here.

The actual `box_car_a` model's ten draws use box_car/box_car_a/box_car_a_dtl
and prop/decal/fabric materials; none matches those two full_out/full_in rules.
Thus this source skin is a valid authored no-op for that model. Strict produces
an explicit native identity customSkin with matched/changed/remaps0 and
`authoredNoOp=true`; the source declaration and owner remain present. This is
different from an absent, ambiguous or unsupported skin, which still refuses.

The integrated Android15 x86_64 trainyard run `9463fe061801` prepared the owner,
assigned its NativeAlias to the real source mover and verified its state.
The game-camera visible/hidden/restored GPU control passed with zero restored
RGB differences. Map teardown checked `Wolf2009_LiveSourceStaticSkins()==0`;
production ARM64 core/game also compile with all QA disabled. These checks do
not establish retail shader parity or execution on a physical ARM device.

`request.visualPolicy` defaults to `WolfSourceStaticSkinVisualPolicy::Strict`.
The explicit `BaseDiffuseWithOmittedVerifiedModulationV1` policy applies after
the authored source skin. It leaves the strict importer, model, geometry and
diffuse shaders unchanged. Only qualified `AmbientDecalModulate` and
`AmbientDecal2xModulate` ColorMap classes gain retained stage-free, nondrawing,
shadow-free customSkin replacements. Mixed diffuse/normal/specular bindings,
alpha tests, explicit colour expressions, unknown techniques and incomplete
modulation declarations do not qualify for omission and keep strict behavior.
There are no source material-name exceptions or invented blend/UV/colour math.
An authored remap still has to pass the original strict conversion guards
unless its replacement qualifies for the explicit omission policy.

The owner retains each original ColorMap DDS as well as source/archive bytes,
raw source vertices and material facts. Preflight checks all referenced native
XYZ/UV/RGBA values against the retained strict import. Policy identity enters
the private alias key, so strict and compatibility customSkins cannot repaint
one another. `authoredNoOp` describes only the authored source skin;
`compatibilityOverlay`, `omittedModulationSurfaces` and
`omittedModulationTriangles` report the separate visual approximation.

The standalone target in this directory tests the shared portable selector
with synthetic material declarations. The optional owner-data check reads
`assets.pk4` and `trainyard.mpk`, applies the real selected skin, compares every
source vertex/index and validates all selected original DDS payloads. Expected
coverage is ten retained surfaces/3458 triangles: seven strict diffuse surfaces
(3358 triangles) and three omitted modulation passes (100 triangles). This
checks source facts and eligibility, not native material parsing or GPU output.

Build with the same C++11 compiler and compression options as
`src/idtech4a_render`, using `cmake -S tests/source-static-skin -B BUILD_DIR`,
then build `wolf2009-static-skin-policy` and run `--self-test`. Pass an original
`SP/base` path for the read-only wagon check. Native integration must additionally
check nondrawing/shadow state, actor presence, unchanged shared geometry and
owner cleanup; retail shader parity remains false under both policies.
