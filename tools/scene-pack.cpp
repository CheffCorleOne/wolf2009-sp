// SPDX-License-Identifier: GPL-3.0-or-later
// Local diagnostic export of user-supplied game data. Never ship the output.
#include "Archive.h"
#include "Geometry.h"
#include "ZipArchive.h"
#include "Material.h"
#include "Dds.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
std::string Key(std::string value){for(char& c:value){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c=char(c+32);}return value;}
bool Ends(const std::string& s,const char* end){const std::string suffix(end);return s.size()>=suffix.size()&&s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0;}
std::string Canonical(const std::string& path,bool directory){
#ifdef _WIN32
    HANDLE handle=CreateFileA(path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL,NULL);
    Need(handle!=INVALID_HANDLE_VALUE,"Cannot resolve existing path");char text[32768];DWORD count=GetFinalPathNameByHandleA(handle,text,sizeof(text),FILE_NAME_NORMALIZED);CloseHandle(handle);
    Need(count>0&&count<sizeof(text),"Cannot canonicalize path");std::string out(text,count);if(out.compare(0,4,"\\\\?\\")==0)out.erase(0,4);return out;
#else
    (void)directory;char* p=realpath(path.c_str(),NULL);Need(p!=NULL,"Cannot resolve existing path");std::string out(p);free(p);return out;
#endif
}
void NewDirectory(const std::string& path){
#ifdef _WIN32
    Need(_mkdir(path.c_str())==0,"Output directory must be new; existing directories are never modified");
#else
    Need(mkdir(path.c_str(),0755)==0,"Output directory must be new; existing directories are never modified");
#endif
}
std::string NewOutput(const std::string& source,const std::string& requested){
    const std::size_t slash=requested.find_last_of("/\\");const std::string leaf=slash==std::string::npos?requested:requested.substr(slash+1);
    Need(!leaf.empty()&&leaf!="."&&leaf!=".."&&leaf.find(':')==std::string::npos,"Invalid output directory name");
    const std::string parent=Canonical(slash==std::string::npos?".":requested.substr(0,slash+1),true),out=parent+"/"+leaf;
    const std::string input=Key(Canonical(source,false)),output=Key(out);const std::size_t base=input.find("/sp/base/");
    if(base!=std::string::npos){const std::string installation=input.substr(0,base);Need(output.compare(0,installation.size()+1,installation+"/")!=0&&output!=installation,"Output must be outside the original installation");}
    return out;
}
class Output {
    FILE* file_;
public:
    explicit Output(const std::string& path):file_(std::fopen(path.c_str(),"wbx")){Need(file_!=NULL,"Cannot exclusively create output");}
    ~Output(){if(file_)std::fclose(file_);}
    FILE* Get(){return file_;}
    void Finish(){FILE* f=file_;file_=NULL;Need(std::fclose(f)==0,"Output close failed");}
    void Write(const Bytes& data){Need(std::fwrite(data.data(),1,data.size(),file_)==data.size(),"Output write failed");}
};
struct Location{std::size_t chunk,entry;};
struct Material{wolf2009::MaterialResult binding;std::string texture;std::size_t id;};
struct FaceSurface{const wolf2009::GeometrySurface* source;std::size_t material;std::vector<std::size_t> uv;};
struct UV{float u,v;};
}
int main(int argc,char** argv){
    if(argc!=4){std::cerr<<"wolf2009-pack INPUT.mpk ASSETS.pk4 NEW_OUTPUT_DIRECTORY\nLocal OBJ/MTL/DDS diagnostic export. Output contains original game data; do not redistribute.\n";return 2;}
    try{
        const std::string output=NewOutput(argv[1],argv[3]);
        std::ifstream input(argv[1],std::ios::binary),assets(argv[2],std::ios::binary);Need(bool(input)&&bool(assets),"Cannot open MPK or assets PK4");
        const wolf2009::Archive archive=wolf2009::Archive::Scan(input);
        const wolf2009::ZipArchive zip=wolf2009::ZipArchive::Scan(assets);
        wolf2009::MaterialCatalog catalog;
        for(int pass=0;pass<2;++pass)for(std::size_t i=0;i<zip.Entries().size();++i){
            const std::string name=Key(zip.Entries()[i].name);if(!Ends(name,pass==0?".guide":".mtr"))continue;
            const Bytes bytes=zip.ReadEntry(assets,i);const std::string text(bytes.begin(),bytes.end());
            if(pass==0)catalog.AddGuideSource(name,text);else catalog.AddMaterialSource(name,text);
        }
        std::map<std::string,Location> buffers,textures;Location proc={0,0};std::string procName;std::size_t procs=0;
        for(std::size_t c=0;c<archive.Chunks().size();++c){const wolf2009::Chunk& chunk=archive.Chunks()[c];
            if(chunk.type!="PROC"&&chunk.type!="PBBF"&&chunk.type!="TXTR")continue;
            for(std::size_t e=0;e<chunk.entries.size();++e){const Location loc={c,e};
                if(chunk.type=="PROC"){proc=loc;procName=chunk.entries[e].name;++procs;}
                else if(chunk.type=="PBBF")Need(buffers.insert(std::make_pair(Key(chunk.entries[e].name),loc)).second,"Ambiguous PBBF resource");
                else Need(textures.insert(std::make_pair(wolf2009::MaterialCatalog::NormalizeVirtualPath(chunk.entries[e].name),loc)).second,"Ambiguous TXTR resource");
            }
        }
        Need(procs==1,"Expected exactly one PROC in MPK");
        const wolf2009::BufferResolver resolver=[&](const std::string& name){const auto f=buffers.find(Key(name));Need(f!=buffers.end(),"Missing PBBF buffer");return archive.ReadEntry(input,f->second.chunk,f->second.entry);};
        const wolf2009::GeometryScene scene=wolf2009::DecodeProc7(archive.ReadEntry(input,proc.chunk,proc.entry),procName,resolver);
        std::map<std::string,std::size_t> materialIds;std::vector<Material> materials;std::vector<FaceSurface> surfaces;
        std::vector<std::uint32_t> positions(scene.vertices.size(),(std::numeric_limits<std::uint32_t>::max)());std::vector<std::uint32_t> used;std::vector<UV> uv;
        std::size_t triangles=0;
        for(const auto& source:scene.surfaces){if(source.model.compare(0,5,"_area")!=0||source.indices.empty())continue;
            const std::string key=Key(source.material);std::size_t id;
            const auto found=materialIds.find(key);if(found==materialIds.end()){id=materials.size();materialIds[key]=id;Material m;m.id=id;m.binding=catalog.Resolve(source.material);materials.push_back(m);}else id=found->second;
            FaceSurface surface;surface.source=&source;surface.material=id;std::map<std::uint32_t,std::size_t> surfaceUV;
            const auto& binding=materials[id].binding;
            for(const std::uint32_t index:source.indices){
                if(positions[index]==(std::numeric_limits<std::uint32_t>::max)()){positions[index]=std::uint32_t(used.size());used.push_back(index);}
                auto f=surfaceUV.find(index);if(f==surfaceUV.end()){const auto& v=scene.vertices[index];const std::size_t at=uv.size();const UV tex={v.u*binding.uvScale[0],v.v*binding.uvScale[1]};Need(std::isfinite(tex.u)&&std::isfinite(tex.v),"Scaled UV overflow");uv.push_back(tex);surfaceUV[index]=at;surface.uv.push_back(at);}else surface.uv.push_back(f->second);
            }
            triangles+=source.indices.size()/3;surfaces.push_back(std::move(surface));
        }
        Need(!surfaces.empty(),"Map has no static world surfaces");NewDirectory(output);NewDirectory(output+"/textures");
        std::map<std::string,std::string> exported;std::size_t textureBytes=0,unresolved=0,missing=0,unsupported=0;
        for(auto& material:materials){
            if(!material.binding.resolved){++unresolved;continue;}
            const std::string name=wolf2009::MaterialCatalog::NormalizeVirtualPath(material.binding.diffusePath);
            const auto previous=exported.find(name);if(previous!=exported.end()){material.texture=previous->second;continue;}
            const auto found=textures.find(name);if(found==textures.end()){++missing;continue;}
            Need(exported.size()<128,"Texture count budget exceeded");const Bytes dds=archive.ReadEntry(input,found->second.chunk,found->second.entry);
            Need(dds.size()<=128u*1024u*1024u-textureBytes,"Texture source budget exceeded");
            try{wolf2009::DecodeDdsMip0(dds);}catch(const std::exception&){++unsupported;continue;}
            const std::string leaf="textures/"+std::to_string(exported.size())+".dds";Output file(output+"/"+leaf);file.Write(dds);file.Finish();
            textureBytes+=dds.size();exported[name]=leaf;material.texture=leaf;
        }
        Output mtl(output+"/scene.mtl");for(const auto& material:materials){
            Need(std::fprintf(mtl.Get(),"newmtl m%llu\nKd 0.75 0.75 0.75\n",static_cast<unsigned long long>(material.id))>=0,"MTL write failed");
            if(!material.texture.empty())Need(std::fprintf(mtl.Get(),"map_Kd %s\n",material.texture.c_str())>=0,"MTL texture write failed");
        }mtl.Finish();
        Output obj(output+"/scene.obj");Need(std::fprintf(obj.Get(),"# User-supplied Wolf2009 static world. No placements, shader emulation or gameplay.\nmtllib scene.mtl\n")>=0,"OBJ write failed");
        for(const auto i:used){const auto& v=scene.vertices[i];Need(std::fprintf(obj.Get(),"v %.9g %.9g %.9g\n",double(v.x),double(v.y),double(v.z))>=0,"OBJ position write failed");}
        for(const auto& v:uv)Need(std::fprintf(obj.Get(),"vt %.9g %.9g\n",double(v.u),double(v.v))>=0,"OBJ UV write failed");
        for(std::size_t s=0;s<surfaces.size();++s){const auto& surface=surfaces[s];Need(std::fprintf(obj.Get(),"g surface%llu\nusemtl m%llu\n",static_cast<unsigned long long>(s),static_cast<unsigned long long>(surface.material))>=0,"OBJ group write failed");
            for(std::size_t i=0;i<surface.source->indices.size();i+=3)Need(std::fprintf(obj.Get(),"f %u/%llu %u/%llu %u/%llu\n",positions[surface.source->indices[i]]+1,static_cast<unsigned long long>(surface.uv[i]+1),positions[surface.source->indices[i+1]]+1,static_cast<unsigned long long>(surface.uv[i+1]+1),positions[surface.source->indices[i+2]]+1,static_cast<unsigned long long>(surface.uv[i+2]+1))>=0,"OBJ face write failed");
        }obj.Finish();
        std::cout<<"{\"vertices\":"<<used.size()<<",\"triangles\":"<<triangles<<",\"materials\":"<<materials.size()<<",\"textures\":"<<exported.size()<<",\"unresolvedMaterials\":"<<unresolved<<",\"missingTextures\":"<<missing<<",\"unsupportedTextures\":"<<unsupported<<",\"gameDataIncluded\":true}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Scene pack failed: "<<e.what()<<"\n";return 1;}
}
