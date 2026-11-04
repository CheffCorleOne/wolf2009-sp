// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeSourceStaticSkin.h"
#include "RenderData.h"
#include "SkinCatalog.h"
#include "ZipArchive.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"

namespace {
namespace render=wolf2009::idtech4a;
using render::Bytes;
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string Name(const std::string& raw){
    Need(!raw.empty()&&raw.size()<=4096,"Static skin requires explicit bounded paths/names");
    std::string out=raw;
    for(char& c:out){const unsigned char b=static_cast<unsigned char>(c);
        Need(b>32&&b<127&&c!=':'&&c!='"'&&c!='{'&&c!='}'&&c!='*'&&c!='#',"Unsafe static skin name byte");
        if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
    Need(out.front()!='/'&&out.back()!='/',"Static skin path must be relative");
    for(std::size_t at=0;at<out.size();){const auto end=out.find('/',at);const auto part=out.substr(at,end==std::string::npos?end:end-at);
        Need(!part.empty()&&part!="."&&part!="..","Unsafe static skin path component");if(end==std::string::npos)break;at=end+1;}
    return out;
}
std::uint64_t Hash(const unsigned char* data,std::size_t n,std::uint64_t value=14695981039346656037ull){for(std::size_t i=0;i<n;++i){value^=data[i];value*=1099511628211ull;}return value;}
std::string Hex(std::uint64_t value){std::string out(16,'0');for(int i=15;i>=0;--i){out[std::size_t(i)]="0123456789abcdef"[value&15];value>>=4;}return out;}
struct Source {
    idFile* file=NULL;Bytes bytes;std::uint64_t fingerprint=0;
    ~Source(){if(file)fileSystem->CloseFile(file);}
};
typedef std::map<std::string,std::unique_ptr<Source>> Sources;
const Bytes& Read(Sources& retained,const std::string& raw,std::size_t maximum){
    const std::string path=Name(raw);const auto old=retained.find(path);if(old!=retained.end())return old->second->bytes;
    Need(retained.size()<256,"Static skin source-file budget exceeded");
    std::unique_ptr<Source> source(new Source);source->file=fileSystem->OpenFileRead(path.c_str(),false);
    Need(source->file!=NULL,"Static skin exact source file is absent");const int size=source->file->Length();
    Need(size>0&&std::size_t(size)<=maximum&&source->file->Tell()==0,"Static skin source byte/position budget");
    std::size_t retainedBytes=0;for(const auto& item:retained)retainedBytes+=item.second->bytes.size();
    Need(retainedBytes<=(512u<<20)&&std::size_t(size)<=(512u<<20)-retainedBytes,"Static skin aggregate source byte budget");
    source->bytes.resize(std::size_t(size));std::size_t at=0;
    while(at<source->bytes.size()){const int n=source->file->Read(source->bytes.data()+at,int(source->bytes.size()-at));Need(n>0&&std::size_t(n)<=source->bytes.size()-at,"Short static skin source read");at+=std::size_t(n);}
    source->fingerprint=Hash(source->bytes.data(),source->bytes.size());const auto inserted=retained.emplace(path,std::move(source));return inserted.first->second->bytes;
}
std::string DeclText(const idDecl* value){
    Need(value&&value->GetState()!=DS_DEFAULTED,"Static skin native declaration is absent/defaulted");const int n=value->GetTextLength();
    Need(n>0&&n<=65536,"Static skin declaration byte budget");std::vector<char> bytes(std::size_t(n)+1,0);value->GetText(bytes.data());return std::string(bytes.data(),std::size_t(n));
}
const idDecl* Existing(declType_t type,const std::string& name){
    if(const auto* value=declManager->FindDeclWithoutParsing(type,name.c_str(),false))return value;
    for(int i=0;i<declManager->GetNumDecls(type);++i){const auto* value=declManager->DeclByIndex(type,i,false);if(value&&name==value->GetName())return value;}return NULL;
}
std::map<std::string,std::string> ownedDeclarations;
int liveSkins=0;
void CheckAvailable(declType_t type,const std::string& name,const std::string& text){
    const auto prior=ownedDeclarations.find(name);const auto* existing=Existing(type,name);
    if(prior==ownedDeclarations.end())Need(!existing,"Static skin generated namespace is occupied by a foreign declaration");
    else Need(prior->second==text&&existing&&DeclText(existing)==text,"Static skin owned declaration changed or alias collided");
}
void Declare(declType_t type,const std::string& name,const std::string& text){
    CheckAvailable(type,name,text);if(ownedDeclarations.count(name))return;
    Need(ownedDeclarations.size()<4096,"Static skin native declaration budget");
    idDecl* value=declManager->CreateNewDecl(type,name.c_str(),"<wolf2009-source-static-skin>");Need(value!=NULL,"Static skin native declaration allocation failed");
    value->SetText(text.c_str());value->Invalidate();ownedDeclarations.emplace(name,text);
}
struct Draw {
    int id=0,vertices=0,indexes=0;
    const idDrawVert* vertexData=NULL;const glIndex_t* indexData=NULL;
    const idMaterial* material=NULL;std::string materialText;
    render::RenderSurface source;
    render::DiffuseMaterial importedProfile;
    std::string authoredMaterial;
    wolf2009::MaterialResult authoredFacts;
};
struct Target {const idMaterial* from=NULL;const idMaterial* to=NULL;std::string name,text;bool omittedModulation=false;};
void CheckNativeGeometry(const render::RenderSurface& source,const render::DiffuseMaterial& material,const modelSurface_t& native){
    Need(native.geometry&&native.geometry->verts&&native.geometry->indexes&&native.geometry->numIndexes==int(source.indices.size()),"Static skin native/source triangle geometry differs");
    const auto* data=native.geometry;
    for(std::size_t i=0;i<source.indices.size();++i){const unsigned at=source.indices[i];const unsigned actual=unsigned(data->indexes[i]);
        Need(at<source.vertices.size()&&actual<unsigned(data->numVerts),"Static skin native/source index is out of range");
        const auto& input=source.vertices[at];const auto& vertex=data->verts[actual];
        Need(vertex.xyz.x==input.x&&vertex.xyz.y==input.y&&vertex.xyz.z==input.z,"Static skin native/source positions differ");
        const float u=input.u*(material.supported?material.binding.uvScale[0]:1.0f),v=input.v*(material.supported?material.binding.uvScale[1]:1.0f);
        Need(std::isfinite(u)&&std::isfinite(v)&&vertex.st.x==u&&vertex.st.y==v,"Static skin native/source UV encoding differs");
        const auto color=render::NativeDiffuseColor(material,input.diffuseColor);for(unsigned c=0;c<4;++c)Need(vertex.color[c]==color[c],"Static skin native/source colour encoding differs");
    }
}
}

struct WolfSourceStaticSkin::Impl {
    WolfSourceStaticSkinRequest request;
    Sources sources;
    idRenderModel* model=NULL;
    const idDeclSkin* skin=NULL;
    std::vector<Draw> draws;std::vector<Target> targets;
    std::string alias,skinText;
    WolfSourceStaticSkinState state;
    Impl(){++liveSkins;}
    ~Impl(){--liveSkins;}
    bool Preflight(idStr& reason)const{reason.Clear();try{
        Need(model&&renderModelManager->CheckModel(request.nativeModelName.c_str())==model&&model->IsLoaded()&&!model->IsDefaultModel()&&model->IsDynamicModel()==DM_STATIC,"Static skin native model identity/liveness changed");
        Need(Name(model->Name())==Name(request.nativeModelName)&&model->NumSurfaces()==int(draws.size()),"Static skin native model name/surface count changed");
        for(std::size_t i=0;i<draws.size();++i){const auto* actual=model->Surface(int(i));const auto& saved=draws[i];
            Need(actual&&actual->geometry&&actual->id==saved.id&&actual->shader==saved.material&&actual->geometry->verts==saved.vertexData&&actual->geometry->indexes==saved.indexData&&actual->geometry->numVerts==saved.vertices&&actual->geometry->numIndexes==saved.indexes,"Static skin shared native surface changed");
            Need(DeclText(saved.material)==saved.materialText,"Static skin original native material changed");
            CheckNativeGeometry(saved.source,saved.importedProfile,*actual);}
        Need(skin&&Existing(DECL_SKIN,alias)==skin&&skin->GetState()==DS_PARSED&&DeclText(skin)==skinText,"Static skin native customSkin declaration changed");
        for(const auto& target:targets){Need(target.to&&Existing(DECL_MATERIAL,target.name)==target.to&&target.to->GetState()==DS_PARSED&&DeclText(target.to)==target.text,"Static skin private material changed");
            Need(target.to->IsDrawn()!=target.omittedModulation&&(!target.omittedModulation||!target.to->SurfaceCastsShadow()),"Static skin private material drawing policy changed");}
        for(const auto& source:sources){const auto& value=*source.second;Need(value.file&&value.file->Length()==int(value.bytes.size())&&value.file->Tell()==int(value.bytes.size()),"Static skin retained source file changed");}
        return true;
    }catch(const std::exception& e){reason=e.what();return false;}}
};
WolfSourceStaticSkin::WolfSourceStaticSkin(std::unique_ptr<Impl> p):impl_(std::move(p)){}
WolfSourceStaticSkin::~WolfSourceStaticSkin(){}
const char* WolfSourceStaticSkin::NativeAlias()const{return impl_->alias.c_str();}
const char* WolfSourceStaticSkin::AuthoredName()const{return impl_->request.authoredSkin.c_str();}
const char* WolfSourceStaticSkin::NativeModelName()const{return impl_->request.nativeModelName.c_str();}
bool WolfSourceStaticSkin::Preflight(idStr& reason)const{return impl_->Preflight(reason);}
bool WolfSourceStaticSkin::Query(WolfSourceStaticSkinState& out,idStr& reason)const{if(!impl_->Preflight(reason))return false;out=impl_->state;return true;}
int Wolf2009_LiveSourceStaticSkins(){return liveSkins;}

std::unique_ptr<WolfSourceStaticSkin> Wolf2009_PrepareSourceStaticSkin(const WolfSourceStaticSkinRequest& request,idStr& reason){reason.Clear();try{
    Need(liveSkins<32,"Static skin live-owner budget");
    const char* visualPolicy=Wolf2009_StaticSkinVisualPolicyName(request.visualPolicy);Need(visualPolicy!=NULL,"Unknown static skin visual policy");
    std::unique_ptr<WolfSourceStaticSkin::Impl> out(new WolfSourceStaticSkin::Impl);out->request=request;
    out->state.visualPolicy=request.visualPolicy;
    out->request.exactModelResource=Name(request.exactModelResource);out->request.nativeModelName=Name(request.nativeModelName);out->request.materialArchive=Name(request.materialArchive);out->request.authoredSkin=Name(request.authoredSkin);
    const auto source=render::ParseResourcePath(out->request.exactModelResource),native=render::ParseResourcePath(out->request.nativeModelName);
    Need(source.type=="modl"&&native.type=="modl"&&source.mount==native.mount&&source.name==native.name,"Static skin native model does not name the exact source MODL");
    out->model=renderModelManager->CheckModel(out->request.nativeModelName.c_str());
    Need(out->model&&out->model->IsLoaded()&&!out->model->IsDefaultModel()&&out->model->IsDynamicModel()==DM_STATIC,"Static skin needs an already qualified resident static model");
    const Bytes& archiveBytes=Read(out->sources,out->request.materialArchive,64u<<20);
    out->state.archiveBytes=archiveBytes.size();out->state.archiveFingerprint=Hash(archiveBytes.data(),archiveBytes.size());
    std::istringstream stream(std::string(archiveBytes.begin(),archiveBytes.end()),std::ios::in|std::ios::binary);
    const auto archive=wolf2009::ZipArchive::Scan(stream);const auto skins=wolf2009::SkinCatalog::Read(stream,archive);
    auto geometry=render::LoadGeometry(source,[&](const std::string& path){const auto parsed=render::ParseResourcePath(path);Need(parsed.mount==source.mount&&(parsed.type=="modl"||parsed.type=="pbbf"),"Static skin geometry escaped its exact source mount/type");return Read(out->sources,path,256u<<20);});
    Need(geometry.models==1&&geometry.skippedMeshes==0&&!geometry.surfaces.empty()&&geometry.surfaces.size()<=4096,"Static skin requires complete single static-MODL geometry");
    const auto original=render::CompactSurfaces(geometry);const auto applied=skins.Apply(out->request.authoredSkin,geometry);const auto replacement=render::CompactSurfaces(geometry);
    Need(!original.empty()&&original.size()==replacement.size()&&out->model->NumSurfaces()==int(original.size()),"Static skin source/native draw count differs");
    // Source skins are reusable material-name rules, not model assignments.
    // A present, unambiguous skin can legitimately name none of this model's
    // materials. SkinCatalog has already qualified the declaration/directives;
    // preserve that authored no-op via an explicit native identity customSkin.
    out->state.authoredNoOp=applied.changedSurfaces==0;
    const auto catalog=render::ReadMaterialCatalog(archiveBytes);
    struct Pending {const idMaterial* from;render::DiffuseMaterial profile;bool omittedModulation;};std::vector<Pending> pending;std::map<const idMaterial*,std::size_t> remaps;
    std::ostringstream key;key<<out->request.exactModelResource<<'\n'<<out->request.nativeModelName<<'\n'<<out->request.authoredSkin<<'\n'<<Hex(out->state.archiveFingerprint)<<'\n'<<visualPolicy<<'\n';
    std::set<std::string> textures;
    for(std::size_t i=0;i<original.size();++i){const auto* surface=out->model->Surface(int(i));Need(surface&&surface->shader&&surface->geometry,"Static skin native surface is absent");
        const auto from=render::BuildDiffuseMaterial(source,original[i],catalog.get());
        Need(from.name==surface->shader->GetName()&&DeclText(surface->shader)==from.declaration,"Static skin resident material differs from the exact supplied catalog/profile");
        CheckNativeGeometry(original[i],from,*surface);
        auto target=render::BuildDiffuseMaterial(source,replacement[i],catalog.get());
        Draw saved;saved.id=surface->id;saved.vertices=surface->geometry->numVerts;saved.indexes=surface->geometry->numIndexes;saved.vertexData=surface->geometry->verts;saved.indexData=surface->geometry->indexes;saved.material=surface->shader;saved.materialText=from.declaration;
        saved.source=original[i];saved.importedProfile=from;saved.authoredMaterial=replacement[i].material;saved.authoredFacts=target.binding;out->draws.push_back(std::move(saved));
        const bool omit=Wolf2009_StaticSkinOmitsVerifiedModulation(request.visualPolicy,target.binding);
        if(!omit&&original[i].material==replacement[i].material)continue;
        if(omit){
            // Keep the exact ColorMap payload/facts even though this explicit
            // compatibility profile samples no texture and draws no fragment.
            const std::string colorImage=source.Path("TXTR",target.binding.colorPath+".dds");
            wolf2009::DecodeDdsMip0(Read(out->sources,colorImage,64u<<20));textures.insert(colorImage);
            target.declaration="{\n noShadows\n}\n";target.image.clear();target.supported=false;
            ++out->state.omittedModulationSurfaces;out->state.omittedModulationTriangles+=unsigned(original[i].indices.size()/3);
        }else{
            Need(from.supported&&target.supported,"Static skin changed source/target shader is outside the qualified native diffuse policy");
            for(unsigned axis=0;axis<2;++axis)Need(from.binding.uvScale[axis]==target.binding.uvScale[axis],"Static skin would require rebaking shared UVs");
            for(const auto& vertex:original[i].vertices)Need(render::NativeDiffuseColor(from,vertex.diffuseColor)==render::NativeDiffuseColor(target,vertex.diffuseColor),"Static skin would require re-encoding shared vertex colours");
            const auto image=render::ParseResourcePath(target.image);Need(image.mount==source.mount&&image.type=="txtr","Static skin target texture escaped its exact source mount");
            wolf2009::DecodeDdsMip0(Read(out->sources,target.image,64u<<20));textures.insert(target.image);
        }
        const auto old=remaps.find(surface->shader);if(old!=remaps.end()){Need(pending[old->second].profile.declaration==target.declaration&&pending[old->second].omittedModulation==omit,"Static skin gives conflicting replacements for one native material");continue;}
        remaps.emplace(surface->shader,pending.size());pending.push_back(Pending{surface->shader,target,omit});
        key<<from.name<<'\n'<<target.name<<'\n'<<target.declaration;
    }
    // Identical pairs and a wholly unmatched valid source skin retain the
    // authored name and explicit identity mapping without shared-data changes.
    for(const auto& item:out->sources)key<<item.first<<'\n'<<item.second->bytes.size()<<'\n'<<Hex(item.second->fingerprint)<<'\n';
    const std::string identity=key.str();out->alias=source.Prefix()+"static-skin/"+Hex(Hash(reinterpret_cast<const unsigned char*>(identity.data()),identity.size()));
    std::ostringstream skin;skin<<"{\n";
    for(std::size_t i=0;i<pending.size();++i){Target target;target.from=pending[i].from;target.name=out->alias+"/material/"+std::to_string(i);target.text=pending[i].profile.declaration;target.omittedModulation=pending[i].omittedModulation;
        CheckAvailable(DECL_MATERIAL,target.name,target.text);skin<<" \""<<target.from->GetName()<<"\" \""<<target.name<<"\"\n";out->targets.push_back(std::move(target));}
    if(pending.empty()){std::set<const idMaterial*> seen;for(const auto& draw:out->draws)if(seen.insert(draw.material).second)skin<<" \""<<draw.material->GetName()<<"\" \""<<draw.material->GetName()<<"\"\n";}
    skin<<"}\n";out->skinText=skin.str();CheckAvailable(DECL_SKIN,out->alias,out->skinText);
    for(auto& target:out->targets){Declare(DECL_MATERIAL,target.name,target.text);target.to=declManager->FindMaterial(target.name.c_str(),false);Need(target.to&&target.to->GetState()!=DS_DEFAULTED&&target.to->IsDrawn()!=target.omittedModulation&&(!target.omittedModulation||!target.to->SurfaceCastsShadow()),"Static skin replacement material failed its explicit drawing policy");}
    Declare(DECL_SKIN,out->alias,out->skinText);out->skin=declManager->FindSkin(out->alias.c_str(),false);Need(out->skin&&out->skin->GetState()!=DS_DEFAULTED,"Static skin declaration failed to parse");
    out->state.surfaces=unsigned(original.size());out->state.matchedSurfaces=unsigned(applied.matchedSurfaces);out->state.changedSurfaces=unsigned(applied.changedSurfaces);out->state.remaps=unsigned(pending.size());out->state.textures=unsigned(textures.size());out->state.explicitPairsOnly=true;out->state.sharedGeometryUnchanged=true;
    out->state.compatibilityOverlay=out->state.omittedModulationSurfaces!=0;
    Need(out->Preflight(reason),reason.IsEmpty()?"Static skin final preflight failed":reason.c_str());
    return std::unique_ptr<WolfSourceStaticSkin>(new WolfSourceStaticSkin(std::move(out)));
}catch(const std::exception& e){reason=e.what();return std::unique_ptr<WolfSourceStaticSkin>();}}
