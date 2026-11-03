// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_DECLARATION_RUNTIME_H
#define WOLF2009_SOURCE_DECLARATION_RUNTIME_H
// Private C++11 game-module interface. Include ordinary native headers first.
// No pointer, STL object or new layout in this header crosses API8.
#include <cstddef>
#include <cstdint>
#include <memory>
namespace wolf2009 { class CompiledDeclarationCatalog;class SourceDefinitions; }

enum WolfSourceDeclarationPolicy {
    WolfSourceDeclaration_Unspecified=0,
    WolfSourceDeclaration_SelectedTypedPacketV1=1
};
enum WolfSourceDeclarationRole {
    WolfSourceDeclaration_RoleUnspecified=0,
    WolfSourceDeclaration_SelectedMap=1,
    WolfSourceDeclaration_SelectedExplicit=2,
    WolfSourceDeclaration_SelectedGlobal=3
};
struct WolfSourceDeclarationRequest {
    // Map/Explicit: exact wolf2009/<mount>/DECL/<resource> API8 path.
    // Global: safe relative ordinary VFS .decls path, copied byte-for-byte.
    idStr resourcePath;
    idStr loadIdentity; // Explicit opaque identity for this one selected packet.
    WolfSourceDeclarationPolicy policy;
    WolfSourceDeclarationRole role;
    WolfSourceDeclarationRequest() : policy(WolfSourceDeclaration_Unspecified),role(WolfSourceDeclaration_RoleUnspecified) {}
};
struct WolfSourceDeclarationCounts {
    std::size_t packetBytes=0,entries=0,entityDefinitions=0,modelDefinitions=0,otherDeclarations=0;
    std::uint64_t packetFingerprint=0;
};
class WolfSourceDeclarationPacket {
  public:
    ~WolfSourceDeclarationPacket();
    const wolf2009::CompiledDeclarationCatalog& Catalog() const;
    const wolf2009::SourceDefinitions& Definitions() const;
    const WolfSourceDeclarationCounts& Counts() const;
    WolfSourceDeclarationPacket(const WolfSourceDeclarationPacket&)=delete;
    WolfSourceDeclarationPacket& operator=(const WolfSourceDeclarationPacket&)=delete;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit WolfSourceDeclarationPacket(std::unique_ptr<Impl>);
    friend std::unique_ptr<WolfSourceDeclarationPacket> Wolf2009_LoadSourceDeclarations(const WolfSourceDeclarationRequest&,idStr&);
};
// Returns null+reason on refusal, with no published partial result. Reads at
// most 64MiB through ordinary OpenFileRead, checks exact bytes/EOF and closes
// the idFile before decoding. The result owns copied packet/catalog/frontend
// data independently of request and file lifetimes. At most 20000 selected
// entity/model records are imported with 128MiB frontend storage/source caps.
// No declManager registration, spawn, fallback/default tree or clock change.
// Each call returns one independent selected namespace. Global and map inputs
// are never merged or assigned automatic precedence by this loader.
// Catalog epoch is explicitly zero: idFile supplies no mount-epoch capability.
// This establishes selected-packet facts, not mount pins or retail namespace
// coverage. Native entity/type admission and all-parent resolution are separate.
std::unique_ptr<WolfSourceDeclarationPacket> Wolf2009_LoadSourceDeclarations(
    const WolfSourceDeclarationRequest& request,idStr& reason);
#endif
