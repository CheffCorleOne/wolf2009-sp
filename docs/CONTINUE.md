# Continue development

Read [CHECKPOINT](CHECKPOINT.md), [DEVELOPMENT](DEVELOPMENT.md),
[NEXT-STEPS](NEXT-STEPS.md) and [AGENTS.md](../AGENTS.md) before changing code.
The target is a playable SP campaign. No mission is complete.

## Start from the validated source

`main` contains the current reviewed source. APK `v0.11.0-engine-dev` uses
source `78cf2c9`: prepared trainyard now has explicit MP40 grant, held fire
and reload buttons. Actual-APK ammo/reload, release, Home/return and fresh
process checks passed on Android 15 x86_64. It still has no playable mission.
Core actor/door checkpoint: `580959c`. Actor/arrival run `86c9d5c22a98` and
door run `eb5902bfd353` are separate component results; consult the checkpoint.

The original installation is read-only. Native tests use a unique
`/data/local/tmp/wolf2009-runtime-<nonce>` directory on the selected device.
Do not reinstall or reset the user's app for each core test. Private logs,
source extracts, toolchains and binary hashes belong in ignored `evidence/`
and `build/`; public validation summaries contain no personal paths.

## Next blocking behavior

Implement whole-bank native SNDS registration before resuming type19
`ScriptedVOStreamIn`. The original waits for a category-8 bank, not a single
sample. The measured English bank has 19 members. Reading them, finding a
cached sound or waiting a fixed interval cannot prove readiness.

Two separate branches provide reviewed starting points:

| Branch | Starting commit | Scope still to validate |
|---|---|---|
| `work/audio-bank-20261104` | `be6b8c4` | Owning, bounded SNDS data plan; native decode/registration absent |
| `work/audio-receipt-20261104` | `950c8d1` | Strict full-decode/cache-identity receipt; native cache runtime pending |

Bring these branches up to date with `main` and read their test READMEs before
integration. The receipt is not a lifetime
lease, a Ready flag or playback. Its caller must retain stable cache data and
obey the decoder's thread/lock contract. Preserve old codec behavior with the
new opt-in disabled. Keep the two-compiler-job team limit.

After that gate, connect actual readiness to the greeter's original OnUsed
route, actor-issued start acknowledgement and native door endpoint. Separate
actor/door component tests do not prove that chain.

## Evidence to retain

Local relay-key Ghidra evidence checks bounded disassembly against original
bytes and LLVM call sites. Decompiled argument names were misleading; use the
verified stack/register contract. The relay forwards its own key.

Local scripted-VO notes retain the bank-manager/slot control flow and exact
pack expansion. Local bank inventory retains all 19 member identities.
Do not commit raw decompilation or proprietary payloads. If local evidence
is unavailable, reproduce the narrow observation against your own game copy
before relying on an undocumented interpretation.

Use [ENGINE](ENGINE.md) for build/test commands. Extend shared C++ handlers
under explicit source profiles, then test ordinary events, frames, failure
paths and teardown. Package another APK after a useful engine milestone.
