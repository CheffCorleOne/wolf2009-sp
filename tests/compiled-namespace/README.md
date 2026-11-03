# Compiled declaration namespace

`CompiledDeclarationNamespace` implements the measured ordinary-text owner
gates of the compiled reader. It starts empty and requires an explicit policy
and an ordered sequence of `(catalog, load unit, section, reader mode)` calls.
It does not infer a retail load schedule from the input's Global/Map role.

Each source-file group resets seen08 on that owner's declarations. A protected20
entry skips incoming text before owner/duplicate checks, except for the literal
`model` type (retail34). A same-owner unseen entry can be replaced. Ordinary
foreign-owner or same-group duplicates are skipped. Successful acceptance sets
seen08; ProtectAccepted mode also sets protected20. Ordinary mode never clears
an existing protected20. Owner filenames fold ASCII case only; slashes and
extensions remain significant. Declaration names use the explicit catalog
case/slash/last-dot profile, including a last dot in a directory component.

All candidates and decisions remain accessible, including prior entries for
replacements/skips, section applications, raw labels, flags and source pools.
Returned records own immutable catalog origins. Mutable returned copies cannot
change the namespace. A whole section commits only after all checks succeed.
Counts, key bytes, work, retained documents/pools and query allocation have
separate limits. A newly opened/changed immutable packet requires a new explicit
load identity; applying another section of the same unit can reuse its identity.

This profile has no seeded native declarations, implicit/default owner sentinel,
bit10 native-text state, external lifecycle flag changes, removal/unload, raw-ID
native index import or runtime coverage claim. Missing in this selected set is
not permission to synthesize a default declaration.

The test executable is available in the main portable CMake project, or through
this directory's standalone CMake project. `--self-test` covers owner/duplicate/
protected/model transitions, empty groups, reload, canonical collisions, owning
lifetime, transaction rollback and budgets. `--source-base <SP/base>` freshly
reads the exact global ZIP member and trainyard DECL packet, then verifies two
explicit plans: protected global sections followed by ordinary map sections,
and all sections ordinary. Both select4890 entries (1555 entityDef/48 model),
while1621 protected skips versus1625 replacements preserve a genuine int/string
conflict without coercion. These plans test the reader mechanism; they are not
assertions of the original runtime's current cvar or caller sequence.

The selected entity graph resolves1553 closures. Two original definitions,
`tesla_prong_arcs_conductance` and `trigger_tutorial`, retain missing-parent
refusals for `tesla_prong_arcs` and `trigger_multi` in this selected set. The test
checks these absences explicitly; it never invents parent/default definitions.
This is selected-data resolution coverage, not native class implementation.

The game-private `SourceDeclarationSetRuntime` builds the same selected data
through API8-loaded packets and imports effective entity/model definitions
under an explicit canonical name policy. Native QA checks both plans and owning
facts after packet/set destruction. It creates no entities or native declarations.
