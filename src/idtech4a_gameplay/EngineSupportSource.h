// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_SUPPORT_SOURCE_H
#define WOLF2009_ENGINE_SUPPORT_SOURCE_H
#include "SupportGameplayMap.h"
#include <memory>
class EngineSupportMountLease {
    std::string label_;
    unsigned long long epoch_;
  public:
    explicit EngineSupportMountLease(const std::string&);
    ~EngineSupportMountLease();
    const std::string& Label() const {return label_;}
    unsigned long long Epoch() const {return epoch_;}
    EngineSupportMountLease(const EngineSupportMountLease&)=delete;
    EngineSupportMountLease& operator=(const EngineSupportMountLease&)=delete;
};
struct EngineSupportSource {
    std::unique_ptr<EngineSupportMountLease> lease;
    wolf2009::SupportGameplayMap plan;
};
EngineSupportSource Wolf2009_LoadSupportSource(const std::string& canonicalMapName);
unsigned Wolf2009_SourceLeaseReleaseFailures();
#endif
