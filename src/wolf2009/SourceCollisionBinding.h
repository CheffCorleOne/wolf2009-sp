// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_COLLISION_BINDING_H
#define WOLF2009_SOURCE_COLLISION_BINDING_H
#include <cstddef>
#include <cstdint>
#include <string>
namespace wolf2009 {
// Explicit interpretation policies. The common-contents/envelope policy is
// native compatibility, not the original Havok selector or runtime filter.
enum class SourceCollisionBodyPolicy {
    Unspecified, DeclaredDoom3BitsV1, NativeCommonContentsCylinderEnvelopeV1,
    NativeSolidEntityCylinderEnvelopeV1
};
struct SourceCollisionBodyRequest {
    SourceCollisionBodyPolicy policy=SourceCollisionBodyPolicy::Unspecified;
    std::string resourcePath,bodyName;
    std::uint64_t expectedEpoch=0,expectedBytes=0,expectedFingerprint=0;
};
struct SourceCollisionBodyBinding {
    SourceCollisionBodyRequest request;
    std::uint64_t epoch=0,bytes=0,fingerprint=0,shapes=0,corePoints=0;
    std::uint32_t nativeContents=0;
};
// Data-only API8 filesystem protocol. Exact names are hex encoded; the model
// alias includes the actual source generation/identity, not only a file path.
const char* SourceCollisionBodyRequestPrefix();
const char* SourceCollisionBodyModelPrefix();
std::string SourceCollisionBodyRequestPath(const SourceCollisionBodyRequest&);
SourceCollisionBodyRequest ParseSourceCollisionBodyRequestPath(const std::string&);
std::string SerializeSourceCollisionBodyBinding(const SourceCollisionBodyBinding&);
SourceCollisionBodyBinding ParseSourceCollisionBodyBinding(const std::string&);
std::string SourceCollisionBodyModelName(const SourceCollisionBodyBinding&);
SourceCollisionBodyBinding ParseSourceCollisionBodyModelName(const std::string&);
bool SameSourceCollisionBodyRequest(const SourceCollisionBodyRequest&,const SourceCollisionBodyRequest&);
}
#endif
