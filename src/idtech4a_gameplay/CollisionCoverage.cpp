// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only inventory of existing decoder coverage. This tool deliberately
// does not infer collision from render geometry, decode new shape payloads or
// reinterpret additional rigid bodies as live entities.
#include "GameplayMap.h"
#include "HavokPack.h"
#include "MapPhysics.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
std::string Json(const std::string &s) {
    static const char hex[]="0123456789abcdef";
    std::string out="\"";
    for(const unsigned char c:s) {
        if(c=='"' || c=='\\') { out+='\\'; out+=char(c); }
        else if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}
template<class K> void Counts(std::ostream &out,const std::map<K,std::size_t> &values) {
    out << '{'; bool first=true;
    for(const auto &item:values) {
        if(!first) out << ','; first=false;
        std::ostringstream key; key << item.first;
        out << Json(key.str()) << ':' << item.second;
    }
    out << '}';
}
std::string Lower(std::string s) {
    for(char &c:s) { if(c>='A'&&c<='Z')c=char(c-'A'+'a'); if(c=='\\')c='/'; }
    return s;
}
void Report(const char *file,std::ostream &out) {
    using namespace wolf2009;
    std::ifstream input(file,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot open map package");
    const auto archive=Archive::Scan(input);
    std::string name;
    for(const auto &chunk:archive.Chunks()) if(chunk.type=="PROC") for(const auto &entry:chunk.entries) {
        if(!name.empty()) throw std::runtime_error("Ambiguous map resource"); name=entry.name;
    }
    if(name.empty()) throw std::runtime_error("Map PROC absent");
    const auto bytes=ReadMapResource(input,archive,name,"HKXS",64u<<20);
    const detail::HavokPack pack(bytes,64u<<20,100000,1000000);
    std::map<std::string,std::size_t> objectTypes,bodyGroups;
    std::size_t storageParts=0,storageTriangles=0,placedParts=0,transformedParts=0,roundedBoxes=0;
    std::uint32_t worldBody=0;
    std::size_t worldMatches=0;
    for(const auto &object:pack.objects) {
        ++objectTypes[object.second];
        if(object.second=="hkpRigidBody") {
            const auto pointer=pack.pointers.find(object.first+112);
            const std::string body=pointer==pack.pointers.end()?"":Lower(pack.DataString(pointer->second));
            ++bodyGroups[body.empty()?"unnamed":body.compare(0,7,"models/")==0?"model_named":
                body.compare(0,5,"maps/")==0?"map_named":"other_named"];
            if(body==Lower(name)) {worldBody=object.first;++worldMatches;}
        }
        if(object.second=="hkpBoxShape" && std::abs(pack.F(object.first+16))>1e-7) ++roundedBoxes;
        if(object.second!="hkpStorageExtendedMeshShape") continue;
        const std::size_t triangles=pack.DU(object.first+152),parts=pack.DU(object.first+164);
        if(triangles>100000 || parts>100000) throw std::runtime_error("Storage inventory count limit");
        storageParts+=triangles; placedParts+=parts;
        if(triangles) {
            const auto array=pack.Pointer(object.first+148); pack.ArrayRange(array,triangles,64);
            for(std::size_t i=0;i<triangles;++i) storageTriangles+=pack.DU(array+i*64+16);
        }
        if(parts) {
            const auto array=pack.Pointer(object.first+160); pack.ArrayRange(array,parts,64);
            for(std::size_t i=0;i<parts;++i) {
                const auto sub=array+i*64;
                bool identity=true;
                for(int k=0;k<3;++k)
                    identity=identity && std::abs(pack.F(sub+32+k*4))<=1e-6 &&
                                         std::abs(pack.F(sub+48+k*4))<=1e-6;
                identity=identity && std::abs(std::abs(pack.F(sub+44))-1)<=1e-6;
                transformedParts+=!identity;
            }
        }
    }
    out << "{\"map\":" << Json(name) << ",\"hkxsBytes\":" << bytes.size()
        << ",\"allSourceBodyGroups\":"; Counts(out,bodyGroups);
    out << ",\"allSourceObjectTypes\":"; Counts(out,objectTypes);
    out << ",\"allSourceTriangleSubparts\":" << storageParts
        << ",\"allSourceTriangles\":" << storageTriangles
        << ",\"allSourcePlacedSubparts\":" << placedParts
        << ",\"allSourceNonIdentityPlacedSubparts\":" << transformedParts
        << ",\"allSourceRoundedBoxes\":" << roundedBoxes;
    if(worldMatches==1) out << ",\"worldBodyContents\":" << pack.DU(worldBody+44);
    try {
        const auto scene=DecodeStaticCollision(bytes,name);
        std::map<std::uint32_t,std::size_t> contents;
        std::size_t unknown=0;
        for(const auto &box:scene.boxes) {++contents[box.contents];unknown+=!box.contentsKnown;}
        const auto tri=scene.unsupported.find("storage-triangle-subpart");
        out << ",\"world\":{\"boxes\":" << scene.boxes.size()
            << ",\"geometryComplete\":" << (scene.coverageComplete?"true":"false")
            << ",\"contentsComplete\":" << (scene.contentsComplete?"true":"false")
            << ",\"identityPoseVerified\":" << (scene.identityBodyPoseVerified?"true":"false")
            << ",\"otherSourceRigidBodies\":" << scene.otherRigidBodies
            << ",\"visitedShapes\":" << scene.visitedShapes
            << ",\"triangleSubparts\":" << (tri==scene.unsupported.end()?0:tri->second)
            << ",\"omittedTriangles\":" << scene.omittedTriangles
            << ",\"omittedTransformedSubparts\":" << scene.omittedTransformedSubparts
            << ",\"unknownBoxContents\":" << unknown << ",\"boxContents\":";
        Counts(out,contents);
        out << ",\"unsupported\":"; Counts(out,scene.unsupported);
        bool accepted=false; std::string refusal;
        try {ImportGameplayWorld(scene);accepted=true;} catch(const std::exception &e){refusal=e.what();}
        out << ",\"exactBoxBridgeAccepted\":" << (accepted?"true":"false")
            << ",\"bridgeRefusal\":" << Json(refusal) << "}";
    } catch(const std::exception &e) {
        out << ",\"worldDecodeError\":" << Json(e.what());
    }
    out << '}';
}
}
int main(int argc,char **argv) {
    if(argc<2) {std::cerr << "wolf2009-collision-coverage INPUT.mpk [INPUT2.mpk ...]\n";return 2;}
    int failures=0;
    std::cout << "{\"schema\":1,\"scope\":\"world decoder coverage and source-only typed metadata; no live entity state\",\"maps\":[\n";
    for(int i=1;i<argc;++i) {
        if(i>1) std::cout << ",\n";
        try {std::ostringstream row;Report(argv[i],row);std::cout << row.str();}
        catch(const std::exception &e) {++failures;std::cout << "{\"argument\":" << i
            << ",\"error\":" << Json(e.what()) << "}";}
        std::cerr << "Collision coverage " << i << '/' << argc-1 << '\n';
    }
    std::cout << "\n],\"archiveErrors\":" << failures << "}\n";
    return failures?1:0;
}
