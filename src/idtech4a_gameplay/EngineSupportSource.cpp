// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineSupportSource.h"
#include <stdexcept>
#include <string>
#include <vector>
#include "idlib/precompiled.h"
#include "framework/Wolf2009Streams.h"
#ifdef WOLF2009_SOURCE_MAP_RENDER_CATALOG
extern bool Wolf2009_LoadMaterialCatalog(const char*);
#endif
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
wolf2009::SupportGameplayContentsPolicy MapPolicy(const std::string& map){
    // Session checks a .map before calling game::InitFromNewMap. Select the
    // explicit data profile in that early phase too; a game-side cvar set
    // would happen after the first map parse and cannot make startup work.
    // The descriptor is the only map-profile selector: a collision override
    // must not bypass the material catalog needed by those map entities.
    std::unique_ptr<idFile,void(*)(idFile*)> config(fileSystem->OpenFileRead("wolf2009-session.cfg",false),Close);
    if(!config)return wolf2009::SupportGameplayContentsPolicy::DeclaredDoom3BitsV1;
    const int length=config->Length();if(length<=0||length>32768)throw std::runtime_error("Source map descriptor byte budget");
    std::vector<char> bytes(std::size_t(length)+1,0);if(config->Read(bytes.data(),length)!=length||std::memchr(bytes.data(),0,std::size_t(length)))throw std::runtime_error("Invalid source map descriptor bytes");
    idLexer lexer(LEXFL_NOERRORS|LEXFL_NOFATALERRORS|LEXFL_NOWARNINGS|LEXFL_NOSTRINGCONCAT|LEXFL_ALLOWPATHNAMES);idToken token;
    if(!lexer.LoadMemory(bytes.data(),length,"wolf2009-session.cfg")||!lexer.ReadToken(&token)||token!="Wolf2009SourceSession"||!lexer.ReadToken(&token)||token!="1"||!lexer.ReadToken(&token)||token!="{")throw std::runtime_error("Source map descriptor signature");
    idDict fields;for(;;){if(!lexer.ReadToken(&token))throw std::runtime_error("Source map descriptor unterminated");if(token=="}")break;
        if(token.type!=TT_STRING||fields.GetNumKeyVals()>=32)throw std::runtime_error("Source map descriptor key budget/type");const idStr key=token;
        if(fields.FindKey(key)||!lexer.ReadToken(&token)||token.type!=TT_STRING)throw std::runtime_error("Source map descriptor duplicate/key value type");fields.Set(key,token);}
    if(lexer.ReadToken(&token)||lexer.HadError())throw std::runtime_error("Trailing source map descriptor data");
    if(idStr::Cmp(fields.GetString("mapEntitiesProfile",""),"MeasuredTrainyardIntroV1")==0&&
        idStr::Icmp(fields.GetString("map",""),map.c_str())==0&&idStr::Cmp(fields.GetString("policy",""),"NativePlayerOnlyV1")==0&&
        idStr::Cmp(fields.GetString("sourceMapName",""),"maps/game/trainyard/trainyard")==0){
#ifdef WOLF2009_SOURCE_MAP_RENDER_CATALOG
        const char* archive=fields.GetString("materialArchive","");
        if(!archive||!*archive||!Wolf2009_LoadMaterialCatalog(archive))throw std::runtime_error("Source map material catalog refused before model imports");
#else
        throw std::runtime_error("Source map entity profile requires the source renderer");
#endif
        return wolf2009::SupportGameplayContentsPolicy::NativeSourceWordsCylinderEnvelopeV1;
    }
    return wolf2009::SupportGameplayContentsPolicy::DeclaredDoom3BitsV1;
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
    const auto policy=MapPolicy(canonicalMapName);
    result.plan=wolf2009::PrepareSupportGameplayMap(hkxs,ents,canonicalMapName,result.lease->Epoch(),std::string(),policy);
    return result;
}
EngineSourceBody Wolf2009_LoadSupportBody(const wolf2009::SourceCollisionBodyRequest& request){
    using namespace wolf2009;
    // Validate canonical bounded request before selecting a mount or reading.
    ParseSourceCollisionBodyRequestPath(SourceCollisionBodyRequestPath(request));
    EngineSourceBody result;const auto end=request.resourcePath.find('/',9);
    result.source.lease.reset(new EngineSupportMountLease(request.resourcePath.substr(9,end-9)));
    if(request.expectedEpoch&&request.expectedEpoch!=result.source.lease->Epoch())throw std::runtime_error("Source body mount epoch changed");
    const auto bytes=Read(request.resourcePath);
    if(request.expectedBytes&&request.expectedBytes!=bytes.size())throw std::runtime_error("Source body byte identity changed");
    const auto inventory=ReadSourceMapBodyInventory(bytes);std::size_t matches=0;
    for(const auto& body:inventory.bodies)if(body.hasName&&body.rawName==request.bodyName)++matches;
    if(matches!=1)throw std::runtime_error("Exact source body is absent/ambiguous");
    if(request.expectedFingerprint&&request.expectedFingerprint!=inventory.sourceFingerprint)throw std::runtime_error("Source body fingerprint changed");
    auto& plan=result.source.plan;
    plan.contentsPolicy=request.policy==SourceCollisionBodyPolicy::DeclaredDoom3BitsV1?SupportGameplayContentsPolicy::DeclaredDoom3BitsV1:
        request.policy==SourceCollisionBodyPolicy::NativeSolidEntityCylinderEnvelopeV1?SupportGameplayContentsPolicy::NativeSolidEntityCylinderEnvelopeV1:
        SupportGameplayContentsPolicy::NativeSourceWordsCylinderEnvelopeV1;
    plan.nativeCylinderEnvelopeResponse=request.policy!=SourceCollisionBodyPolicy::DeclaredDoom3BitsV1;
    plan.world=DecodeSelectedSupportGameplayBody(bytes,request.bodyName,plan.contentsPolicy,true);
    plan.responses=AdmitSelectedSupportGameplayBody(plan.world,plan.contentsPolicy);
    auto& binding=result.binding;binding.request=request;binding.epoch=result.source.lease->Epoch();
    binding.bytes=bytes.size();binding.fingerprint=inventory.sourceFingerprint;binding.shapes=plan.world.shapes.size();binding.corePoints=plan.world.totalCorePoints;
    for(const auto& response:plan.responses)binding.nativeContents|=response.contents;
    plan.identity=SerializeSourceCollisionBodyBinding(binding);return result;
}
