# Source sound and MP40 presentation checks

The original `SingleWorldSampleV1` remains strict. The additive
`SinglePlayerSampleV1` admits one Player/High sample with explicitly authored
unit attenuationRange/attenuationMin. Other categories, priorities, missing or
duplicate fields, multiple samples and non-unit attenuation refuse. The full
selected declaration is retained; this is not the original category/RMS mixer.

Synthetic tests exercise both profiles and their separation. Optional read-only
inputs verify the original trainyard footlocker sounds and the global MP40
reload declaration. No original source records or sample bytes are embedded.
The standalone CMake target accepts `WOLF2009_MINIZ_SOURCE_DIR`,
`WOLF2009_SOURCE_SOUND_GLOBAL_DECLS` and `WOLF2009_SOURCE_SOUND_TRAINYARD_MPK`.

`NativeSourceWeaponPresentation` consumes the source weapon's retained reload
commands and an already qualified native animation set. It verifies selected
definition provenance, actual joint identities and exact command arguments.
It binds the explicit source reload SNDS through the existing immutable sound
capability and plays it on SND_CHANNEL_WEAPON. Playback counters, native sample
length and actual emitter state are observable. The weapon controller is the
sole source-frame scheduler; presentation does not create a second timer.

The explicit `NativeWeaponLocalAimAndPointTrackingV1` policy tracks the current
animated POI joint in world space with the retained unit-interval parameter.
It applies eye_angles as a bounded exponential local weapon rotation, using
the fourth/fifth command arguments as native rate/gain. Player camera and attack
ray remain unchanged. These choices do not assert retail viewcomponent parity.
One presentation binds one entity generation. A new equip needs a fresh owner.
The native controller stops its weapon sound channel on reset, disable, failure
and destruction; disabling also restores the hidden idle animation.

Portable success proves parsing and immutable source facts. Native syntax
success proves compilation only. Actual first-person poses, source-time command
crossings, emitter playback and teardown are checked by the integrated weapon
game/GPU tests; a headless sound sink is not an audible-device demonstration.
