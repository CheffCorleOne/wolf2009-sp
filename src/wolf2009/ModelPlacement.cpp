// SPDX-License-Identifier: GPL-3.0-or-later
#include "ModelPlacement.h"
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
namespace wolf2009 {
namespace {
void Need(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string Fold(std::string name) {
    for (std::size_t i = 0; i < name.size(); ++i)
        if (name[i] >= 'A' && name[i] <= 'Z')
            name[i] = char(name[i] - 'A' + 'a');
    return name;
}
std::string Text(const EntityRecord &entity, const char *key) {
    const EntityProperty *p = entity.Find(key);
    if (!p)
        return {};
    Need(p->kind == EntityProperty::Text, "Expected textual model-placement property");
    return p->text;
}
std::string VirtualName(std::string name) {
    Need(!name.empty() && name.size() <= 4096, "Invalid model-placement virtual name length");
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        Need(c >= 32 && c != 127 && c != ':', "Invalid model-placement virtual name byte");
        if (name[i] == '\\')
            name[i] = '/';
    }
    Need(name[0] != '/' && name[name.size() - 1] != '/', "Absolute/empty model-placement virtual path");
    std::size_t start = 0;
    while (start < name.size()) {
        const std::size_t end = name.find('/', start),
                          count = end == std::string::npos ? name.size() - start : end - start;
        const std::string component = name.substr(start, count);
        Need(!component.empty() && component != "." && component != "..",
             "Unsafe model-placement virtual path component");
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return Fold(name);
}
std::string ResourceKey(const std::string &name, const char *extension) {
    std::string key = VirtualName(name);
    const std::string suffix = extension;
    if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0)
        key.resize(key.size() - suffix.size());
    return key;
}
bool IsAnimation(const std::string &key) {
    if (key == "anim" || key == "start_anim" || key == "frame" || key == "num_anims" ||
        key == "loop_last_anim" || key == "auto_advance")
        return true;
    if (key.size() <= 4 || key.compare(0, 4, "anim") != 0)
        return false;
    for (std::size_t i = 4; i < key.size(); ++i)
        if (key[i] < '0' || key[i] > '9')
            return false;
    return true;
}
bool RigidMatrix(const float *r) {
    for (int i = 0; i < 9; ++i)
        if (!std::isfinite(r[i]))
            return false;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            double dot = 0;
            for (int k = 0; k < 3; ++k)
                dot += double(r[a * 3 + k]) * r[b * 3 + k];
            if (std::abs(dot - (a == b ? 1 : 0)) > 1e-4)
                return false;
        }
    const double determinant = double(r[0]) * (double(r[4]) * r[8] - double(r[5]) * r[7]) -
                               double(r[1]) * (double(r[3]) * r[8] - double(r[5]) * r[6]) +
                               double(r[2]) * (double(r[3]) * r[7] - double(r[4]) * r[6]);
    return std::abs(determinant - 1) <= 1e-4;
}
bool Identity(const float *r) {
    for (int i = 0; i < 9; ++i)
        if (std::abs(r[i] - (i % 4 == 0 ? 1 : 0)) > 1e-6)
            return false;
    return true;
}
struct Location {
    std::size_t chunk, entry;
    Location(std::size_t c = 0, std::size_t e = 0) : chunk(c), entry(e) {}
};
Location FindResource(const Archive &archive, const char *type, const std::string &wanted,
                      const char *extension) {
    std::size_t count = 0;
    Location found;
    const std::vector<Chunk> &chunks = archive.Chunks();
    for (std::size_t c = 0; c < chunks.size(); ++c)
        if (chunks[c].type == type)
            for (std::size_t e = 0; e < chunks[c].entries.size(); ++e)
                if (ResourceKey(chunks[c].entries[e].name, extension) == wanted) {
                    found = Location(c, e);
                    ++count;
                }
    Need(count != 0, "Selected model-placement resource is missing from archive");
    Need(count == 1, "Selected model-placement resource is ambiguous in archive");
    return found;
}
} // namespace
PlacedStaticModel ApplyAuthoredModelPlacement(const GeometryScene &local, const EntityRecord &entity,
                                              const std::string &resourceName,
                                              const ModelPlacementOptions &options) {
    PlacedStaticModel out;
    out.authored = ReadEntityPlacement(entity);
    Need(!out.authored.model.empty(), "Model-placement entity has no explicit model resource");
    out.resourceName = ResourceKey(resourceName, ".md5r");
    Need(ResourceKey(out.authored.model, ".md5r") == out.resourceName,
         "Selected model resource differs from entity model");
    Need(out.authored.hasOrigin, "Model-placement entity has no explicit origin");
    for (int i = 0; i < 3; ++i)
        Need(std::isfinite(out.authored.origin[i]), "Non-finite model-placement origin");
    Need(options.rotation == ModelRotationConvention::IdentityOnly ||
             options.rotation == ModelRotationConvention::BasisRows,
         "Unknown model-placement rotation convention");
    if (out.authored.hasRotation) {
        Need(RigidMatrix(out.authored.rotation), "Model placement requires a proper rigid rotation matrix");
        out.rotationSource = ModelRotationSource::ExplicitMatrix;
        out.nonIdentityRotation = !Identity(out.authored.rotation);
        out.angleSupersededByMatrix = out.authored.hasAngle;
        Need(!out.nonIdentityRotation || options.rotation == ModelRotationConvention::BasisRows,
             "Nonidentity model rotation needs explicit BasisRows convention");
    } else if (out.authored.hasAngle) {
        Need(std::isfinite(out.authored.angle) && out.authored.angle == 0,
             "Angle-only nonzero model rotation is unsupported");
        out.rotationSource = ModelRotationSource::ZeroAngle;
    }
    out.skin = Text(entity, "skin");
    out.bind = Text(entity, "bind");
    out.brain = Text(entity, "brain");
    out.unresolvedSkin = !out.skin.empty();
    out.unresolvedParent = !out.bind.empty();
    out.unresolvedBehavior = !out.brain.empty();
    for (std::size_t i = 0; i < entity.properties.size(); ++i) {
        const std::string key = Fold(entity.properties[i].key);
        const bool parent =
            key == "bind" || key == "bindtojoint" || key == "bindtobody" || key == "bindorientated";
        const bool animation = IsAnimation(key),
                   behavior = key == "brain" || key == "scriptobject" || key == "script";
        if (parent || animation || behavior)
            out.dynamicPropertyKeys.push_back(entity.properties[i].key);
        if (parent && key != "bind")
            out.unresolvedParent = true;
        if (animation)
            out.unresolvedAnimation = true;
        if (behavior && key != "brain")
            out.unresolvedBehavior = true;
    }
    Need(!local.vertices.empty() && !local.surfaces.empty(), "Selected static model has no render geometry");
    Need(local.vertices.size() <= options.geometry.vertices &&
             local.surfaces.size() <= options.geometry.surfaces && local.models <= options.geometry.models,
         "Placed model exceeds configured geometry count limits");
    std::size_t triangles = 0;
    for (std::size_t i = 0; i < local.surfaces.size(); ++i) {
        const GeometrySurface &surface = local.surfaces[i];
        Need(surface.indices.size() % 3 == 0, "Malformed selected model triangle list");
        const std::size_t count = surface.indices.size() / 3;
        Need(triangles <= options.geometry.triangles && count <= options.geometry.triangles - triangles,
             "Placed model exceeds triangle limit");
        triangles += count;
        for (std::size_t j = 0; j < surface.indices.size(); ++j)
            Need(surface.indices[j] < local.vertices.size(), "Selected model triangle index out of range");
    }
    Need(triangles != 0, "Selected static model has no render triangles");
    out.geometry = local;
    for (std::size_t i = 0; i < out.geometry.vertices.size(); ++i) {
        GeometryVertex &v = out.geometry.vertices[i];
        Need(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.u) &&
                 std::isfinite(v.v),
             "Non-finite selected model vertex");
        const double xyz[3] = {v.x, v.y, v.z};
        float *components[] = {&v.x, &v.y, &v.z};
        for (int axis = 0; axis < 3; ++axis) {
            double world = out.authored.origin[axis];
            for (int k = 0; k < 3; ++k)
                world += xyz[k] * out.authored.rotation[k * 3 + axis];
            Need(std::isfinite(world) && std::abs(world) <= (std::numeric_limits<float>::max)(),
                 "Placed model vertex exceeds finite float range");
            *components[axis] = static_cast<float>(world);
        }
    }
    return out;
}
PlacedStaticModel LoadPlacedStaticModel1(const Archive &archive, std::istream &input,
                                         const EntityDocument &entities, const std::string &entityName,
                                         const ModelPlacementOptions &options) {
    Need(!entityName.empty(), "Explicit entity name is required for model placement");
    const std::string wanted = Fold(entityName);
    std::size_t selected = 0, matches = 0;
    for (std::size_t e = 0; e < entities.entities.size(); ++e)
        if (Fold(Text(entities.entities[e], "name")) == wanted) {
            selected = e;
            ++matches;
        }
    Need(matches != 0, "Selected model-placement entity is missing");
    Need(matches == 1, "Selected model-placement entity name is ambiguous");
    const EntityRecord &entity = entities.entities[selected];
    const std::string key = ResourceKey(Text(entity, "model"), ".md5r");
    const Location model = FindResource(archive, "MODL", key, ".md5r");
    const std::vector<unsigned char> bytes = archive.ReadEntry(input, model.chunk, model.entry);
    const BufferResolver resolver = [&](const std::string &name) {
        const Location buffer = FindResource(archive, "PBBF", ResourceKey(name, ".md5rbin"), ".md5rbin");
        return archive.ReadEntry(input, buffer.chunk, buffer.entry);
    };
    const GeometryScene local = DecodeStaticModel1(bytes, key, resolver, options.geometry);
    PlacedStaticModel out = ApplyAuthoredModelPlacement(local, entity, key, options);
    out.entityIndex = selected;
    if (!out.bind.empty()) {
        std::size_t parents = 0;
        for (std::size_t e = 0; e < entities.entities.size(); ++e)
            if (Fold(Text(entities.entities[e], "name")) == Fold(out.bind)) {
                out.parentEntityIndex = e;
                ++parents;
            }
        out.parentRecordFound = parents == 1;
        out.parentRecordAmbiguous = parents > 1;
        if (!out.parentRecordFound)
            out.parentEntityIndex = static_cast<std::size_t>(-1);
    }
    return out;
}
} // namespace wolf2009
