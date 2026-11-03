# MP40 single-root LOD proposal

Status: applied to the main implementation after the owner resumed development
on 2026-11-04. The patch remains here as the reviewed checkpoint proposal.

`implementation.patch` adds a separate compatibility policy for exactly one
authored root LOD, requested slot0, and mesh markers that are either explicit0
or absent. At least one mesh marker must be absent. Root distances and every
mesh marker's presence are retained; missing markers are not labelled authored
LOD0. Existing policies and aliases are preserved. This is a bounded selection
policy, not proof of the original missing-field consumer.

Isolated verification:10/10 CTest PASS, including legacy controls and actual
owner-supplied MP40 inputs. The view model has49 joints,7 surfaces,4808 vertices
and6488 triangles.37 fractional source poses/720168 corners were compared;
maximum position error3.814697e-6. `git apply --check --whitespace=error` passed
against the checkpoint source. Native, GPU and Android packaging were not run.

Important remaining work: authored `weapon_attack`, point_of_interest,
sound_weapon and eye_angles commands are unsupported. All seven view material
draws still hit the conservative shader-composition gate. Diffuse resources
exist across the global and weapon packs; this proposal does not solve texture
routing or the original shader composition. It does not create a working gun.

To resume, review and apply from the repository root, then rebuild the dependent
mesh, animation-wire and actor adapters and run their native checks. This patch
is deliberately outside the automatic engine patch chain.
