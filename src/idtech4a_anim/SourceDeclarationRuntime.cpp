// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarationCatalog.h"
#include "SourceDefinitions.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "idlib/precompiled.h"
#include "game/Game_local.h"
#include "SourceDeclarationRuntime.h"
namespace {
const std::size_t MaximumPacketBytes=64u<<20,MaximumSelectedDefinitions=20000;
void Need(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
std::string Fold(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
void RelativePath(const std::string& path){
    Need(!path.empty()&&path.size()<=4096,"source declaration path byte budget");
    for(unsigned char c:path)Need(c>=32&&c<127&&c!=':'&&c!='\\',"invalid source declaration path byte");
    std::size_t begin=0;
    for(;;){const auto end=path.find('/',begin),length=end==std::string::npos?path.size()-begin:end-begin;
        Need(length&&path.compare(begin,length,".")!=0&&path.compare(begin,length,"..")!=0,"invalid source declaration path component");
        if(end==std::string::npos)break;begin=end+1;}
}
void ResourcePath(const std::string& path){
    RelativePath(path);const std::string prefix="wolf2009/";
    Need(path.compare(0,prefix.size(),prefix)==0,"source Map/Explicit declarations require an explicit wolf2009 resource path");
    const auto mountEnd=path.find('/',prefix.size());Need(mountEnd!=std::string::npos,"source declaration path has no mount/type boundary");
    const auto typeEnd=path.find('/',mountEnd+1);Need(typeEnd!=std::string::npos,"source declaration path has no typed resource name");
    Need(Fold(path.substr(mountEnd+1,typeEnd-mountEnd-1))=="decl","source declaration path must explicitly select DECL");
}
void GlobalPath(const std::string& path){
    RelativePath(path);const auto lower=Fold(path);const std::string suffix=".decls";
    Need(lower.compare(0,9,"wolf2009/")!=0&&lower.size()>=suffix.size()&&lower.compare(lower.size()-suffix.size(),suffix.size(),suffix)==0,
         "source Global declarations require a relative ordinary VFS .decls path");
}
void LoadIdentity(const std::string& identity){
    // SourceDefinitions adds record coordinates to this identity in its
    // bounded member name. Leave room instead of inventing/truncating a key.
    Need(!identity.empty()&&identity.size()<=1024,"source declaration load identity must contain 1..1024 bytes");
    for(unsigned char c:identity)Need(c>=32&&c<127,"source declaration load identity contains a control/non-ASCII byte");
}
struct FileCloser {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
std::vector<unsigned char> ReadPacket(const std::string& path){
    Need(fileSystem!=NULL,"source declaration filesystem is unavailable");
    std::unique_ptr<idFile,FileCloser> file(fileSystem->OpenFileRead(path.c_str(),false));
    Need(file.get()!=NULL,"selected source declaration resource is absent");
    const int length=file->Length();Need(length>0&&std::size_t(length)<=MaximumPacketBytes,"selected source declaration packet byte budget");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));std::size_t read=0;
    while(read<bytes.size()){
        const int remaining=static_cast<int>(bytes.size()-read),count=file->Read(bytes.data()+read,remaining);
        Need(count>0&&count<=remaining,"short or invalid source declaration packet read");read+=std::size_t(count);
    }
    unsigned char trailing=0;
    Need(file->Tell()==length&&file->Read(&trailing,1)==0&&file->Length()==length,
         "source declaration file length/EOF changed during copy");
    // The scoped API8 capability is closed before transport/catalog parsing.
    return bytes;
}
wolf2009::CompiledCatalogLimits CatalogLimits(){
    wolf2009::CompiledCatalogLimits limits;limits.loadUnits=1;limits.totalSourceBytes=MaximumPacketBytes;return limits;
}
wolf2009::SourceDefinitionLimits DefinitionLimits(){
    wolf2009::SourceDefinitionLimits limits;limits.members=MaximumSelectedDefinitions;limits.declarations=MaximumSelectedDefinitions;
    limits.totalSourceBytes=128u<<20;limits.storedBytes=128u<<20;return limits;
}
}
struct WolfSourceDeclarationPacket::Impl {
    std::vector<unsigned char> bytes;
    wolf2009::CompiledDeclarationCatalog catalog;
    wolf2009::SourceDefinitions definitions;
    WolfSourceDeclarationCounts counts;
    Impl():catalog(CatalogLimits()),definitions(DefinitionLimits()){}
};
WolfSourceDeclarationPacket::WolfSourceDeclarationPacket(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
WolfSourceDeclarationPacket::~WolfSourceDeclarationPacket(){}
const wolf2009::CompiledDeclarationCatalog& WolfSourceDeclarationPacket::Catalog()const{return impl_->catalog;}
const wolf2009::SourceDefinitions& WolfSourceDeclarationPacket::Definitions()const{return impl_->definitions;}
const WolfSourceDeclarationCounts& WolfSourceDeclarationPacket::Counts()const{return impl_->counts;}
std::unique_ptr<WolfSourceDeclarationPacket> Wolf2009_LoadSourceDeclarations(const WolfSourceDeclarationRequest& request,idStr& reason){
    reason.Clear();
    try{
        Need(request.policy==WolfSourceDeclaration_SelectedTypedPacketV1,"an explicit SelectedTypedPacketV1 source declaration policy is required");
        Need(request.role==WolfSourceDeclaration_SelectedMap||request.role==WolfSourceDeclaration_SelectedExplicit||request.role==WolfSourceDeclaration_SelectedGlobal,
             "source declarations require an explicit selected Map/Explicit/Global role");
        const std::string path=request.resourcePath.c_str(),identity=request.loadIdentity.c_str();
        Need(path.size()==std::size_t(request.resourcePath.Length())&&identity.size()==std::size_t(request.loadIdentity.Length()),
             "source declaration request contains an embedded NUL");
        if(request.role==WolfSourceDeclaration_SelectedGlobal)GlobalPath(path);else ResourcePath(path);LoadIdentity(identity);
        std::unique_ptr<WolfSourceDeclarationPacket::Impl> data(new WolfSourceDeclarationPacket::Impl);
        data->bytes=ReadPacket(path);
        wolf2009::CompiledDeclLimits transportLimits;transportLimits.sourceBytes=MaximumPacketBytes;
        const auto document=wolf2009::CompiledDeclarations::Read(data->bytes,wolf2009::CompiledDeclFormat::TwoSectionLfTextV1,transportLimits);
        wolf2009::CompiledCatalogSource source;source.loadIdentity=identity;source.path=path;
        source.role=request.role==WolfSourceDeclaration_SelectedGlobal?wolf2009::CompiledCatalogRole::Global:
            request.role==WolfSourceDeclaration_SelectedMap?wolf2009::CompiledCatalogRole::Map:wolf2009::CompiledCatalogRole::Explicit;
        source.epoch=0; // Ordinary idFile contains no mount-epoch/pin capability.
        source.byteIdentityKnown=true;source.bytes=document.sourceBytes;source.fingerprint=document.sourceFingerprint;
        data->catalog.AddDocument(document,source);
        auto& counts=data->counts;counts.packetBytes=document.sourceBytes;counts.packetFingerprint=document.sourceFingerprint;counts.entries=data->catalog.EntryCount();
        for(std::size_t i=0;i<counts.entries;++i){const auto entry=data->catalog.Entry(i);const auto type=Fold(entry.type);
            if(type=="entitydef")++counts.entityDefinitions;else if(type=="model")++counts.modelDefinitions;else ++counts.otherDeclarations;}
        Need(counts.entityDefinitions<=MaximumSelectedDefinitions&&counts.modelDefinitions<=MaximumSelectedDefinitions-counts.entityDefinitions,
             "selected source entity/model declaration count budget");
        for(std::size_t i=0;i<counts.entries;++i){const auto entry=data->catalog.Entry(i);const auto type=Fold(entry.type);
            if(type=="entitydef"||type=="model")data->definitions.AddCompiledRecord(entry);}
        Need(data->definitions.DefinitionCount()==counts.entityDefinitions+counts.modelDefinitions,"selected source frontend import count differs");
        return std::unique_ptr<WolfSourceDeclarationPacket>(new WolfSourceDeclarationPacket(std::move(data)));
    }catch(const std::exception& error){reason=error.what();}
    catch(...){reason="source declaration read/decoding failed with a native exception";}
    return std::unique_ptr<WolfSourceDeclarationPacket>();
}
