# Ordinary trainyard game-camera proof

These optional checks run inside the existing EGL pbuffer setup when the
explicit `fixtures/trainyard-session.enabled` flag is present. Core calls the
already loaded GPL game's ordinary `game->Draw(0)` and reads its actual512-square
framebuffer. The probe creates no preview world, substitutes no model, advances
no game frames and changes no camera/player placement.

Game resolves `SourceSessionMapRuntime()->PrimaryMover()` and the live reserved
source player. It verifies the wagon's original source entity/model/skin/data
identities, the resident nondefault static model and the actual entity's custom
skin. Core verifies draw surfaces belonging to that exact native entity.

The three captures are:

1. Original wagon in the current ordinary player camera:
   `offscreen-trainyard-player.bmp`.
2. Only wagon visibility hidden at the same simulation time:
   `offscreen-trainyard-without-wagon.bmp`.
3. Original visibility restored:
   `offscreen-trainyard-restored.bmp`.

Qualified `idEntity::Hide/Show` calls alter rendering only. Stock virtual
`idMover::Hide/Show` changes collision contents and therefore is deliberately
not used by this visual control. Cleanup restores the original visibility even
on an unsuccessful capture. Game probes verify unchanged time/frame, player
pose/velocity/view, wagon pose/velocity/contents/mask and model/skin pointers.

The original/restored frames require nonzero wagon draw surfaces and indices;
the hidden frame requires zero. Presence A/B requires more than64 pixels with
summed RGB delta>12. Restoration requires zero pixels with any RGB difference,
and matching draw-surface/index counts. Saved frames use the existing native
`EngineOffscreenFrame` BMP writer.

This proves geometry contribution from the original source model/skin in the
real game camera. Presence A/B does not isolate a source texture or establish
retail shader/texture parity. It also does not prove a complete campaign.

`TrainyardViewCoreChecks.h` is a C++98 scalar QA seam; no engine objects or STL
cross module boundaries. Game export: `Wolf2009_TrainyardViewQA(int action)`.
Core entrypoint: `Wolf2009_RunTrainyardViewCoreChecks()`.

Integrated Android15 x86_64 run `9463fe061801` passed with the selected explicit
base-diffuse/omitted-modulation visual policy. Ordinary game drawing produced
1380 surfaces, including five visible wagon surfaces/9666 indices; hidden
1375/zero; restored1380/five. Presence A/B changed173682 pixels and restoration
changed zero RGB pixels. Game-side127 checks confirmed unchanged simulation,
camera and physics. Native trainyard1438 checks/790 frames and map teardown
also passed. The earlier strict visual run `5d549c82ec51` separately passed
the same geometry proof with its diagnostic shader fallbacks.

See [the current source-map profile](../../docs/TRAINYARD.md) for the three
explicitly omitted modulation layers and gameplay limitations. The five
visible draws are not the complete model surface count: ten original source
surfaces remain owned, of which three are deliberately nondrawing in this
compatibility profile. Retail shader parity and mission playability remain
unverified.
