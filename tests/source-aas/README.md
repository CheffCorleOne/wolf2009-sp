# DewmAAS 1.40 reader and trainyard navigation

`wolf2009-aas140` runs authored wire and conversion checks. Pass
`--private` followed by all 27 extracted AASS members to run the original
data checks. Keep extracted files under ignored evidence.

The native checks use the original trainyard map and the GPL Doom 3 AAS
manager and router. Build and run them on an Android x86_64 emulator:

```powershell
.\tests\engine-runtime\Build-EngineRuntime.ps1 -Abi x86_64 `
  -WithAas140Checks -Jobs 2
.\tests\engine-runtime\Test-EngineRuntime.ps1 -Serial emulator-5556 `
  -WithAas140Checks -RendererGameRoot 'C:\path\to\Wolfenstein'
```

The fixture adds `aas28` to the game's ordinary AAS type list. It loads
trainyard first with the profile empty, then twice with `WolfAas140V1`.
The checks require the unprofiled refusal, native reachable areas 6199 and
8040, area 8040's location 26 (`trap_door_room`), a continuous native route
under `TFL_WALK | TFL_AIR`, one release during reload and zero live files
after engine shutdown. The route is a navigation query. Movement, tactical
behavior and the runtime `ReachedLocation` condition are later stages.
