// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_NATIVE_SOURCE_AF_CHECKS_H
#define WOLF2009_NATIVE_SOURCE_AF_CHECKS_H
#include <memory>
class WolfSourceDeclarationSet;
class WolfSourceActorBinding;
struct WolfSourceAFRequest;
// Game-private helper. Caller supplies every exact HKXR/profile/identity field;
// this helper does not derive an HKXR filename from a visual or declaration key.
bool Wolf2009_CheckNativeSourceAFQA(const WolfSourceDeclarationSet&,
    std::shared_ptr<const WolfSourceActorBinding>,const char* exactEntitiesPath,
    const WolfSourceAFRequest&);
#endif
