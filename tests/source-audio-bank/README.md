# Encoded whole audio-bank data plan

`SourceAudioBankPlan` is portable C++11 data ownership/admission for the explicit
`SndsOnlyEncodedV1` profile. It contains no native sound/cache, decoder, resource
service, runtime FSM, mixer, VO action or game-clock dependency. A valid plan
means the complete archive data was admitted and retained. It has no Ready,
decoded, registered or playback flag.

The caller supplies a seekable read-only stream, its caller-established exact
virtual filename and a request with the same byte-exact filename. This first
profile accepts a safe ASCII `.spk` filename; it performs no host-path lookup,
locale expansion, fallback or association inference. Optional expected source
size/FNV must match. The caller establishes the filename-to-stream provenance;
FNV identities are local cache keys, not authentication.

Preparation first takes one bounded complete immutable source snapshot. The
existing `Archive::Scan` and `ReadEntry` then operate on that same owned memory:
table and member reads cannot accidentally mix different caller-stream versions.
Their existing zlib/checksum/size/table/alignment/exact-EOF checks remain in use.
Every chunk and entry is processed in source order. There is no selected-member
or silently skipped type path. Empty SNDS chunks remain in the manifest, but an
empty bank, empty encoded member or any non-SNDS raw tag is refused.

`Chunks()` retains each source ordinal, raw FourCC/header bytes, file header/data
offsets, compressed/inflated lengths, compressed-byte FNV and contiguous member
range. `Members()` retains raw type/name, separate normalized lookup name,
chunk/entry ordinals, **inflated-chunk** payload offset, length/FNV and owning
encoded bytes. `SourceBytes()` also retains the complete original package
snapshot, including compressed data, tables and padding. Copies share immutable
ownership; returned references remain valid while a plan copy is retained.

Raw names are unchanged. Duplicate raw names and ASCII-case/slash collisions
are refused across the whole bank, matching the VFS lookup comparison. The
native SNDS transport route additionally checks exact name plus a stripped
`.mp3` alias: a pair such as `sound/x` and `sound/x.mp3` is rejected because that
request would be ambiguous. This is transport-name qualification; member bytes
remain codec-opaque, even when the name ends in `.mp3`. Raw exact lookup performs
no alias rewriting. This policy is deliberately stricter than the generic
archive reader and does not broaden supported native audio formats.

Source, chunk, member, aggregate encoded, name, count and retained-data budgets
are checked. Compressed headers are preflighted before `Archive::Scan` to bound
its aggregate inflation. Before individual reads, a checked charge also covers
each whole-chunk inflation/table parse repeated by `ReadEntry`: the work budget
is the sum of `chunk.inflatedBytes * (1 + chunk.memberCount)`. This prevents a
small retained bank with thousands of tiny entries from authorizing excessive
repeated inflation. A separate bound applies the same accounting to compressed
input bytes: a valid inefficient zlib stream need not have a good compression
ratio. The retained-data charge covers the original snapshot, member
bytes and manifest fields; it is not an allocator/RSS measurement. Temporary
archive inflation is separately bounded by the existing archive limits and
the plan's chunk budget. Input failures throw without publishing a partial
owner or replacing a caller's prior retained plan.

The future core consumer must independently attach strict per-entry decoder and
native cache receipts to **all** enumerated members, with actual byte comparison
in addition to length/FNV. Mount epochs, generation/cancellation, cache leases,
foreign-alias ownership, partial native rollback and eventual whole-bank
completion belong to that separate service. Data admission must never substitute
for any of those results.

The standalone target links only this data plan, the existing archive reader
and zlib or the already installed pinned miniz. It does not change shared root
CMake. With a C++11 host compiler:

```sh
cmake -S tests/source-audio-bank -B build/source-audio-bank \
  -DWOLF2009_MINIZ_SOURCE_DIR=/existing/pinned/miniz
cmake --build build/source-audio-bank --parallel 1
ctest --test-dir build/source-audio-bank --output-on-failure
```

Omit the miniz path to use an installed zlib. For the optional private original
data check, also supply
`-DWOLF2009_ORIGINAL_AUDIO_BANK=/owner/SP/base/streampacks/scripted_voice_kc1_trn_vo_english.spk`.
The test opens that file read-only; no original payload is in the repository.
It checks the independently audited one-chunk/19-member/389672-byte dataset and
prints every source name/length/FNV. These are data checks, with no audio decode.

Validation: C++11 host build with `-Wall -Wextra -Werror` passed; CTest2/2 passed.
Synthetic185 checks/53 refusals cover multiple/empty chunks, nonzero padding,
source and shared ownership, raw identities and offsets, exact filename,
expected-source changes, unknown/late types, duplicate and transport aliases,
empty/late members, corrupt/truncated/zlib-trailing data, complete coverage and
all configured budget classes, exact repeated-inflation work and preflight
refusal of128 oversized-work chunk claims before zlib. The original run covered all19 entries, source
385486 bytes/FNV`c2301fb35aa8f5b2`, encoded total389672 bytes. Native integration
and strict decoding remain outside these checks.

# Native whole-bank registration and type19 readiness

The data plan above is the first stage of a native service for the explicit
`Category8SndsMp3V1` scripted-voice profile. Build it with
`Build-EngineRuntime.ps1 -WithAudioBank` (CMake `WOLF2009_SOURCE_AUDIO_BANK`,
default OFF). The QA build adds `-WithAudioBankChecks`.

## Core bank service

`src/idtech4a_anim/SourceAudioBankNative.cpp` runs in the engine core. The
separately linked game module reaches it only through ordinary `idFile`
operations on reserved paths, so GameAPI8 is unchanged:

- `wolf2009/_sound/bank/v1/request/<locale>/<definition>` expands the type19
  definition with the measured rule `streampacks/<definition>_vo_<locale>.spk`
  (`src/wolf2009/ScriptedVoiceBank.cpp`). Only lowercase `[a-z0-9_]`
  definitions and the locale `english` qualify; `vo_lang`, the entityDef
  `detailPack` and locale fallbacks are not consulted. The service then:
  1. snapshots the complete package and builds the data plan. Unknown chunk
     types, corrupt or truncated archives, duplicate, case-colliding and `.mp3`
     alias names refuse before any mount or cache work;
  2. mounts the package under a fresh label `vobank<generation>`, pins it and
     checks that the mount's resource and chunk inventory equals the plan;
  3. for every member in source order, forms the existing v1 sample capability
     (mount epoch, exact byte count and FNV-1a64), registers it with
     `idSoundCache::FindSound`, and requires a strict full-decode receipt
     (`SourceSoundDecodeReceipt`). The receipt compares the cached bytes with
     the plan's bytes and decodes every frame through the native minimp3 path;
  4. commits Ready once, after every member. Any failure purges this attempt's
     own provisional cache samples (their names carry the fresh epoch, so they
     cannot be foreign), unpins, unmounts and returns a refusal.

  The returned file is either a lease (manifest `state ready`; keeping it open
  keeps the bank, its pin and its mount) or a refusal (`state failed`, owning
  nothing). A live, still-verified bank with the same package is joined, with
  no repeated mount, decode or registration. A lost bank is never revived; a
  new request starts a new generation.
- `wolf2009/_sound/bank/v1/status/<generation>` re-observes a live bank: the
  mount epoch and inventory, and every cache sample (present, not purged or
  default, MP3 format, same PCM count, byte-identical). It returns `ready` or
  `lost`. An unknown or released generation opens nothing (stale).

Nothing in this service is specific to the 19-member pack: the manifest is
enumerated from the package. Only the English scripted-voice pack has been
validated. Other packs, such as later interrogator banks, need their own
qualification run before anything relies on them.

Releasing the last lease unpins and unmounts. Registered samples of a
published bank stay in the engine cache, since a consumer may still be mixing
them. Their stale epoch prevents a reload, and the next ordinary level-load
purge releases their data. Sample pointers are never retained across calls.

The admitted codec profile is the receipt's: resident encoded MPEG1 LayerIII,
mono, 44100 Hz, complete frames without tags or trailing bytes, decoded in
realtime mode (`s_useOpenAL 0`). OpenAL hardware buffers, PCM members, Ogg and
other codecs refuse the whole bank. `s_noSound 1` refuses because no native
cache exists.

## Type19 readiness provider

`src/wolf2009/ScriptedVoiceBank.h` holds the portable state machine
`ScriptedVOStreamInReadiness`. `src/idtech4a_gameplay/NativeScriptedVOReadiness.h`
is the game-module interface for a brain host:

```cpp
wolf2009::ScriptedVOStreamInSource source; // brainResource, nodeId, nodeType 19,
                                           // raw mDefName, locale, level generation, event id
idStr reason;
auto vo = WolfScriptedVOReadiness::Request(source, ownerEntity, reason);
// each Think/Tick of the owner:
vo->Update();
if (vo->ConsumeReady()) { /* advance node 29 exactly once */ }
// vo->State(): Pending, Ready, Failed, Cancelled, Lost; vo->Reason(); vo->Bank()
```

`Request` binds the source and the owner's entity number plus native spawn
generation. It does no work and starts `Pending`, or `Failed` for an
unqualified source. `Update` performs or joins the whole-bank request; only a
returned lease gives `Ready`. `ConsumeReady` is true at most once, after it
re-checks the owner generation and the bank status. Owner deletion, slot
replacement and `Cancel()` give `Cancelled`. A purged, unmounted or reset
backing gives `Lost`. A refused bank gives `Failed`. Every terminal state
releases the lease. `SetListener` offers a once-only callback from `Update` as
an alternative to polling. No timer, frame count or caller boolean can produce
`Ready`. Ready means that the complete bank is registered and retained; it is
not speech, subtitles, visemes, an actor start or a door activation.

## Tests

Portable, with any host C++11 compiler (the host is a declared test double,
not native evidence):

```sh
zig c++ -std=c++11 -Wall -Wextra -Werror -Isrc/wolf2009 \
  tests/source-audio-bank/ScriptedVoiceBankTest.cpp src/wolf2009/ScriptedVoiceBank.cpp -o scripted-voice-bank
./scripted-voice-bank   # SCRIPTED_VOICE_BANK_PORTABLE_OK checks=56 refusals=22
```

Native, on an explicitly selected Android x86_64 target:

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 `
  -BuildDirectory build/engine-audio-x86_64 -WithAudioBankChecks -Jobs 1
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -BuildDirectory build/engine-audio-x86_64 -WithAudioBankChecks `
  -AudioBankGameRoot 'C:\path\to\own\Wolfenstein'
```

The test copies the owner's English pack into the isolated fixture and runs
`MakeNativeFixtures.mjs`. It derives three packs from that copy (final member
shortened by one byte, a flipped Adler-32 byte, seven bytes truncated) and
authors five (generated MPEG silence, an opaque DECL chunk, an `.mp3` alias
pair, a case-colliding pair, a non-MP3 member). They stay in ignored evidence
and the device directory. The map is the authored `wolf2009/runtime`; owners
are ordinary `wolf2009_target` entities. The level-load purge goes through an
explicit core QA seam, because the headless sound system has no output device
and its public `BeginLevelLoad`/`EndLevelLoad` wrappers return early.

## Validated

Run `9941b7d722f0`, Android 15 x86_64 emulator (headless, `s_noSound 0`,
`s_useOpenAL 0`), map `wolf2009/runtime`, build `-WithAudioBankChecks`:

- Core `WOLF2009_AUDIO_BANK_NATIVE_OK checks=402 refusals=11`. The original
  English pack registered all 19 members with 19 strict receipts: 1622 frames,
  1868544 PCM samples, 385486 source bytes and 389672 encoded bytes. The first
  member decoded to 218880 samples, matching the earlier independent first-line
  decode. Each of the 19 native cache samples matched its receipt. A duplicate
  request joined generation 1 without new cache entries or registrations; a
  pinned unmount was refused; release removed the mount; the released
  generation was refused as stale. A re-request produced a fresh generation and
  epoch, an ordinary cache purge turned that live bank `lost`, and a later
  request registered a new generation instead of reviving it.
- Whole-bank refusals: unqualified locale, uppercase definition, absent pack,
  unknown chunk type, corrupt zlib, truncated archive, `.mp3` alias, case
  collision, a non-MP3 member after two registrations, and the original pack
  with its final member shortened (18 receipts and 19 provisional
  registrations, then refusal). No refusal left an owner, mount, status
  generation or resident provisional sample, and an unrelated live bank stayed
  ready. A retry name holding the bad pack failed; after valid bytes were
  restored, the same request registered all 19.
- Game module `WOLF2009_SCRIPTED_VO_READINESS_OK checks=49`: pending across 30
  ordinary frames; ready only after `Update`; once-only consumption and
  listener delivery; a shared generation for a second owner; cancellation;
  owner deletion before and after Ready; same-slot replacement; failed locale,
  node type and partial bank; lost after the purge and a fresh generation
  afterwards; zero providers, leases and banks after release. The engine
  reported `live_banks=0` at shutdown.

Separately, x86_64 production builds without QA compiled with the opt-in OFF
and ON (build only, not run); ARM64 was not built.

Not validated: audible output, mixing or playback of any bank sample; OpenAL
hardware-buffer configurations (refused by design); physical ARM64 devices;
parity with the retail stream slot, its loader statuses or its per-entry
registration ABI; save/restore of readiness; level transitions other than the
explicit purge; other packs or locales; and the trainyard greeter, relay,
actor or door chain consuming this provider. The engine also prints two
expected refusal warnings per member, because `idWaveFile` and the cache
timestamp probe first try `.ogg`/`.wav` names that the reserved capability
route deliberately leaves unresolved.
