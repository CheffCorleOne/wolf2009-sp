// SPDX-License-Identifier: GPL-3.0-or-later
// Adapter only: actual collision, physics and entity behavior run in the GPL
// Doom3 game module. Original executable/DLL code is never loaded or linked.
#include "GameplayMap.h"
#include "NativeCollisionMesh.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "EngineGameplay.h"
#include "EngineRoundedCollision.h"

namespace {
const char nativeCollisionFixturePath[]="wolf2009/_qa/native-collision.cm";
const char nativeCollisionFixtureName[]="wolf2009/_qa/native-collision";
bool nativeCollisionFixtureEnabled=false;
bool NativeCollisionFixturePath(const char *path) {
    return nativeCollisionFixtureEnabled&&path&&idStr::Cmp(path,nativeCollisionFixturePath)==0;
}
wolf2009::NativeCollisionMeshFiles NativeCollisionFixture() {
    wolf2009::NativeCollisionMesh mesh;
    // Authored finite surfaces, independent of retail/render geometry:
    // two-triangle floor, right triangle, player-only wall, sloping quad.
    mesh.vertices={
        {-64,-64,0},{64,-64,0},{64,64,0},{-64,64,0},
        {128,0,16},{192,0,16},{128,64,16},
        {160,-64,0},{160,-64,96},{160,64,96},{160,64,0},
        {-64,160,0},{64,160,32},{64,224,32},{-64,224,0},
        {320,-64,0},{448,-64,0},{448,64,0},{320,64,0}
    };
    const unsigned vertices[][3]={{0,1,2},{0,2,3},{4,5,6},{7,8,9},{7,9,10},{11,12,13},{11,13,14},
                                 {15,16,17},{15,17,18}};
    const unsigned contents[]={3,3,16,8,8,1,1,1,1};
    for(std::size_t i=0;i<sizeof(contents)/sizeof(contents[0]);++i) {
        wolf2009::NativeCollisionTriangle t;
        for(int k=0;k<3;++k)t.vertex[k]=vertices[i][k];
        t.contents=contents[i];t.sides=i>=7?wolf2009::NativeCollisionSides::Both:wolf2009::NativeCollisionSides::Front;
        mesh.triangles.push_back(t);
    }
    // LoadModel does not supply a map geometry CRC. Zero is explicit here;
    // this authored fixture makes no source-provenance or map-load claim.
    const auto files=wolf2009::BuildNativeCollisionMesh(mesh,nativeCollisionFixtureName,0);
    if(files.vertices!=19||files.polygons!=11||files.internalEdges!=5)
        throw std::runtime_error("Native collision fixture topology changed");
    common->Printf("WOLF2009_NATIVE_COLLISION_RESOURCE vertices=%u polygons=%u edges=%u internal=%u map_crc=0\n",
        unsigned(files.vertices),unsigned(files.polygons),unsigned(files.edges),unsigned(files.internalEdges));
    return files;
}
void Close(idFile *f) { if (f) fileSystem->CloseFile(f); }
std::vector<unsigned char> ReadSource(const std::string &path) {
    std::unique_ptr<idFile,void(*)(idFile *)> file(fileSystem->OpenFileRead(path.c_str(),false),Close);
    if (!file) throw std::runtime_error("Same-mount gameplay source is unavailable");
    const int length = file->Length();
    if (length <= 0 || length > (64 << 20)) throw std::runtime_error("Gameplay source size limit");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if (file->Read(bytes.data(),length) != length) throw std::runtime_error("Short gameplay source read");
    return bytes;
}
class GameplayFile : public idFile {
    std::string name, text;
    std::size_t position = 0;
  public:
    GameplayFile(std::string path, std::string content) : name(std::move(path)), text(std::move(content)) {
        if (text.size() > INT_MAX) throw std::runtime_error("Generated map exceeds idFile limit");
    }
    const char *GetName() override { return name.c_str(); }
    const char *GetFullPath() override { return name.c_str(); }
    int Length() override { return static_cast<int>(text.size()); }
    int Tell() override { return static_cast<int>(position); }
    ID_TIME_T Timestamp() override { return 0; }
    int Read(void *out, int count) override {
        if (!out || count <= 0) return 0;
        const auto n = (std::min)(std::size_t(count),text.size()-position);
        if (n) std::memcpy(out,text.data()+position,n);
        position += n;
        return static_cast<int>(n);
    }
    int Write(const void *, int) override { return 0; }
    void ForceFlush() override {}
    void Flush() override {}
    int Seek(long offset, fsOrigin_t origin) override {
        const long long base = origin == FS_SEEK_SET ? 0 : origin == FS_SEEK_CUR ?
            static_cast<long long>(position) : origin == FS_SEEK_END ? static_cast<long long>(text.size()) : -1;
        if (base < 0 || offset < -base || offset > static_cast<long long>(text.size())-base) return -1;
        position = static_cast<std::size_t>(base+offset);
        return 0;
    }
};
}
bool Wolf2009_IsGameplayMapPath(const char *path) {
    if (!path) return false;
    if (Wolf2009_IsRoundedCollisionFixturePath(path)) return true;
    if (NativeCollisionFixturePath(path)) return true;
    wolf2009::GameplayMapSourcePaths sources;
    return wolf2009::ResolveGameplayMapPath(path,sources);
}
idFile *Wolf2009_OpenGameplayMap(const char *path) {
    try {
        if (Wolf2009_IsRoundedCollisionFixturePath(path))
            return Wolf2009_OpenRoundedCollisionFixturePath(path);
        if (NativeCollisionFixturePath(path))
            return new GameplayFile(path,NativeCollisionFixture().cmText);
        wolf2009::GameplayMapSourcePaths sources;
        if (!path || !wolf2009::ResolveGameplayMapPath(path,sources)) return NULL;
        const auto marker = sources.collision.find("/HKXS/");
        const auto resource = sources.collision.substr(marker+6,sources.collision.size()-(marker+6)-5);
        const auto collision = wolf2009::DecodeStaticCollision(ReadSource(sources.collision),resource);
        const auto entities = wolf2009::DecodeEntities4(ReadSource(sources.entities));
        const auto map = wolf2009::BuildStaticGameplayMap(collision,entities);
        const auto files = wolf2009::EncodeGameplayMap(map);
        common->Printf("Wolf2009 gameplay map: %u exact world boxes, %u spawn, %u unimplemented ENTS objects\n",
            unsigned(files.worldBoxes),unsigned(files.entities-1),unsigned(map.unimplementedEntities));
        return new GameplayFile(path,files.mapText);
    } catch (const std::exception &error) {
        common->Warning("Wolf2009 gameplay map refused: %s",error.what());
        return NULL;
    }
}
void Wolf2009_EnableNativeCollisionFixture(bool enabled) {
    nativeCollisionFixtureEnabled=enabled;
}
