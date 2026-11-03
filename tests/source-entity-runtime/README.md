# Selected source entity runtime checks

`SourceEntityRuntime` owns one selected ENTS record, its complete resolved
compiled defaults, exact byte identity and both sides of every instance/default
override. Numeric words remain typed; an incompatible override never exposes
the parent's value. Unknown fields remain facts for native class admission.
Arbitrary spawnclasses do not become generic visual entities.

The standalone C++11 target `wolf2009-source-entity-runtime` checks selection,
ownership, typed overrides, refusal/budgets and an optional freshly read
trainyard DECL/ENTS pair. `WOLF2009_SOURCE_ENTITY_SOURCE_MAP` supplies the selected
owner archive. This test selects the five actual `container_locker_foot_01`
records, without claiming that selection executes gameplay. Source assets are
not distributed. It imports only the test's required input-reader dependencies.

`NativeSourceEntities.cpp` implements a separately registered `idAnimatedEntity`
class for the explicitly admitted `rvAnimatedContainer` profile. Its private
spawn dictionary uses source name/placement, an immutable source actor binding
and qualified sound mappings. Full declaration-closure identities are checked
before entity allocation. Its normal native animator cycles `start_anim` with
the measured count2^30, plays numeric sequence clips once and dispatches their
qualified frame1 sounds. No extension/rig/class inference or manual replay timer
is used. It retains the ENTS `idFile`, facts, actor binding and extra native
animation refs until deletion.

Placement uses an explicit `NativeNearUnitPlacementV1` compatibility profile.
The actual ENTS917 matrix is rigid. ENTS921 has XY row length0.997245924389 and
Z row length1; this is retained authored nonuniform scale, not float rounding.
The pinned Doom3 renderer requires unit axes, so native physics/rendering use
a proper row basis. Scale error above0.005 and normalized shear above1e-4 refuse
before Spawn. Raw matrix/decomposition remain owning facts. Runtime state
reports the approximation, Frobenius norm of A-R and current model-space radius;
their product bounds point displacement before float rounding. Portable tests
also retain exact affine decomposition for a future backend; no affine native
renderer support is claimed. Native QA checks both proper basis and the declared
raw-placement error bound rather than asserting raw affine rendering.

Two native compatibility policies are explicit:

- `NativeSessionDictionaryV1` stores a namespaced basename/entity persistence
  key in the GPL session dictionary. This is not retail save compatibility.
- `NativeIdItemBoundsQueryV1` visits native trigger-enabled `idItem` entities in
  entity order, using transformed animated bounds and a capacity50 limit. It
  retains source mask0x9000 separately rather than treating it as Doom3 bits.
  A matching item receives the real native `EV_Activate(opener)`; this does not
  qualify the surrounding original loot entities or their source broadphase.

The delayed give queue uses native game milliseconds and generation-checked
openers; self-reactivation suppresses giving while literal NULL remains distinct
from a deleted opener. Animation completion does not cancel independent giving.
The original factory remains an explicit no-AF diagnostic profile: it reports
the unsupported handoff before later source target/mission effects. A separate
`Wolf2009_SpawnSourceEntityWithAF` requires a complete explicit HKXR/resource,
identity and native policy request. It prepares before allocation, then hands
the completed pose to a real native AF helper. Highlight/focus, actor-use
notification and damage replacement remain unimplemented.

`Wolf2009_CheckSourceEntityQA(set, binding, exactENTSPath)` is a game-module helper
for the common native harness, called after existing mover/player checks. It
uses actual indices917/921 and tests independent native cycles, name isolation,
Use/Activate, first-frame sound emitter playback, the1200ms give timer with a
real GPL item/player inventory, the known AF boundary, session respawn,
generation replacement and deletion with a pending timer. It restores its own
session keys/inventory and deletes every owned entity. The caller then releases
native modelDef/animation references, the binding and computed model resources
in their normal lifecycle. Portable checks and syntax compilation alone do not
establish native runtime success.

At an actual clip endpoint the native completion adaptation explicitly holds
its final source frame before the AF boundary. QA compares the normal cached
joint palette with direct source `GetSingleFrame` plus native hierarchy FK;
it does not force a render callback before checking the cached pose. An old
AnimDone event before a newer animation's endpoint does not prematurely hold
that newer clip; the measured sequence argument remains unchecked.

The final combined native run passed243 entity checks/235 frames and518 AF
checks/134 frames. The AF profile is a fixed-root WORLD hinge with one dynamic
lid and two joint modifiers; source root collision walls are excluded. It uses
conservative box rounding and bounded NativeRestDelta anchor projection, with
explicitly incomplete collision coverage. Exact Havok behaviour is not claimed.
See [the paused checkpoint](../../docs/CHECKPOINT.md) for measured values and
remaining verification, including the small observed controlled angular motion.
