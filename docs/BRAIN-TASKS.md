# Brain tasks: single-procedure contracts

Reviewed 2026-11-09. This page records behavioral contracts for the BRAI node
types and conditions that [CAMPAIGN-RE](CAMPAIGN-RE.md) classifies as needing
one procedure read and that are first needed in **trainyard** or **mtw**. It
also records the shared property machinery that every node type uses to read
its serialized arguments. **Nothing here is implemented or observed at
runtime.** It is a contract and handler plan.

Sources: the hash-identified `Gamex86.dll` (`7cb1cbed…2d47`, private copy
only), the 633 original BRAI resources (read through the existing private
decoder) and the per-type tables of [CAMPAIGN-RE.json](CAMPAIGN-RE.json).
Disassembly came from the Android NDK `llvm-objdump` on that copy. No compiler,
game process, Ghidra import or Android action was used. Raw disassembly and
scripts stay in ignored `evidence/brain-contracts/`.

Labels follow [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md):

- **Static** — read in the disassembly and checked at the call sites (argument
  pushes, `this` register, stack cleanup). No execution was observed.
- **Inferred** — consistent with Static facts and names, not closed.
- **Unknown** — no adequate evidence.

"Established" in the status column means the Enter/Evaluate procedure, its
argument parsing and its direct engine calls are Static. It does not mean that
the engine subsystem behind a called method is understood; the "Requires"
column names that dependency.

## 1. Shared argument machinery (Static)

### 1.1 Serialized form

A node record is `type id x y n ^type flags [^cond 1024] key value …`. An
explicit output condition is `type ^type flags key value …`. `x y n` are
editor data. Arguments are optional key/value pairs; a key that is absent keeps
its default. Keys are matched by name, so their order does not matter.

The condition `flags` token is a `BRAINFLAGS` bit set. The DLL's enum table
names `ONCE_ONLY` 1, `NOT` 2, `EXECUTE` 16, `COMMAND` 32, `INTERRUPT` 64,
`ISDONECONDITION` 0x400, `TRACK_LAST_SEEN` 0x800, `INITIALIZED` 0x2000,
`STARTED` 0x8000, `STOPPED` 0x10000, `PAUSED` 0x20000, `PENDING` 0x40000,
`DONE` 0x80000 and `DISABLED` 0x100000. Every Evaluate read on this page
returns its raw result XOR the `NOT` bit (`flags >> 1 & 1`, helper
`0x201dab50`). The corpus uses condition flags `0` and `2` only.

### 1.2 Property records

Each descriptor (`+0x2c`) points to a zero-terminated table of 0x34-byte
property records: kind, name, description, default text, minimum, maximum, an
extra string, two flags, member offset, member size, an optional enum table and
a final word. Task properties start at object offset `0x4c`, condition
properties at `0x20`. The base Initialize `0x201dab20` only sets `INITIALIZED`;
it parses nothing. Text values are converted by the kind switch `0x201e36e0`
when the graph is read; the matching ToString switch is `0x201e0980`.

| Kind | Size | Stored as | Read at Enter/Evaluate |
|---|---:|---|---|
| `0x04` int | 4 | `atoi`, empty → 0 | direct |
| `0x05` float | 4 | `atof`, empty → 0 | direct |
| `0x06` native bool | 1 | `false` if empty or first byte is lowercase `f`, else `true` | direct |
| `0x08` entity reference | 0x3c | Focus/name grammar, §1.4 | resolver `0x20212980` |
| `0x0b` task reference | 0x2c | `"<entity> <index>"`, §1.5 | resolver `0x20210410` |
| `0x0e` duration | 0x28 | `sscanf "%s %d"`: value text + unit | `0x20210150` → int ms |
| `0x11`, `0x12`, `0x13`, `0x15`, `0x16`, `0x17`, `0x18`, `0x1d`, `0x1e` | 0x24 | raw text, verbatim | resolved text (§1.3), then int (`0x15`), float (`0x16`) or bool (`0x18`) |
| `0x19` vector | 0x6c | three component texts | `0x202111a0` |

Kinds `0x11` (mission name), `0x12` (global-variable text), `0x13`
(local-variable text), `0x15` (int text), `0x16` (float text), `0x17` (plain
string) and `0x18` (bool text) all keep the original text. They are converted
only when the task runs, after `@` resolution.

**Bool kind `0x18` (Static, `0x2020ff40`).** The value is resolved (§1.3) and
compared with `"true"` by `idStr::Icmp` (ASCII case-insensitive). Only a
case-insensitive `true` is true; everything else, including `"false"`,
`"fasle"`, `"0"`, `"1"` and the empty string, is false. All bool arguments of
the node types on this page are kind `0x18`, so shake_train's `mNoTarget
"fasle"` evaluates to **false**, the same as the intended `false`. Defaults use
mixed case (`"True"` for EnableDamage, `"False"` for KillTarget) and parse the
same way. The native byte kind `0x06` (first byte `f`) is not used by these
node types.

**Duration kind `0x0e` (Static, `0x2020faa0`, `0x20210150`).** The text must
contain two fields. With fewer, the value becomes `"0"` and the unit 0. A unit
of 2 or more is reset to 0. At use, the value is resolved (§1.3), converted by
`atof`, multiplied by 1000 when the unit is 1, then rounded to the nearest
integer (float32) and truncated. The `TimeUnits` enum names unit 0
`Milliseconds` and 1 `Seconds`. **This closes the Wait unit question for
duration properties: `"2 1"` is 2000 ms.** Wait itself embeds a Timer
condition whose `mDuration` is this kind.

### 1.3 `@` value resolution (Static, `0x2020fb60`)

Text that does not start with `@` is used unchanged. For `@name`:

1. `@time` (case-insensitive) → the current integer game time in ms, formatted
   `%d` (the same clock member Tick reads).
2. `@random` → `0…99` from the game's LCG.
3. The owning network's local-variable dictionary (`Network+0x8ac`): if the key
   `name` exists, its value text, or `"0"` when the entry has no value.
4. Otherwise the dictionary returned by virtual `+0x24` of the network's
   `+0x20` object (the network owner context): its value for `name`.
5. Otherwise the shared game dictionary that SetGlobalVariable writes: if
   `name` exists, its value as `%d`.
6. Otherwise the original `@name` text.

The network is found by walking the task's parent chain (`+0x8`) to the top
and dynamic-casting it to `rvBrain::Network` (`0x201daa30`, cached at `+0xc`).

### 1.4 Entity references (Static)

**Parse (`0x2020f920`).** The `Focus` enum gives the keywords: `None` 0, `Me`
1, `MyEnemy` 2, `MyLeader` 3, `MyPartner` 4, `Player` 5, `Friend`, `Enemy`,
`Actor`, `Usable`, `Waypoint`, `Weapon`, `Mover`, `Door`, `Light`, `Trigger`,
`Tgt`, `Tgt_Music`, `Tgt_Remove`, `Tgt_Use`, `Vehicle`, `Sound`, `Entity` (6–22),
`MyTarget` 23, `MyVehicle` 24 and `MyTgtOccupier` 25. A `FocusRequirement`
table marks 23 and 25 as taking an index.

- Empty text sets mode `None`.
- A case-insensitive exact keyword sets the mode and keeps the name empty.
- A case-insensitive keyword **prefix** with an index requirement, followed by
  text whose `atoi` is non-zero, sets that mode, the index and the remaining
  text as name (`"MyTarget 1"` → mode 23, index 1).
- Anything else is stored as an entity name; the mode stays at its default.

Defaults go through the same parser. The constructor leaves mode `Me`.

**Resolve (`0x20212980`, five stack arguments).** Every Enter/Evaluate on this
page calls it as `Get(task, 0, 0, 0, 0)` with `ecx` = the property: no actor,
mode, index or name override. The actor is the network owner (`Network+0x20`)
cast to `idAI`, or the raw owner when the cast fails.

1. A cached handle (`+0x2c`, spawn id << 13 | entity number) is returned while
   it is still live, unless the stored name starts with `@`.
2. `MyTarget N` / `MyTgtOccupier N` (N > 0): entry `N-1` of the actor's target
   list (`+0xc4` count, `+0xd0` handles). A dead handle gives null. For
   `MyTgtOccupier` the target's virtual `+0x268` result is returned.
3. A non-empty name: `@` text is resolved (§1.3) and looked up without caching;
   a plain name is looked up through the game's entity hash (case-sensitive
   hash, case-insensitive compare) and the handle is cached.
4. Empty name: `None` → null; `Me` → the actor; `Player` → the local player;
   `MyEnemy` → actor virtual `+0x384`; `MyVehicle` → the actor's `+0x76c`
   helper. Other modes → null.

Bind (`0x2020ff70`, reached through task Bind `0x201f09e0`, `0x201f2d60`,
`0x201fc0d0` and condition Bind `0x201e8b90`, `0x201ea3b0`) only clears the
cached handle. A removed entity is never returned: the handle check fails and
named references are looked up again. **Every task on this page does nothing
when its entity reference resolves to null.** No warning is printed except
where noted.

### 1.5 Task references (Static, `0x202104c0`, `0x20210410`)

Text `"<entity> <index>"`: the part before the first space is the entity name,
`%d` after it is the index; without a space the whole text is the name and the
index is 0. `" 11"` means "own network, index 11". At use, a cached task
pointer is returned if set. Index 0 gives null. If the name finds an `idAI`
with a network (`+0x9f4`), the index selects from that network's task list
(`+0x82c`); otherwise the **task's own network** is used, also when the name
names a non-AI entity. An index outside the list gives null. The index is used
directly as the list index. **Inferred:** the list index equals the serialized
node ID; the loader's placement was not re-read.

### 1.6 Event dispatch (Static)

`0x202b0880`/`0x202b08e0` are synchronous `ProcessEvent` calls with 0 or 3
arguments. `0x202b0820` checks the receiver's class event table and silently
returns false when the class has no handler. Event names come from the static
`idEventDef` initializers: `hide`, `show`, `stopMoving`, `stopRotating`,
`sway (ffv)`, `enableCreditsScreen`, `disableCreditsScreen`, `hideTip` and
`tutorialTextShowHide (sssd)`.

## 2. Trainyard, in usage order

Order follows [TRAINYARD-CHAIN](TRAINYARD-CHAIN.md): shake_train, the
interrogator and greeter chains, movetrain, then allies_contact_1,
trainyard_music, train_mover and dummy_secret. "Actor" in the tables means the
network owner of §1.4. Every procedure listed returns nothing and has no Exit;
the network completes it in the same Tick and follows its outputs.

| # | ID | Node type | Arguments (kind, default) | Enter behavior | Requires | Status |
|---:|---:|---|---|---|---|---|
| 1 | 357 | EnablePlayerWeapons | `mEnabled` (bool, `true`) | Local player; sets bit 3 of player byte `+0x958` to **not** `mEnabled`. No player: no-op. | Player weapon lock; GPL `idPlayer` enable/disable weapon | established |
| 2 | 102 | NoTargetSet | `mNoTarget` (bool, `true`), `mActor` (entity, `Me`) | Sets bit 0 of entity byte `+0x158` to `mNoTarget`. Any entity class. | Entity no-target flag read by AI acquisition; GPL `fl.notarget` | established |
| 3 | 468 | EnableCredits | `mEnable` (bool, `true`) | Local player `ProcessEvent(enableCreditsScreen)` if true, else `disableCreditsScreen`. | New: Wolf credits UI on the player | established |
| 4 | 141 | MoverSway | `mMover` (entity, `Mover`), `mSpeed` (float text, `50`), `mPhase` (float text, `1`), `mAngles` (vector, `0 0 0`) | `ProcessEvent(sway, speed, phase, angles)` on the mover. | New: Wolf mover `sway` event | established |
| 5 | 130 | StopMover | `mMover` (entity, `Mover`), `mStopTranslation` (bool, `true`), `mStopRotation` (bool, `true`) | `ProcessEvent(stopMoving)` then `ProcessEvent(stopRotating)`, each only when its bool is true. | GPL `idMover` `stopMoving`/`stopRotating` | established |
| 6 | 75 | SetUsable | `mEntity` (entity, `Usable`), `mUsable` (bool, `true`) | Sets bit 7 of entity byte `+0x159` to `mUsable`. | Wolf player-use eligibility flag; consumer not traced | established |
| 7 | 252 | EnableDamage | `mEntity` (entity, `Me`), `mEnable` (bool, `True`) | Sets bit 3 of entity byte `+0x158` to `mEnable`. | Damage gate flag; by name take-damage (Inferred); consumer not traced | established |
| 8 | 306 | ShowTip | `mTitle` (string, `Brain Tip`), `mTip` (string, `Tip Text`), `mAutoHide` (bool, `true`) | Stores resolved `mTip` text, `mAutoHide`, two zero floats and a zero byte in the tip slot of the HUD/mission singleton `0x20876490` (`+0x3e8d8…`). **`mTitle` is not read.** | Tip display state; GPL `idPlayer::ShowTip` is similar but takes a title | established |
| 9 | 307 | HideTip | none | Local player: posts `hideTip` through the event queue (`0x202b0350`, zero delay). | GPL `hideTip` player event | established |
| 10 | 154 | Show | `mSubject` (entity, `Me`), `mHide` (bool, `false`) | `ProcessEvent(hide)` if `mHide`, else `ProcessEvent(show)`. | GPL `idEntity` `hide`/`show`; `idAI`/actor overrides not read | established |
| 11 | 456 | ExecConsoleCommand | `mCommand` (string, empty) | `cmdSystem->BufferCommandText(2, va("%s\n", resolved mCommand))` (vtable `+0x20`, append). | GPL `idCmdSystem`; the commands themselves (`CheckOneOfMany`, `toggleshroud`, `ScaleformToggleHUD`, `ScaleformPlayVideo …`, `contentManagerCommand save full`, `g_infiniteshroudenergy 1`) are Wolf commands | established |
| 12 | 490 | ScriptedVOStreamOut | `mOwner` (entity, `Me`), `mDefName` (string, empty) | Missing owner: game warning "Can't find ent …" and return. Non-empty `mDefName`: VO manager `0x2078cf60` stream-out (`0x201976c0`) with (def, owner). Empty: if the owner is `idAI`, uses its `def_scripted_voice` spawn arg (empty if absent); otherwise warning "Invalid owner/def combination". | Release side of the VO bank gate used by ScriptedVOStreamIn | established |
| 13 | 100 | ReachedLocation (condition) | `mLocation` (entity, `Tgt`), `mActor` (entity, `Me`) | Missing actor → raw **true**. Otherwise raw = actor `IsInLocation(location)` (`0x200421e0`): false unless the actor has a current location record (`+0x154`) and the location is an `aasLocationEntity`; the record membership test is `0x201394c0`. | AAS location tracking per actor (subsystem) | established (Evaluate); dependency subsystem |
| 14 | 448 | PlayerHasWeapon (condition) | `mWeaponName` (string, `weapon_plasma_cannon`) | No player → raw false. Otherwise raw = player inventory (`+0x53c`) virtual `+0x90` with the resolved name. | Player inventory query; GPL inventory has an equivalent lookup | established |
| 15 | 188 | EndBranch | none | Sets network byte `+0x8a5`. At the end of that Tick the network clears it and its active task (`+0x828`), then either pops the stack entry at `+0x874` (`0x201dc9e0`) or, with an empty stack, compacts and unbinds every bound actor (`+0x904` list, `0x201deb60`). Reset clears the byte. | Network end-of-branch; actor unbind is actor subsystem | established (Enter, Tick handling); unbind Inferred |
| 16 | 289 | EnableRootTask | `mRootTask` (task ref), `mEnable` (bool, `true`) | Resolves the task (§1.5). For each of its outputs, clears `DISABLED` (0x100000) when enabling, sets it when disabling. The task itself is not entered, exited or reset. Null task: no-op. | BrainFlow already honors `DISABLED` | established |
| 17 | 325 | AddDynamiteIndicator | `mEntity` (entity, `Me`), `mRadius` (float text, `300.0`) | Writes float spawn arg `GrenadeIndicatorRadius` = radius on the entity, then registers the entity with manager `0x2078cf60` (`0x2018e380`). | New: Wolf grenade/dynamite HUD indicator | established |
| 18 | 316 | HidePressButton | none | Local player: cancels its pending `tutorialTextShowHide` events, then sends the HUD message `HideKeyPress` (type 12) to the Scaleform manager `0x20875e70`. | New: Wolf tutorial/HUD messages | established |
| 19 | 446 | AnyNazisAlert (condition) | none | Raw = global alerted-AI counter `0x2078f578` > 0. | AI alert counter maintained by AI awareness (subsystem) | established (Evaluate); producer unknown |
| 20 | 94 | UnBind | `mSubject` (entity, `Me`) | `idEntity::Unbind` (`0x20040e30`, non-virtual) on the subject. | GPL `idEntity::Unbind` | established |
| 21 | 166 | ApplyImpulse | `mEntity` (entity, `Me`), `mPoint` (vector, `0 0 0`), `mImpulse` (vector, `0 0 0`), `mRelative` (bool, `false`) | When `mRelative`, adds the entity physics origin (`+0x2ec` virtual `+0x98`, id 0) to `mPoint`. Calls entity virtual `+0x198(0, 0, point, impulse)`. | GPL `idEntity::ApplyImpulse(ent, id, point, impulse)` shape; slot mapping Inferred | established |
| 22 | 373 | MissionIsObjectiveActive (condition) | `mMission` (mission text), `mObjectiveNumber` (int text, `1`) | Finds the mission by resolved name in singleton `0x20876490` (`0x203c2100`); requires it active (`0x203bd210`); gets objective `number-1` (`0x203bc7c0`); raw = objective state word `+0x9c` == 1. Any failure → raw false. | New: `rvMission` objective state | established (Evaluate); state values Unknown |

Notes:

- **SetUsable** applies to any entity class; the greeter uses `mEntity=Me`.
- **Show** on an `idAI` dispatches the same `show` event; whether `idAI`
  overrides it (unhide plus activation) was not read.
- **ExecConsoleCommand** is deferred to the command buffer, so its effect
  happens after the current Tick, not inside it.
- **EndBranch**: the byte `+0x8a5` is the flag the probe documentation called
  "raised by firing a per-task byte"; it is EndBranch's. The stack branch at
  `+0x874` belongs to the unused stack mode and stays unsupported.
- **ReachedLocation** returns true when its actor is missing. In the corpus,
  `mActor` is `Player` in 35 of 44 uses.
- **MissionIsObjectiveActive** appears only in train_mover node 34, twice,
  once for `mission_trainyard` and once for `mission_demo_trainyard`.

### 2.1 Suggested acceptance tests (trainyard)

Each test runs through the ordinary native lifecycle (spawn, BRAI bind, Think
→ Tick, teardown) with an explicit source profile. Synthetic graphs are fine
for portable checks; native checks need real entities.

| ID | Test |
|---:|---|
| 357 | shake_train S1 node 31 → player weapon fire refused; a later `mEnabled true` re-enables. No player: no crash, flow continues. |
| 102 | Node with `"fasle"`, `"FALSE"`, `"true"`, `"TRUE"` and an absent key: flag values false, false, true, true, true (default). |
| 468 | Refusal receipt until a credits UI exists; the event name and bool recorded. |
| 141/130 | Mover receives exactly one `sway(1.0, 1.0, (-0.08,-0.02,0))`; StopMover with both bools stops both motions; with `mStopRotation false` only translation stops. |
| 75 | Greeter probe 61 makes the greeter non-usable: a player Use does not reach its network. |
| 252 | `Player` EnableDamage false → native damage ignored; true restores it. |
| 306/307 | Tip text `#str_007236` shown, title ignored, `mAutoHide false` keeps it; HideTip removes it on the next event service, not synchronously. |
| 154 | X5 Show of hidden `nazi_interragator_3/4`: entities become visible exactly once; missing subject → no-op and the chain continues. |
| 456 | `CheckOneOfMany` appended to the command buffer and executed after the Tick; unknown Wolf commands stay explicit refusals. |
| 490 | Stream-out releases the bank that ScriptedVOStreamIn admitted; missing owner logs the warning and does not block the chain. |
| 100 | Player inside/outside `trap_door_room`; absent actor yields true; `NOT` inverts. |
| 448 | allies 293/294 false before and true after picking up `weapon_mp40`. |
| 188 | After EndBranch the network has no active task at Tick end and its bound actors return to base AI; reset clears the flag. |
| 289 | Disabling root `allies_kreisau_contact_1 98` stops its outputs from firing; re-enable restores them; a non-AI name resolves in the own network. |
| 325 | `train_dynamite` gets `GrenadeIndicatorRadius 350` and appears in the indicator manager. |
| 316 | Pending tutorial text events cancelled; `HideKeyPress` sent once. |
| 446 | Counter 0 → false; one alerted AI → true. |
| 94/166 | train_mover boxcars unbound, then impulses applied at the given points; relative mode offsets by origin. |
| 373 | Objective 7 active/inactive/complete states map to true/false/false. |

## 3. Variable expressions (Static)

SetLocalVariable, CheckLocalVariable, CheckGlobalVariable and the runtime-tested
SetGlobalVariable share one evaluator in the variable manager singleton
`0x20694194`. Its first member is the shared game dictionary.

- **Check** (`0x200db030`, `ecx` = manager; arguments: text, dictionary,
  task). A null dictionary means the shared game dictionary. The engine lexer
  (flags `0x80c`) tokenizes the text, which is evaluated at level 4. The result
  is `value != 0`. Tokens after the expression are ignored.
- **Assign** (`0x200db0f0`; arguments: variable name, text, dictionary, task).
  The same evaluation, then `SetInt(name, value)` on the dictionary.

Grammar, from `0x200dbf30` (levels), `0x200dc2f0` (apply) and `0x200db880`
(primary):

| Level | Operators | Notes |
|---:|---|---|
| 4 | `&&`, `\|\|` | both operands are always evaluated |
| 3 | `>`, `>=`, `<`, `<=`, `==`, `!=` | result 0/1 |
| 2 | `+`, `-` | int32 |
| 1 | `*`, `/`, `%` | `/` by zero: warning "Divide by zero in variable expression", result = left; `%` by zero: result = left, no warning |
| 0 | `( expr )`, unary `-`, unary `!`, number, `@name`, identifier | |

Each level parses its left operand one level below and reads **one** token. If
the token is an operator of its own level, the level parses the right operand at
the **same** level. Operators are therefore **right-associative** within a
level: `a - b - c` is `a - (b - c)`, and `x == 1 && y == 0 || z` is
`(x == 1) && ((y == 0) || z)`. A token that is not an operator of the level is
put back.

Primary terms:

- A number token gives its integer value.
- An identifier is read with `GetInt` from the given dictionary. A missing key
  gives 0.
- `@name` is resolved by §1.3 with the task. A result that starts with a digit
  is converted by `atoi`. Otherwise the result text is looked up in the
  **shared** game dictionary (missing → 0), so a negative `@` value is looked up
  as a key, not converted.
- `!x` is `x == 0`, and `-x` negates.

Before the evaluator runs, the whole property text goes through §1.3 once.
It is replaced only when the entire text is a single `@name` (for example
`@cn_is_extended`). Texts such as `@time + 6000` pass unchanged and are
resolved term by term.

## 4. Midtown West

Midtown West is a hub without a single scripted chain. The order below is the
order of first occurrence in the map's brain list, which approximates play
order (Inferred). It starts with the black-market collector and the tutorial
guide, followed by the mission givers, then ambient and conversation graphs.

| # | ID | Node type | Arguments (kind, default) | Enter / Evaluate behavior | Requires | Status |
|---:|---:|---|---|---|---|---|
| 1 | 71 | ActionKeyed (condition) | none | Raw = network byte `+0x827`. The `idAI` action-key handler (`0x201ac930`) sets it on the AI's network and its secondary networks; Reset clears it. Usually the primary condition of OnActionKeyed. | Player action key routed to the AI (subsystem) | established (Evaluate); producer partly read |
| 2 | 290 | EnableAllRootTasks | `mEnable` (bool, `true`) | For each root task of the own network (`+0x844` list), clears or sets `DISABLED` on all of its outputs, like EnableRootTask. | BrainFlow `DISABLED` | established |
| 3 | 293 | OpenPurchaseLocker | `mAmmoOnly` (bool, `false`); `mGarbage1` (enum, `Kreisau`), `mGarbage2`, `mGarbage3` unused | Local player: posts `openPurchaseLocker(int ammoOnly)` with zero delay. | New: Wolf purchase/locker UI | established |
| 4 | 467 | IsMenuOpen (condition) | none | Raw = local player `0x2031a950()`; no player → false. | Player menu state | established (Evaluate); query body not read |
| 5 | 216 | MissionObjectiveSuccess | `mMission` (mission text), `mObjectiveNumber` (int text, `1`), `mSilent` (bool, `false`) | Finds the mission (`0x203c2100`). If it exists, calls `0x203cc450(number-1, silent)`; otherwise no-op. | New: `rvMission` objectives | established (Enter); manager Unknown |
| 6 | 221 | CheckGlobalVariable (condition) | `mExpression` (global text) | Check(expression, shared dictionary, task), §3. | §3 + shared dictionary | established |
| 7 | 469 | StartJournalTutorial | none | Local player `ProcessEvent(startJournalTutorial)`. | New: Wolf journal UI | established |
| 8 | 196 | CheckLocalVariable (condition) | `mExpression` (local text) | Check(expression, network local dictionary `+0x8ac`, task). No network → raw false. | §3 | established |
| 9 | 195 | SetLocalVariable | `mVar` (local text), `mExpression` (local text) | Assign(resolved `mVar`, resolved `mExpression`, network local dictionary, task). No network → no-op. | §3 | established |
| 10 | 185 | LockPlayer | none | Sets bit 4 (`0x10`) of local player byte `+0x957`. | New: Wolf player lock; consumer not traced | established |
| 11 | 186 | UnlockPlayer | none | Clears that bit. | as LockPlayer | established |
| 12 | 200 | MissionGive | `mMission` (mission text), `saveOnMissionGive` (bool, `true`) | Stores `saveOnMissionGive` in global byte `0x20878580`, then calls `0x203c9f50(resolved name, 0, 1)` on the mission singleton `0x20876490`. | New: `rvMission` give/save | established (Enter); manager Unknown |
| 13 | 32 | Print | `printText` (string, `Debug text`) | Prints `"%s\n"` with the resolved text to the game console. | Console print | established |
| 14 | 41 | IsDead (condition) | `mEntity` (entity, `Me`) | Missing entity → raw **true**. Otherwise raw = entity health float `+0xd8` ≤ 0. | Entity health | established |
| 15 | 175 | SetPhysicsOwner | `mSubject` (entity, `Me`), `mOwner` (entity, `Entity`) | Subject `ProcessEvent(setOwner, owner)`; a null owner is passed as null. | GPL `idEntity` `setOwner` | established |
| 16 | 464 | UseAllTargets | `mActor` (entity, `Me`), `mUseKey` (string) | Resolves the actor, falling back to the network owner when null. Calls virtual Use `+0x24c(actor, resolved key)` on each live entry of the actor's target list (`+0xc4`/`+0xd0`), in order; skips stale entries. | Use routing as UseTarget (receivers: TRAINYARD-CHAIN §3.2) | established |
| 17 | 444 | HasPlayerFiredWeapon (condition) | none | Gets the current weapon from player inventory (`+0x53c`) virtual `+0xd0`. Raw = its `+0xb0` → `+0x20` time equals one of two game time words (`0x20682cc4`, `0x20682cc8`). Without a player, inventory or weapon it returns false **without** applying `NOT`. | Player weapon fire time | established (Evaluate); time words Inferred as current/previous frame |
| 18 | 433 | MovedRecently (condition) | `mEntity` (entity, `Me`), `mElapsed` (duration, `1000`) | Needs the entity's tracking record `+0x154`, the same one ReachedLocation uses. Raw = `0x20138da0(record, elapsed ms)`. Missing entity or record → raw false. | Entity tracking record (subsystem) | established (Evaluate); record Unknown |
| 19 | 62 | TaskDone (condition) | `mTask` (task ref) | Raw true when the task resolves, has `STARTED` (0x8000) and is done (`0x201daba0`). Done means `DONE` (0x80000) is already set, or the task carries `COMMAND` (0x20, the probe-entry flag) and its primary condition is absent or true; in that case `DONE` is set. | Probe pass state | established |
| 20 | 298 | ArgSet (condition) | `mTarget` (entity, `Me`), `mArgName` (string), `mCheckValue` (string) | Missing target → raw **true**. Looks up the resolved name in the target's spawn args (virtual `+0x24` returns `+0x6c`). With a non-empty check value, raw = the key exists and its value equals the check value (case-sensitive). Otherwise raw = the key exists, is non-empty, and is not `"0"` or `"0.0"`. | Spawn-arg dictionary | established |
| 21 | 495 | StopAllSpeakingAI | `mFadeOutTime` (duration, `500`) | Calls VO manager `0x2078cf60` `0x2018e980(fade ms)`. | Speech subsystem stop-all | established (Enter); manager Unknown |

Notes:

- **ArgSet** with `mTarget=Light` (two uses) resolves to null, because `Light`
  is a keyword without a name. These uses therefore return raw true.
- **TaskDone** is useful only for tasks entered by the probe pass; a normally
  entered task never carries `COMMAND`.
- **IsDead** and **ArgSet** treat a missing entity as satisfied, which matters
  after RemoveTarget.

### 4.1 Suggested acceptance tests (mtw)

| ID | Test |
|---:|---|
| 71 | An action key on the black-market collector latches once; reset clears it. |
| 290 | Disabling all roots blocks OnUsed/OnActionKeyed outputs until they are re-enabled. |
| 293/469 | Explicit refusal receipts until the Wolf UI exists; the event and argument are recorded. |
| 216/200 | Objective number `n` becomes index `n-1`; the `saveOnMissionGive` latch is set before the give call. |
| 221/196/195 | Table tests of §3 on original texts: `(mission_trainyard_complete == 1) && (mission_filler_mt_bm_1_complete == 0)`, `finishTime < @time`, `@time + @mod_duration`, `!disable`, `random + 1`, right-associative `10 - 3 - 2` = 9, divide by zero. |
| 185/186 | Player movement is refused between Lock and Unlock. |
| 41 | Removed entity → true; health 0 → true; `NOT` inverts. |
| 175 | `@chair` is resolved from the local dictionary and set as physics owner. |
| 464 | All targets of the actor receive the key in target-list order; a removed target is skipped. |
| 444 | True only on the frame of a shot; the inverted form is false when the player is missing. |
| 433 | A moving actor is true within `mElapsed` and false after standing still. |
| 62 | A probe-entered task with a true primary → true and `DONE` set; a normally entered task → false. |
| 298 | `idle_loopSpeakAnims` absent, `0`, `0.0`, `1`, and with `mCheckValue 2`. |
| 495 | All speaking AI fade out over 500 ms. |

## 5. Read, but subsystem work remains

These procedures were read because they are short. Their behavior lives in
actor or manager code that is not recovered, so their CAMPAIGN-RE status is
unchanged.

| ID | Node type | What the Enter does (Static) | Missing |
|---:|---|---|---|
| 253 | SetUndying | Casts `mActor` (default `Me`) to `idActor` and writes byte `+0x5f9` = `mEnable` (default `true`). The byte is written again when the actor is an `idPlayer`. Non-actors: no-op. | The damage path that reads `+0x5f9` |
| 69 | KillTarget | `idAI` target: `0x20184e50(mBypassDeathAnim)`. Other entities: virtual `+0x218` with a damage record whose inflictor and attacker are the network owner, and 0.0. Null: no-op. | AI death and death-override behavior |
| 49 | IgnoreEnemiesSet | Casts `mActor` to `idAI`. When `mIgnore` (default `true`) is set, first calls `0x201a4680`; then sets bit 2 of byte `+0x1c84` = `mIgnore`. | AI enemy acquisition |
| 73 | UnbindBrain | `idAI` owner: virtual `+0x45c`, `+0x460(1)`, `+0x450(network)`. `rvTarget_ScriptRunner` or `idMover` owner: virtual `+0x2f8`. Either way it then posts `unbindBrain` on the owner. Other owners: no-op. | AI/scripted-owner brain binding |

CAMPAIGN-RE listed UnbindBrain as one-procedure work. It needs the `idAI`
brain-binding subsystem.

## 6. Totals

Of the 44 trainyard/mtw items classified as one procedure read (31 node types,
13 conditions):

| Status | Node types | Conditions |
|---|---:|---:|
| established, no remaining dependency | 26 | 6 |
| established, with a named Unknown dependency | 4 | 7 |
| moved to subsystem RE | 1 (UnbindBrain) | 0 |

The items with a dependency are EndBranch (actor unbind), MissionGive,
MissionObjectiveSuccess, StopAllSpeakingAI, ReachedLocation, AnyNazisAlert,
MissionIsObjectiveActive, ActionKeyed, IsMenuOpen, HasPlayerFiredWeapon and
MovedRecently. In each, the Enter or Evaluate is Static but calls a manager,
record or producer whose behavior is Unknown. SetUndying, KillTarget and
IgnoreEnemiesSet (§5) were read for context and stay subsystem RE.

[CAMPAIGN-RE.json](CAMPAIGN-RE.json) records the established items with status
`contract-established-not-implemented`. Its `contract` field is `static`, or
`static-partial` where a dependency remains. No item is implemented.

## 7. Open questions

- Consumers of the flag bits: entity `+0x158` bit 3 (EnableDamage), `+0x159`
  bit 7 (SetUsable), player `+0x957` bit 4 (LockPlayer) and `+0x958` bit 3
  (EnablePlayerWeapons).
- How `idAI` handles `show`/`hide` (activation, unhide), and its Killed path.
- The `rvMission` singleton `0x20876490`: give, objective success, the
  objective state values (`+0x9c`) and the consumer of the tip slot.
- The per-entity tracking record `+0x154` (location membership, recent
  movement) and how it is updated.
- The producer of the alerted-AI counter `0x2078f578`.
- Whether the task list index equals the serialized node ID (task references).
- `CheckOneOfMany`, `toggleshroud` and the other Wolf console commands.
- The meaning of lexer flags `0x80c` in this engine build.

## Reproduce

Read-only inputs: the installed `SP/base/maps/**/*.mpk` and a private copy of
`Gamex86.dll` with the SHA-256 above. Private scripts in ignored
`evidence/brain-contracts/scripts/` do the following:

- `props.mjs` dumps the property records of each descriptor.
- `args.mjs` builds argument value histograms over all 633 graphs.
- `usage.mjs` lists per-map usage.
- `dis.mjs` and `fn.mjs` disassemble single procedures.
- `strref.mjs`, `dataref.mjs` and `evdef.mjs` find string, data and
  event-definition references.
- `td.mjs` resolves RTTI names.

They reuse the existing `evidence/campaign` and `evidence/trainyard-chain`
decoders and the full disassembly produced with the Android NDK
`llvm-objdump`.
