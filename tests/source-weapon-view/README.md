# Original MP40 through the actual game camera

These optional checks run after the existing native EGL pbuffer setup, when
`WOLF2009_SOURCE_WEAPON_VIEW_QA` is compiled and the isolated source-session
fixture is explicitly requested. They use the already loaded GPL game module.
They do not create a detached preview world or draw a replacement model.

The game probe equips the retained original MP40 view binding on the real
source player/controller, exercises native input, and checks model identity,
finite49-joint poses, ammo32-to31, one authored attack, and source-timed reload
POI/eye/sound dispatch. The reload check observes a qualified original sound
sample and a playing native emitter; audible device output is not asserted.

The core calls ordinary `game->Draw(0)`, verifies three actual weapon draw
surfaces, reads the real512-square GLES framebuffer, and saves:

- `offscreen-source-mp40-idle.bmp`
- `offscreen-source-mp40-flat.bmp`
- `offscreen-source-mp40-restored.bmp`
- `offscreen-source-mp40-fire.bmp`
- `offscreen-source-mp40-reload.bmp`

The flat control changes only the weapon's per-instance skin at the same
simulation time. It preserves the hidden upgrades and geometry. Restoring the
original skin must restore the frame, while fire/reload must change both native
joints and visible pixels. Pixel differences ignore summed RGB deltas<=12.

Success requires both `WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GAME_QA_OK` and
`WOLF2009_NATIVE_SOURCE_WEAPON_VIEW_GPU_QA_OK`, followed by the ordinary source
session and engine teardown checks. Source code or syntax checks alone are not
a GPU pass. NativeBaseDiffuseV1 and weapon-local eye/point tracking are explicit
compatibility policies; original lighting/viewcomponent parity is not claimed.

Validated on Android15 x86_64 in run `80c55add17dc`: original texture37816
pixels, restored0, fire30519, reload58152, 3 actual gun draw surfaces. The same
run passes full native MP40 clip/damage/reload lifecycle both without a view
(924 checks/402 frames) and with the source view (924/400), then releases all
players/weapons/views/profile/session and animation descriptors at shutdown.
