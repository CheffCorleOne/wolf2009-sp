# AI movement: source root motion and the per-frame move update

Reviewed 2026-11-10. This page unblocks AI walking. It records how the
original locomotion clips carry root motion and how the retail
`Gamex86.dll` (`7cb1cbed…2d47`) turns a move command into per-frame
translation and turning. It maps both to the GPL Doom 3 and Raven Quake 4
code in the pinned upstream and plans a `SourceAIMoveV1` profile. It
continues [AI-ACTOR](AI-ACTOR.md) stage 4, which stops because the move
update (`+0x4dc`) needs decoded root motion.

Labels follow [AI-ACTOR](AI-ACTOR.md):

- **Data**: measured from original HKXA, ENTS or `.def` data.
- **Static**: read from the DLL disassembly at the named procedure or call
  site. Not executed.
- **Retail trace**: the owner's PC run recorded in the greeter door research
  (64 samples of the greeter's world origin, about 0.517 s apart).
- **Inferred**: consistent with the above, not closed.
- **Unknown**: no adequate evidence.

Field names in `code` such as `move.fl.allowAnimMove` are the DLL's own
debug strings (printer `0x2017d810`), not guesses. Other names are
descriptive labels. Offsets are for this build only. A decompiler signature
is not used as a contract anywhere on this page.

## 1. Summary

| Question | Answer | Status |
|---|---|---|
| Where is root motion? | In a separate extracted-motion channel: each locomotion clip's `hkaSplineSkeletalAnimation` points to an `hkaDefaultAnimatedReferenceFrame`. The skeleton's `origin` joint has no translation and a constant rotation | Data |
| Encoding | One sample per frame. `xyz` is the cumulative translation from frame 0 in model space (+X forward, +Y left, +Z up), in game units. `w` is the cumulative yaw in radians about +Z, left positive | Data |
| Speeds | Unarmed run `std_nil_nml_run`: 126.20 units per 0.75 s cycle, 168.3 u/s, linear. The retail greeter ran at 154–175 u/s chord speed | Data, retail trace |
| Per-frame translation | Taken from the animator's extracted-motion delta, not from a fixed speed. It is rotated by the actor's axis and handed to the AI physics object | Static |
| Turning | A constant-rate turn toward `move.yaw_ideal` with rates from `maxTurn*` keys (deg/s), or an animation-driven turn from the clip's extracted yaw for `anim_turn` clips | Static |
| ReachedAndFacing's turn state | Is the move command state. On arrival the AI posts `reached` on the goal, then issues a face-entity command; IsFacing becomes true when that command ends, which happens when `yaw_current` is within 0.01° of `yaw_ideal` | Static |
| Style bits `0x11` | `scripted_run` + `scripted_pose_combat`. `scripted_run` forces the running flag, which selects the `run` clip suffix | Static |
| Clip name | `<prefix>` + `run` / `walk` / `dash`, with walk and run override strings first. The prefix rules are not closed | Static (suffix), Unknown (prefix) |
| Path following, smoothing, travel mask | Path and route sensor objects. Only their interface and obstacle checks were read | Static (interface), Unknown (algorithm) |

## 2. Root motion in the data

### 2.1 Where it is stored (Data)

The existing named reader ([spline-clip](../tests/spline-clip/README.md))
already retains the pointer at animation offset 24 as the opaque
`extractedMotion` fact. It points to an `hkaDefaultAnimatedReferenceFrame`
with this 32-bit layout:

| Offset | Field | Observed |
|---:|---|---|
| 16 | `up` (4 floats) | (0, 0, 1, 0) |
| 32 | `forward` (4 floats) | (1, 0, 0, 0) |
| 48 | `duration` | equal to the clip duration, bit for bit |
| 52 | `referenceFrameSamples` pointer, count, capacity | count = clip frame count |

Census over the two packs read for this page:

| Pack | Clips | With reference frame | Conforming to the table |
|---|---:|---:|---:|
| `maps/game/global/global.spk` HKXA | 1,174 (933 with 52 tracks) | 551 (all 52-track) | 551 |
| `maps/game/trainyard/trainyard.mpk` HKXA | 187 | 36 | 35 |

The single exception is the vehicle clip
`models/vehicles/train_engine/train_move.hkx`, whose frame uses
`up = (0, 1, 0)` and `forward = (0, 0, 1)`. A first profile must refuse it by
name. In every conforming clip, sample 0 has zero translation and
`w = 2^-10` (0.0009765625). Clips without turning end with `w` between
0.0010 and 0.0014 rad, which is under 0.025°. Consumers should therefore use
differences of `w`, not absolute values (**Inferred**).

No other channel carries root motion (**Data**). The 52-joint
`humanoid_male` rig's joint 0 is `origin`. Its translation mask is empty and
its rotation is the constant basis change (0.5, 0.5, 0.5, 0.5) in the run,
walk and turn clips that were sampled. With the clip sampled through a port
of the private spline decoder, the `hips` joint in model space stays within
about 1.3 units of the vertical axis. Its heading stays near 0° even at the
end of `st1_nil_nml_to_run_rt_135`, whose reference frame turns by −135°.
Translation and yaw have both been removed from the bone tracks.

### 2.2 Axes, yaw and units (Data)

- +X is forward. In `std_nil_nml_run` the translation is (7.011 × frame,
  0, 0) to within 1e-6.
- Left turns are positive. Every `…_lt_45` clip ends at `w − w0 =
  0.7854` (π/4), every `…_lt_135` at 2.3562 (3π/4), and the `_rt_` clips at
  the negative values. The translation of `to_walk_lt_45` ends at (65.27,
  62.20), to the left.
- Units are game units and radians. The speeds below fall in the range
  measured for the retail greeter (§2.4).
- Cycles: the last frame of a looping clip repeats frame 0's pose (hips
  (0.05, 34.62, 0.00) at frames 0 and 18 of the run). One cycle is
  `frames − 1` intervals at 24 Hz, and per-cycle displacement is the last
  sample's translation.

### 2.3 Numeric checks (Data)

| Clip | Frames | Duration (s) | End translation (x, y) | End yaw (rad) | Speed (u/s) |
|---|---:|---:|---|---:|---:|
| `std_nil_nml_run` (`std_nil_nml_run`) | 19 | 0.750 | 126.20, 0.00 | 0 | 168.3 |
| `std_mg3_nml_run` (`std_mg1_nml_run`) | 17 | 0.667 | 123.04, 0.00 | 0 | 184.6 |
| `std_mg3_com_run` | 17 | 0.667 | 96.49, 0.00 | 0 | 144.7 |
| `std_mg3_com_run2` / `_run3` | 17 | 0.667 | 143.25 / 120.78 | 0 | 214.9 / 181.2 |
| `std_mg1_aim_run` | 14 | 0.542 | 96.89, 0.00 | 0 | 178.9 |
| `civ_nil_nml_walk_02` (`std_nil_nml_walk`) | 29 | 1.167 | 60.99, 0.00 | 0 | 52.3 |
| `std_mg3_nml_walk` (`std_mg1_nml_walk`) | 31 | 1.250 | 47.92, 0.00 | 0 | 38.3 |
| `std_mg3_com_walk` | 26 | 1.042 | 53.55, 0.00 | 0 | 51.4 |
| `std_nil_afd_walk` (trainyard pack) | 32 | 1.292 | 48.24, 0.00 | 0 | 37.3 |
| `st1_nil_nml_to_run` | 36 | 1.458 | 139.61, −1.55 | 0 | — |
| `st1_nil_nml_to_run_lt_45` | 35 | 1.417 | 96.39, 99.53 | +π/4 | — |
| `st1_nil_nml_to_run_rt_135` | 45 | 1.833 | −126.97, −135.54 | −3π/4 | — |
| `std_nil_nml_run_to_st1` (and `_lt/_rt_45/135`) | 31–44 | 1.25–1.79 | 126.22 (±2.4 lateral) | 0 / ±π/4 / ±3π/4 | — |
| `std_nil_nml_run_to_st1_short` (5 variants) | 23–31 | 0.92–1.25 | 63.1 | as named | — |
| `std_mg1_nml_run_to_st1` (5 variants) | 31–44 | 1.25–1.79 | 123.04 | as named | — |

The `.def` alias is in parentheses where it differs from the HKXA file.

Two regularities matter for stopping. The forward translation of every
`…_run_to_st1` variant equals one cycle of the matching run (126.22 vs
126.20 for `nil_nml`; 123.04 vs 123.04 for `mg1_nml`), and `_short` is half
of it. The turn variants put their turn after that translation. These clips
carry the `anim_approach_goal` flag (`.def` text). This is consistent with a
stop that starts one cycle before the goal (**Inferred**, see §3.6).

### 2.4 The greeter's own clips and the retail trace

ENTS gives `train_greeter` (entity 2034) `angle 0`, `anim
srp_tyd_nml_train_door_open` and `frame 148` (**Data**). Its body model,
`model_civilian_male_04`, inherits `model_civilian_male_base`, which maps
`std_nil_afd_walk` and `std_nil_afd_run` both to the 37 u/s afd walk. The
humanoid base defines `std_nil_nml_run` and `std_nil_nml_walk`. No
`std_nil_com_*` locomotion clip exists in the `.def` corpus.

The door-open clip (182 frames, 7.542 s) has **zero** root motion through
zero-based frame 147. From frame 148 (6.167 s) it steps back and turns left.
At frame 181 the translation is (−76.45, 82.03) and the yaw +2.356 rad
(+135°). The authored `frame 148` is the first frame with motion.

Retail trace (**Retail trace**, `pc-door-trace`):

| Interval (s) | Greeter (x, y) at end | Chord speed (u/s) |
|---|---|---:|
| ≤ 18.966 | 176.75, 271.75 (spawn) | 0 |
| 18.966–19.483 | 170.10, 272.84 | 13.0 |
| 19.483–19.999 | 156.38, 288.26 | 40.0 |
| 19.999–20.516 | 91.05, 336.52 | 157.1 |
| 20.516–21.033 | 16.98, 388.02 | 174.5 |
| 21.033–23.099 | through (−129.68, 470.58) and (−157.50, 547.30) | 170.9, 155.8, 157.9, 153.9 |
| 23.099–25.166 | −271.06, 702.03 | 126.3, 85.0, 33.1, 6.0 |
| ≥ 25.166 | −271.06, 702.03, z 312.06 | 0 |

Checks against the data:

1. **Move start moves the actor (Inferred).** Applying the door-open clip's
   reference frame at yaw 0 from the spawn origin with a fitted start time
   of 13.022 s predicts (169.88, 273.41) at 19.483 s and (158.72, 292.02) at
   19.999 s. The retail positions differ by 0.6 and 4.4 units. The fitted
   start lies before the first sampled door-joint change (14.316 s), as the
   synchronous `activate` in AI-ACTOR stage 4 requires. At 20.516 s the clip
   is ending and the prediction no longer holds.
2. **Run speed.** The straight running intervals give 154–175 u/s, in the
   range of `std_nil_nml_run` (168.3) and far above the afd walk (37.3). Two
   chords exceed 168.3 by up to 3.7 %, so either the selected clip, its rate
   or the trace timing differs slightly (**Unknown**).
3. **Stop.** From 23.099 s the greeter covers about 130 chord units, close to
   the 126.22 of `std_nil_nml_run_to_st1`, and ends 0.07 units (2D) from
   `run_to_ladder_point` (−271, 702, 318), although the brain range is 20.
   This favors an approach-goal stop over a stop at range (**Inferred**).
   The deceleration profile does not match a single unblended stop clip.

## 3. The original move update (Static)

### 3.1 Move state fields

From the debug printer `0x2017d810` (names are the DLL's strings):

| Field | Meaning |
|---|---|
| `ai+0x11b0` | `move.fl.done 0x1`, `moving 0x2`, `crouching 0x4`, `blocked 0x8`, `pathBlocked 0x10`, `onGround 0x20` |
| `ai+0x11b1` | `running 0x1`, `idealRunning 0x2`, `dashing 0x8`, `idealDashing 0x10`, `disabled 0x20`, `allowPathfinding`, `allowObstacleAvoid 0x80` |
| `ai+0x11b2` | `allowDirectional 0x1`, `allowAnimMove 0x2`, `allowHiddenMove 0x4`, `allowPushMovables 0x8`, `allowSlideTurning 0x10`, `allowBreakObstacles 0x20`, `noRun 0x40`, `noWalk 0x80` |
| `ai+0x11b3` | `noTurn 0x1`, `noTurnCover 0x2`, `noGravity 0x10`, `noSeekMove 0x20`, `noCrouch`, `noStand 0x80` |
| `ai+0x11b4` | `pureMotionExtraction 0x4`, `approachFacePosition 0x10`, `approachFaceEntity 0x20`, `approachFaceDirection 0x40`, `approachFaceActive 0x80` |
| `ai+0x11b5` | `scripted_run 0x1`, `scripted_walk 0x2`, `scripted_dash 0x4`, `scripted_endInCrouch 0x8`, `scripted_pose_combat 0x10`, `scripted_pose_stealth 0x20`, `scripted_noClusters` |
| `ai+0x11b6`/`0x11b7` | `interrupt_on*` bits; `touchTriggers 0x4` |
| `ai+0x11c0` | `move.command`: `MOVE_NONE` 0, `FACE_ENEMY` 1, `FACE_ENTITY` 2, `FACE_ENTITY_DIRECTION` 3, `FACE_POSITION` 4, `TO_ENEMY` 5, `TO_ENTITY` 6, `TO_FOLLOW` 7, `TO_COVER` 8, `TO_POSITION` 9, `TO_DIRECTION` 10 (table `0x205be198`) |
| `ai+0x11c4` | move status (cleared by StartMove and StopMove) |
| `ai+0x11cc` | `move.range` |
| `ai+0x1248` / `0x124c` | `move.yaw_ideal` / `move.yaw_current` (degrees) |
| `ai+0x12a8` / `0x12ac` | `move.direction_current` / `direction_ideal` (`MOVEDIR_FORWARD/BACKWARD/LEFT/RIGHT`) |
| `ai+0x12dc` | goal position; `ai+0x12e8` `move.goalAreaNum`; `ai+0x12ec` `move.goalEntity` |
| `ai+0x1380` | `move.approachFaceEntity` |
| `ai+0x1444` | `aasPathSensor` (`lastSearchTime`, `lastGoalAreaNum`, `pathAreas.length`, `pathAreas.distance`, `pathClusters.distance`) |
| `ai+0x1448`, `ai+0x144c` | `aasRouteSensor` objects (`hitEntity` at sensor `+0xa8`) |
| `ent+0x154` | AAS movement state: `originAreaNum +0x40`, `height +0x64`, `radiusMajor +0x68`, `radiusMinor +0x6c`, `speed +0x74`, `speedForward +0x78` |

GoToAndFaceTarget's `0x11` is therefore `scripted_run | scripted_pose_combat`
(the AI-ACTOR table is consistent: Walk `0x2`, Dash `0x5` = run + dash).

### 3.2 Run, walk and clip choice

- **Forced run resolver** `0x20198040`: `noWalk` → run; else `noRun` →
  walk; else `scripted_run` → run; else `scripted_walk` → walk; else no
  forced choice. **Dash resolver** `0x20199780`: a global setting, then
  `scripted_dash`, then `ai+0x11b1` bit 0x4.
- **Move clip name** `0x201681c0`: while `move.fl.running` and the run
  override string (`ai+0x37fc`) is non-empty, return it. While not running
  and the walk override (`ai+0x37d8`) is non-empty, return that. Otherwise
  build `<prefix>` (virtual `+0x6a4`, `0x20167c00`) and append `dash` when
  `move.fl.dashing`, else `run` when `move.fl.running`, else `walk`. A
  `_crouch` variant is appended under further conditions that were not
  closed.
- **Prefix** `0x20167c00` concatenates fragments from the string table at
  `0x20503517`: stance `std_`, `st1_`, `st2_`, `ch1_`; weapon `nil_`,
  `mg1_`, `hg1_`; mode `nml_`, `com_`, `aim_`, `brd_`, `ptr_`, `foc_`,
  `cov_`, `rdy_riflebutt_`. Each choice comes from a predicate virtual
  (`+0x684` … `+0x698`) given a pair of request bits. The combat predicate
  (`+0x688`, `0x20166380`) depends on enemy and action state. **Which prefix
  the greeter gets is Unknown.** The data (§2.4) and the retail speed point
  to `std_nil_nml_run`.
- Turn and start variants use the suffix strings `to_run`, `to_walk`,
  `to_dash`, `_lt_45`, `_rt_45`, `_lt_135`, `_rt_135`, `turn_`,
  `turn_nomove_` (`0x205035dc`–`0x20503688`); their selection logic was
  not read.

### 3.3 Per-frame translation (move update `0x2019f660`)

In Think order, the update that AI-ACTOR §6.1 names does the following
(only the parts that were read are listed):

1. **Clip flags.** It reads the anim flags of the current clip
   (`0x20016b70`, `0x200173c0`). It latches `ANIM_TURN` (unless
   `ai+0x11b8` bit 0x4 is set), `ANIM_TURN_FORCE_POSITIVE/NEGATIVE` and,
   when `ai+0x1294` is set, `ANIM_APPROACH_GOAL`. The bit numbers are those
   in [AI-PRESENTATION](AI-PRESENTATION.md) §3.1.
2. **Advance the animator** (virtual `+0x340` → engine animator `+0x88`).
3. **Approach goal.** The remaining approach distance `ai+0x1284` is
   reduced by the length of this frame's animation translation, floored at
   0.
4. **Anim turn.** The remaining anim-turn angle `ai+0x127c` is reduced by
   the yaw of this frame's animation rotation.
5. **Delta.** If `ai+0x1348` is set, a stored delta (`ai+0x134c`) is used.
   Otherwise, if the frame time is non-zero and the actor is dead, or
   `allowAnimMove`, or `pureMotionExtraction`, or the clip has
   `AI_PURE_MOTION_EXTRACTION`, or entity byte `+0x15f` bit 0x2 is set, the
   delta is the animator's **delta translation** (`+0x344` →
   `0x20017520`: the translation column of the matrix returned by engine
   animator `+0x94`) divided by the frame time, times a speed scale
   (`+0x4fc`: 1.0 unless dashing), times a per-axis scale (`ai+0x129c`).
   Otherwise the delta is zero. The animator's delta rotation is `+0x348`
   (`0x20017580`). Both return identity when entity byte `+0x3a0` bit
   0x20 is set.
6. **World delta.** The delta is rotated by the actor axis `ai+0x494`.
   Unless the clip has `AI_NO_DELTA_PROCESSING`, virtual `+0x538`
   (`0x201984e0`) zeroes it when the area at origin + delta has a flag
   byte (`+0x2a`, bit 0x4) set. The identity of that area field was not
   closed.
7. **Translation check.** With a route sensor and `allowAnimMove`, a
   non-zero delta is passed to `rvAASRouteSensor::HandleAnimTranslation`
   (`0x20133760`, its only caller is `0x201a3dbd`). It stores the segment
   from the physics origin to origin + delta, queries the AAS world
   (`+0x15c` of the AAS object) along it and records the blocking entity
   (`+0xa8`) and two result bytes (`+0x250`, `+0x251`). The update then
   compares the distance to that entity. Its reaction was not read.
8. **Physics.** The delta goes to the AI physics object (`ai+0x10b8`, RTTI
   `idPhysics_AI` / `rvHkPhysics_AI`) through `0x202edec0`, together with
   `noGravity`. `onGround` is read back into `move.fl.onGround`. The delta
   times the frame time is kept at `ai+0x1384`.

The translation is therefore **animation-driven**. No fixed speed was found
in this path; `aas->speed` exists, but its use was not read.

### 3.4 Turning

Spawn keys of the move state (`0x20198bd0`) include `maxTurnIdle`,
`maxTurnAlert`, `maxTurnWalking`, `maxTurnRunning`, `turnAnimScaleEnemy`,
`turnAnimScaleRandom`, `turnSmoothRadius`, `allowTurnSmoothing`,
`allowSlideTurning`, `noTurn`, `noTurnMove`, `forceRun`, `forceWalk`,
`directionalMovement`, `animate_z` and `ignorePathVoids`. The greeter inherits
`humanoid_base` (`.def`): `maxTurnIdle 180`, `maxTurnAlert 240`,
`maxTurnWalking 240`, `maxTurnRunning 420`, `turnSmoothRadius 20`,
`allowTurnSmoothing 1`, `directionalMovement 0`. Missing keys default to
5.0, 4.5 and 5.0 (setter `0x20184160`).

The turn section of the move update has three branches:

- **Anim turn** (latched `ANIM_TURN`): the yaw step comes from this frame's
  extracted rotation (`0x2019b700`, which takes the remaining angle and the
  force flags). The anim turn ends when the remaining angle is below the
  thresholds (30° and 0.1°) while the current action is the move action
  (`ai+0x3b60 == ai+0xdec`); `ai+0x11b8` bit 0x4 then blocks it for the
  rest of the clip. How the step is scaled to the required turn was not
  closed.
- **`noTurnCover`** uses `ai+0x1264` as a rate.
- **Ordinary turn**, when a frame-level turn flag, `allowSlideTurning` or
  the clip flag `AI_ALLOW_SLIDE_TURN` allows it: `diff =
  normalize180(yaw_ideal − yaw_current)`. The rate is `ai+0x1264` when
  non-zero; otherwise 3000 when dashing, `maxTurnRunning` when running,
  `maxTurnWalking` when moving, `maxTurnAlert` when `ai+0x1c84` bit 0x2,
  else `maxTurnIdle`. The step is `diff` clamped to ±rate × elapsed ms ×
  0.001, times the turn scale `ai+0x1268`; `yaw_current` is normalized
  after adding it. Unlike Quake 4, there is no turn-velocity integration.

The view axis `ai+0x494` is rebuilt from `yaw_current` at the end.
`FacingIdeal` (`0x2019ad90`) is true, and snaps `yaw_current` to
`yaw_ideal`, when the normalized difference is below 0.01°. TurnToward
(`0x2019db90`/`0x2019da60`) sets `yaw_ideal` and returns the same test.

### 3.5 Arrival, facing and IsFacing

For moving commands the update tests ReachedPos (AI-ACTOR stage 4) on the
goal position with `move.range`. On success:

1. It posts the event `0x20740a88` (`reached`, as in MoveToEntity) on
   `move.goalEntity` with the AI as argument (`0x202b08a0`).
2. It copies the scripted pose bits into `ai+0x954`.
3. With `approachFacePosition` it turns toward the stored position
   (`0x2019e410`, tolerance 0.1). With `approachFaceEntity` it calls
   FaceEntity (`0x2019e220`) on `move.approachFaceEntity` with tolerance 0,
   or 0.1 when that entity is an `idActor`. It then clears the approach
   bits and `allowAnimMove` and sets `ai+0x11b3` bit 0x8. With
   `approachFaceDirection` it calls `0x2019e320`. Otherwise it calls
   StopMove (`+0x4e0`).

GoToAndFaceTarget's face target is stored through `0x2019a340`, which sets
`approachFaceEntity` and `ai+0x1380` (AI-ACTOR §6.1).

FaceEntity sets `yaw_ideal` toward the entity's physics origin, makes the
AI's own origin the goal, stores the face entity as `move.goalEntity`, sets
`move.command = MOVE_FACE_ENTITY` and clears `done`, `moving`, `blocked` and
`pathBlocked`. A null entity calls StopMove and fails. For commands 1–4 the
update calls StopMove once FacingIdeal is true. Turning while not moving
uses `maxTurnIdle` (or `maxTurnAlert`).

StopMove (`0x2019a1a0`) clears `allowAnimMove`, the approach bits,
`pureMotionExtraction`, the style and interrupt bits, the status and the
command. It sets `done`, clears `moving`, `blocked` and `pathBlocked`,
clears the goal and approach-face entities and resets the three sensors.

**IsFacing** (`0x2019afb0`) with these names: false for a null face; false
while `moving` and `approachFaceEntity` are set and `move.approachFaceEntity`
is the face; false while `move.command == MOVE_FACE_ENTITY` and
`move.goalEntity` is the face; otherwise true. ReachedAndFacing therefore
becomes true after: arrival within range → `reached` → FaceEntity → turn at
the idle rate → FacingIdeal → StopMove (status 0, so ReachedPos also
passes).

### 3.6 Path following (interface only)

Each frame with a moving command the update asks the path sensor
(`ai+0x1444`, `0x2012e6b0`, `0x2012e9f0`) for a seek position (`ai+0x1338`)
and its area (`ai+0x1344`). The route sensor at `ai+0x144c` runs
`rvAASRouteSensor::Search` (`0x20136940`) from the current area to a
candidate position, passing `move.doorOnPath` (`ai+0x1224`) and the goal
entity. When it reports no hit (sensor `+0xa4` zero, or byte `+0x23e`
clear), the candidate becomes the seek position and the path sensor's
`pathAreas.length` is decremented. This looks like a look-ahead that skips
path corners (**Inferred**). With `allowObstacleAvoid`, the sensor at
`ai+0x1448` then runs Search on the accepted seek position. Then
`0x2019bc10`/`0x2019dd30` update the direction and the approach-goal state,
and TurnToward the seek position sets `yaw_ideal`. **Unknown**: the seek
position rule (corner, `turnSmoothRadius` smoothing), the travel-flag mask,
cluster use (`scripted_noClusters`), the cost model, how path voids and
doors are handled, and how the stop clip and its turn variant are chosen.

## 4. Engine mapping

| Original contract | Reusable upstream code | New work |
|---|---|---|
| Extracted-motion channel | none: GPL `idAnimator::GetDelta`/`GetDeltaRotation` and Quake 4 `idAI::GetAnimMoveDelta` read the MD5 `origin` joint | Portable reference-frame reader and delta sampler over the existing HKXA reader |
| Animation-driven translation | Quake 4 `idAI::AnimMove` shape (delta → `physicsObj.SetDelta` → `RunPhysics` → `onGround`, `TouchTriggers`); GPL `idPhysics_Monster` | Delta / frame time × speed and axis scales; `pureMotionExtraction`; the area filter; Havok character physics is not reproduced (use `idPhysics_Monster` as a named stand-in) |
| Turning | Quake 4 `FacingIdeal` (same 0.01° snap), `TurnToward` | Constant-rate clamp with `maxTurn*` keys (Quake 4 integrates `turnVel`); anim turn from extracted yaw (Quake 4 blends two synced anims instead) |
| Commands, StopMove, FaceEntity, ReachedPos | Quake 4 `idMoveState`, `StartMove`, `StopMove`, `FaceEntity`, `ReachedPos` (`quake4/ai/AI_Move.cpp`) | Wolf enum values, flag layout, `reached` event, approach-face sequence, IsFacing semantics |
| Path following | GPL `idAASLocal` routing (already loaded by `WolfAas140V1`), Doom 3 `idAI::GetMovePos`/`idAAS::WalkPathToGoal`, Quake 4 `GetMovePos`/`CheckObstacleAvoidance` | Path and route sensors are Wolf-specific; a GPL seek rule can only be a named stand-in |
| Clip choice | GPL `idDeclModelDef::GetAnim` and the repo animation-set path | Run/walk/dash resolvers, override strings, fragment prefix |

The Quake 4 SDK files live in the pinned upstream's git objects
(`doom3/neo/quake4/ai/AI_Move.cpp`, `AI_Move.h`, `AAS_tactical.*`). The
sparse engine checkout does not contain them. Any copied or adapted Quake 4
code keeps its license notice, is marked and is recorded in
[PROVENANCE](PROVENANCE.json); it is not relabelled GPL.
