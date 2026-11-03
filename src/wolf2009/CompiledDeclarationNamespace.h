// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_COMPILED_DECLARATION_NAMESPACE_H
#define WOLF2009_COMPILED_DECLARATION_NAMESPACE_H
#include "CompiledDeclarationCatalog.h"

namespace wolf2009 {
enum class CompiledNamespacePolicy { OrdinaryTextOwnersV1=1 };
enum class CompiledNamespaceReaderMode { Unspecified=0, Ordinary=1, ProtectAccepted=2 };
enum class CompiledNamespaceDecisionKind {
    AcceptedNew, AcceptedReplacement, SkippedProtected, SkippedOwnerConflict, SkippedDuplicate
};
struct CompiledNamespaceSelected {
    CompiledCatalogEntry entry;
    std::string typeKey,nameKey,ownerLabel; // First ordinary owner's raw label.
    bool seen08=false,protected20=false;
    std::size_t decision=0;
};
struct CompiledNamespaceDecision {
    CompiledNamespaceDecisionKind kind=CompiledNamespaceDecisionKind::AcceptedNew;
    CompiledNamespaceReaderMode mode=CompiledNamespaceReaderMode::Unspecified;
    CompiledCatalogEntry incoming,previous;
    std::string typeKey,nameKey,incomingOwner,previousOwner;
    std::size_t application=0,group=0,selectedOrdinal=0;
    bool hadPrevious=false,previousSeen08=false,previousProtected20=false,resultSeen08=false,resultProtected20=false;
    bool model34ProtectionBypass=false;
};
struct CompiledNamespaceApplication {
    std::shared_ptr<const CompiledDeclarations> document;
    std::shared_ptr<const CompiledCatalogSource> source;
    std::size_t loadOrdinal=0,section=0,decisionBegin=0,decisionEnd=0;
    CompiledNamespaceReaderMode mode=CompiledNamespaceReaderMode::Unspecified;
    std::size_t accepted=0,replaced=0,skipped=0;
};
struct CompiledNamespaceLimits {
    std::size_t applications=256,decisions=200000,selected=100000,owners=20000;
    std::size_t keyBytes=8192,work=20000000,retainedBytes=256u<<20,resultBytes=64u<<20;
};
// Starts empty; only ordinary imported text owners exist. No implicit/default
// sentinel, bit10/native text gate, external mutator, seeded native declaration,
// unload/default/removal or automatically chosen global/map schedule is modeled.
// Type keys fold ASCII only. Names use the explicit catalog case/slash/last-dot
// profile; owner labels fold ASCII only (preserving slash and extension bytes).
// Every group resets seen08 for that owner. protected20 is sticky; literal
// retail type "model" (34) bypasses its skip, but not owner/duplicate checks.
// New immutable packets require distinct explicit loadIdentity values. Sections
// of the same catalog unit can reuse its identity; fingerprints alone do not
// authorize treating different immutable objects as the same input.
class CompiledDeclarationNamespace {
  public:
    explicit CompiledDeclarationNamespace(CompiledNamespacePolicy,
        const CompiledNamespaceLimits& = CompiledNamespaceLimits());
    ~CompiledDeclarationNamespace();
    CompiledDeclarationNamespace(const CompiledDeclarationNamespace&)=delete;
    CompiledDeclarationNamespace& operator=(const CompiledDeclarationNamespace&)=delete;
    // Explicit application order is a caller plan, not proof of retail load
    // order. Whole-section strong transaction; copied sealed entries and all
    // decisions/selected records own their source documents/pools afterward.
    void ApplySection(const CompiledDeclarationCatalog&,std::size_t loadOrdinal,
        std::size_t section,CompiledNamespaceReaderMode);
    std::size_t SelectedCount() const;
    CompiledCatalogEntry Selected(std::size_t index) const;
    CompiledNamespaceSelected SelectedState(std::size_t index) const;
    std::vector<CompiledCatalogEntry> SelectedEntries() const;
    // Unique/Missing only in this explicitly selected ordinary-owner set.
    // Missing is not complete retail namespace coverage/default permission.
    CompiledCatalogLookup Lookup(const std::string& type,const std::string& name) const;
    std::size_t DecisionCount() const;
    CompiledNamespaceDecision Decision(std::size_t index) const;
    std::size_t ApplicationCount() const;
    CompiledNamespaceApplication Application(std::size_t index) const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wolf2009
#endif
