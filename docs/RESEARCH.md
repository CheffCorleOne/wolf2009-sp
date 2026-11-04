# Research

The original PC game uses game API **19**; the pinned Doom3 engine uses API
**8**. Loading the archives cannot bridge that interface or supply Wolf2009's
single-player behavior. The port keeps the native engine and reconstructs
supported behavior from original data and bounded code/runtime evidence.

These notes summarize verified findings through 2026-11-04. Current native
coverage is recorded in [CHECKPOINT](CHECKPOINT.md).

## Reference build and provenance

The local reference is Wolfenstein (2009). An isolated PC copy loaded
`trainyard` and `empty`. Both `SP/Wolf2.exe` and `SP/Gamex86.dll` report PE
VersionInfo `0, 91, 25, 7022`; that field does not identify a retail patch.
`Gamex86.dll` is PE32/x86 and exports only `GetGameAPI`, at `0x2006da50`.
Its API-version check is the source of the value 19 above.

Original SHA-256 identities:

| File | SHA-256 |
|---|---|
| `Wolf2.exe` | `e8604f6450d183254ccf4af7e59bcf7a2aea0332f69739955854d95f3b9140c9` |
| `Gamex86.dll` | `7cb1cbedec4b24e612fddf815ce96bbad3f79dff6160767bd778d77fc9382d47` |

The engine base is [glKarin/idTech4A++](https://github.com/glKarin/com.n0n3m4.diii4a),
pinned at `ce49510f3b0c04969eab8ebe8238416e84e4f0bb`. Reproducible patches,
dependency revisions and licenses are listed in [PROVENANCE.json](PROVENANCE.json).
Quake4 and Prey SDK game code has separate license restrictions and is not a
source of GPL Wolf2009 gameplay code here. Extracted assets, original binaries
and detailed local evidence remain outside public Git.

## What the files establish

The reference inventory contains 1,491 files totaling 6,470,427,097 bytes.
Single-player data includes 27 MPK, 1,407 SPK, two PK4 and 30 Bink videos.
Multiplayer is outside the port's scope.

| Data | Verified facts |
|---|---|
| MPK/SPK | All 1,434 containers validated: version 300, reversed FourCC tags and zlib chunks. |
| SP PK4 | Both archives have footer signature `02 0a 53 48`; ZIP directories and all entry CRCs were checked. The engine reader needs the footer adaptation. |
| Geometry | `MD5RVersion 1`, `MD5RProcVersion 7` and PBBF. PROC/PBBF geometry was validated across all 27 MPK maps. |
| Entities | `ENTS Version 4`, with source names, classes and placement. |
| Graphics | DDS textures; 663 vertex and 381 pixel shaders with SM2.0 tokens. The executable imports D3D9/D3DX9. These 1,044 shader files are not ready-made GLES programs. |
| Other resources | Havok packfile magic, `DewmAAS 1.40`, SGFX and BRAI families. Their signatures identify further work; they do not establish supported behavior. |

Format coverage and refusals are documented in [VALIDATION](VALIDATION.md),
[GEOMETRY](GEOMETRY.md) and [ENGINE](ENGINE.md).

## Names do not uniquely identify content

In three inspected trainyard packs, all 291 detail/character resource names
also occur in the main MPK, but 290 differ in content. One station support
has 200 rendered vertices in the main model and 523 in the detailed model.
The VFS therefore accepts an explicit pack, type and name and reports ambiguous
lookup. Automatic retail selection rules still need evidence for each profile.

Compiled declarations have their own framing and typed values. The global
catalog is the exact `maps/game/global/global.decls` member of `assets.pk4`,
rather than a DECL chunk in the global SPK. The measured English startup loads
four explicit global/map and main/locale sources with section-order and
protection rules. Tags 0–4 distinguish strings, pooled float/int and inline
float bits/int; a float word cannot be treated as a boolean or decimal literal.
See [DECLARATIONS](DECLARATIONS.md).

## Contracts recovered from gameplay data and code

The trainyard relay keeps three separate facts: incoming `train_Stop`, its own
authored `useKey=train_stop`, and outgoing `train_stop`. The selected
`rvTarget_BRAINUse` path reads its own dictionary value and forwards that key
with the actor argument to a live target. RTTI, event registration and bounded
handler/call-site assembly establish this contract. Ghidra's inferred types
and argument lists are hypotheses to check against those instructions. Global
case folding would conceal the actual routing rule.

The greeter model declares `empty_male_skel.hkx`. This rig and
`humanoid_male.hkx` have the same 52 joint names and parents, but different
reference poses. Matching topology does not justify a rig alias.
The model's bind transforms come from MODL;
the declared mesh rig and explicitly selected full local animation tracks
retain their separate identities.

Other recovered distinctions matter too. `rvAnimatedContainer` and
`rvSearchable` are different `idAnimated` subclasses: playing a locker's open
clip does not implement its search, item, AF, highlight or sound behavior.
A modelDef `.md5anim` reference resolves through `animBlendTree`; an authored
tree takes precedence over an implicit default. Default `SingleKeyframe` with
`Cycles1` is distinct from a node that holds one source frame. The bounded
native animation controller is described in [SETS](../src/idtech4a_anim/SETS.md).

## Type19 waits for a whole voice bank

With `vo_lang=english`, the recovered request for `scripted_voice_kc1_trn`
expands to `streampacks/scripted_voice_kc1_trn_vo_english.spk`. This exact
385,486-byte pack contains one SNDS chunk with **19 unique members** and no
other chunk types. Full tables, zlib consumption, payload ranges and EOF were
checked. Its SHA-256 is
`73fdcb4f2be911f4687bf50afc528f39f1e39beeec71a16ee1c4811e779ffc9c`.
The source definition's 19 lipSync mappings match those 19 sample names.

On the measured ordinary category-8 path, typed entries are processed before
loader status 3 leads to slot state 7. The imported audio registration service's
exact ABI, failure contract and buffer ownership remain unresolved. Reading
the archive or preparing one SNDS sample cannot stand for that whole-bank gate.
The MPEG frame audit found mono/44,100-Hz payloads; it did not perform native
PCM decoding or cache registration. The portable bank plan and separate strict
decode-receipt work preserve that boundary. Bank ownership and actual type19
completion remain the next steps in [NEXT-STEPS](NEXT-STEPS.md).
