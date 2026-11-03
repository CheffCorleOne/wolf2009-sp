// SPDX-License-Identifier: GPL-3.0-or-later
// Core half of the API8-safe readonly animation descriptor/lease interface.
#include "AnimationWire.h"
#include "EngineSkinnedModelData.h"
#include <atomic>
#include <climits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include "idlib/precompiled.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "framework/Wolf2009Streams.h"
#include "EngineSkinnedModel.h"
#include "EngineAnimationBinding.h"
namespace {
namespace wire = wolf2009::animation;
namespace render = wolf2009::idtech4a;
void Need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Registration { wire::Binding binding; render::SkinnedModelRequest request; wire::Bytes metadata; };
std::map<std::string, Registration> bindings;
std::mutex bindingMutex;
std::atomic<unsigned> openDescriptors(0);
bool commandsRegistered = false;
class File {
    idFile* file;
  public:
    explicit File(const char* path) : file(fileSystem->OpenFileRead(path,false)) {}
    ~File() { if (file) fileSystem->CloseFile(file); }
    wire::Bytes Read() const {
        Need(file != NULL, "animation binding source is absent from its selected mount");
        const int length = file->Length(); Need(length > 0 && std::size_t(length) <= wire::MaximumSourceBytes, "animation binding source byte budget");
        wire::Bytes out(static_cast<std::size_t>(length));
        Need(file->Read(out.data(),length) == length, "short animation binding source idFile read"); return out;
    }
};
class Leases {
    std::map<std::string,unsigned long long> values;
  public:
    ~Leases() { for (const auto& value : values) Wolf2009_UnpinMount(value.first.c_str(),value.second); }
    void Pin(const wire::Binding& binding) {
        for (unsigned i = 0; i < 3; ++i) {
            const auto mount = render::ParseResourcePath(binding.paths[i]).mount;
            const auto prior = values.find(mount);
            if (prior != values.end()) { Need(prior->second == binding.sources[i].epoch, "animation binding shared-mount epoch mismatch"); continue; }
            unsigned long long epoch = 0;
            Need(Wolf2009_PinMount(mount.c_str(),epoch), "animation binding source could not be pinned");
            if (epoch != binding.sources[i].epoch) { Wolf2009_UnpinMount(mount.c_str(),epoch); throw std::runtime_error("stale animation binding epoch"); }
            try { values.insert(std::make_pair(mount,epoch)); } catch (...) { Wolf2009_UnpinMount(mount.c_str(),epoch); throw; }
        }
    }
    void Check() const {
        for (const auto& value : values) {
            unsigned long long epoch = 0;
            Need(Wolf2009_MountEpoch(value.first.c_str(),epoch) && epoch == value.second, "animation descriptor source epoch changed while pinned");
        }
    }
};
wire::Bytes ReadChecked(const wire::Binding& b, unsigned index) {
    wire::Bytes bytes = File(b.paths[index].c_str()).Read(); const auto& identity = b.sources[index];
    Need(bytes.size() == identity.bytes && wire::Fingerprint(bytes.data(),bytes.size()) == identity.fingerprint,
         "animation binding source byte identity changed"); return bytes;
}
void CheckModel(const Registration& registration) {
    const auto& b = registration.binding;
    idRenderModel* model = renderModelManager->CheckModel(b.modelAlias.c_str());
    Need(model && model->IsLoaded() && !model->IsDefaultModel() && model->IsDynamicModel() == DM_CACHED && model->NumJoints() == 52 &&
         idStr::Icmp(model->Name(),b.modelAlias.c_str()) == 0, "animation binding model alias is not the qualified native source model");
    auto* md5 = dynamic_cast<idRenderModelMD5*>(model); render::SkinnedModelSourceInfo info;
    Need(md5 && Wolf2009_GetSkinnedModelSources(md5,info) && render::SameSkinnedModelRequest(info.request,registration.request),
         "animation binding model request differs from its resident source identity");
    const std::array<render::SkinnedSourceIdentity,3> source{{info.model,info.animation,info.skeleton}};
    for (unsigned i = 0; i < 3; ++i)
        Need(source[i].path == b.paths[i] && source[i].bytes == b.sources[i].bytes && source[i].fingerprint == b.sources[i].fingerprint &&
             info.request.sourceEpochs[i] == b.sources[i].epoch, "animation binding current model source identity mismatch");
}
class BindingFile : public idFile {
    std::string name;
    wire::Binding request;
    wire::Bytes bytes;
    std::unique_ptr<Leases> leases;
    std::size_t position = 0;
  public:
    BindingFile(std::string n, wire::Binding b, wire::Bytes data, std::unique_ptr<Leases> l)
        : name(std::move(n)), request(std::move(b)), bytes(std::move(data)), leases(std::move(l)) { ++openDescriptors; }
    ~BindingFile() override { --openDescriptors; }
    const char* GetName() override { return name.c_str(); }
    const char* GetFullPath() override { return name.c_str(); }
    int Length() override { return int(bytes.size()); }
    int Tell() override { return int(position); }
    ID_TIME_T Timestamp() override { return 0; }
    int Read(void* destination, int count) override {
        if (!destination || count <= 0) return 0;
        const std::size_t take = (std::min)(std::size_t(count),bytes.size()-position);
        if (take) memcpy(destination,bytes.data()+position,take); position += take; return int(take);
    }
    int Seek(long offset, fsOrigin_t origin) override {
        const long long base = origin == FS_SEEK_SET ? 0 : origin == FS_SEEK_CUR ? static_cast<long long>(position) :
                               origin == FS_SEEK_END ? static_cast<long long>(bytes.size()) : -1;
        if (base < 0 || offset < -base || offset > static_cast<long long>(bytes.size())-base) return -1;
        position = std::size_t(base+offset); return 0;
    }
};
void Register_f(const idCmdArgs& args) {
    if (args.Argc() != 2) { common->Printf("usage: wolf2009SourceAnimation <registered native model alias>\n"); return; }
    idStr alias;
    const bool ok = Wolf2009_RegisterSourceAnimation(args.Argv(1),alias);
    common->Printf("WOLF2009_SOURCE_ANIMATION registered=%d alias=%s\n",int(ok),alias.c_str());
}
}
bool Wolf2009_RegisterSourceAnimation(const char* modelAlias, idStr& alias) {
    alias.Clear();
    try {
        std::lock_guard<std::mutex> lock(bindingMutex);
        Need(modelAlias && renderModelManager, "missing animation source model alias");
        render::SkinnedModelRequest request;
        Need(Wolf2009_GetSkinnedModelRequest(modelAlias,request), "animation source model is not registered");
        idRenderModel* model = renderModelManager->CheckModel(modelAlias);
        auto* md5 = dynamic_cast<idRenderModelMD5*>(model); render::SkinnedModelSourceInfo info;
        Need(md5 && md5->IsLoaded() && !md5->IsDefaultModel() && Wolf2009_GetSkinnedModelSources(md5,info) &&
             render::SameSkinnedModelRequest(info.request,request), "animation registration requires a qualified source model");
        Registration value; value.request = request; auto& b = value.binding;
        b.modelAlias = render::SkinnedModelCacheName(request); b.paths = {{request.model,request.animation,request.skeleton}};
        const std::array<render::SkinnedSourceIdentity,3> ids{{info.model,info.animation,info.skeleton}};
        for (unsigned i = 0; i < 3; ++i) { b.sources[i].epoch = request.sourceEpochs[i]; b.sources[i].bytes = ids[i].bytes; b.sources[i].fingerprint = ids[i].fingerprint; }
        Leases leases; leases.Pin(b);
        ReadChecked(b,0); const auto hkxa = ReadChecked(b,1), skel = ReadChecked(b,2);
        const auto clip = wolf2009::GreeterAnimation::Load(hkxa,skel);
        b.duration = clip.Duration(); b.frameDuration = clip.FrameDuration(); b.frames = std::uint32_t(clip.FrameCount());
        b.joints = std::uint32_t(clip.Joints().size()); b.clipName = clip.ResourceName(); b.skeletonName = clip.SkeletonName();
        value.metadata = wire::Metadata(b); const std::string name = wire::Alias(b);
        const auto prior = bindings.find(name);
        Need(prior == bindings.end() || prior->second.metadata == value.metadata, "animation binding alias collision with different full metadata");
        Need(prior != bindings.end() || bindings.size() < 4096, "animation binding registration budget");
        CheckModel(value); leases.Check(); bindings.insert(std::make_pair(name,std::move(value)));
        alias = name.c_str(); return true;
    } catch (const std::exception& error) { common->Warning("Wolf2009 source animation registration: %s",error.what()); return false; }
}
bool Wolf2009_RemoveAnimationBinding(const char* name) {
    if (!name || !Wolf2009_IsSourceAnimationAlias(name)) return false;
    try {
        std::lock_guard<std::mutex> lock(bindingMutex);
        const auto path = render::ParseResourcePath(name);
        return bindings.erase(path.Path(path.type,path.name)) != 0;
    } catch (const std::exception&) { return false; }
}
idFile* Wolf2009_OpenSourceAnimation(const char* name) {
    try {
        std::lock_guard<std::mutex> lock(bindingMutex);
        Need(name && Wolf2009_IsSourceAnimationAlias(name), "not a source animation descriptor namespace");
        const auto path = render::ParseResourcePath(name); const std::string canonical = path.Path(path.type,path.name);
        const auto found = bindings.find(canonical);
        Need(found != bindings.end(), "unknown or stale source animation binding");
        const Registration registration = found->second;
        std::unique_ptr<Leases> leases(new Leases); leases->Pin(registration.binding);
        ReadChecked(registration.binding,0);
        wire::Envelope envelope; envelope.binding = registration.binding;
        envelope.animation = ReadChecked(registration.binding,1); envelope.skeleton = ReadChecked(registration.binding,2);
        CheckModel(registration); leases->Check();
        wire::Bytes bytes = wire::Serialize(envelope); Need(bytes.size() <= INT_MAX, "animation descriptor native idFile size budget");
        return new BindingFile(canonical,registration.binding,std::move(bytes),std::move(leases));
    } catch (const std::exception& error) { common->Warning("Wolf2009 source animation descriptor: %s",error.what()); return NULL; }
}
void Wolf2009_InitAnimationBindings() {
    std::lock_guard<std::mutex> lock(bindingMutex);
    if (!commandsRegistered) { cmdSystem->AddCommand("wolf2009SourceAnimation",Register_f,CMD_FL_RENDERER,"register a leased original source animation binding"); commandsRegistered = true; }
}
void Wolf2009_ShutdownAnimationBindings() {
    std::lock_guard<std::mutex> lock(bindingMutex);
    if (commandsRegistered) { cmdSystem->RemoveCommand("wolf2009SourceAnimation"); commandsRegistered = false; }
    bindings.clear(); // Existing idFiles own their requests, bytes and leases.
}
unsigned Wolf2009_OpenAnimationDescriptors() { return openDescriptors.load(); }
