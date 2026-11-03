# Source player, native first-person compatibility

`SourcePlayerProfile` resolves and owns the selected player, model, startup
library and exact map loadout closures. Default slots remain separate from the
selected map grant. Missing/invalid selected startup data never falls back to
the armed defaults. Numeric encodings and source field origins are retained.

The private `NativeSourcePlayer` factory creates a real GPL `idPlayer` subclass
in its reserved client slot during ordinary map startup. It maps explicit health,
mass, movement/eye defaults and map grants, with a separate source weapon
controller. It does not pass a proprietary dictionary to the Doom script system.
It uses the independently authored bootstrap body/script. Original body/AF,
viewcomponent, lean, sprint, Veil, health regeneration and retail saves remain
unimplemented and are reported as such. Native respawn reapplies the selected
map loadout and resets the source weapon controller to none; retail persistence
is not qualified.

`DecodeSourcePlayerClip` owns the full selected HKXS resource, exact named body,
raw transform, padded supports, planes, radius and filter. Native convexification
uses the declared local supports, converted once to binary32 game units. It does
not apply body/COM placement or add the separately retained `keepDistance`2.
Zero-radius direct ConvexVertices shapes are required. Stand and crouch are
separate native hulls. Growing checks the full standing hull at the current
origin, so an obstructing low ceiling keeps the player crouched. Death keeps the
last admitted hull in this first native policy.

Standalone CMake tests cover owning profiles, source/map selection boundaries,
malformed shapes and optional owner-supplied original declarations/HKXS. The
optional original test confirms the actual player health100/mass200, default
slots0/1/2/3/10 versus trainyard grants0/1/11 and selected none, plus both actual
50-declared/52-padded/72-plane clips. These tests alone make no native runtime
claim. `NativeSourcePlayerChecks.cpp` is linked by the native runtime QA and
exercises normal startup, original empty-floor/wall movement, crouch/ceiling,
jump and the explicitly native death/respawn path through actual game frames.
