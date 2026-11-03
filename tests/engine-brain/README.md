# Native literal BrainTask checks

This is an independently authored diagnostic profile for the real GPL game
module. It supports OnSpawn, OnUsed, Wait, UseTarget and SetGlobalVariable;
primary Exists/Used/Timer conditions; literal entity selectors; and canonical
nonnegative int32 values. Unsupported nodes, fields, expressions or conditions
reject the complete group before initial effects.

`idWolfBrainTask` binds through the ordinary entity registry and Think lifecycle.
`idWolfBrainGlobals` owns a mandatory initialized shared int32 dictionary.
Generation-aware references refuse deleted or reused entities. CanUseKey runs
before the Use latch; delivery does not recursively tick another task.

The provider explicitly selects `diagnostic-ms` timing and an authored defaults
format. This is not a decoder for retail globalVars, an expression interpreter,
or a claim about the original game's clock, save state or provider lifetime.
Save/restore is refused. Original graph admission does not imply that its
source entities can already spawn in the port.

Build `tests/engine-runtime/Build-EngineRuntime.ps1 -WithBrainChecks`, then run
`Test-EngineRuntime.ps1 -Serial <x86_64 emulator> -WithBrainChecks`. The generator
creates only authored BRAI/defaults/defs in the isolated fixture. The QA export
is loaded from the already running libgame through RTLD_NOLOAD; it is absent
with `WOLF2009_BRAIN_QA=OFF`, the default.

The native test covers cross-entity timed delivery and shared writes, key gates,
reset, same-slot generation reuse, unsupported graphs/receivers/defaults, a
65-task group with its provider spawned last, and complete registry teardown.
The portable literal-profile test separately performs 59 checks.
