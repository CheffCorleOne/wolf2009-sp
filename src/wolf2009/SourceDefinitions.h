// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_DEFINITIONS_H
#define WOLF2009_SOURCE_DEFINITIONS_H
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wolf2009 {
struct CompiledCatalogEntry;
enum class SourceDefinitionKind { EntityDef, Model, Other };
// This is our explicit lookup/merge policy, not proof of retail hash semantics.
enum class SourceDefinitionPolicy { AsciiCaseFoldLiteralChildOverridesV1 };
struct SourceDefinitionArchiveIdentity {
    std::string name; // caller-supplied opaque source identity, never opened here
    std::size_t bytes = 0;
    std::uint64_t fingerprint = 0;
};
struct SourceDefinitionMemberIdentity {
    SourceDefinitionArchiveIdentity archive;
    std::string name; // original member-name bytes
    std::size_t bytes = 0;
    std::uint64_t fingerprint = 0; // computed FNV-1a of the supplied member bytes
    // Compiled inputs use a packet/record identity in name. The owning entry
    // retains the actual authored source-file label, packet span, pools and
    // load identity/epoch. Token positions remain local to its exact payload.
    std::shared_ptr<const CompiledCatalogEntry> compiledRecord;
};
struct SourceDefinitionToken {
    // Quoted text decodes only escaped quote/backslash; other backslashes stay
    // literal. Quoted TAB/CR/LF are retained as source value bytes. raw preserves
    // the original spelling including quotes/escapes; no expressions execute.
    std::string text, raw;
    std::size_t begin = 0, end = 0, line = 1, column = 1;
    bool quoted = false;
    char brace = 0;
};
enum class SourceDefinitionItemKind { Inherit, Literal, Opaque };
enum class SourceLiteralEncoding {
    PlainText, CompiledString, CompiledSignedInteger, CompiledOpaque,
    CompiledFloat32, CompiledFloatArray, CompiledIntegerArray
};
struct SourceDefinitionItem {
    SourceDefinitionItemKind kind = SourceDefinitionItemKind::Opaque;
    SourceDefinitionToken key;
    std::vector<SourceDefinitionToken> values;
    std::string rawText;
    std::size_t begin = 0, end = 0;
    // In compiled entityDef text, values preserve type/aux/value tokens.
    // Qualified string/integer values start at index2. Other typed values
    // remain explicit barriers rather than exposing a parent's literal.
    SourceLiteralEncoding encoding = SourceLiteralEncoding::PlainText;
    std::int32_t rawValueType = 0, rawValueAux = 0;
    // Exact owning words for admitted numeric encodings, including kind4's
    // single signed-int word. Float32 is one word; arrays preserve every word.
    // Nonfinite float bits are DATA, not runtime numeric admission.
    std::vector<std::uint32_t> numericWords;
};
struct SourceDefinition {
    SourceDefinitionKind kind = SourceDefinitionKind::Other;
    SourceDefinitionToken kindToken, nameToken;
    SourceDefinitionMemberIdentity member;
    std::vector<SourceDefinitionItem> items;
    std::string rawText;
    std::size_t begin = 0, end = 0;
    std::size_t bodyBegin = 0, bodyEnd = 0; // Includes the actual outer braces.
};
struct SourceDefinitionProvenance {
    SourceDefinitionMemberIdentity member;
    std::string declarationKind, declarationName;
    std::string rawDeclarationKind, rawDeclarationName;
    std::size_t declarationBegin = 0, declarationEnd = 0;
    std::size_t item = static_cast<std::size_t>(-1), begin = 0, end = 0;
};
struct SourceDefinitionFact {
    SourceDefinitionProvenance origin;
    SourceDefinitionItem data; // owns raw key/values/block text after catalog destruction
};
struct SourceResolvedLiteral {
    std::string lookupKey;
    std::vector<SourceDefinitionToken> value;
    // Identical inherited values may have several independent provenances.
    std::vector<SourceDefinitionFact> origins;
    bool gameplayImplemented = false;
    SourceLiteralEncoding encoding = SourceLiteralEncoding::PlainText;
    std::int32_t rawValueType = 0, rawValueAux = 0;
    std::vector<std::uint32_t> numericWords;
    // CompiledOpaque has no qualified value tokens; origins retain all raw
    // metadata/value bytes. Float/pool numeric values also have EMPTY value;
    // numericWords owns their bits without synthesizing decimal tokens. Only
    // PlainText, CompiledString and CompiledSignedInteger expose value tokens.
    // Consumers must qualify encoding before use.
};
struct SourceDefinitionInheritance {
    SourceDefinitionFact parent;
    SourceDefinitionProvenance resolved;
    bool explicitMapContext = false;
};
struct SourceDefinitionContext {
    SourceDefinitionPolicy policy = SourceDefinitionPolicy::AsciiCaseFoldLiteralChildOverridesV1;
    // Only exact model-parent text "MAPDEF" uses this explicit named model.
    // No map-name inference, filesystem lookup or built-in source mapping.
    std::string mapModelDefinition;
};
struct ResolvedSourceDefinition {
    SourceDefinitionPolicy policy = SourceDefinitionPolicy::AsciiCaseFoldLiteralChildOverridesV1;
    SourceDefinitionProvenance selected;
    std::vector<SourceDefinitionProvenance> declarations; // reachable parents before child
    std::vector<SourceDefinitionInheritance> inheritance;
    std::map<std::string, SourceResolvedLiteral> literalFields;
    std::vector<SourceDefinitionFact> opaqueFacts;
    bool gameplaySupported = false;
    const SourceResolvedLiteral* Find(const std::string& key) const;
};
enum class SourceAnimationReferencePolicy { AsciiCaseFoldAnimLiteralChildOverridesV1 };
enum class SourceAnimationReferenceShape {
    SingleLiteralNoTail, LiteralHeaderWithOpaqueBlock, OtherUnimplemented
};
struct SourceModelAnimationReference {
    std::string lookupKey; // full ASCII-folded name; numeric suffixes stay intact
    SourceDefinitionToken logicalName, assetReference;
    bool hasLiteralAssetReference = false;
    SourceAnimationReferenceShape shape = SourceAnimationReferenceShape::OtherUnimplemented;
    // Full items retain blocks/options and independent inherited provenances.
    std::vector<SourceDefinitionFact> origins;
    bool runtimeImplemented = false;
};
struct ResolvedSourceModelAnimations {
    SourceAnimationReferencePolicy policy = SourceAnimationReferencePolicy::AsciiCaseFoldAnimLiteralChildOverridesV1;
    ResolvedSourceDefinition model; // unchanged literal resolver result/opaque facts
    // Unsupported entries remain here as barriers to inherited pure entries.
    std::map<std::string, SourceModelAnimationReference> references;
    bool gameplaySupported = false;
    const SourceModelAnimationReference* Find(const std::string& logicalName) const;
};
struct SourceDefinitionLimits {
    std::size_t members = 128, sourceBytesPerMember = 4u << 20, totalSourceBytes = 32u << 20;
    std::size_t declarations = 20000, tokens = 1000000, tokenBytes = 8192, storedBytes = 128u << 20;
    std::size_t nesting = 64, parentsPerDeclaration = 32, inheritanceDepth = 64;
    // Conservative retained-data charges include repeated identity/key bytes,
    // string growth and container nodes. These are not exact allocator/RSS caps.
    std::size_t resolutionWork = 500000, resolutionBytes = 128u << 20;
    std::size_t resultFacts = 100000, resultBytes = 64u << 20;
};
// Bounded data-only indexing/resolution. AddMember has a strong transaction:
// malformed text, inconsistent supplied archive identity and duplicate member
// or declaration names retain all previous members/definitions. Other named
// declaration kinds are retained opaque, never silently treated as supported.
// Resolve checks the entire reachable selected dependency closure, including
// dependencies hidden by child overrides. Missing parents, cycles and distinct
// inherited values without an explicit child override refuse. EntityDef admits
// literal key/value pairs; model literals are mesh/skeleton/skin/offset. Other
// model commands (including anim/channel/frame-command blocks) stay opaque.
// Source/member fingerprints express byte identity, not authentication.
class SourceDefinitions {
  public:
    explicit SourceDefinitions(const SourceDefinitionLimits& = SourceDefinitionLimits());
    ~SourceDefinitions();
    SourceDefinitions(const SourceDefinitions&) = delete;
    SourceDefinitions& operator=(const SourceDefinitions&) = delete;
    void AddMember(const SourceDefinitionArchiveIdentity&, const std::string& memberName,
                   const std::vector<unsigned char>& bytes);
    // Imports ONE owning named catalog record. Compiled entityDef KV grammar
    // is separate from plain source: 0/0 string and 4/1 signed int keep their
    // token profile. Kind3/aux1 owns inline float bits; kinds1/2 with aux1..255
    // own a checked span from pool1(float)/pool0(integer), respectively. Count1
    // floats use CompiledFloat32; larger float spans use CompiledFloatArray;
    // pool0 spans use CompiledIntegerArray. No decimal tokens are synthesized.
    // Other types/counts remain opaque barriers; malformed admitted pool refs
    // refuse import. The count-span admission does not qualify vector getters,
    // booleans, finite runtime values or conversion/formatting behavior.
    // Unqualified inherit
    // refuses import. Model grammar stays the existing literal/opaque profile.
    // Source IDs are retained metadata, never native indices/type mappings.
    void AddCompiledRecord(const CompiledCatalogEntry&);
    std::size_t MemberCount() const;
    std::size_t DefinitionCount() const;
    const SourceDefinition& Definition(std::size_t index) const;
    ResolvedSourceDefinition Resolve(SourceDefinitionKind, const std::string& name,
                                    const SourceDefinitionContext& = SourceDefinitionContext()) const;
    // Additive data-only query: first validates the full existing model closure,
    // then merges full logical anim names under our explicit child-override
    // policy. Duplicate own names, unknown-name anim syntax and model-level
    // remove refuse. Ambiguous namespace commands hidden in an opaque header
    // also refuse; other unknown commands remain in model.opaqueFacts.
    // A block-bearing literal header is a fact, never an implemented callback.
    // Bare comma/list syntax, extra header tokens and wrapped headers are not
    // pure literals. No extension rewrite, file lookup, alias grouping or rig/
    // playback semantics is inferred. Plain-source resolution is preserved.
    // The second phase has its own resolutionWork/resolutionBytes cap; its
    // resultBytes/resultFacts charges include the retained ordinary model and
    // added references/provenance. These remain conservative, not RSS limits.
    ResolvedSourceModelAnimations ResolveModelAnimations(const std::string& name,
                                    const SourceDefinitionContext& = SourceDefinitionContext()) const;
  private:
    void AddInput(const SourceDefinitionArchiveIdentity&,const std::string&,
                  const std::vector<unsigned char>&,const CompiledCatalogEntry*);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wolf2009
#endif
