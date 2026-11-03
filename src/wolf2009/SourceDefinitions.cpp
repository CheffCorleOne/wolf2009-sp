// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceDefinitions.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
const std::size_t None=static_cast<std::size_t>(-1);
void Need(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
void Charge(std::size_t& used,std::size_t amount,std::size_t limit,const char* reason) {
    Need(used<=limit&&amount<=limit-used,reason);used+=amount;
}
void StringCharge(std::size_t& used,std::size_t bytes,std::size_t limit,const char* reason) {
    Charge(used,bytes,limit,reason);Charge(used,bytes,limit,reason);Charge(used,2,limit,reason);
}
void MemberStrings(std::size_t& used,const SourceDefinitionMemberIdentity& member,std::size_t limit,const char* reason) {
    StringCharge(used,member.archive.name.size(),limit,reason);StringCharge(used,member.name.size(),limit,reason);
}
std::string Fold(std::string value) {for(char& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
void Label(const std::string& text) {
    Need(!text.empty()&&text.size()<=4096,"Invalid source definition identity/name");
    for(unsigned char c:text)Need(c>=32&&c!=127,"Control byte in source definition identity/name");
}
std::uint64_t Hash(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash=14695981039346656037ull;
    for(auto byte:bytes){hash^=byte;hash*=1099511628211ull;}return hash;
}
bool SameArchive(const SourceDefinitionArchiveIdentity& a,const SourceDefinitionArchiveIdentity& b) {
    return a.name==b.name&&a.bytes==b.bytes&&a.fingerprint==b.fingerprint;
}
std::string Key(SourceDefinitionKind kind,const std::string& name,const std::string& other=std::string()) {
    return std::to_string(static_cast<int>(kind))+":"+(kind==SourceDefinitionKind::Other?Fold(other)+":":"")+Fold(name);
}
SourceDefinitionKind Kind(const std::string& name) {
    if(name=="entityDef")return SourceDefinitionKind::EntityDef;
    if(name=="model")return SourceDefinitionKind::Model;
    return SourceDefinitionKind::Other;
}
std::size_t TokenStorage(const SourceDefinitionToken& token) {
    return sizeof(SourceDefinitionToken)+token.text.size()+token.raw.size();
}
struct Lexer {
    const std::string& text;const SourceDefinitionLimits& limits;
    std::size_t p=0,line=1,column=1,stored=0;
    std::vector<SourceDefinitionToken> tokens;
    Lexer(const std::string& input,const SourceDefinitionLimits& l):text(input),limits(l) {}
    void Advance(){if(text[p++]=='\n'){++line;column=1;}else ++column;}
    bool At(const char* value)const{return text.compare(p,2,value)==0;}
    void Run() {
        while(p<text.size()) {
            const unsigned char c=static_cast<unsigned char>(text[p]);
            if(c<=32){Need(c==' '||c=='\t'||c=='\r'||c=='\n',"Invalid declaration control byte");Advance();continue;}
            if(At("//")){while(p<text.size()&&text[p]!='\n')Advance();continue;}
            if(At("/*")) {
                Advance();Advance();while(p<text.size()&&!At("*/"))Advance();
                Need(p<text.size(),"Unterminated declaration comment");Advance();Advance();continue;
            }
            Need(tokens.size()<limits.tokens,"Source definition token budget exceeded");
            SourceDefinitionToken token;token.begin=p;token.line=line;token.column=column;
            if(c=='{'||c=='}'){token.brace=static_cast<char>(c);token.text.assign(1,static_cast<char>(c));Advance();}
            else if(c=='"') {
                token.quoted=true;Advance();bool closed=false;
                while(p<text.size()) {
                    char ch=text[p];if(ch=='"'){Advance();closed=true;break;}
                    Need((static_cast<unsigned char>(ch)>=32&&ch!=127)||ch=='\t'||ch=='\r'||ch=='\n',
                         "Invalid control byte in quoted declaration token");
                    Advance();
                    if(ch=='\\'&&p<text.size()&&(text[p]=='"'||text[p]=='\\')){ch=text[p];Advance();}
                    Need(token.text.size()<limits.tokenBytes,"Source definition token byte budget exceeded");token.text+=ch;
                }
                Need(closed,"Unterminated declaration quote");
            }else {
                while(p<text.size()&&static_cast<unsigned char>(text[p])>32&&text[p]!='{'&&text[p]!='}'&&!At("//")&&!At("/*")) {
                    Need(text[p]!='"'&&text[p]!=127,"Invalid bare declaration token");
                    Need(token.text.size()<limits.tokenBytes,"Source definition token byte budget exceeded");
                    token.text+=text[p];Advance();
                }
            }
            token.end=p;Need(token.end-token.begin<=2*limits.tokenBytes+2,"Raw declaration token budget exceeded");
            token.raw=text.substr(token.begin,token.end-token.begin);
            Charge(stored,TokenStorage(token),limits.storedBytes,"Declaration decoded token storage budget exceeded");
            tokens.push_back(std::move(token));
        }
    }
};
struct Parsed {
    std::vector<std::shared_ptr<const SourceDefinition>> definitions;
    std::size_t tokens=0,stored=0;
};
Parsed Parse(const SourceDefinitionMemberIdentity& member,const std::vector<unsigned char>& bytes,const SourceDefinitionLimits& limits) {
    const std::string text(bytes.begin(),bytes.end());Lexer lex(text,limits);lex.Run();
    Parsed out;out.tokens=lex.tokens.size();out.stored=lex.stored;
    const auto& tokens=lex.tokens;std::vector<std::size_t> close(tokens.size(),None),stack;
    for(std::size_t n=0;n<tokens.size();++n) {
        if(tokens[n].brace=='{'){Need(stack.size()<limits.nesting,"Declaration nesting budget exceeded");stack.push_back(n);}
        else if(tokens[n].brace=='}'){Need(!stack.empty(),"Unmatched closing declaration brace");close[stack.back()]=n;stack.pop_back();}
    }
    Need(stack.empty(),"Unterminated declaration brace");
    std::size_t p=0;
    while(p<tokens.size()) {
        Need(tokens.size()-p>=3&&!tokens[p].brace&&!tokens[p+1].brace&&tokens[p+2].brace=='{',
             "Expected a named source declaration");
        Need(out.definitions.size()<limits.declarations,"Source definition count budget exceeded");
        Label(tokens[p].text);Label(tokens[p+1].text);
        const auto begin=p,body=p+3,end=close[p+2];Need(end!=None,"Declaration body is not bounded");
        // Includes every repeated identity copy and the future catalog index
        // key/node. Charge before allocating these retained strings.
        Charge(out.stored,sizeof(SourceDefinition)+2*sizeof(std::shared_ptr<const SourceDefinition>)+128,
               limits.storedBytes,"Declaration metadata/index storage budget exceeded");
        MemberStrings(out.stored,member,limits.storedBytes,"Repeated declaration source identity budget exceeded");
        const auto kind=Kind(tokens[p].text);
        const auto keyBytes=2+tokens[p+1].text.size()+(kind==SourceDefinitionKind::Other?tokens[p].text.size()+1:0);
        StringCharge(out.stored,keyBytes,limits.storedBytes,"Declaration index key budget exceeded");
        StringCharge(out.stored,tokens[end].end-tokens[begin].begin,limits.storedBytes,"Declaration raw text budget exceeded");
        std::shared_ptr<SourceDefinition> decl(new SourceDefinition);
        decl->kindToken=tokens[p];decl->nameToken=tokens[p+1];decl->kind=kind;decl->member=member;
        decl->begin=tokens[begin].begin;decl->end=tokens[end].end;
        decl->rawText=text.substr(decl->begin,decl->end-decl->begin);
        p=body;std::set<std::string> ownKeys;std::size_t parents=0;
        while(p<end) {
            const auto start=p;SourceDefinitionItem item;item.key=tokens[p++];
            const auto key=Fold(item.key.text);
            if(decl->kind==SourceDefinitionKind::Other) {p=end;}
            else if(key=="inherit") {
                Need(p<end&&!tokens[p].brace,"Missing source parent value");++p;item.kind=SourceDefinitionItemKind::Inherit;
            }
            else if(item.key.brace=='{'){p=close[start]+1;}
            else if(p<end&&tokens[p].brace=='{'){p=close[p]+1;}
            else if(decl->kind==SourceDefinitionKind::EntityDef||key=="mesh"||key=="skeleton"||key=="skin") {
                Need(p<end&&!tokens[p].brace,"Missing source literal/parent value");++p;
                item.kind=SourceDefinitionItemKind::Literal;
            }else {
                // Unknown model commands are retained as complete line/block
                // facts. Their nested frame commands are never interpreted.
                while(p<end&&!tokens[p].brace&&tokens[p].line==item.key.line)++p;
                if(p<end&&tokens[p].brace=='{'){Need(key!="offset","Block in model offset literal");p=close[p]+1;}
                if(key=="offset") {Need(p>start+1,"Missing model offset literal");item.kind=SourceDefinitionItemKind::Literal;}
            }
            Need(p<=end&&p>start,"Malformed source declaration item");
            item.begin=tokens[start].begin;item.end=tokens[p-1].end;
            for(std::size_t n=start+1;n<p;++n){Charge(out.stored,TokenStorage(tokens[n]),limits.storedBytes,"Declaration item token budget exceeded");item.values.push_back(tokens[n]);}
            Charge(out.stored,TokenStorage(item.key)+sizeof(SourceDefinitionItem),limits.storedBytes,"Declaration item storage budget exceeded");
            item.rawText=text.substr(item.begin,item.end-item.begin);
            Charge(out.stored,item.rawText.size(),limits.storedBytes,"Opaque declaration storage budget exceeded");
            if(item.kind==SourceDefinitionItemKind::Inherit) {
                Label(item.values[0].text);Need(++parents<=limits.parentsPerDeclaration,"Source parent count budget exceeded");
            }else if(item.kind==SourceDefinitionItemKind::Literal)
                Need(ownKeys.insert(key).second,"Duplicate source literal key under explicit lookup policy");
            decl->items.push_back(std::move(item));
        }
        out.definitions.push_back(std::move(decl));p=end+1;
    }
    return out;
}
struct Ref {
    std::size_t declaration,item;
    Ref(std::size_t d=0,std::size_t i=0):declaration(d),item(i){}
    bool operator<(const Ref& b)const{return declaration<b.declaration||(declaration==b.declaration&&item<b.item);}
    bool operator==(const Ref& b)const{return declaration==b.declaration&&item==b.item;}
};
struct Edge {Ref parent;std::size_t target;bool map;};
struct Intermediate {std::map<std::string,std::set<Ref>> fields;std::set<Ref> opaque;};
SourceDefinitionProvenance Provenance(const SourceDefinition& decl,std::size_t item=None) {
    SourceDefinitionProvenance out;out.member=decl.member;out.declarationKind=decl.kindToken.text;out.declarationName=decl.nameToken.text;
    out.rawDeclarationKind=decl.kindToken.raw;out.rawDeclarationName=decl.nameToken.raw;
    out.declarationBegin=decl.begin;out.declarationEnd=decl.end;out.item=item;
    out.begin=item==None?decl.begin:decl.items[item].begin;out.end=item==None?decl.end:decl.items[item].end;return out;
}
} // namespace
struct SourceDefinitions::Impl {
    SourceDefinitionLimits limits;
    std::vector<SourceDefinitionMemberIdentity> members;
    std::vector<std::shared_ptr<const SourceDefinition>> definitions;
    std::map<std::string,std::size_t> index;
    std::size_t sourceBytes=0,tokens=0,stored=0;
    explicit Impl(const SourceDefinitionLimits& l):limits(l){}
};
SourceDefinitions::SourceDefinitions(const SourceDefinitionLimits& limits):impl_(new Impl(limits)) {
    Need(limits.members&&limits.sourceBytesPerMember&&limits.totalSourceBytes&&limits.declarations&&limits.tokens&&
         limits.tokenBytes&&limits.tokenBytes<=1048576&&limits.storedBytes&&limits.nesting&&limits.nesting<=256&&
         limits.parentsPerDeclaration&&limits.inheritanceDepth&&limits.inheritanceDepth<=256&&
         limits.resolutionWork&&limits.resolutionBytes&&limits.resultFacts&&limits.resultBytes,"Invalid source definition limits");
}
SourceDefinitions::~SourceDefinitions(){}
void SourceDefinitions::AddMember(const SourceDefinitionArchiveIdentity& archive,const std::string& name,
                                  const std::vector<unsigned char>& bytes) {
    Label(archive.name);Label(name);Need(archive.bytes,"Missing supplied archive byte identity");
    const auto& l=impl_->limits;
    Need(impl_->members.size()<l.members&&!bytes.empty()&&bytes.size()<=l.sourceBytesPerMember,"Source member count/byte budget exceeded");
    for(const auto& old:impl_->members)if(old.archive.name==archive.name) {
        Need(SameArchive(old.archive,archive),"Conflicting supplied archive identity");
        Need(Fold(old.name)!=Fold(name),"Duplicate source member under explicit lookup policy");
    }
    std::size_t total=impl_->sourceBytes;Charge(total,bytes.size(),l.totalSourceBytes,"Total source definition byte budget exceeded");
    SourceDefinitionMemberIdentity member;member.archive=archive;member.name=name;member.bytes=bytes.size();member.fingerprint=Hash(bytes);
    std::size_t identityBytes=2*sizeof(SourceDefinitionMemberIdentity);
    MemberStrings(identityBytes,member,l.storedBytes,"Source member identity storage budget exceeded");
    auto remaining=l;remaining.tokens-=impl_->tokens;remaining.storedBytes-=impl_->stored;
    Need(identityBytes<=remaining.storedBytes,"Total source identity storage budget exceeded");remaining.storedBytes-=identityBytes;
    remaining.declarations-=impl_->definitions.size();
    Parsed parsed;
    try {parsed=Parse(member,bytes,remaining);}
    catch(const std::exception& error){throw std::runtime_error(name+": "+error.what());}
    std::size_t tokenTotal=impl_->tokens,storedTotal=impl_->stored;
    Charge(tokenTotal,parsed.tokens,l.tokens,"Total source definition token budget exceeded");
    Charge(storedTotal,parsed.stored,l.storedBytes,"Total source definition storage budget exceeded");
    Charge(storedTotal,identityBytes,l.storedBytes,"Total source member identity storage budget exceeded");
    Need(parsed.definitions.size()<=l.declarations-impl_->definitions.size(),"Total source definition count budget exceeded");
    // Copy only metadata/shared immutable objects; commit after every duplicate
    // check succeeds. Failed parsing or allocation never changes the catalog.
    std::unique_ptr<Impl> next(new Impl(*impl_));
    for(const auto& declaration:parsed.definitions) {
        const auto key=Key(declaration->kind,declaration->nameToken.text,declaration->kindToken.text);
        Need(next->index.emplace(key,next->definitions.size()).second,"Duplicate named source declaration under explicit lookup policy");
        next->definitions.push_back(declaration);
    }
    next->members.push_back(member);next->sourceBytes=total;next->tokens=tokenTotal;next->stored=storedTotal;impl_.swap(next);
}
std::size_t SourceDefinitions::MemberCount()const{return impl_->members.size();}
std::size_t SourceDefinitions::DefinitionCount()const{return impl_->definitions.size();}
const SourceDefinition& SourceDefinitions::Definition(std::size_t index)const {
    Need(index<impl_->definitions.size(),"Source definition index outside catalog");return *impl_->definitions[index];
}
const SourceResolvedLiteral* ResolvedSourceDefinition::Find(const std::string& key)const {
    const auto at=literalFields.find(Fold(key));return at==literalFields.end()?NULL:&at->second;
}
ResolvedSourceDefinition SourceDefinitions::Resolve(SourceDefinitionKind kind,const std::string& name,const SourceDefinitionContext& context)const {
    Need(kind==SourceDefinitionKind::EntityDef||kind==SourceDefinitionKind::Model,"Opaque declaration kind cannot be resolved as supported data");
    Need(context.policy==SourceDefinitionPolicy::AsciiCaseFoldLiteralChildOverridesV1,"Unknown source definition resolver policy");Label(name);
    struct Resolver {
        const Impl& src;const SourceDefinitionContext& context;
        std::size_t work=0,workingBytes=0,resultBytes=0,resultFacts=0;
        std::map<std::size_t,unsigned> state;std::map<std::size_t,Intermediate> memo;
        std::vector<std::size_t> order;std::vector<Edge> edges;
        Resolver(const Impl& s,const SourceDefinitionContext& c):src(s),context(c){}
        void Work(std::size_t n=1){Charge(work,n,src.limits.resolutionWork,"Source inheritance work budget exceeded");}
        void Memory(std::size_t n){Charge(workingBytes,n,src.limits.resolutionBytes,"Source inheritance metadata memory budget exceeded");}
        void MemoryKey(const std::string& key) {
            Memory(128+sizeof(std::string));StringCharge(workingBytes,key.size(),src.limits.resolutionBytes,"Source inheritance key memory budget exceeded");
        }
        std::size_t Find(SourceDefinitionKind k,const std::string& n) {
            const auto at=src.index.find(Key(k,n));Need(at!=src.index.end(),"Selected source declaration/parent is missing");return at->second;
        }
        bool Same(const Ref& a,const Ref& b)const {
            const auto& x=src.definitions[a.declaration]->items[a.item].values;
            const auto& y=src.definitions[b.declaration]->items[b.item].values;
            if(x.size()!=y.size())return false;
            for(std::size_t n=0;n<x.size();++n)if(x[n].text!=y[n].text||x[n].brace!=y[n].brace)return false;
            return true;
        }
        const Intermediate& Visit(std::size_t id,std::size_t depth) {
            Work();Need(depth<=src.limits.inheritanceDepth,"Source inheritance depth budget exceeded");
            auto visit=state.find(id);
            if(visit==state.end()){Memory(128+sizeof(Intermediate)+sizeof(std::size_t));visit=state.emplace(id,0).first;}
            Need(visit->second!=1,"Source definition inheritance cycle");if(visit->second==2)return memo.at(id);visit->second=1;
            const auto& decl=*src.definitions[id];Intermediate out;std::set<std::string> own;
            for(const auto& item:decl.items)if(item.kind==SourceDefinitionItemKind::Literal){MemoryKey(item.key.text);own.insert(Fold(item.key.text));}
            for(std::size_t n=0;n<decl.items.size();++n) {
                const auto& item=decl.items[n];if(item.kind!=SourceDefinitionItemKind::Inherit)continue;
                const bool map=decl.kind==SourceDefinitionKind::Model&&item.values[0].text=="MAPDEF";
                Need(!map||!context.mapModelDefinition.empty(),"Literal MAPDEF requires explicit named caller context");
                const auto parent=Find(decl.kind,map?context.mapModelDefinition:item.values[0].text);
                Edge edge;edge.parent=Ref(id,n);edge.target=parent;edge.map=map;Memory(2*sizeof(Edge));edges.push_back(edge);
                const auto& inherited=Visit(parent,depth+1);
                for(const auto& field:inherited.fields) {
                    Work();
                    if(own.count(field.first))continue;
                    auto at=out.fields.find(field.first);
                    if(at==out.fields.end()){MemoryKey(field.first);at=out.fields.emplace(field.first,std::set<Ref>()).first;}
                    auto& result=at->second;
                    for(const auto& origin:field.second) {
                        Work();Need(result.empty()||Same(*result.begin(),origin),"Conflicting inherited literal values require an explicit child override");
                        if(!result.count(origin)){Memory(128+sizeof(Ref));result.insert(origin);}
                    }
                }
                for(const auto& fact:inherited.opaque){Work();if(!out.opaque.count(fact)){Memory(128+sizeof(Ref));out.opaque.insert(fact);}}
            }
            for(std::size_t n=0;n<decl.items.size();++n) {
                Work();const auto& item=decl.items[n];
                if(item.kind==SourceDefinitionItemKind::Literal){MemoryKey(item.key.text);Memory(128+sizeof(Ref));out.fields[Fold(item.key.text)]={Ref(id,n)};}
                else if(item.kind==SourceDefinitionItemKind::Opaque){Memory(128+sizeof(Ref));out.opaque.insert(Ref(id,n));}
            }
            Memory(2*sizeof(std::size_t)+128+sizeof(Intermediate));order.push_back(id);visit->second=2;return memo.emplace(id,std::move(out)).first->second;
        }
        void Bytes(std::size_t n){Charge(resultBytes,n,src.limits.resultBytes,"Resolved source provenance byte budget exceeded");}
        SourceDefinitionProvenance Origin(std::size_t id,std::size_t item=None) {
            const auto& d=*src.definitions[id];
            Bytes(sizeof(SourceDefinitionProvenance));
            MemberStrings(resultBytes,d.member,src.limits.resultBytes,"Resolved source identity byte budget exceeded");
            StringCharge(resultBytes,d.kindToken.text.size(),src.limits.resultBytes,"Resolved declaration kind byte budget exceeded");
            StringCharge(resultBytes,d.nameToken.text.size(),src.limits.resultBytes,"Resolved declaration name byte budget exceeded");
            StringCharge(resultBytes,d.kindToken.raw.size(),src.limits.resultBytes,"Resolved raw declaration kind byte budget exceeded");
            StringCharge(resultBytes,d.nameToken.raw.size(),src.limits.resultBytes,"Resolved raw declaration name byte budget exceeded");
            return Provenance(d,item);
        }
        SourceDefinitionFact Fact(const Ref& ref) {
            Charge(resultFacts,1,src.limits.resultFacts,"Resolved source fact budget exceeded");
            const auto& item=src.definitions[ref.declaration]->items[ref.item];
            Bytes(sizeof(SourceDefinitionFact)+item.rawText.size()+TokenStorage(item.key));
            for(const auto& token:item.values)Bytes(TokenStorage(token));
            SourceDefinitionFact out;out.origin=Origin(ref.declaration,ref.item);out.data=item;return out;
        }
        ResolvedSourceDefinition Run(SourceDefinitionKind kind,const std::string& name) {
            const auto selected=Find(kind,name);const auto& resolved=Visit(selected,1);
            ResolvedSourceDefinition out;out.policy=context.policy;out.selected=Origin(selected);
            for(auto id:order)out.declarations.push_back(Origin(id));
            for(const auto& edge:edges) {
                SourceDefinitionInheritance value;value.parent=Fact(edge.parent);value.resolved=Origin(edge.target);value.explicitMapContext=edge.map;
                out.inheritance.push_back(std::move(value));
            }
            for(const auto& entry:resolved.fields) {
                SourceResolvedLiteral value;value.lookupKey=entry.first;Bytes(entry.first.size());
                const auto first=*entry.second.begin();const auto& source=src.definitions[first.declaration]->items[first.item].values;
                for(const auto& token:source)Bytes(TokenStorage(token));value.value=source;
                for(const auto& ref:entry.second)value.origins.push_back(Fact(ref));
                out.literalFields.emplace(entry.first,std::move(value));
            }
            for(const auto& ref:resolved.opaque)out.opaqueFacts.push_back(Fact(ref));return out;
        }
    } resolver(*impl_,context);
    return resolver.Run(kind,name);
}
const SourceModelAnimationReference* ResolvedSourceModelAnimations::Find(const std::string& name)const {
    const auto at=references.find(Fold(name));return at==references.end()?NULL:&at->second;
}
ResolvedSourceModelAnimations SourceDefinitions::ResolveModelAnimations(const std::string& name,const SourceDefinitionContext& context)const {
    // Do not bypass a missing/cyclic/conflicting dependency behind an anim
    // override. The ordinary resolver remains the first, unchanged gate.
    auto model=Resolve(SourceDefinitionKind::Model,name,context);
    struct AnimationResolver {
        typedef std::map<std::string,std::set<Ref>> Table;
        struct Header {SourceAnimationReferenceShape shape=SourceAnimationReferenceShape::OtherUnimplemented;bool asset=false;};
        const Impl& src;const SourceDefinitionContext& context;
        std::size_t work=0,memory=0,resultBytes=0,resultFacts=0;
        std::map<std::size_t,Table> memo;
        AnimationResolver(const Impl& s,const SourceDefinitionContext& c):src(s),context(c){}
        void Work(){Charge(work,1,src.limits.resolutionWork,"Animation reference work budget exceeded");}
        void Memory(std::size_t bytes){Charge(memory,bytes,src.limits.resolutionBytes,"Animation reference merge memory budget exceeded");}
        void MemoryKey(const std::string& key){Memory(128+sizeof(std::string));StringCharge(memory,key.size(),src.limits.resolutionBytes,"Animation merge key budget exceeded");}
        void Output(std::size_t bytes){Charge(resultBytes,bytes,src.limits.resultBytes,"Animation reference result byte budget exceeded");}
        void OutputString(const std::string& value){StringCharge(resultBytes,value.size(),src.limits.resultBytes,"Animation result string budget exceeded");}
        void Token(const SourceDefinitionToken& token){Output(2*sizeof(SourceDefinitionToken));OutputString(token.text);OutputString(token.raw);}
        void Origin(const SourceDefinitionProvenance& origin){
            Output(2*sizeof(SourceDefinitionProvenance));MemberStrings(resultBytes,origin.member,src.limits.resultBytes,"Animation result source identity budget exceeded");
            OutputString(origin.declarationKind);OutputString(origin.declarationName);OutputString(origin.rawDeclarationKind);OutputString(origin.rawDeclarationName);
        }
        void SourceOrigin(const SourceDefinition& source){
            Output(2*sizeof(SourceDefinitionProvenance));MemberStrings(resultBytes,source.member,src.limits.resultBytes,"Animation result source identity budget exceeded");
            OutputString(source.kindToken.text);OutputString(source.nameToken.text);OutputString(source.kindToken.raw);OutputString(source.nameToken.raw);
        }
        void Fact(const SourceDefinitionFact& fact){
            Charge(resultFacts,1,src.limits.resultFacts,"Animation reference result fact budget exceeded");
            Output(2*sizeof(SourceDefinitionFact));Origin(fact.origin);Token(fact.data.key);OutputString(fact.data.rawText);
            for(const auto& token:fact.data.values){Work();Token(token);}
        }
        void Model(const ResolvedSourceDefinition& model){
            Output(sizeof(ResolvedSourceModelAnimations)+sizeof(ResolvedSourceDefinition));Origin(model.selected);
            for(const auto& origin:model.declarations){Work();Origin(origin);}
            for(const auto& edge:model.inheritance){Work();Output(2*sizeof(SourceDefinitionInheritance));Fact(edge.parent);Origin(edge.resolved);}
            for(const auto& field:model.literalFields){Work();Output(128+sizeof(SourceResolvedLiteral));OutputString(field.first);OutputString(field.second.lookupKey);
                for(const auto& token:field.second.value){Work();Token(token);}for(const auto& fact:field.second.origins)Fact(fact);}
            for(const auto& fact:model.opaqueFacts){Work();Fact(fact);}
        }
        std::size_t Find(const std::string& value)const {
            const auto found=src.index.find(Key(SourceDefinitionKind::Model,value));
            Need(found!=src.index.end(),"Animation reference dependency disappeared");return found->second;
        }
        bool Atom(const SourceDefinitionToken& token)const {
            if(token.brace||token.text.empty())return false;
            for(unsigned char c:token.text)if(c<=32||c==127)return false;
            // The catalog deliberately does not split bare punctuation. Do not
            // mislabel a no-whitespace multipoint/list as one asset reference.
            return token.quoted||token.text.find_first_of(",()[]")==std::string::npos;
        }
        Header Classify(const SourceDefinitionItem& item){
            Header out;std::size_t scalars=0;
            while(scalars<item.values.size()&&!item.values[scalars].brace){Work();++scalars;}
            if(scalars!=2||item.values[0].line!=item.key.line||item.values[1].line!=item.key.line||!Atom(item.values[0])||!Atom(item.values[1]))return out;
            if(scalars==item.values.size()){out.asset=true;out.shape=SourceAnimationReferenceShape::SingleLiteralNoTail;}
            else if(item.values[scalars].brace=='{'&&item.values.back().brace=='}'){
                out.asset=true;out.shape=SourceAnimationReferenceShape::LiteralHeaderWithOpaqueBlock;
            }
            return out;
        }
        bool Same(const Ref& a,const Ref& b){
            const auto& x=src.definitions[a.declaration]->items[a.item];const auto& y=src.definitions[b.declaration]->items[b.item];
            const auto hx=Classify(x),hy=Classify(y);if(hx.shape!=hy.shape||hx.asset!=hy.asset||x.values.size()!=y.values.size())return false;
            // Name lookup is folded, but values stay byte-sensitive. Quoting
            // alone may differ when both records have the same classification.
            for(std::size_t i=1;i<x.values.size();++i){Work();if(x.values[i].text!=y.values[i].text||x.values[i].brace!=y.values[i].brace)return false;}
            return true;
        }
        void Merge(std::size_t id){
            Work();const auto& decl=*src.definitions[id];Table table;std::map<std::string,Ref> own;
            Memory(sizeof(Table)+128);
            for(std::size_t n=0;n<decl.items.size();++n){
                Work();const auto& item=decl.items[n];if(item.kind!=SourceDefinitionItemKind::Opaque)continue;
                const auto command=Fold(item.key.text);
                Need(command!="remove","Model animation reference policy does not implement remove");
                // Unknown items are line-bounded by the existing catalog. A
                // second namespace command on that line could otherwise hide
                // an own name and expose its inherited entry. Blocks remain
                // opaque; keywords inside their bodies are not model commands.
                std::size_t parameter=0;
                for(const auto& token:item.values){Work();if(token.brace)break;if(command=="anim"&&parameter++<2)continue;if(token.quoted)continue;
                    const auto nested=Fold(token.text);Need(nested!="anim"&&nested!="remove"&&nested!="inherit","Ambiguous namespace command in an opaque model line");}
                if(command!="anim")continue;
                Need(!item.values.empty()&&Atom(item.values[0]),"Animation reference has no bounded literal logical name");Label(item.values[0].text);
                MemoryKey(item.values[0].text);Memory(sizeof(Ref)+128);
                Need(own.emplace(Fold(item.values[0].text),Ref(id,n)).second,"Duplicate own animation name under explicit lookup policy");
            }
            for(const auto& item:decl.items){
                Work();if(item.kind!=SourceDefinitionItemKind::Inherit)continue;
                const auto parent=Find(item.values[0].text=="MAPDEF"?context.mapModelDefinition:item.values[0].text);
                const auto prior=memo.find(parent);Need(prior!=memo.end(),"Animation reference closure is not parent-first");
                for(const auto& entry:prior->second){
                    Work();if(own.count(entry.first))continue;
                    auto at=table.find(entry.first);if(at==table.end()){MemoryKey(entry.first);Memory(sizeof(std::set<Ref>));at=table.emplace(entry.first,std::set<Ref>()).first;}
                    for(const auto& ref:entry.second){Work();Need(at->second.empty()||Same(*at->second.begin(),ref),"Conflicting inherited animation references require an explicit child override");
                        if(!at->second.count(ref)){Memory(128+sizeof(Ref));at->second.insert(ref);}}
                }
            }
            for(const auto& entry:own){Work();MemoryKey(entry.first);Memory(sizeof(std::set<Ref>)+128+sizeof(Ref));table[entry.first]={entry.second};}
            Memory(128+sizeof(Table)+sizeof(std::size_t));memo.emplace(id,std::move(table));
        }
        ResolvedSourceModelAnimations Run(ResolvedSourceDefinition model){
            Model(model); // Includes moved ordinary facts and all repeated IDs.
            for(const auto& declaration:model.declarations)Merge(Find(declaration.declarationName));
            const auto& table=memo.at(Find(model.selected.declarationName));ResolvedSourceModelAnimations out;
            out.model=std::move(model);
            for(const auto& entry:table){
                Work();const auto first=*entry.second.begin();const auto& decl=*src.definitions[first.declaration];const auto& item=decl.items[first.item];const auto header=Classify(item);
                Output(128+sizeof(SourceModelAnimationReference));OutputString(entry.first);OutputString(entry.first);Token(item.values[0]);if(header.asset)Token(item.values[1]);
                SourceModelAnimationReference result;result.lookupKey=entry.first;result.logicalName=item.values[0];result.shape=header.shape;result.hasLiteralAssetReference=header.asset;
                if(header.asset)result.assetReference=item.values[1];
                for(const auto& ref:entry.second){Work();const auto& source=*src.definitions[ref.declaration];
                    // Charge before copying any owning provenance/token strings.
                    const auto& originItem=source.items[ref.item];Charge(resultFacts,1,src.limits.resultFacts,"Animation reference result fact budget exceeded");
                    Output(2*sizeof(SourceDefinitionFact));SourceOrigin(source);Token(originItem.key);OutputString(originItem.rawText);for(const auto& token:originItem.values){Work();Token(token);}
                    SourceDefinitionFact fact;fact.origin=Provenance(source,ref.item);fact.data=originItem;result.origins.push_back(std::move(fact));
                }
                out.references.emplace(entry.first,std::move(result));
            }
            return out;
        }
    } resolver(*impl_,context);
    return resolver.Run(std::move(model));
}
} // namespace wolf2009
