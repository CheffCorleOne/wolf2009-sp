// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineSupportSource.h"
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "framework/Wolf2009Streams.h"
namespace {
unsigned releaseFailures=0;
void Close(idFile* file){if(file)fileSystem->CloseFile(file);}
std::vector<unsigned char> Read(const std::string& path) {
    std::unique_ptr<idFile,void(*)(idFile*)> file(fileSystem->OpenFileRead(path.c_str(),false),Close);
    if(!file)throw std::runtime_error("Same-mount support source is unavailable");
    const int length=file->Length();
    if(length<=0||length>(64<<20))throw std::runtime_error("Support source byte budget exceeded");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if(file->Read(bytes.data(),length)!=length)throw std::runtime_error("Short support source read");
    return bytes;
}
}
EngineSupportMountLease::EngineSupportMountLease(const std::string& label):label_(label),epoch_(0) {
    if(!Wolf2009_PinMount(label_.c_str(),epoch_))throw std::runtime_error("Cannot pin source support mount");
}
EngineSupportMountLease::~EngineSupportMountLease() {
    if(epoch_&&!Wolf2009_UnpinMount(label_.c_str(),epoch_))++releaseFailures;
}
unsigned Wolf2009_SourceLeaseReleaseFailures(){return releaseFailures;}
EngineSupportSource Wolf2009_LoadSupportSource(const std::string& canonicalMapName) {
    wolf2009::GameplayMapSourcePaths paths;
    if(!wolf2009::ResolveGameplayMapPath(canonicalMapName+".map",paths))
        throw std::runtime_error("Invalid canonical source support map");
    const auto end=canonicalMapName.find('/',9);
    EngineSupportSource result;
    result.lease.reset(new EngineSupportMountLease(canonicalMapName.substr(9,end-9)));
    // The pin prevents unmount/remount between the two typed reads. Provider
    // backing bytes have the existing StreamProvider stability contract.
    const auto hkxs=Read(paths.collision),ents=Read(paths.entities);
    result.plan=wolf2009::PrepareSupportGameplayMap(hkxs,ents,canonicalMapName,result.lease->Epoch());
    return result;
}
