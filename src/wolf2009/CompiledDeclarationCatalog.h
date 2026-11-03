// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_COMPILED_DECLARATION_CATALOG_H
#define WOLF2009_COMPILED_DECLARATION_CATALOG_H
#include "CompiledDeclarations.h"
#include <memory>

namespace wolf2009 {
enum class CompiledCatalogRole { Unspecified, Global, Map, Explicit };
struct CompiledCatalogSource {
    std::string loadIdentity; // Unique explicit load unit, not a priority rule.
    std::string path; // Exact caller provenance; never opened or normalized here.
    CompiledCatalogRole role=CompiledCatalogRole::Unspecified;
    std::uint64_t epoch=0; // Zero is an offline input, not a mounted-byte lease.
    bool byteIdentityKnown=false;
    std::size_t bytes=0;
    std::uint64_t fingerprint=0; // Local FNV identity, not authentication.
};
enum class CompiledCatalogNamePolicy {
    Exact=1,
    AsciiCaseSlashLastDotV1=2
};
enum class CompiledCatalogLookupStatus { Missing, Unique, Ambiguous };
struct CompiledCatalogEntry {
    // The immutable document retains BOTH prefix pools, all framing/spans,
    // defaultTypeId/rawId and exact raw payload, even after catalog destruction.
    std::shared_ptr<const CompiledDeclarations> document;
    std::shared_ptr<const CompiledCatalogSource> source;
    std::size_t loadOrdinal=0,documentOrdinal=0,catalogOrdinal=0;
    std::size_t section=0,group=0,record=0;
    std::string type,name; // Decoded literal header values; spelling stays raw.
    // Record-local half-open spans. Type/name include their source quotes;
    // body includes braces, envelope excludes surrounding comments/whitespace.
    CompiledDeclSpan typeSpan,nameSpan,bodySpan,envelopeSpan;
    const CompiledDeclGroup& Group() const;
    const CompiledDeclRecord& Record() const;
    // A process-local integrity check, not authentication. Only the catalog
    // creates the private immutable snapshot; ordinary copies keep its seal.
    // Inline so a downstream portable consumer needs no catalog link merely
    // to reject a default/modified public entry before retaining provenance.
    bool IsUnmodified() const {
        const CompiledCatalogEntry* original=original_.get();
        return original&&document==original->document&&source==original->source&&
            loadOrdinal==original->loadOrdinal&&documentOrdinal==original->documentOrdinal&&catalogOrdinal==original->catalogOrdinal&&
            section==original->section&&group==original->group&&record==original->record&&type==original->type&&name==original->name&&
            SameSpan(typeSpan,original->typeSpan)&&SameSpan(nameSpan,original->nameSpan)&&
            SameSpan(bodySpan,original->bodySpan)&&SameSpan(envelopeSpan,original->envelopeSpan);
    }
  private:
    static bool SameSpan(const CompiledDeclSpan& a,const CompiledDeclSpan& b) {return a.begin==b.begin&&a.end==b.end;}
    std::shared_ptr<const CompiledCatalogEntry> original_;
    friend class CompiledDeclarationCatalog;
};
struct CompiledCatalogLookup {
    CompiledCatalogLookupStatus status=CompiledCatalogLookupStatus::Missing;
    std::vector<CompiledCatalogEntry> matches; // Owning; explicit load/record order.
    // Missing means absent only from this selected catalog, not retail/runtime
    // namespace absence, nor permission to synthesize a default declaration.
};
struct CompiledCatalogLimits {
    std::size_t loadUnits=128,entries=100000,totalSourceBytes=128u<<20;
    std::size_t recordTextBytes=4u<<20,tokenBytes=8192,nesting=64;
    std::size_t parseWork=256u<<20,retainedBytes=256u<<20;
    std::size_t lookupMatches=100000,lookupBytes=64u<<20;
};
class CompiledCatalogError : public std::runtime_error {
    CompiledDeclFailure category_;
    std::size_t offset_;
  public:
    CompiledCatalogError(CompiledDeclFailure category,std::size_t offset,const char* reason)
        :std::runtime_error(reason),category_(category),offset_(offset){}
    CompiledDeclFailure Category() const { return category_; }
    std::size_t Offset() const { return offset_; } // Record-local if parsing.
};
// Immutable owning load units, append-only selection. No filesystem, native
// registry, key/value semantics, rawId lookup, duplicate precedence or reload.
// Named ASCII declaration envelopes are checked; bodies remain opaque text.
// All records must qualify before AddDocument commits. Exact DTO ownership is
// copied before publication; a caller's mutable shared_ptr cannot alter it.
class CompiledDeclarationCatalog {
  public:
    explicit CompiledDeclarationCatalog(const CompiledCatalogLimits& = CompiledCatalogLimits());
    ~CompiledDeclarationCatalog();
    CompiledDeclarationCatalog(const CompiledDeclarationCatalog&)=delete;
    CompiledDeclarationCatalog& operator=(const CompiledDeclarationCatalog&)=delete;
    void AddDocument(const CompiledDeclarations&,const CompiledCatalogSource&);
    std::size_t LoadUnitCount() const;
    std::size_t EntryCount() const;
    CompiledCatalogEntry Entry(std::size_t index) const;
    std::shared_ptr<const CompiledDeclarations> Document(std::size_t loadOrdinal) const;
    std::shared_ptr<const CompiledCatalogSource> Source(std::size_t loadOrdinal) const;
    // Type keys always fold ASCII case ONLY. Exact names preserve all bytes;
    // profile2 folds ASCII, maps backslash to slash and removes the suffix at
    // the LAST dot, even in a directory component. Raw names remain unchanged.
    CompiledCatalogLookup Lookup(const std::string& type,const std::string& name,
        CompiledCatalogNamePolicy=CompiledCatalogNamePolicy::Exact) const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wolf2009
#endif
