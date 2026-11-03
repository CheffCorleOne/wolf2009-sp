// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinnedModelData.h"
#include "StreamMounts.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
namespace {
using namespace wolf2009;
using namespace wolf2009::idtech4a;
unsigned checks = 0;
void Check(bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
void Reject(const std::function<void()>& action) {
    bool refused = false; try { action(); } catch (const std::exception&) { refused = true; }
    Check(refused, "invalid skinned adapter input accepted");
}
SkinnedModelRequest Request(std::size_t lod = 0) {
    return MakeSkinnedModelRequest("wolf2009/own/modl/models/own/body", "wolf2009/own/hkxa/maps/own/clip",
                                   "wolf2009/global/skel/maps/own/skeleton", lod);
}
GreeterJointPose Joint(double x, double y, double z, unsigned axis, double radians) {
    GreeterJointPose out; out.translation = {{x,y,z}};
    out.rotation[axis] = std::sin(radians/2); out.rotation[3] = std::cos(radians/2); return out;
}
struct Fixture {
    GreeterSkinGeometry source;
    std::vector<GreeterJoint> skeleton;
    Fixture() : skeleton(52) {
        source.levelOfDetail = 0;
        for (unsigned i = 0; i < 52; ++i) {
            skeleton[i].name = "own_joint_" + std::to_string(i); skeleton[i].parent = i ? 0 : -1;
            source.jointNames.push_back(skeleton[i].name);
            source.bindModelJoints.push_back(Joint(i % 4, i % 3, 2, i % 3, i * .03));
        }
        source.bindGeometry.vertices = {{1,2,3,.25f,.75f,0x11223344u}, {4,5,6,.5f,.125f,0x78563412u},
                                         {-1,3,7,1,.5f,0xffabcdefu}, {99,99,99,0,0,0x00112233u}};
        source.influences.resize(4);
        for (unsigned v = 0; v < 4; ++v) {
            source.influences[v].joints = {{0,1,2,3}}; source.influences[v].weights = {{.1,.2,.3,.4}};
            source.influences[v].mirroredTangent = v == 0;
        }
        GeometrySurface a; a.model = "own_body"; a.material = "own/vertex-color"; a.indices = {2,0,1};
        GeometrySurface b = a; b.material = "own/second"; b.indices = {1,2,0};
        source.bindGeometry.surfaces = {a,b}; source.bindGeometry.models = 1;
    }
};
double Compare(const SkinnedModelData& prepared, const GreeterSkinGeometry& source, const GreeterPose& pose) {
    const auto reference = PoseGreeterBody(source, pose);
    double error = 0;
    for (std::size_t s = 0; s < prepared.surfaces.size(); ++s) {
        const auto actual = SkinPreparedSurface(prepared, s, pose.model);
        Check(actual.size() == prepared.surfaces[s].sourceVertices.size(), "compacted pose vertex count changed");
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto& expected = reference.vertices[prepared.surfaces[s].sourceVertices[i]];
            for (unsigned axis = 0; axis < 3; ++axis) {
                const double wanted = axis == 0 ? expected.x : axis == 1 ? expected.y : expected.z;
                error = (std::max)(error, std::abs(actual[i][axis] - wanted));
            }
        }
    }
    // Float joint-local offsets introduce a separate, measured conversion error.
    Check(error <= 2e-4, "native-weight preparation diverged from source skinning"); return error;
}
void SelfTest() {
    Fixture f; const auto model = PrepareSkinnedModel(Request(), f.source, f.skeleton);
    Check(model.joints.size() == 52 && model.surfaces.size() == 2, "prepared skeleton/draw count");
    Check(model.surfaces[0].sourceVertices == std::vector<std::uint32_t>({2,0,1}) &&
          model.surfaces[0].geometry.indices == std::vector<std::uint32_t>({0,1,2}), "source draw order/compaction");
    Check(model.surfaces[0].geometry.vertices[1].diffuseColor == 0x11223344u &&
          model.surfaces[0].geometry.vertices[1].u == .25f && model.surfaces[0].geometry.vertices[1].v == .75f &&
          model.surfaces[0].mirroredTangents[1] == 1, "raw RGBA/UV/handedness lost during preparation");
    Check(model.surfaces[1].geometry.material == "own/second" && model.surfaces[1].sourceVertices[0] == 1, "draw material identity mixed");
    DiffuseMaterial modulate; modulate.supported = true; modulate.nativeVertexColor = true;
    Check(NativeDiffuseColor(modulate, model.surfaces[0].geometry.vertices[1].diffuseColor) ==
          std::array<unsigned char,4>{{0x44,0x33,0x22,0x11}}, "prepared raw colour did not reach the native RGBA contract");
    GreeterPose bind; bind.model = f.source.bindModelJoints; Compare(model, f.source, bind);
    GreeterPose moved = bind;
    moved.model[0] = Joint(10,20,30,2,1.5707963267948966); moved.model[1] = Joint(-3,7,2,0,-.7);
    Compare(model, f.source, moved);
    const auto first = SkinPreparedSurface(model, 0, bind.model);
    const auto second = SkinPreparedSurface(model, 0, moved.model);
    Check(first != second && first == SkinPreparedSurface(model, 0, bind.model), "two supplied-pose instances shared mutable skinning state");
    for (unsigned i = 0; i < 52; ++i) {
        Check(model.joints[i].name == f.skeleton[i].name && model.joints[i].parent == f.skeleton[i].parent, "joint hierarchy identity changed");
    }
    SkinnedModelRequests requests; const auto name = requests.Register(Request());
    Check(name == requests.Register(Request()) && HasResourceType(name.c_str(), "MD5R"), "registered skinned request is unstable");
    SkinnedModelRequest found; Check(requests.Find(name, found) && SameSkinnedModelRequest(found, Request()), "request lookup changed complete source identity");
    auto altered = Request(); altered.skeleton = "wolf2009/other/skel/maps/own/skeleton";
    Check(name != requests.Register(altered) && name != requests.Register(Request(1)), "skeleton source/LOD absent from model cache key");
    altered = Request(); altered.animation = "wolf2009/other/hkxa/maps/own/clip";
    Check(name != requests.Register(altered), "animation source absent from model cache key");
    altered = Request(); altered.model = "wolf2009/other/modl/models/own/body";
    Check(name != requests.Register(altered), "MODL archive identity absent from model cache key");
    altered = Request(); altered.sourceEpochs = {{1,2,3}};
    const auto epochName = requests.Register(altered);
    Check(name != epochName, "runtime mount epochs absent from model cache key");
    for (unsigned i = 0; i < 3; ++i) {
        auto newer = altered; ++newer.sourceEpochs[i];
        Check(epochName != requests.Register(newer), "one source remount reused an earlier model alias");
    }
    requests.Clear(); Check(!requests.Find(name, found), "shutdown retained a stale skinned request");
    Reject([] { MakeSkinnedModelRequest("wolf2009/a/proc/a", "wolf2009/a/hkxa/b", "wolf2009/a/skel/c", 0); });
    Reject([] { Request((std::numeric_limits<std::size_t>::max)()); });
    for (unsigned mutation = 0; mutation < 11; ++mutation) {
        Fixture bad;
        if (mutation == 0) bad.skeleton[1].parent = 2;
        if (mutation == 1) bad.skeleton[1].name = bad.skeleton[0].name;
        if (mutation == 2) bad.source.jointNames[1] = "wrong";
        if (mutation == 3) bad.source.influences[0].weights[0] = -.1;
        if (mutation == 4) bad.source.influences[0].joints[0] = 65535;
        if (mutation == 5) bad.source.bindGeometry.surfaces[0].indices[0] = 999;
        if (mutation == 6) bad.source.bindModelJoints[0].rotation[3] = 2;
        if (mutation == 7) bad.source.bindGeometry.vertices[0].x = (std::numeric_limits<float>::quiet_NaN)();
        if (mutation == 8) bad.source.levelOfDetail = 1;
        if (mutation == 9) bad.source.influences.pop_back();
        if (mutation == 10) bad.source.bindGeometry.surfaces[0].indices.pop_back();
        Reject([&] { PrepareSkinnedModel(Request(), bad.source, bad.skeleton); });
    }
    SkinnedModelLimits small; small.vertices = 5;
    Reject([&] { PrepareSkinnedModel(Request(), f.source, f.skeleton, small); });
    Reject([&] { SkinPreparedSurface(model, 0, std::vector<GreeterJointPose>(1)); });
    Reject([&] { SkinPreparedSurface(model, model.surfaces.size(), bind.model); });
    auto corrupt = model; corrupt.surfaces[0].weights[0][0].jointLocalPosition[0] = (std::numeric_limits<float>::quiet_NaN)();
    Reject([&] { SkinPreparedSurface(corrupt, 0, bind.model); });
    corrupt = model; corrupt.surfaces[0].weights[0][0].weight = .9f;
    Reject([&] { SkinPreparedSurface(corrupt, 0, bind.model); });
    bool read = false;
    Reject([&] { LoadSkinnedModel(Request(), [&](const std::string&) { read = true; return Bytes(); }); });
    Check(read, "registered source load never read its original MODL");
    f.source.bindGeometry.skippedMeshes = 7;
    Check(PrepareSkinnedModel(Request(), f.source, f.skeleton).otherLodMeshes == 7, "authored excluded LOD count lost");
    std::cout << "WOLF2009_SKINNED_SYNTHETIC checks=" << checks << " joints=52 draws=2 native_runtime=0\n";
}
bool Ends(const std::string& text, const char* tail) {
    const std::string suffix(tail); return text.size() >= suffix.size() && text.compare(text.size()-suffix.size(), suffix.size(), suffix) == 0;
}
void Source(const std::string& base) {
    const std::map<std::string,std::string> packages{{"main", "maps/game/trainyard/trainyard.mpk"},
        {"global", "maps/game/global/global.spk"}, {"character", "streampacks/allies_kreisau_agt02.spk"}};
    StreamMounts mounts([&](const std::string& label) {
        return std::unique_ptr<std::istream>(new std::ifstream(base + "/" + packages.at(label), std::ios::binary));
    });
    for (const auto& entry : packages) mounts.Mount(entry.first);
    const ResourceReader read = [&](const std::string& path) {
        const auto source = ParseResourcePath(path); std::vector<StreamResource> matches;
        const auto collect = [&](const std::string& name) {
            for (const auto& found : mounts.Find(source.type, name).matches) if (found.mountId == source.mount) matches.push_back(found);
        };
        collect(source.name);
        if (source.type == "modl" && Ends(source.name, ".md5r")) collect(source.name.substr(0, source.name.size()-5));
        Check(matches.size() == 1, "private source resource absent/ambiguous in selected mount"); return mounts.Read(matches.front());
    };
    const std::string animationPath = "wolf2009/main/hkxa/maps/game/trainyard/trainyard";
    const std::string skeletonPath = "wolf2009/global/skel/maps/game/global/global";
    const auto animationBytes = read(animationPath), skeletonBytes = read(skeletonPath);
    const auto clip = GreeterAnimation::Load(animationBytes, skeletonBytes);
    std::string previous;
    const std::array<std::pair<const char*,std::size_t>,3> selections{{{"main",0},{"character",0},{"character",1}}};
    for (const auto& selection : selections) {
        const char* label = selection.first;
        const auto request = MakeSkinnedModelRequest(std::string("wolf2009/") + label + "/modl/models/characters/civilians/civ_male_04/anims/body_cm04.md5r", animationPath, skeletonPath, selection.second);
        const auto data = LoadSkinnedModel(request, read);
        const auto body = DecodeGreeterBody1(read(request.model), ParseResourcePath(request.model).name, clip.Joints(), request.levelOfDetail);
        Check(data.joints.size() == 52 && data.modelSource.bytes && data.modelSource.path == request.model &&
              data.animationSource.path == animationPath && data.skeletonSource.path == skeletonPath, "loaded source identity not retained");
        GreeterPose bind; bind.model = body.bindModelJoints;
        double error = Compare(data, body, bind);
        for (double seconds : {0.0, .5 * clip.FrameDuration(), .271 * clip.Duration(), .5 * clip.Duration(), clip.Duration()}) {
            error = (std::max)(error, Compare(data, body, clip.SamplePose(seconds)));
        }
        const auto reload = LoadSkinnedModel(request, read);
        Check(data.modelSource.fingerprint == reload.modelSource.fingerprint && data.modelSource.bytes == reload.modelSource.bytes &&
              SkinnedModelCacheName(request) == SkinnedModelCacheName(reload.request), "reload changed immutable source/key");
        Check(previous.empty() || previous != SkinnedModelCacheName(request), "two source archives shared a model-manager key"); previous = SkinnedModelCacheName(request);
        std::size_t vertices = 0, indices = 0;
        for (const auto& draw : data.surfaces) { vertices += draw.geometry.vertices.size(); indices += draw.geometry.indices.size(); }
        std::cout << "WOLF2009_SKINNED_SOURCE source=" << label << " lod=" << selection.second << " joints=" << data.joints.size() << " draws=" << data.surfaces.size()
                  << " vertices=" << vertices << " triangles=" << indices/3 << " other_lod_meshes=" << data.otherLodMeshes << " pose_checks=6 max_position_error=" << error
                  << " native_runtime=0\n";
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--self-test")) SelfTest();
        else if (argc == 3 && std::string(argv[1]) == "--source-base") Source(argv[2]);
        else throw std::runtime_error("usage: wolf2009-skinned-model [--self-test | --source-base SP/base]");
        return 0;
    } catch (const std::exception& error) { std::cerr << "WOLF2009_SKINNED_FAIL " << error.what() << '\n'; return 1; }
}
