// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_DECLARATION_SET_RUNTIME_H
#define WOLF2009_SOURCE_DECLARATION_SET_RUNTIME_H
// Game-private C++11 interface. Include native headers first. Nothing here
// crosses API8 or installs declarations in the native declManager.
#include <cstddef>
#include <memory>
#include <vector>
#include "SourceDeclarationRuntime.h"
namespace wolf2009 { class CompiledDeclarationNamespace; class SourceDefinitions; }
class WolfSourceDeclarationPacket;

enum WolfSourceDeclarationSetPolicy {
    WolfSourceDeclarationSet_Unspecified=0,
    WolfSourceDeclarationSet_OrdinaryOwnerSectionsV1=1
};
enum WolfSourceDeclarationSectionMode {
    WolfSourceDeclarationSection_Unspecified=0,
    WolfSourceDeclarationSection_Ordinary=1,
    WolfSourceDeclarationSection_ProtectAccepted=2
};
struct WolfSourceDeclarationSetStep {
    const WolfSourceDeclarationPacket* packet;
    std::size_t section;
    WolfSourceDeclarationSectionMode mode;
    WolfSourceDeclarationSetStep():packet(NULL),section(0),mode(WolfSourceDeclarationSection_Unspecified){}
};
struct WolfSourceDeclarationSetRequest {
    WolfSourceDeclarationSetPolicy policy;
    std::vector<WolfSourceDeclarationSetStep> steps;
    WolfSourceDeclarationSetRequest():policy(WolfSourceDeclarationSet_Unspecified){}
};
struct WolfSourceDeclarationSetCounts {
    std::size_t steps=0,selected=0,entityDefinitions=0,modelDefinitions=0,otherDeclarations=0;
};
class WolfSourceDeclarationSet {
  public:
    ~WolfSourceDeclarationSet();
    const wolf2009::CompiledDeclarationNamespace& Namespace() const;
    const wolf2009::SourceDefinitions& Definitions() const;
    const WolfSourceDeclarationSetCounts& Counts() const;
    WolfSourceDeclarationSet(const WolfSourceDeclarationSet&)=delete;
    WolfSourceDeclarationSet& operator=(const WolfSourceDeclarationSet&)=delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceDeclarationSet(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceDeclarationSet> Wolf2009_CreateSourceDeclarationSet(const WolfSourceDeclarationSetRequest&,idStr&);
};
// Requires an explicit ordered section/mode plan over already loaded packets.
// No inferred Global/Map priority, implicit/default owners, native indices or
// external retail lifecycle flag resets. The selected ordinary-owner profile
// applies the checked reader gates, then imports selected model/entityDef data
// under explicit case/slash/last-dot declaration names. All entries, decisions,
// pools and resolved field origins own their source after input handles die.
// At most 256 steps, 20000 selected entity/model records and 128MiB frontend
// storage/source. Failure publishes no partial result or native effects.
std::unique_ptr<WolfSourceDeclarationSet> Wolf2009_CreateSourceDeclarationSet(
    const WolfSourceDeclarationSetRequest&,idStr& reason);

enum WolfSourceDeclarationStartupPolicy {
    WolfSourceDeclarationStartup_Unspecified=0,
    WolfSourceDeclarationStartup_CompiledLocalizedV1=1
};
struct WolfSourceDeclarationStartupRequest {
    WolfSourceDeclarationStartupPolicy policy;
    WolfSourceDeclarationRequest globalLocale,globalMain,mapLocale,mapMain;
    WolfSourceDeclarationStartupRequest():policy(WolfSourceDeclarationStartup_Unspecified){}
};
// Observed compiled startup sequence (English trainyard retail trace): locale
// global section0, main global sections0/1 are protected; locale map sections0/1,
// then main map sections0/1 are ordinary. The caller explicitly selects all four
// exact resources/identities; no language/path inference or missing-file fallback.
// Reads through API8 and returns one owning selected namespace/frontend. Locale
// global's unconsumed section1 must be empty in this qualified profile. Rejects
// other policies/roles or missing/malformed inputs without publishing a result.
// Data only: no native indices, class registration, entity Spawn or clock effects.
std::unique_ptr<WolfSourceDeclarationSet> Wolf2009_LoadSourceDeclarationStartup(
    const WolfSourceDeclarationStartupRequest&,idStr& reason);
#endif
