# AI presentation: scripted animation, frame commands and speech

Reviewed 2026-11-10. This page closes the static unknowns that block AI
stages 2 and 3 of [AI-ACTOR](AI-ACTOR.md) §8: scripted animation, model frame
commands, speech, and the exact data of the trainyard execution scene
(interrogator nodes 43 → 21/22 → 25 → 26/33/35 → 17,
[TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) E5b–E8). It ends with a native plan and
acceptance tests. **Nothing on this page is implemented or observed at
runtime.** For what runs, see [CHECKPOINT](CHECKPOINT.md).

Sources: the hash-identified retail `Gamex86.dll` (`7cb1cbed…2d47`) and
`Wolf2.exe` (`e8604f64…`), private copies only; the editor `.def` text in
`assets.pk4`; the trainyard ENTS, BRAI and HKXA resources of the owner's MPK.
Method: NDK `llvm-objdump` text with the private annotation scripts of
[AI-ACTOR](AI-ACTOR.md); no game process, compiler, debugger or new Ghidra
import. Raw output stays in the ignored `evidence/ai-presentation/`.

Status labels:

- **Established**: the assembly and its call sites were read, including
  argument push order. No execution was observed.
- **Data**: read from original ENTS, BRAI, HKXA or `.def` text.
- **Inferred**: consistent with Established and Data facts, not closed.
- **Unknown**: no adequate evidence yet.

Names such as `ScriptedAnim`, `AnimDone` or `CallFrameCommands` are
descriptive labels, chosen to match the closest Raven/id function. Debug
strings in the DLL are quoted where they exist. A label is not a recovered
signature. Offsets are vftable byte offsets (`+0x468`) or object fields
(`ai+0x954`) in this build only.

## 1. Summary

| Area | Contract | Status |
|---|---|---|
| Animate 51 Enter | `ScriptedAnim(name, mBlendFrames, mNumLoops, mEndWithIdle)` on an `idAI`; direct play/cycle with 4 blend frames on `rvVehicle`/`idAnimated` | Established |
| ScriptedAnim `+0x468` | begin scripted, look the clip up, turn to current yaw, play or cycle, then `State_ScriptedAnim` → `State_ScriptedStop` | Established |
| Clip lookup | per-entity model declaration; a trailing digit selects that exact clip, otherwise a random clip with that base name (game LCG) | Established |
| AnimFinished 50 | fail time, name check through the current animator channel, remaining time ≤ `FRAME2MS(mBlendFrames)` | Established |
| AnimOverrideDeath 57 consumer | death choice `+0x69c`: override first, without an existence check | Established |
| KillTarget 69 | `<kill>` on an `idAI` with damage flags `0x1`, or `0x11` when bypassed; bit `0x10` suppresses the death clip only for ragdoll-on-death AI | Established |
| Frame-command owner | `rvAnimFrameCommandList` per clip; dispatcher `0x203fe5d0`; range rule (from, to] on zero-based frames | Established (list); caller in the engine Unknown |
| `trigger`, `speak*`, `interactive_ragdoll` bodies | synchronous `activate`, `speak`, `speak_deferred`, `interactiveRagdoll` events | Established |
| Speak `+0x3ac` | `lipcount_` variants, spawn-arg line lookup, lowercase, repeated `_stream` strip, lipSync declaration, voice channel or speaker, subtitles, deferred request | Established; engine sample lookup Inferred |
| SNDS member | chosen by the lipSync declaration's `sample` field; `_stream` is never re-appended | Established (game side) + Data |
| SayFinished 10 / SayInterrupted 496 / StopSpeaking 15 | line comparison; interrupt time; stop with `mStopAnims`, `mFadeOutTime` | Established |
| Lip-sync visemes and head animation | engine-side | Unknown |

## 2. Scripted animation

### 2.1 Animate 51 Enter `0x201f5290` (Established)

The task embeds AnimFinished at `+0x4c`, so the fields are: `mAnimName`
`+0x6c`, `mActor` `+0x90`, `mBlendFrames` `+0xcc` (default 2),
`mAnimSucceeded` `+0xf0`, `mAnimFailTime` `+0xf4`, `mNumLoops` `+0xf8`
(default 0), `mEndWithIdle` `+0x11c` (default true).

1. `mAnimSucceeded = false`; `mAnimFailTime = gameLocal.time + 1`.
2. `idAI` actor: `mAnimSucceeded = ScriptedAnim(name, mBlendFrames,
   mNumLoops, mEndWithIdle)` (vslot `+0x468`, `0x201ac4d0`).
3. Otherwise `rvVehicle`, then `idAnimated`: look the clip up (`0x20017300`).
   If it is found, `mAnimSucceeded = true` and the entity plays it with a
   constant **4 blend frames**: `+0x324(clip, 4, mNumLoops)` when `mNumLoops`
   is non-zero, else `+0x318(clip, 4, rate 1.0)`. The earlier reading of the
   constant as channel 4 in [AI-ACTOR](AI-ACTOR.md) §4.2 is corrected here:
   both wrappers pass it as blend frames.
4. Any other entity: nothing; AnimFinished then completes on the next later
   tick (§2.5).

### 2.2 ScriptedAnim `+0x468` = `0x201ac4d0` (Established)

Arguments: (name, blendFrames, numLoops, endWithIdle).

1. **Begin** (`+0x458`, `0x201abd60`, argument `endWithIdle`): return false
   for a dead AI (`ai+0x954` bit `0x4`). Set the scripted bit `0x20` and
   store `endWithIdle` in bit `0x10` of `ai+0x954`. Clear the movement style
   `ai+0x11b5`. If hidden and `ai+0x955` bit `0x2` is clear, Show (`+0x100`).
   Optionally call `+0x3c4(1)` (guard `0x20003010`, not read). Outside a
   vehicle, stop movement (`+0x4e0`). Set `ai+0x11b2` bit `0x2`.
2. Look the clip up (`0x20017300`, §2.3). **A missing clip reports through
   `+0x350` and returns false, but the scripted bit set in step 1 stays
   set.** The report is `Missing '%s' animation on '%s' (%s)` with the
   current brain task, state and action. A cvar selects warning or developer
   warning.
3. Store the clip index in `ai+0x38d4`. Turn toward the current yaw
   (`0x2019da60(ai+0x124c)`), which cancels a pending turn.
4. `numLoops != 0`: AnimCycle (`+0x324` → `+0x320`, `0x200190f0`) with the
   loop count. Otherwise AnimPlay (`+0x318` → `+0x314`, `0x20018fe0`) at
   rate 1.0. Both log `idAnimatedEntity::AnimPlay` /
   `idAnimatedEntity::AnimCycle` through vslot `+0x44` with flag 4.
5. `0x20166070`: clear `ai+0x95c` bit `0x1` and store the result of
   `0x20016b70` (not read) in `ai+0x391c`.
6. Unless the AI is in a vehicle (`ai+0x774` non-zero or a live handle in
   `ai+0x76c`): cancel the current action (`0x2015f6a0`), clear the state
   thread `ai+0x9a0`, reset `ai+0x1e18`, call `0x201d6ce0(0,0,1)`, then post
   `State_ScriptedAnim` and `State_ScriptedStop` (`0x201ad230`).
7. Return true.

AnimPlay and AnimCycle hand the clip to the animator object `ent+0x30c` with
`FRAME2MS(blendFrames)` = `blendFrames × 1000 / 24` (integer). The anim flag
bit `NO_TRANS_POSE` is passed through. AnimCycle maps `numLoops = −1` to a
global default (`0x205ecea4`), `0` to 1 and `N` to N. It starts at a random
offset for `RANDOM_CYCLE_START` and synchronizes with the playing clip for
`SYNC_CYCLE`. The animator is created by the engine import at `+0x38` of the
game import table, so the animator implementation is in `Wolf2.exe`.

### 2.3 Clip lookup `0x20017300` (Established)

The entity's model declaration (`ent+0x308`, declaration type `0x22`,
`rvAnimModelDef`) owns a hash of clips keyed by the name without trailing
digits, hashed case-insensitively.

- Name ending in a digit (`0x20405870`): exact, case-sensitive match on the
  full clip name.
- Otherwise (`0x204059c0`): collect every clip whose base name matches
  case-sensitively and choose one with the game LCG at `0x2063376c`:
  `seed = seed × 214013 + 2531011`; `index = ((seed >> 17) × count) >> 15`.
- Empty or null name: not found.

This is Doom 3 `idDeclModelDef::GetAnim` behavior. All execution clip names
end in a letter and have exactly one match, so the random draw is a no-op
for this scene. It still advances the shared LCG.

### 2.4 States (Established)

From the `idAI` state table (`0x205bf110`):

| State | Body | Result |
|---|---|---|
| `State_ScriptedAnim` | `0x201cedd0` | `SRESULT_WAIT` while the scripted bit is set and `AnimDone(ai+0x38d4, 4)` is false; else `SRESULT_DONE` |
| `State_ScriptedStop` | `0x201cee50` | `EndScripted(false)` (`+0x460`), then `SRESULT_DONE` |
| `Wait_ScriptedDone` | `0x201cee70` | `SRESULT_WAIT` while the scripted bit is set |

`AnimDone(index, blend)` (`+0x338`, `0x20016cb0`) is true when the current
clip on the animator's channel is not `index` or its remaining time is at
most `FRAME2MS(blend)`. The state therefore ends **4 frames (166 ms)** before
clip end, independently of `mBlendFrames`.

`EndScripted(stopMove)` (`+0x460`, `0x201abc90`): no-op for a dead AI or in a
vehicle. With `stopMove`, cancel the action and movement. Then post
`Wait_ScriptedManual` if `ai+0x954` bit `0x40` (scripted manual) is set, and
always the initial state from `+0x76c`. Clear the scripted bit. The
`endWithIdle` bit's consumer was not traced (**Unknown**). The Quake 4 SDK
shows the same shape (`idAI::ScriptedAnim`, `ScriptedBegin`, `ScriptedEnd`,
`State_ScriptedStop` in `quake4/ai/AI.cpp` and `AI_States.cpp`), except that
Wolf has no torso/legs split in this path.

### 2.5 AnimFinished 50 Evaluate `0x201ebe70` (Established)

Condition fields: `mAnimName` `+0x20`, `mActor` `+0x44`, `mBlendFrames`
`+0x80`, `mAnimSucceeded` `+0xa4`, `mAnimFailTime` `+0xa8`. The result is
XORed with the condition's invert bit.

| Case | Result |
|---|---|
| `mAnimSucceeded` false | `now > mAnimFailTime` |
| `idAI` | `now > mAnimFailTime` **and** `+0x33c(name, mBlendFrames)` |
| `rvVehicle` / `idAnimated` | `now > mAnimFailTime` **and** `+0x334(mBlendFrames)` |
| no resolvable entity | true |

`+0x33c` (`0x200174a0`) is true when the animator's current clip does not
match `name` or when `+0x334` is true. The match (`0x20017430`) compares
`name` case-insensitively with the current clip's full name and base name.
`+0x334` (`0x200174e0`) compares the animator's remaining time with
`FRAME2MS(blendFrames)` (`≤`). Because `mAnimFailTime = start + 1`, the
condition is never true in the millisecond of Enter. A later Animate that
replaces the clip finishes the first AnimFinished through the name check.

### 2.6 Death override and KillTarget (Established)

AnimOverrideDeath 57 stores `ai+0x388c` ([AI-ACTOR](AI-ACTOR.md),
`SourceInterrogatorChainV1`). Its consumer:

- **KillTarget 69** (`0x201ed6b0`): fields `mKillTarget` `+0x4c`,
  `mBypassDeathAnim` `+0x88` (default `False`). An `idAI` target receives the
  `<kill>` body `0x20184e50(bypass)`. Any other entity gets `+0x218` with an
  empty damage record and zero.
- **`<kill>`** zeroes the field at `ai+0xd8`, calls `0x20012240`, builds a
  damage record (attacker from the handle at `+0x604` or the AI itself) with
  flags word `+0x3c` = `0x1`, or `0x11` when bypassed, and calls the kill
  virtual `+0x218` (`0x20155650`) with damage 0.
- In that path the death-clip predicate `+0x5e4` (`0x2014e6f0`) returns
  false unless `ai+0x956` bit `0x4` is set, and also when `+0x5e4` byte is
  set, some physics states apply or the current action is `ai+0x2a58`. When
  `ai+0x956` bit `0x10` is clear it returns true. When that bit is set, it
  returns true for `ai+0x11b0` bit `0x4`, `ai+0x11b3` bit `0x2` or damage
  flag `0x400`, otherwise `!(flags & 0x10)`. **The bypass therefore
  suppresses the clip only for an AI with bit `0x10` set.** Bit `0x4` as
  `playDeathAnim` and bit `0x10` as ragdoll-on-death are **Inferred** from
  the spawn keys; both hostages carry `playDeathAnim 1` (Data).
- The choice `+0x69c` (`0x2016b1d0`): a non-empty `ai+0x388c` is copied to
  `ai+0x1c88` **without an existence check**. Else spawn arg
  `deathAnimOverride` if the clip exists (`0x200173a0`). Else a computed
  directional choice.
- With a name, `+0x31c(name, 4, 1.0)` plays it with 4 blend frames. If the
  playing clip lacks `NO_INTERACTIVE_RAGDOLL` (anim flag `0x80000`), the AI
  goes interactive-ragdoll at once (`0x20012330`). The AI then posts
  `State_Wait_DeathAnim`.

## 3. Frame commands

### 3.1 Data model (Established)

Model declarations are `rvAnimModelDef` (type `0x22`). The anim block parser
(`0x203fda20`) reads `{`, then until `}`:

- `frame <list> <command…>`: `<list>` is comma-separated one-based frame
  numbers, stored **zero-based** (`n − 1`, `0x204046e0`). The clip gets an
  `rvAnimFrameCommandList` (vftable `0x20563134`) at `rvAnim+0x14`; each
  command is a 0x30-byte `rvAnimFrameCommand` with a type code.
- `rate <float>`.
- Any other word is an anim flag, matched case-insensitively against this
  table; the flag bit is `1 << index`:

  | Bit | Flag | Bit | Flag |
  |---|---|---|---|
  | 0 | PREVENT_IDLE_OVERRIDE | 11 | AI_DRAW_WEAPON |
  | 1 | RANDOM_CYCLE_START | 12 | AI_HIDE_WEAPON |
  | 2 | AI_NO_TURN | 13 | AI_FACE_COVER |
  | 3 | AI_NO_LOOK | 14 | AI_NO_PAIN |
  | 4 | AI_NO_ATTACK | 15 | AI_PURE_MOTION_EXTRACTION |
  | 5 | AI_LOOK_MUZZLE | 16 | AI_ALLOW_SLIDE_TURN |
  | 6 | ANIM_TURN | 17 | NO_DELTA_BLEND_OUT |
  | 7 | ANIM_TURN_FORCE_POSITIVE | 18 | FRAME_CMDS_ON_PRIMARY_ONLY |
  | 8 | ANIM_TURN_FORCE_NEGATIVE | 19 | NO_INTERACTIVE_RAGDOLL |
  | 9 | ANIM_APPROACH_GOAL | 20 | NO_TRANS_POSE |
  | 10 | SYNC_CYCLE | 21 | AI_NO_DELTA_PROCESSING |

  **Correction:** `no_interactive_ragdoll` in the `kc_one`/`kc_two` blocks is
  flag bit 19, not a frame command ([TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) E7).

Command names are compared case-sensitively by parser `0x204003c0`. Codes
used on the trainyard path:

| Name | Code | Name | Code |
|---|---|---|---|
| `sound` | `0x01` | `speak` | `0x45` |
| `sound_voice` | `0x02` | `speak_no_subtitle` | `0x46` |
| `trigger` | `0x10` | `speak_random` | `0x47` |
| `trigger_alias` | `0x11` | `speak_deferred` | `0x48` |
| `fx_offset` | `0x1a` | `aiming_enable` / `aiming_disable` | `0x5d` / `0x5e` |
| `ai_enablePureMotionExtraction` / `ai_disable…` | `0x3f` / `0x40` | `interactive_ragdoll` | `0x70` |
| `ai_set_eyeFocus` | `0x42` | | |

The parser knows codes up to `0x73`. Unknown names report `Unknown command
'%s'`.

### 3.2 Dispatch and frame crossing

**Established (game side).** `rvAnimFrameCommandList` vslot `+0x10`
(`0x20403980`) takes (from, to, entity). It keeps a cursor; when the cursor
is past the end or its entry's frame is greater than `from`, it restarts at
0. It skips entries with frame ≤ `from`, then dispatches every command of each
entry with `from < frame ≤ to`, in stored order, through `0x203fe5d0(entity)`.
Vslot `+0x14` (`0x20403b40`) dispatches all commands of one entry without a
range. Neither is called directly anywhere in the DLL. The animator that
owns the clip times lives in `Wolf2.exe` (§2.2), so the caller is
**Inferred** to be engine-side and was not read.

Consequences for an implementation:

- Several commands on one frame (for example `kc_one` frame 207 `fx_offset`
  then `trigger`) fire in the same call in list order. The insert routine
  `0x20404f60` was not read, so textual order within a frame is **Inferred**.
- One call never wraps. Looping and the first frame (frame index 0, which
  needs `from = −1`) depend on the caller (**Unknown**). GPL Doom 3
  `idAnimBlend::CallFrameCommands` makes exactly these choices: `from = −1`
  for the start, and a wrap through `idAnim::CallFrameCommands`.
- Blending out: Doom 3 `idAnimator::ServiceAnims` services every blend on
  every channel. The Wolf flag `FRAME_CMDS_ON_PRIMARY_ONLY` and the AnimPlay
  code that calls the previous blend's vslot `+0x14(0)` when that flag is set
  (`0x200190bb`) suggest that Wolf also dispatches for non-primary blends by
  default (**Inferred**). None of the execution clips sets the flag.

`idCameraAnim` has its own dispatcher (`0x200268d0`, vslot `+0x2f8`) with
`trigger`, `trigger_alias` and `light_fade`. It does not serve model clips.

### 3.3 Command bodies (Established)

Events go through `0x202b0820`, the synchronous ProcessEvent helper. It
does nothing, and returns false, when the receiver's class has no handler.

| Code | Body |
|---|---|
| `trigger` | `FindEntity(name)`; found → ProcessEvent(`activate`, owner). Missing → warning `Framecommand 'trigger' on entity '%s': Could not find entity '%s'`. No `SIG_TRIGGER` and no GUI trigger, unlike Doom 3 |
| `speak` | skipped without a line; ProcessEvent(`speak`, line, 1) on the owner |
| `speak_no_subtitle` | ProcessEvent(`speak`, line, 0) |
| `speak_random` | no-op in this build |
| `speak_deferred` | ProcessEvent(`speak_deferred`, line, 1) |
| `interactive_ragdoll` | ProcessEvent(`interactiveRagdoll`) |

`idAI` event bodies (table `0x205be330`):

- `speak` `0x201832d0`: `Speak(line, variant 1, failDelay 0, subtitle, null,
  null, clearFocus false, occlusion false)` through `+0x3ac`.
- `speak_deferred` `0x20183300` → `+0x680` (`0x201731e0`): append a 0x7c-byte
  request to the queue at `ai+0x3758…0x3764`. `0x2016da70` (an `idAI`
  per-frame routine, Inferred) calls `0x20173570`, which issues the head through `+0x3ac` once the actor
  is no longer speaking and pops it. `idAI` StopSpeaking (`0x20173180`)
  empties the queue before the actor stop.
- `interactiveRagdoll` → `0x20012330`, debug tag
  `rvAFEntity::AFPhysicsSetInteractive`: when not yet ragdolled and the AF
  has bodies with flag `0x4`, switch to interactive AF physics and become
  active. The physics meaning is **Inferred** from the tag.

`fx_offset` (`0x1a`) was not read (**Unknown**).

## 4. Speech

### 4.1 Say 12 and the speech fields

Say Enter (`0x201f5e90`) and SayFinished (`0x201ec1e0`) are in
[AI-ACTOR](AI-ACTOR.md) §5.1 (Established there). `Speak` (`+0x3ac`) takes
eight arguments, now all identified:

| # | Argument | Say passes | `speak` frame passes |
|---|---|---|---|
| 1 | line key | `mLine` | line |
| 2 | allow variants | 1 | 1 |
| 3 | fail delay (ms) | `mFailDelay` | 0 |
| 4 | subtitle | `!mSuppressSubtitle` | 1 (0 for `speak_no_subtitle`) |
| 5 | explicit lipSync name | "" | null |
| 6 | lipSync name to exclude | "" | null |
| 7 | clear focus | `mClearFocus` | false |
| 8 | occlusion | `mUseOcclusion` | false |

`idAI` Speak (`0x20173040`) refuses a null line. A dead AI with
`ai+0x1cac > 0` speaks only `lipsync_fall` and returns true without speaking
for any other line. Otherwise it clears
`ai+0x3748`/`ai+0x374c`, calls `idActor::Speak` and adds an AI-side follow-up
gated by `ai+0x1c85` bit `0x2` (not read further).

### 4.2 `idActor::Speak` `0x20005ce0` (Established unless marked)

1. Refuse when `actor+0x6dd` is set or the line is empty.
2. If argument 5 is non-empty, use it as the lipSync name and skip to step 5.
3. **Variant:** with argument 2 set, and either `actor+0x698` empty or the
   detail-pack interface (`0x205e6780`, vslot `+0x50`) accepting it, read the
   spawn arg `"lipcount_" + line.substr(8)`, default 0. The first eight
   characters are dropped without checking that they are `lipsync_`. For
   count > 1 draw r with the game LCG (§2.3). r > 0 appends `r + 1`. The
   `+0x50` query is **Inferred** to mean "detail pack loaded" from the
   `dialogDetailPackPriority` warning text.
4. **Resolve:** look the (variant) key up in the actor's spawn args, default
   "".
5. Lowercase the name and strip every trailing `_stream` (`0x204089d0`, a
   repeated suffix strip).
6. If argument 6 is non-empty and equals the name, treat the line as missing
   **without** a warning.
7. **Missing:** `actor+0x760 = now + failDelay`; warn
   `Missing vo '%s' on '%s'` unless step 6 applied; return false.
8. Record `actor+0x758` = start time, `actor+0x75c` = start time,
   `actor+0x710` = the **original line** (before the variant suffix), and
   `actor+0x734` = the lipSync name.
9. Look up the **lipSync declaration** by that name (declaration manager
   vslot `+0xd8`). A null result warns `LipSync '%s' on '%s' not found!` and
   **returns true** without playing or updating `actor+0x760`.
10. Head gate: if declaration vslot `+0x48` returns 0, the live head
    (`actor+0x4d0`) has a non-empty name at `head+0x340` and the detail-pack
    interface rejects it, then:
    - with the critical flag `actor+0x6dc`: warn `Critical lipsync '%s' (%s)
      skipped on '%s'. Check 'dialogDetailPackPriority.'` and return false;
    - otherwise store a pending request (`actor+0x6de = 1`, line `+0x6e0`,
      variant `+0x704`, fail delay `+0x708`, subtitle `+0x70c`) and return
      true. The critical flag also forces the subtitle on.
11. **Play:** with a live speaker entity (`actor+0x658`), its `0x2009da80`
    plays the lipSync name and returns the length. Otherwise the actor's own
    voice path `0x2004c040(name, &length)` uses an engine import. A length
    ≤ 0 warns `LipSync '%s' on '%s' uses sound '%s' with indeterminate
    duration!`. Otherwise `actor+0x75c += length`.
12. **Subtitle:** with argument 4 and a non-empty declaration subtitle string
    (`decl+0x2c`), the subtitle manager (`0x20875e70`, `0x203aa390`) shows it
    for `length + 500` ms.
13. With argument 8 (clear focus) on an `idAI`, reset `ai+0xf20` to 0 when it
    is non-negative.
14. `actor+0x760 = now`; return true.

### 4.3 How the stripped name reaches the SNDS bank

The stripped name is a **lipSync declaration name**, not an SNDS member name
(Established, game side). Data closes the rest of the chain:

```text
Say / speak line   lipsync_trn_1007_wi1
spawn arg value    trn_1007_wi1_stream    (scripted_voice_wi1_trn, merged into the actor)
strip _stream      trn_1007_wi1           (lipSync declaration name)
lipSync decl       lipsync/vo_<language>/vo.lipsync:  sample sound/…, stream 1, subtitles #str_…, visemes
SNDS member        the declaration's sample, in streampacks/scripted_voice_wi1_trn_vo_<language>.spk
```

`_stream` is never re-appended. The resolution of `sample` inside the
mounted pack happens in the engine import and was not read (**Inferred**).
The merge of `def_scripted_voice` keys into the actor's spawn args is still
**Inferred** ([AI-ACTOR](AI-ACTOR.md) §5.1).

The repository already resolves this chain: `src/wolf2009/ScriptedVoice.*`
checks `lipsync_<name>` → `<name>_stream` and reads `sample`, `subtitles` and
`stream` from the lipSync declaration. `SourceAudioBankNative` registers every
SNDS member of the whole bank by raw name. A native Speak should look up the
line through the actor's merged spawn args, strip and lowercase as in steps
4–5, resolve the lipSync declaration, and play the registered cache sample
named by `sample`. It must not search the bank by the stripped name.

### 4.4 Stop, interrupt and stream-out (Established)

- **StopSpeaking 15** (`0x201f6420`): cast `mSpeaker` (`+0x4c`) to
  `idActor`; call `+0x3b0(mStopAnims (+0x88), mFadeOutTime (+0xac, ms))`.
- **SayInterrupted 496** (`0x201ec5e0`): true for an `idAI` whose interrupt
  time `ai+0x374c` is non-zero and not later than now. Say Enter sets
  `ai+0x3748 = 1`, `ai+0x374c = 0`; `idAI` Speak clears both. The two writers
  that set an interrupt time (`0x20171a20` and `0x2018e980`) do so only when
  `ai+0x3748` is set; that they implement `speakStopRange` and
  StopAllSpeakingAI is **Inferred** from the condition description.
- **ScriptedVOStreamOut 490**: [BRAIN-TASKS](BRAIN-TASKS.md) §2.

### 4.5 Speaker, head and lip-sync interfaces

| Interface | Role | Status |
|---|---|---|
| `actor+0x658` speaker handle | plays the line instead of the actor and returns its length | Established call; body `0x2009da80` not read |
| `actor+0x4d0` head handle, `head+0x340` | head detail-pack name for the step-10 gate | Established |
| `0x205e6780` vslot `+0x50` | detail-pack acceptance query | Established call; meaning Inferred |
| lipSync declaration vslots `+0x48`, `+0x2c` | head-gate bypass; subtitle key | Established use; `+0x48` meaning Unknown |
| visemes, mouth animation | engine-side, from the lipSync `visemes` data | Unknown |

## 5. Execution-scene fixture (stage 2 acceptance data)

Data: `brains/trainyard/nazi_interragator` BRAI, trainyard ENTS, the
`model_trainyard_base` anim blocks of `def/maps/trainyard.def` and the
trainyard HKXA. Every clip is an `hkaSplineSkeletalAnimation` with 52
transform tracks and one annotation track, at 1/24 s per frame. Duration =
(frames − 1)/24 s.

### 5.1 Clips

| Clip (logical → HKXA) | Frames | Duration | Flags | Frame commands (one-based) |
|---|---:|---:|---|---|
| `srp_tyd_execute_nzi_one` → `srp_tyd_execute_nzi_01` | 273 | 11.333 s | — | f112 `speak lipsync_trn_1001_wi1`; f207 `trigger execution_gunshot` |
| `srp_tyd_execute_nzi_two` → `srp_tyd_execute_nzi_02` | 281 | 11.667 s | — | f97 `speak lipsync_trn_1006_wi2`; f207 `trigger execution_gunshot2` |
| `srp_tyd_execute_kc_one` → `srp_tyd_execute_kc_01` | 283 | 11.750 s | `NO_INTERACTIVE_RAGDOLL` | f207 `fx_offset fx_execute_blood01 head_channel 0 0 0 0 0 0`; f207 `trigger blood_decal_1`; f230 `interactive_ragdoll` |
| `srp_tyd_execute_kc_two` → `srp_tyd_execute_kc_02` | 285 | 11.833 s | `NO_INTERACTIVE_RAGDOLL` | f207 `fx_offset … head_channel …`; f207 `trigger blood_decal_2`; f230 `interactive_ragdoll` |
| four `*_start_idle` clips | 2 | 0.042 s | — | — |

Zero-based keys and their clip times at 24 frames/s: f97 → 96 (4000 ms),
f112 → 111 (4625 ms), f207 → 206 (8583.3 ms), f230 → 229 (9541.7 ms). The
clip-time mapping assumes the GPL time-to-frame conversion; the engine's own
conversion was not read (**Inferred**). The frame commands come from the
editor `.def`. Their equality with the compiled `model_trainyard_base`
declaration must be checked by the implementation through the compiled
reader before admission.

### 5.2 Entities

| ENTS | Name | Class and keys |
|---|---|---|
| 2091 / 2092 | `nazi_interragator_1/2` | see [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §4 |
| 2149 | `trainyard_hostage_1` | `trainyard_allies01`, (72,728,296), `Hide 1`, `playDeathAnim 1`, `hideWeapon 1`, `no_drops 1`, `speakStopRange 0` |
| 2150 | `trainyard_hostage_2` | `trainyard_allies02`, (120,728,296), same keys, `frame 1` |
| 2151 | `execution_gunshot` | `speaker`, (73,710,324), `s_shader ai_kar98_20ft`, `s_waitfortrigger 1`, `speakerMode restart` |
| 2207 | `execution_gunshot2` | `speaker`, (121,710,324), `s_shader ai_kar98_60ft`, `s_waitfortrigger 1`, `speakerMode restart`, `occlusionDry 50`, `indoorOutdoorFlag outdoor`, `speakerScope sound` |
| 477 / 478 | `blood_decal_1/2` | `func_static`, `Hide 1`, `noclipmodel 1` |
| 2152 / 2153 | `kill_prisoner_1/2` | `func_killer` targeting the hostages; no brain or trigger in this chain uses them (Data, bounded to the five chain graphs) |

### 5.3 Nodes (interrogator 1 network)

Defaults apply where the BRAI record stores none: Animate `mBlendFrames 2`,
`mNumLoops 0`, `mEndWithIdle true`; KillTarget `mBypassDeathAnim False`.

| Node | Entry | Arguments | Effect (this page) |
|---|---|---|---|
| 12/3 OnUsed | root | `player_leaves_train` | N → 43 |
| 43/54 | N | `mAnimName ""`, actor interrogator 1 | P → 42, 41, 40 (empty idle overrides on interrogator 2 and both hostages); N → 21 |
| 21/57 | N | hostage 1, `srp_tyd_execute_kc_one` | `ai+0x388c`; N (no condition) → 22 |
| 22/57 | N | hostage 2, `srp_tyd_execute_kc_two` | N (no condition) → 25 |
| 25/51 Animate | N | interrogator 1, `srp_tyd_execute_nzi_one`, blend 2, loops 0, end with idle | ScriptedAnim; P → 26, 33, 35 in output order; N [AnimFinished] → 17 |
| 26/51 Animate | P | interrogator 2, `srp_tyd_execute_nzi_two`, blend 2 | ScriptedAnim on interrogator 2; no Exit, never evaluated |
| 33/69 KillTarget | P | hostage 1, bypass false | `<kill>` flags `0x1` → death clip `kc_one` |
| 35/69 KillTarget | P | hostage 2, bypass false | `<kill>` → death clip `kc_two` |
| 17/33 | N | `mSetToTrue false`, interrogator 1 | P → 16 (interrogator 2 false), 46, 47 (SetUndying false); N → 6 Hunt |

Expected order in the Tick that enters 25 (Established flow from
[TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §3.1 and the bodies above):

1. Enter 25: interrogator 1 begins scripted, plays `nzi_one` with 2 blend
   frames (83 ms) and posts `State_ScriptedAnim`/`State_ScriptedStop`.
2. Probe 26: the same on interrogator 2 with `nzi_two`.
3. Probes 33, 35: each hostage is killed in that Tick and plays its override
   clip with 4 blend frames (166 ms). `NO_INTERACTIVE_RAGDOLL` prevents the
   immediate interactive ragdoll; each hostage waits in
   `State_Wait_DeathAnim`.

Expected frame events, relative to the shared clip start (**Inferred**
timing, §5.1):

| t (ms) | Owner | Event |
|---:|---|---|
| 4000 | interrogator 2 | `speak lipsync_trn_1006_wi2` → Speak(variant 1, fail delay 0, subtitle) |
| 4625 | interrogator 1 | `speak lipsync_trn_1001_wi1` |
| 8583 | interrogator 1 / 2 | `activate` on 2151 / 2207 (gunshots) |
| 8583 | hostage 1 / 2 | `fx_offset` (Unknown body), then `activate` on 477 / 478 |
| 9542 | hostage 1 / 2 | `interactiveRagdoll` |
| ≥ 11250 | interrogator 1 | AnimFinished on 25 true (remaining ≤ 83 ms) → 17 |

`State_ScriptedAnim` ends at about 11167 ms for interrogator 1 (remaining
≤ 166 ms) and posts the idle; node 25's AnimFinished then sees a different
current clip and is also true. Whichever comes first ends node 25 in the
next brain Tick. The `activate` response of a hidden `func_static` and of a
`speaker` with `s_waitfortrigger` is outside this page (**Unknown** for
`func_static`; the `idSound` Use/activate shape is in
[TRAINYARD-CHAIN](TRAINYARD-CHAIN.md) §3.2).

## 6. Native mapping

| Original contract | Reuse | New work |
|---|---|---|
| Clip lookup, play, cycle, blend | GPL `idDeclModelDef::GetAnim`, `idAnimator::PlayAnim/CycleAnim`, `FRAME2MS`; repo `EngineAnimationSet`, `SourceControlledEntity::PlayOnce` | Random base-name draw on the shared game LCG; loop counts; source clip owners for the execution clips |
| ScriptedAnim states | Quake 4 SDK `idAI::ScriptedAnim/ScriptedBegin/ScriptedEnd`, `rvStateThread` (`game/State.h`, already in the pinned tree) | `SourceAIScriptedAnimV1` state pair with the 4-frame `AnimDone`; failed lookup keeps the scripted bit |
| AnimFinished | GPL `idAnimState::AnimDone` | Fail-time rule; name match on full and base name |
| Frame commands | GPL `idAnim::CallFrameCommands`, `idAnimBlend::CallFrameCommands`; repo `SourceWeaponController` frame scheduler | Source anim blocks from the compiled model declaration; (from, to] with explicit start and wrap; `trigger`, `speak*`, `interactive_ragdoll` |
| Death override and KillTarget | GPL `idAI::Killed`, `idActor` death clip, AF | Override precedence without existence check; predicate `+0x5e4`; `State_Wait_DeathAnim` |
| Speech | GPL `idEntity::StartSoundShader` on `SND_CHANNEL_VOICE`; repo `ScriptedVoice`, `SourceAudioBankNative`, `NativeScriptedVOReadiness` | `Speak` resolution, variants, lipSync declaration, end time from the decoded sample length, subtitles, deferred queue |

The repo's animation set factory and `SourceControlledEntity` currently admit
**no** frame commands, flags or loops ([SETS](../src/idtech4a_anim/SETS.md)),
and `NativeSourceScriptedActor` uses `NativeFullPoseNoExtractionNoFrameCommandsV1`.
Stage 2 needs a new admitted profile, not a relaxation of those guards.

## 7. Implementation plan

### Stage 2 — `SourceAIScriptedAnimV1` (execution scene)

Order:

1. **Source clip admission.** Extend the animation-set path with a profile
   that admits the four execution clips and four start idles of
   `model_trainyard_base` for the two body models. Read their anim blocks
   from the compiled declaration; admit only the flags and commands of §5.1
   and refuse the rest by name.
2. **ScriptedAnim and states** on the native AI actor: Begin, lookup, play or
   cycle, `State_ScriptedAnim`/`State_ScriptedStop` with `AnimDone(…, 4)`,
   EndScripted with the manual branch. Wire Animate 51 and AnimFinished 50
   into `SourceBrainActionsV1`, including probe-entered Animate (26).
3. **Frame-command service.** One scheduler per animated actor, called from
   the ordinary animator service with (from, to] zero-based frames, `from =
   −1` at clip start, explicit wrap for cycles. Dispatch `trigger` as a
   synchronous native `activate`; `speak`/`speak_deferred` to the stage-3
   API, or a named refusal before it exists; `interactive_ragdoll` as a named
   refusal; `fx_offset` as a named refusal.
4. **KillTarget 69 and the death path**: `<kill>` record with the bypass flag,
   predicate `+0x5e4`, override choice, death clip with 4 blend frames,
   `State_Wait_DeathAnim`. Ragdoll stays a refusal.

Acceptance (native, original trainyard):

- From the real exit relay, nodes 43 → 21 → 22 → 25 run in one Tick; probes
  26, 33, 35 fire in that order; each actor starts its clip once.
- Each `execution_gunshot*` speaker receives exactly one `activate` per clip
  play, from the native frame service at zero-based frame 206; each blood
  decal receives one `activate`; replaying a clip after a reset fires again
  exactly once.
- AnimFinished on 25 is false at Enter and in the first later tick, and true
  only when the native animator reports remaining ≤ 83 ms or another clip
  playing. A boundary test checks 84 ms (false) and 83 ms (true).
- A synthetic Animate with a missing clip returns false, keeps the scripted
  bit, logs the missing-animation report and finishes AnimFinished on the
  next later tick.
- KillTarget with bypass false on a `playDeathAnim` hostage plays the override
  clip; with an override naming an absent clip, the death path records the
  original's unchecked choice as a named failure.
- Teardown during the scene leaves zero frame services, pending events and
  state posts.

### Stage 3 — `SourceAISpeechV1`

Order:

1. Portable `SpeechLineResolverV1` in `src/wolf2009`: merged spawn-arg view,
   `lipcount_` key (`line.substr(8)`), injected LCG sample, lowercase,
   repeated `_stream` strip, exclusion argument, lipSync declaration lookup.
2. Native `Speak` on the AI actor over the whole-bank lease: play the cache
   sample named by the declaration's `sample` on the native voice channel;
   end time = start + decoded length; missing line and missing declaration
   follow §4.2 exactly, including the `return true` on a missing declaration.
3. Say 12, SayFinished 10, StopSpeaking 15, SayInterrupted 496, the
   `speak_deferred` queue and the `speak` frame commands.
4. Subtitles, head detail-pack gate and lip-sync stay named refusals.

Acceptance:

- Every Say and `speak` line of the five trainyard chain graphs resolves to
  the lipSync declaration and SNDS member the data names; a test table holds
  the expected triples.
- `lipcount_` selection with injected samples covers r = 0, 1 and
  count − 1; the LCG advance matches the formula above.
- SayFinished on node 7 is false while the native voice channel plays
  `trn_1007_wi1` and true after it stops; a second Say on the same actor
  finishes the first through the line comparison.
- `speak_deferred` issues only after the current line ends.
- A missing line finishes after `mFailDelay`; a missing declaration does not
  advance `actor+0x760`, recorded as the original edge case.

## 8. Unknowns

- The engine-side caller of the frame-command list: time-to-frame
  conversion, first-frame and loop handling, blend-out policy and
  `FRAME_CMDS_ON_PRIMARY_ONLY`.
- `fx_offset` body; `activate` on a hidden `func_static`.
- The `endWithIdle` bit consumer; guard `0x20003010` and `+0x3c4(1)` in
  Begin; `0x201d6ce0(0,0,1)` in ScriptedAnim.
- Engine-side lipSync sample resolution, viseme playback and the head's
  mouth animation; lipSync declaration `+0x48`.
- The source keys behind `ai+0x956` bits `0x4`/`0x10` and `actor+0x6dc`.
- Whether `def_scripted_voice` keys are merged into the actor's spawn args.
- Equality of the `.def` anim blocks with the compiled
  `model_trainyard_base` declaration.

## Reproduce

Read-only inputs: private `Gamex86.dll` and `Wolf2.exe` copies with the
hashes above, their NDK `llvm-objdump` text, the trainyard BRAI dump and
HKXA index from earlier passes, and `def/maps/trainyard.def` and
`def/voice/scripted_voice.def` from `assets.pk4`. Helpers in the ignored
`evidence/ai-presentation/` print strings, find data references, scan for
virtual calls and map parser names to command codes. Output stays there.
