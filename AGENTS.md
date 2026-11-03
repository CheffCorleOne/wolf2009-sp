# Wolf2009 SP port

Default communication: Russian. Read README.md, docs/RESEARCH.md,
docs/ROADMAP.md, docs/GEOMETRY.md and docs/CONTINUE.md before extending the port.

- This targets Wolfenstein (2009), single-player. It is not RTCW or Enemy Territory.
- Original installation is a read-only reference. Never patch or replace its
  executables, libraries, configs, archives, saves, or registry entries.
- All builds, evidence and experiments belong here or under a separate tools
  directory. The user explicitly authorized GitHub publication of this engine
  and Android releases on 2026-11-03. Use the connected account, publish only
  reviewed source/APK artifacts, and keep original commercial assets excluded.
  Ask before downloading paid tools. Runtime experiments may use the isolated
  local game copy; protect the owner's existing installation and saves.
- Do not add original game assets, original executables, decompiled proprietary
  implementations, local paths/evidence, credentials or extracted resources to Git.
- `src/wolf2009` is the canonical standalone C++ loader. `engine/` is an ignored
  pinned upstream checkout; preserve engine changes in `patches/`.
- Current user priority: reverse engineering and shared C++ engine/game
  compatibility. The Android host already works as a validation frontend.
  Spend implementation effort on original resource semantics and SP behavior;
  package platform releases after a meaningful engine milestone or when asked.
- On 2026-11-04 the owner resumed full Codex implementation after Antigravity
  quota was exhausted. Use bounded parallel agents where useful, integrate the
  saved Gemini changes, and finish player/MP40 before campaign work. The earlier
  Gemini-only/conserve-Codex preference is superseded. Do not enable paid overflow
  or an API-billed service. Strategy/context is in docs/STRATEGY-REVIEW.md.
- Do not assume Quake4/Prey SDK game modules are GPL because an engine is GPL.
  Preserve each upstream license; use independently implemented SP code.
- Distinguish archive validation, rendering, engine integration and gameplay.
  Never label this bootstrap a working Android game port.
- Current tests: `npm test`, `tools/build-native.ps1`, `npm run test:native`.
  Geometry: `tools/build-preview.ps1`, `npm run test:geometry`.
  C++ loader uses C++11, zlib or bundled miniz; no operating-system calls.
- Current baseline is recorded in docs/PROVENANCE.json. Do not upgrade it
  without checking the compatibility patch and running relevant tests.
