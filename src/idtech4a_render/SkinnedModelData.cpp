// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinnedModelData.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#ifdef __FAST_MATH__
#error Skinned_model_admission_requires_finite_IEEE_arithmetic
#endif
namespace wolf2009 {
namespace idtech4a {
namespace {
void Need(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::string SourcePath(const std::string& value, const char* type) {
    ResourcePath path = ParseResourcePath(value);
    Need(path.type == type, "skinned request has an incorrect source FourCC");
    return path.Path(type, path.name);
}
SkinnedModelRequest Checked(const SkinnedModelRequest& input) {
    Need(input.levelOfDetail <= 255, "skinned request LOD exceeds bounded profile");
    SkinnedModelRequest out;
    out.model = SourcePath(input.model, "modl");
    out.animation = SourcePath(input.animation, "hkxa");
    out.skeleton = SourcePath(input.skeleton, "skel");
    out.levelOfDetail = input.levelOfDetail;
    out.sourceEpochs = input.sourceEpochs;
    return out;
}
std::uint64_t Hash(const unsigned char* bytes, std::size_t count, std::uint64_t value = UINT64_C(14695981039346656037)) {
    for (std::size_t i = 0; i < count; ++i) value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    return value;
}
std::string Hex(std::uint64_t value) {
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[std::size_t(i)] = "0123456789abcdef"[value & 15]; value >>= 4; }
    return out;
}
std::string Decimal(std::size_t value) {
    std::string out;
    do { out.push_back(char('0' + value % 10)); value /= 10; } while (value);
    std::reverse(out.begin(), out.end());
    return out;
}
void Pose(const GreeterJointPose& pose) {
    double length = 0;
    for (double value : pose.translation) Need(std::isfinite(value) && std::abs(value) <= 1e8, "invalid skinned joint translation");
    for (double value : pose.rotation) { Need(std::isfinite(value), "invalid skinned joint rotation"); length += value * value; }
    Need(std::abs(length - 1) <= 1e-5, "skinned joint quaternion must be unit");
}
std::array<double, 3> Rotate(const std::array<double, 4>& q, const std::array<double, 3>& value) {
    const std::array<double, 3> t{{2 * (q[1]*value[2] - q[2]*value[1]),
                                  2 * (q[2]*value[0] - q[0]*value[2]),
                                  2 * (q[0]*value[1] - q[1]*value[0])}};
    return {{value[0] + q[3]*t[0] + q[1]*t[2] - q[2]*t[1],
             value[1] + q[3]*t[1] + q[2]*t[0] - q[0]*t[2],
             value[2] + q[3]*t[2] + q[0]*t[1] - q[1]*t[0]}};
}
GreeterJointPose Relative(const GreeterJointPose& parent, const GreeterJointPose& child) {
    GreeterJointPose out;
    const auto& p = parent.rotation; const auto& c = child.rotation;
    const std::array<double, 4> inv{{-p[0], -p[1], -p[2], p[3]}};
    out.translation = Rotate(inv, {{child.translation[0] - parent.translation[0], child.translation[1] - parent.translation[1], child.translation[2] - parent.translation[2]}});
    out.rotation = {{inv[3]*c[0]+inv[0]*c[3]+inv[1]*c[2]-inv[2]*c[1],
                     inv[3]*c[1]-inv[0]*c[2]+inv[1]*c[3]+inv[2]*c[0],
                     inv[3]*c[2]+inv[0]*c[1]-inv[1]*c[0]+inv[2]*c[3],
                     inv[3]*c[3]-inv[0]*c[0]-inv[1]*c[1]-inv[2]*c[2]}};
    Pose(out);
    return out;
}
SkinnedSourceIdentity Identity(const std::string& path, const Bytes& bytes) {
    SkinnedSourceIdentity out; out.path = path; out.bytes = bytes.size(); out.fingerprint = Hash(bytes.data(), bytes.size()); return out;
}
}
SkinnedModelRequest MakeSkinnedModelRequest(const std::string& model, const std::string& animation,
                                           const std::string& skeleton, std::size_t lod) {
    SkinnedModelRequest out; out.model = model; out.animation = animation; out.skeleton = skeleton; out.levelOfDetail = lod; return Checked(out);
}
bool SameSkinnedModelRequest(const SkinnedModelRequest& a, const SkinnedModelRequest& b) {
    return a.model == b.model && a.animation == b.animation && a.skeleton == b.skeleton && a.levelOfDetail == b.levelOfDetail && a.sourceEpochs == b.sourceEpochs;
}
std::string SkinnedModelCacheName(const SkinnedModelRequest& input) {
    const auto request = Checked(input);
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const std::string& field : {request.model, request.animation, request.skeleton, Decimal(request.levelOfDetail),
                                    Hex(request.sourceEpochs[0]), Hex(request.sourceEpochs[1]), Hex(request.sourceEpochs[2])}) {
        const std::string prefix = Decimal(field.size()) + ":";
        hash = Hash(reinterpret_cast<const unsigned char*>(prefix.data()), prefix.size(), hash);
        hash = Hash(reinterpret_cast<const unsigned char*>(field.data()), field.size(), hash);
    }
    const auto source = ParseResourcePath(request.model);
    return source.Prefix() + "md5r/lod" + Decimal(request.levelOfDetail) + "/" + Hex(hash) + "/" + source.name;
}
std::string SkinnedModelRequests::Register(const SkinnedModelRequest& input) {
    const auto request = Checked(input); const auto name = SkinnedModelCacheName(request);
    const auto found = requests_.find(name);
    Need(found == requests_.end() || SameSkinnedModelRequest(found->second, request), "skinned cache hash collision with a different complete request");
    Need(found != requests_.end() || requests_.size() < 4096, "skinned request registration budget");
    requests_.insert(std::make_pair(name, request));
    return name;
}
bool SkinnedModelRequests::Find(const std::string& name, SkinnedModelRequest& request) const {
    const auto source = ParseResourcePath(name);
    const std::string canonical = source.Path(source.type, source.name);
    const auto found = requests_.find(canonical);
    if (found == requests_.end()) return false;
    request = found->second; return true;
}
static PreparedSkinnedGeometry PrepareGeometry(const std::string& modelPath,std::size_t lod,const GreeterSkinGeometry& source,
                                     const std::vector<GreeterJoint>& skeleton, const SkinnedModelLimits& limits) {
    Need(source.sourceLodDistances.size()<=32&&source.sourceMeshLods.size()<=limits.surfaces,
         "skinned source LOD metadata budget");
    PreparedSkinnedGeometry out;out.modelPath=modelPath;out.levelOfDetail=lod;
    out.lodPolicy=source.lodPolicy;out.sourceHasLodRecords=source.sourceHasLodRecords;
    out.selectionUsesCompatibilitySlot=source.selectionUsesCompatibilitySlot;
    out.sourceLodDistances=source.sourceLodDistances;out.sourceMeshLods=source.sourceMeshLods;
    const std::size_t count = skeleton.size();
    Need(count && count <= limits.joints && count <= 65536 && source.bindModelJoints.size() == count &&
         source.jointNames.size() == count && source.influences.size() == source.bindGeometry.vertices.size(), "skinned source/skeleton array mismatch or budget");
    Need(source.levelOfDetail == lod &&
         source.bindGeometry.vertices.size() <= limits.vertices && source.bindGeometry.surfaces.size() <= limits.surfaces,
         "skinned source LOD/buffer budget");
    out.otherLodMeshes = source.bindGeometry.skippedMeshes;
    std::set<std::string> names;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& joint = skeleton[i];
        Need(!joint.name.empty() && joint.name.size() <= 255 && names.insert(joint.name).second && joint.name == source.jointNames[i] &&
             (i ? joint.parent >= 0 && std::size_t(joint.parent) < i : joint.parent == -1), "skinned joint identity/hierarchy differs");
        Pose(source.bindModelJoints[i]);
        SkinnedJoint next; next.name = joint.name; next.parent = joint.parent; next.bindModel = source.bindModelJoints[i];
        next.bindLocal = i ? Relative(source.bindModelJoints[std::size_t(joint.parent)], next.bindModel) : next.bindModel;
        out.joints.push_back(next);
    }
    std::size_t totalVertices = 0, totalIndices = 0;
    const auto unused = (std::numeric_limits<std::uint32_t>::max)();
    Need(source.bindGeometry.vertices.size() < unused, "skinned source vertex index overflow");
    std::vector<std::uint32_t> remap(source.bindGeometry.vertices.size(), unused), touched;
    for (const auto& surface : source.bindGeometry.surfaces) {
        Need(!surface.indices.empty() && surface.indices.size() % 3 == 0 && surface.indices.size() <= limits.indices - totalIndices,
             "skinned draw is empty/non-triangular or exceeds index budget");
        totalIndices += surface.indices.size();
        SkinnedSurface next; next.geometry.model = surface.model; next.geometry.material = surface.material;
        for (std::uint32_t index : surface.indices) {
            Need(index < source.bindGeometry.vertices.size(), "skinned draw index outside source buffer");
            if (remap[index] == unused) {
                Need(totalVertices < limits.vertices, "compacted skinned vertex budget"); ++totalVertices;
                remap[index] = static_cast<std::uint32_t>(next.geometry.vertices.size()); touched.push_back(index);
                const auto& vertex = source.bindGeometry.vertices[index]; const auto& influence = source.influences[index];
                for (float value : {vertex.x, vertex.y, vertex.z, vertex.u, vertex.v}) Need(std::isfinite(value), "nonfinite skinned source vertex");
                std::array<SkinnedWeight, 4> weights;
                double total = 0;
                for (unsigned c = 0; c < 4; ++c) {
                    const auto joint = influence.joints[c]; const double weight = influence.weights[c];
                    Need(joint < count && std::isfinite(weight) && weight >= 0 && weight <= 1, "invalid skinned source influence"); total += weight;
                    const auto& bind = source.bindModelJoints[joint]; const auto& q = bind.rotation;
                    const auto local = Rotate({{-q[0], -q[1], -q[2], q[3]}}, {{vertex.x-bind.translation[0], vertex.y-bind.translation[1], vertex.z-bind.translation[2]}});
                    weights[c].joint = joint; weights[c].weight = float(weight);
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        weights[c].jointLocalPosition[axis] = float(local[axis]);
                        Need(std::isfinite(weights[c].jointLocalPosition[axis]), "skinned local weight offset exceeds float range");
                    }
                }
                Need(std::abs(total - 1) <= 1e-6, "skinned influence weights do not sum to one");
                next.geometry.vertices.push_back(vertex); next.weights.push_back(weights); next.sourceVertices.push_back(index);
                next.mirroredTangents.push_back(influence.mirroredTangent ? 1 : 0);
            }
            next.geometry.indices.push_back(remap[index]);
        }
        out.surfaces.push_back(std::move(next));
        for (std::uint32_t index : touched) remap[index] = unused;
        touched.clear();
    }
    Need(!out.surfaces.empty(), "skinned model contains no render draws");
    return out;
}
SkinnedModelData PrepareSkinnedModel(const SkinnedModelRequest& input,const GreeterSkinGeometry& source,
    const std::vector<GreeterJoint>& skeleton,const SkinnedModelLimits& limits){
    SkinnedModelData out;out.request=Checked(input);
    static_cast<PreparedSkinnedGeometry&>(out)=PrepareGeometry(out.request.model,out.request.levelOfDetail,source,skeleton,limits);return out;
}
SkinnedModelData LoadSkinnedModel(const SkinnedModelRequest& input, const ResourceReader& read, const SkinnedModelLimits& limits) {
    const auto request = Checked(input); Need(bool(read), "skinned model has no source reader");
    const auto get = [&](const std::string& path) { Bytes bytes = read(path); Need(!bytes.empty() && bytes.size() <= limits.sourceBytes, "skinned source byte budget"); return bytes; };
    const Bytes model = get(request.model), animation = get(request.animation), skeleton = get(request.skeleton);
    const auto clip = GreeterAnimation::Load(animation, skeleton);
    GeometryLimits geometry; geometry.sourceBytes = limits.sourceBytes; geometry.vertices = limits.vertices; geometry.surfaces = limits.surfaces;
    const auto body = DecodeGreeterBody1(model, ParseResourcePath(request.model).name, clip.Joints(), request.levelOfDetail, geometry);
    auto out = PrepareSkinnedModel(request, body, clip.Joints(), limits);
    out.modelSource = Identity(request.model, model); out.animationSource = Identity(request.animation, animation); out.skeletonSource = Identity(request.skeleton, skeleton);
    return out;
}
std::vector<std::array<float, 3>> SkinPreparedSurface(const PreparedSkinnedGeometry& model, std::size_t surface,
                                                    const std::vector<GreeterJointPose>& joints) {
    Need(surface < model.surfaces.size() && joints.size() == model.joints.size(), "skinned supplied pose/surface mismatch");
    for (const auto& joint : joints) Pose(joint);
    const auto& source = model.surfaces[surface];
    Need(source.weights.size() == source.geometry.vertices.size(), "skinned prepared weight array mismatch");
    std::vector<std::array<float, 3>> out;
    for (const auto& weights : source.weights) {
        std::array<double, 3> value{{0, 0, 0}};
        double total = 0;
        for (const auto& weight : weights) {
            Need(weight.joint < joints.size() && std::isfinite(weight.weight) && weight.weight >= 0 && weight.weight <= 1, "invalid prepared skinned weight");
            total += weight.weight;
            for (float component : weight.jointLocalPosition) Need(std::isfinite(component), "invalid prepared skinned offset");
            const auto& joint = joints[weight.joint];
            const auto local = Rotate(joint.rotation, {{weight.jointLocalPosition[0], weight.jointLocalPosition[1], weight.jointLocalPosition[2]}});
            for (unsigned axis = 0; axis < 3; ++axis) value[axis] += double(weight.weight) * (local[axis] + joint.translation[axis]);
        }
        Need(std::abs(total - 1) <= 1e-5, "prepared skinned weights do not sum to one");
        out.push_back({{float(value[0]), float(value[1]), float(value[2])}});
    }
    return out;
}
std::vector<std::array<float,3>> SkinPreparedSurface(const SkinnedModelData& model,std::size_t surface,const std::vector<GreeterJointPose>& joints){
    return SkinPreparedSurface(static_cast<const PreparedSkinnedGeometry&>(model),surface,joints);
}
namespace {
SkinnedMeshRequestV2 CheckedV2(SkinnedMeshRequestV2 r){
    Need(r.levelOfDetail<=255,"mesh v2 LOD budget");r.model=SourcePath(r.model,"modl");r.skeletonContainer=SourcePath(r.skeletonContainer,"skel");
    Need(!r.skeletonName.empty()&&r.skeletonName.size()<=4096,"mesh v2 exact skeleton name budget");
    for(unsigned char c:r.skeletonName)Need(c>=32&&c<=126,"mesh v2 skeleton name byte");
    Need(r.geometryProfile==SkinnedVertexProfile::Weighted64||r.geometryProfile==SkinnedVertexProfile::WeightedOrRigid64,"mesh v2 declaration profile");
    Need(r.lodPolicy==SkinnedLodPolicy::Authored||r.lodPolicy==SkinnedLodPolicy::SingleResourceWithoutLodRecords||
        r.lodPolicy==SkinnedLodPolicy::RootAuthoredMeshUnspecifiedSlotV1,"mesh v2 LOD policy");
    Need(r.lodPolicy==SkinnedLodPolicy::Authored||r.levelOfDetail==0,"mesh v2 compatibility policy requires slot0");
    Need(ParseResourcePath(r.model).mount!=ParseResourcePath(r.skeletonContainer).mount||r.sourceEpochs[0]==r.sourceEpochs[1],"mesh v2 shared mount has inconsistent epochs");return r;
}
}
SkinnedMeshRequestV2 MakeSkinnedMeshRequestV2(const std::string& model,const std::string& container,const std::string& name,std::size_t lod,SkinnedVertexProfile profile){
    return MakeSkinnedMeshRequestV2(model,container,name,lod,profile,SkinnedLodPolicy::Authored);
}
SkinnedMeshRequestV2 MakeSkinnedMeshRequestV2(const std::string& model,const std::string& container,const std::string& name,std::size_t lod,SkinnedVertexProfile profile,SkinnedLodPolicy lodPolicy){
    SkinnedMeshRequestV2 r;r.model=model;r.skeletonContainer=container;r.skeletonName=name;r.levelOfDetail=lod;r.geometryProfile=profile;r.lodPolicy=lodPolicy;return CheckedV2(r);
}
bool SameSkinnedMeshRequestV2(const SkinnedMeshRequestV2& a,const SkinnedMeshRequestV2& b){return a.model==b.model&&a.skeletonContainer==b.skeletonContainer&&a.skeletonName==b.skeletonName&&a.levelOfDetail==b.levelOfDetail&&a.geometryProfile==b.geometryProfile&&a.lodPolicy==b.lodPolicy&&a.sourceEpochs==b.sourceEpochs;}
std::string SkinnedMeshCacheNameV2(const SkinnedMeshRequestV2& value){
    const auto r=CheckedV2(value);std::uint64_t hash=UINT64_C(14695981039346656037);
    // Preserve the published authored domain exactly. Other policies cannot
    // alias it, and registry collision checks still compare the complete key.
    const std::string domain=r.lodPolicy==SkinnedLodPolicy::Authored?"mesh-binding-v2":
        r.lodPolicy==SkinnedLodPolicy::SingleResourceWithoutLodRecords?"mesh-binding-v2-single-resource-without-lod-records":
        "mesh-binding-v2-root-authored-mesh-unspecified-slot-v1";
    for(const std::string& field:{domain,r.model,r.skeletonContainer,r.skeletonName,Decimal(r.levelOfDetail),Decimal(std::size_t(r.geometryProfile)),Hex(r.sourceEpochs[0]),Hex(r.sourceEpochs[1])}){
        const std::string prefix=Decimal(field.size())+":";hash=Hash(reinterpret_cast<const unsigned char*>(prefix.data()),prefix.size(),hash);hash=Hash(reinterpret_cast<const unsigned char*>(field.data()),field.size(),hash);}
    const auto p=ParseResourcePath(r.model);return p.Prefix()+"md5r/v2/lod"+Decimal(r.levelOfDetail)+"/"+Hex(hash)+"/"+p.name;
}
std::string SkinnedMeshRequestsV2::Register(const SkinnedMeshRequestV2& value){const auto r=CheckedV2(value);const auto name=SkinnedMeshCacheNameV2(r);const auto prior=requests_.find(name);
    Need(prior==requests_.end()||SameSkinnedMeshRequestV2(prior->second,r),"mesh v2 full-key hash collision");Need(prior!=requests_.end()||requests_.size()<4096,"mesh v2 registration budget");requests_.insert(std::make_pair(name,r));return name;}
bool SkinnedMeshRequestsV2::Find(const std::string& alias,SkinnedMeshRequestV2& out)const{const auto p=ParseResourcePath(alias);const auto found=requests_.find(p.Path(p.type,p.name));if(found==requests_.end())return false;out=found->second;return true;}
SkinnedMeshDataV2 LoadSkinnedMeshV2(const SkinnedMeshRequestV2& value,const ResourceReader& read,const SkinnedModelLimits& limits){
    const auto r=CheckedV2(value);Need(bool(read),"mesh v2 source reader absent");const auto get=[&](const std::string& path){Bytes b=read(path);Need(!b.empty()&&b.size()<=limits.sourceBytes,"mesh v2 source byte budget");return b;};
    const Bytes model=get(r.model),skel=get(r.skeletonContainer);SkeletonDataLimits l;l.sourceBytes=limits.sourceBytes;l.joints=limits.joints;
    SkinnedMeshDataV2 out;out.request=r;out.skeleton=ReadSkeletonData(skel,r.skeletonName,l);
    // Existing conversion takes this value type, but consumes only identity.
    // Reference poses remain exclusively in out.skeleton with qualification;
    // they are never substituted for the model's own bind transforms.
    std::vector<GreeterJoint> topology;topology.reserve(out.skeleton.joints.size());
    for(const auto& joint:out.skeleton.joints){GreeterJoint identity;identity.name=joint.name;identity.parent=joint.parent;topology.push_back(std::move(identity));}
    GeometryLimits g;g.sourceBytes=limits.sourceBytes;g.vertices=limits.vertices;g.surfaces=limits.surfaces;
    const auto source=DecodeWeightedModel1(model,ParseResourcePath(r.model).name,topology,r.levelOfDetail,r.geometryProfile,r.lodPolicy,g);
    static_cast<PreparedSkinnedGeometry&>(out)=PrepareGeometry(r.model,r.levelOfDetail,source,topology,limits);
    out.modelSource=Identity(r.model,model);out.skeletonSource=Identity(r.skeletonContainer,skel);return out;
}
}
}
