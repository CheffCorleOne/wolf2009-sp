# Strict native MP3 cache receipt leaf

This core-private leaf observes one already registered sound-cache sample. It
does not load/play sounds, bind SFX/mixer/voice categories, retain a bank, or
acknowledge a BRAI node. A successful receipt owns an exact encoded snapshot,
canonical native sample name/fingerprint and actual complete PCM/frame counts.
The bank caller must retain cache/mount/consumer lifetime separately.

The admitted first profile is resident encoded MPEG1 LayerIII mono/44100, with
complete frames and no skipped tags/leading/trailing bytes. PCM-only, default,
purged and OpenAL-only samples refuse. No cvar is forced to make them qualify.

`SourceSoundDecodeReceipt.cpp` inspects the real native cache under its decoder
critical section. Call on the engine resource thread, outside that lock, while
cache lifetime is stable. The leaf uses the private decoder's actual read/error
accounting; ordinary `DecodeMP3` calls retain their historical behavior.

`patches/idtech4a-wolf2009-source-mp3-receipt.patch` is additive after the
prepared engine patches. It changes only `snd_decoder.cpp` and its included
`snd_mp3.cpp`, guarded by opt-in `WOLF2009_SOURCE_MP3_RECEIPT`/`_SND_MP3`. No public
decoder vtable, `idSoundCache` layout or GameAPI8 changes. Add
`SourceSoundDecodeReceipt.cpp` to core objects only; the engine codec TU already
has the animation include directory. Enable `WOLF2009_SOURCE_MP3_RECEIPT` for
both adapter and decoder and compile only `snd_decoder.cpp` with C++11 (its
ordinary baseline is GNU++98). Hook-OFF keeps the legacy TU GNU++98 compatible.
Never compile `snd_mp3.cpp` separately.

Portable tests compile the same framing/identity/read-accounting code against
the pinned engine's minimp3. Set `WOLF2009_MINIMP3_INCLUDE_DIR` to that existing
read-only `neo/externlibs/minimp3` directory. The tiny valid MP3 is authored
silence generated in C++; no original game sample is committed. Cases cover
complete actual PCM and valid silence, exact byte/name identity, truncation,
trailing/leading data, unsupported formats, work budgets, actual short/extra
reads and explicit read errors. This does not prove native-cache integration.

Optional core native QA is `Wolf2009_CheckSourceSoundDecodeReceiptQA(valid,
truncated,error)`. Generate three owned fixtures into a new private directory
with `node tests/source-sound-receipt/GenerateFixtures.mjs OUTPUT`. The ordinary
QA host must first initialize its encoded-MP3 sound profile/cache and stage
these files; this entry does not change backend/cvars. It checks the actual
cache, copied receipt lifetime, repeated calls, no cache mutation and refusal of
truncated/error fixtures. Root owns native build/hook/runtime integration.

All-bank readiness remains untested and unimplemented. The future bank service
must enumerate/decode/register every supported member, hold leases, handle
rollback/loss/generations and deliver its own once-only completion.

Validated for this leaf: authored portable codec test 47 checks passed; NDK
syntax passed for hook-OFF GNU++98 decoder, hook-ON C++11 decoder, adapter and
native QA entry. Additive patch applies cleanly to the prepared two source
files. Actual native-cache QA/runtime has not run yet.
