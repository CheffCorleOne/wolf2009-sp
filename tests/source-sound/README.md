# Original single-sample sound binding

`SourceSound` reads a sealed selected `sfx` declaration and retains its owning
record, category, duration, RMS and authored sample path. The explicit
`SingleWorldSampleV1` profile admits one World - Normal layer, one WAV reference
and weight1. Unsupported fields, routing, layers and weights refuse rather than
silently changing the sound. The portable checks include the owner's original
trainyard declarations and the two exact corresponding SNDS entries; assets are
not distributed.

`SourceSoundBinding` uses ordinary API8 files. The caller explicitly chooses a
mounted SNDS resource with an MP3 suffix; its name must match the authored WAV
stem. There is no global extension rewrite, package search or generated audio.
A readonly sample capability retains the source mount and records its epoch,
size and byte fingerprint. Native sound shaders reference that exact capability
so later native cache reads cannot select another mounted generation. The game
binding owns the source facts and sample capability for its lifetime.

`NativeWorldUnitGainV1` is an explicit native mixer policy: unit gain and Doom3
world attenuation from1 to10 metres. Source category/RMS/duration remain facts;
retail category buses, RMS normalization and exact attenuation are not claimed.
Binding requires the real native sound cache to load one non-default original
sample. Actor bindings retain typed sound owners and validate their selected
declaration provenance before admitting authored frame commands.

The common native harness enables the sound cache for animation-v2 checks and
keeps headless audio callbacks as an output sink. A decoded sample and native
emitter playback are separate observations from audible output on a device.
