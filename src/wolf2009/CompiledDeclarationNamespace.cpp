// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompiledDeclarationNamespace.h"
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace wolf2009 { namespace {
void Need(bool value,const char* reason,CompiledDeclFailure category=CompiledDeclFailure::InvalidData){
    if(!value)throw CompiledCatalogError(category,0,reason);
}
void Charge(std::size_t& used,std::size_t amount,std::size_t cap,const char* reason){
    Need(used<=cap&&amount<=cap-used,reason,CompiledDeclFailure::LimitExceeded);used+=amount;
}
void Objects(std::size_t& used,std::size_t count,std::size_t size,std::size_t cap){
    Need(!size||count<=(std::numeric_limits<std::size_t>::max)()/size,"Namespace storage arithmetic overflow",CompiledDeclFailure::LimitExceeded);
    // Conservative capacity allowance, not an exact allocator/RSS limit.
    Charge(used,count*size,cap,"Namespace retained object budget");
    Charge(used,count*size,cap,"Namespace retained capacity budget");
}
void String(std::size_t& used,std::size_t size,std::size_t cap){Objects(used,size,1,cap);Charge(used,2,cap,"Namespace string terminator budget");}
void Label(const std::string& text,std::size_t cap){
    Need(!text.empty(),"Namespace requires nonempty raw labels");Need(text.size()<=cap,"Namespace label byte budget",CompiledDeclFailure::LimitExceeded);
    for(unsigned char c:text)Need(c>=32&&c<127,"Namespace requires checked ASCII labels");
}
std::string Fold(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
std::string Canonical(std::string text){
    text=Fold(std::move(text));for(char& c:text)if(c=='\\')c='/';
    const auto dot=text.find_last_of('.');if(dot!=std::string::npos)text.resize(dot);return text;
}
typedef std::pair<std::string,std::string> TypedKey;
void EntryStorage(std::size_t& storage,const CompiledCatalogEntry& entry,std::size_t cap){
    Objects(storage,1,sizeof(CompiledCatalogEntry),cap);
    // Account both this copy and the private catalog seal it keeps alive.
    String(storage,entry.type.size(),cap);String(storage,entry.name.size(),cap);
    String(storage,entry.type.size(),cap);String(storage,entry.name.size(),cap);
}
void DocumentStorage(std::size_t& storage,const CompiledDeclarations& doc,std::size_t cap){
    Objects(storage,1,sizeof(CompiledDeclarations),cap);
    for(const auto& pool:doc.pools)Objects(storage,pool.words.size(),sizeof(std::uint32_t),cap);
    for(const auto& section:doc.sections){Objects(storage,section.groups.size(),sizeof(CompiledDeclGroup),cap);
        for(const auto& group:section.groups){String(storage,group.sourceFile.size(),cap);Objects(storage,group.records.size(),sizeof(CompiledDeclRecord),cap);
            for(const auto& record:group.records)Objects(storage,record.text.size(),1,cap);}}
}
void SourceStorage(std::size_t& storage,const CompiledCatalogSource& source,std::size_t cap){
    Objects(storage,1,sizeof(CompiledCatalogSource),cap);String(storage,source.path.size(),cap);String(storage,source.loadIdentity.size(),cap);
}
}
struct CompiledDeclarationNamespace::Impl {
    CompiledNamespaceLimits limits;
    std::vector<CompiledNamespaceSelected> selected;
    std::map<TypedKey,std::size_t> index;
    std::map<std::string,std::vector<std::size_t>> owners;
    std::vector<CompiledNamespaceDecision> decisions;
    std::vector<CompiledNamespaceApplication> applications;
    std::set<const CompiledDeclarations*> documents;
    std::set<const CompiledCatalogSource*> sources;
    struct Unit {std::shared_ptr<const CompiledDeclarations> document;std::shared_ptr<const CompiledCatalogSource> source;};
    std::map<std::string,Unit> units;
    std::size_t work=0,storage=0;
    explicit Impl(const CompiledNamespaceLimits& l):limits(l){}
    void Work(std::size_t amount=1){Charge(work,amount,limits.work,"Namespace cumulative work budget");}
    void Keep(std::size_t n,std::size_t size){Objects(storage,n,size,limits.retainedBytes);}
    void KeepString(const std::string& value){String(storage,value.size(),limits.retainedBytes);}
    void KeepEntry(const CompiledCatalogEntry& value){EntryStorage(storage,value,limits.retainedBytes);}
};
CompiledDeclarationNamespace::CompiledDeclarationNamespace(CompiledNamespacePolicy policy,const CompiledNamespaceLimits& limits):impl_(new Impl(limits)){
    Need(policy==CompiledNamespacePolicy::OrdinaryTextOwnersV1,"An explicit OrdinaryTextOwnersV1 namespace policy is required");
    Need(limits.applications&&limits.decisions&&limits.selected&&limits.owners&&limits.keyBytes&&limits.keyBytes<=1048576&&limits.work&&limits.retainedBytes&&limits.resultBytes,
         "Invalid compiled namespace limits");
    impl_->Keep(1,sizeof(Impl));
}
CompiledDeclarationNamespace::~CompiledDeclarationNamespace(){}
void CompiledDeclarationNamespace::ApplySection(const CompiledDeclarationCatalog& catalog,std::size_t loadOrdinal,std::size_t section,CompiledNamespaceReaderMode mode){
    Need(mode==CompiledNamespaceReaderMode::Ordinary||mode==CompiledNamespaceReaderMode::ProtectAccepted,"An explicit compiled section reader mode is required");
    Need(loadOrdinal<catalog.LoadUnitCount(),"Compiled namespace load unit is outside catalog");
    const auto document=catalog.Document(loadOrdinal);const auto source=catalog.Source(loadOrdinal);
    Need(document&&source&&section<document->sections.size(),"Compiled namespace section is outside document");
    const auto& limits=impl_->limits;
    Label(source->loadIdentity,limits.keyBytes);Label(source->path,limits.keyBytes);
    Need(impl_->applications.size()<limits.applications,"Namespace application count budget",CompiledDeclFailure::LimitExceeded);
    std::unique_ptr<Impl> next(new Impl(*impl_));
    const auto known=next->units.find(source->loadIdentity);
    if(known!=next->units.end()){
        // A changed/reopened packet requires its own explicit load identity.
        // An FNV match alone cannot establish equal immutable source objects.
        Need(known->second.document==document&&known->second.source==source,"Compiled namespace load identity refers to a different immutable packet");
    }else{
        next->Keep(1,sizeof(Impl::Unit)+128);next->KeepString(source->loadIdentity);
        Impl::Unit unit;unit.document=document;unit.source=source;next->units.emplace(source->loadIdentity,std::move(unit));
    }
    if(next->documents.insert(document.get()).second){next->Work(document->sourceBytes);DocumentStorage(next->storage,*document,limits.retainedBytes);next->Keep(1,128);}
    if(next->sources.insert(source.get()).second){SourceStorage(next->storage,*source,limits.retainedBytes);next->Keep(1,128);}
    // Get only sealed entries supplied by this catalog. Check each coordinate
    // against the owned document; empty groups still perform their seen reset.
    std::vector<std::size_t> positions;std::size_t temporary=0;
    for(std::size_t i=0;i<catalog.EntryCount();++i){next->Work();const auto entry=catalog.Entry(i);
        if(entry.loadOrdinal!=loadOrdinal||entry.section!=section)continue;
        Need(entry.IsUnmodified()&&entry.document==document&&entry.source==source,"Namespace input is not the catalog's immutable entry");
        Label(entry.type,limits.keyBytes);Label(entry.name,limits.keyBytes);
        next->Work(entry.type.size());next->Work(entry.name.size());
        Objects(temporary,1,sizeof(std::size_t),limits.resultBytes);positions.push_back(i);
    }
    CompiledNamespaceApplication application;application.document=document;application.source=source;application.loadOrdinal=loadOrdinal;
    application.section=section;application.mode=mode;application.decisionBegin=next->decisions.size();
    std::size_t position=0;
    const auto& groups=document->sections[section].groups;
    for(std::size_t g=0;g<groups.size();++g){next->Work();const auto& group=groups[g];Label(group.sourceFile,limits.keyBytes);next->Work(group.sourceFile.size());
        const auto owner=Fold(group.sourceFile);auto ownerAt=next->owners.find(owner);
        if(ownerAt==next->owners.end()){
            Need(next->owners.size()<limits.owners,"Namespace owner count budget",CompiledDeclFailure::LimitExceeded);
            next->Keep(1,sizeof(std::pair<std::string,std::vector<std::size_t>>)+128);next->KeepString(owner);
            ownerAt=next->owners.emplace(owner,std::vector<std::size_t>()).first;
        }
        for(auto id:ownerAt->second){next->Work();next->selected[id].seen08=false;}
        for(std::size_t r=0;r<group.records.size();++r){next->Work();Need(position<positions.size(),"Namespace section entry coverage is incomplete");
            const auto incoming=catalog.Entry(positions[position++]);
            Need(incoming.group==g&&incoming.record==r,"Namespace section entry order differs from document");
            const auto type=Fold(incoming.type),name=Canonical(incoming.name);const TypedKey key(type,name);const auto at=next->index.find(key);
            CompiledNamespaceDecision decision;decision.incoming=incoming;decision.typeKey=type;decision.nameKey=name;decision.incomingOwner=group.sourceFile;
            decision.application=next->applications.size();decision.group=g;decision.mode=mode;
            if(at==next->index.end()){
                Need(next->selected.size()<limits.selected,"Namespace selected count budget",CompiledDeclFailure::LimitExceeded);
                decision.selectedOrdinal=next->selected.size();decision.kind=CompiledNamespaceDecisionKind::AcceptedNew;
                CompiledNamespaceSelected selected;selected.entry=incoming;selected.typeKey=type;selected.nameKey=name;selected.ownerLabel=group.sourceFile;
                selected.seen08=true;selected.protected20=mode==CompiledNamespaceReaderMode::ProtectAccepted;selected.decision=next->decisions.size();
                next->Keep(1,sizeof(CompiledNamespaceSelected)+128);next->KeepEntry(incoming);
                next->KeepString(type);next->KeepString(name);next->KeepString(group.sourceFile);
                next->Keep(1,sizeof(std::pair<TypedKey,std::size_t>)+128);next->KeepString(type);next->KeepString(name);next->Keep(1,sizeof(std::size_t));
                next->index.emplace(key,next->selected.size());ownerAt->second.push_back(next->selected.size());next->selected.push_back(std::move(selected));
                ++application.accepted;
            }else{
                decision.hadPrevious=true;decision.selectedOrdinal=at->second;auto& selected=next->selected[at->second];
                decision.previous=selected.entry;decision.previousOwner=selected.ownerLabel;
                decision.previousSeen08=selected.seen08;decision.previousProtected20=selected.protected20;
                decision.model34ProtectionBypass=selected.protected20&&type=="model";
                if(selected.protected20&&type!="model")decision.kind=CompiledNamespaceDecisionKind::SkippedProtected;
                else if(Fold(selected.ownerLabel)!=owner)decision.kind=CompiledNamespaceDecisionKind::SkippedOwnerConflict;
                else if(selected.seen08)decision.kind=CompiledNamespaceDecisionKind::SkippedDuplicate;
                else{
                    decision.kind=CompiledNamespaceDecisionKind::AcceptedReplacement;selected.entry=incoming;selected.seen08=true;
                    selected.protected20=selected.protected20||mode==CompiledNamespaceReaderMode::ProtectAccepted;selected.decision=next->decisions.size();
                    next->KeepEntry(incoming);
                    ++application.replaced;
                }
                if(decision.kind!=CompiledNamespaceDecisionKind::AcceptedReplacement)++application.skipped;
            }
            const auto& result=next->selected[decision.selectedOrdinal];decision.resultSeen08=result.seen08;decision.resultProtected20=result.protected20;
            Need(next->decisions.size()<limits.decisions,"Namespace decision count budget",CompiledDeclFailure::LimitExceeded);
            next->Keep(1,sizeof(CompiledNamespaceDecision));next->KeepEntry(incoming);if(decision.hadPrevious)next->KeepEntry(decision.previous);
            next->KeepString(type);next->KeepString(name);next->KeepString(decision.incomingOwner);next->KeepString(decision.previousOwner);
            next->decisions.push_back(std::move(decision));
        }
    }
    Need(position==positions.size(),"Namespace section contains unaccounted catalog entries");
    application.decisionEnd=next->decisions.size();next->Keep(1,sizeof(CompiledNamespaceApplication));next->applications.push_back(std::move(application));
    impl_.swap(next);
}
std::size_t CompiledDeclarationNamespace::SelectedCount()const{return impl_->selected.size();}
CompiledCatalogEntry CompiledDeclarationNamespace::Selected(std::size_t index)const{
    Need(index<impl_->selected.size(),"Namespace selected index outside range");std::size_t bytes=0;EntryStorage(bytes,impl_->selected[index].entry,impl_->limits.resultBytes);
    return impl_->selected[index].entry;
}
CompiledNamespaceSelected CompiledDeclarationNamespace::SelectedState(std::size_t index)const{
    Need(index<impl_->selected.size(),"Namespace selected state index outside range");const auto& result=impl_->selected[index];std::size_t bytes=0;
    Objects(bytes,1,sizeof(result),impl_->limits.resultBytes);EntryStorage(bytes,result.entry,impl_->limits.resultBytes);
    for(const auto* value:{&result.typeKey,&result.nameKey,&result.ownerLabel})String(bytes,value->size(),impl_->limits.resultBytes);return result;
}
std::vector<CompiledCatalogEntry> CompiledDeclarationNamespace::SelectedEntries()const{
    std::size_t bytes=0;Objects(bytes,impl_->selected.size(),sizeof(CompiledCatalogEntry),impl_->limits.resultBytes);
    for(const auto& entry:impl_->selected)EntryStorage(bytes,entry.entry,impl_->limits.resultBytes);
    std::vector<CompiledCatalogEntry> result;result.reserve(impl_->selected.size());for(const auto& entry:impl_->selected)result.push_back(entry.entry);return result;
}
CompiledCatalogLookup CompiledDeclarationNamespace::Lookup(const std::string& type,const std::string& name)const{
    Label(type,impl_->limits.keyBytes);Label(name,impl_->limits.keyBytes);CompiledCatalogLookup result;
    const auto found=impl_->index.find(TypedKey(Fold(type),Canonical(name)));if(found==impl_->index.end())return result;
    result.matches.push_back(Selected(found->second));result.status=CompiledCatalogLookupStatus::Unique;return result;
}
std::size_t CompiledDeclarationNamespace::DecisionCount()const{return impl_->decisions.size();}
CompiledNamespaceDecision CompiledDeclarationNamespace::Decision(std::size_t index)const{
    Need(index<impl_->decisions.size(),"Namespace decision index outside range");const auto& result=impl_->decisions[index];std::size_t bytes=0;
    Objects(bytes,1,sizeof(result),impl_->limits.resultBytes);EntryStorage(bytes,result.incoming,impl_->limits.resultBytes);
    if(result.hadPrevious)EntryStorage(bytes,result.previous,impl_->limits.resultBytes);
    for(const auto* value:{&result.typeKey,&result.nameKey,&result.incomingOwner,&result.previousOwner})String(bytes,value->size(),impl_->limits.resultBytes);return result;
}
std::size_t CompiledDeclarationNamespace::ApplicationCount()const{return impl_->applications.size();}
CompiledNamespaceApplication CompiledDeclarationNamespace::Application(std::size_t index)const{
    Need(index<impl_->applications.size(),"Namespace application index outside range");std::size_t bytes=0;Objects(bytes,1,sizeof(CompiledNamespaceApplication),impl_->limits.resultBytes);
    return impl_->applications[index];
}
} // namespace wolf2009
