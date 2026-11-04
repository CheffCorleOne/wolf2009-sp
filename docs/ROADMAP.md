# Roadmap

The next playable target is trainyard through its first encounter. The final
target is the full Wolfenstein (2009) single-player campaign on native Android,
followed by other platforms. No completion percentage or reliable finish date
is claimed.

| Stage | Current position / completion gate |
|---|---|
| Read original resources | Archive, declaration, geometry, texture, rig and spline readers exist. Geometry/body inventories cover all 27 map packs; this is data coverage, not mission support. |
| Run shared native systems | Engine lifecycle, source worlds, collision, player, MP40 and animation have native tests. Trainyard reaches wagon arrival. |
| Continue trainyard | Qualify whole-bank audio readiness, connect greeter use/animation/door, then reach the exit trigger. Separate component tests precede automatic mission progression. |
| First encounter | Original trigger chain, AI/navigation, native weapons/damage/death and player recovery work together. |
| Complete trainyard | All required events, interactions, save/reload and the next-map transition work without skipped nodes. |
| Complete the campaign | Reuse shared handlers across missions, qualify source differences and test continuous progression and recovery. |
| Ship Android | Physical ARM64 tests, input/UI, audio, memory/performance and lifecycle coverage on actual devices. |
| Port other platforms | iOS and WebAssembly hosts/backends with their own build and runtime validation. |

The implementation stays in portable C++ and native engine adapters. Android
packaging is a validation frontend; rebuilding an APK does not advance a
missing gameplay handler. Existing GLES support remains the graphics path.
Vulkan is deferred while campaign compatibility is the priority.

[NEXT-STEPS](NEXT-STEPS.md) defines the immediate acceptance gates.
[DEVELOPMENT](DEVELOPMENT.md) records the workflow and lessons behind them.
