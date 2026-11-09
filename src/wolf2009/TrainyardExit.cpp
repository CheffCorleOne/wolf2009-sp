// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrainyardExit.h"
#include "HavokPack.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
const double MetresPerUnit=0.0254;
struct Output {std::vector<std::string> tokens;std::vector<std::vector<std::string>> conditions;};
struct Node {std::uint32_t type=0,id=0;std::vector<std::string> tokens;std::vector<Output> outputs;};
std::uint32_t Unsigned(const std::string& text){
    Need(!text.empty()&&text.size()<=10&&(text.size()==1||text[0]!='0'),"Brain integer token is not canonical");
    std::uint64_t value=0;for(unsigned char c:text){Need(c>='0'&&c<='9',"Brain integer token byte");value=value*10+(c-'0');}
    Need(value<=0xffffffffull,"Brain integer token overflow");return std::uint32_t(value);
}
std::map<std::uint32_t,Node> Nodes(const std::vector<unsigned char>& data){
    Need(!data.empty()&&data.size()<=(4u<<20),"Brain byte budget");
    std::map<std::uint32_t,Node> nodes;Node* owner=NULL;std::size_t cursor=0,record=0,tokens=0;
    while(cursor<data.size()){
        Need(++record<=20000,"Brain record budget");std::size_t depth=0;
        while(cursor<data.size()&&data[cursor]=='\t'){++depth;++cursor;}
        Need(depth<=64,"Brain nesting budget");std::vector<std::string> row;
        while(cursor<data.size()&&data[cursor]!='\n'){
            Need(++tokens<=500000,"Brain token budget");std::string token;
            while(cursor<data.size()&&data[cursor]!=0){const unsigned char c=data[cursor++];
                Need(token.size()<1024,"Brain token byte budget");token+=char(c);}
            Need(cursor<data.size(),"Unterminated brain token");++cursor;row.push_back(token);
        }
        if(cursor<data.size())++cursor;
        Need(!row.empty(),"Empty brain record");
        if(record==1){Need(depth==0&&row==std::vector<std::string>({"h1023","32","32","32","0"}),"Unsupported brain envelope");continue;}
        if(depth==0){Need(row.size()>=7,"Truncated brain node header");Node node;node.type=Unsigned(row[0]);node.id=Unsigned(row[1]);node.tokens=row;
            const auto inserted=nodes.insert(std::make_pair(node.id,node));Need(inserted.second,"Duplicate brain node ID");owner=&inserted.first->second;}
        else if(depth==1){Need(owner!=NULL,"Brain output without node");Output output;output.tokens=row;owner->outputs.push_back(output);}
        // Deeper condition records stay attached (marked) so that any output
        // carrying them is refused by Single(); unrelated nodes stay opaque.
        else{Need(owner!=NULL&&!owner->outputs.empty(),"Brain condition record without an output");if(depth>2)row.insert(row.begin(),"<nested>");owner->outputs.back().conditions.push_back(row);}
    }
    return nodes;
}
const Node& At(const std::map<std::uint32_t,Node>& nodes,std::uint32_t id,std::uint32_t type){
    const auto found=nodes.find(id);Need(found!=nodes.end()&&found->second.type==type,"Measured brain node is absent or has another type");return found->second;}
// Header: type id m m m ^type flags [^condition 1024]* key value...
std::map<std::string,std::string> Properties(const Node& node,std::uint32_t primary){
    const auto& t=node.tokens;Need(t[5]=="^"+std::to_string(node.type),"Brain node type marker differs");
    // Serialized runtime flags (0/1/16/32) are retained state, never semantics.
    Need(t[6]=="0"||t[6]=="1"||t[6]=="16"||t[6]=="32","Brain node serialized flag word is outside the measured set");
    std::size_t at=7;
    if(primary){Need(t.size()>=9&&t[7]=="^"+std::to_string(primary)&&t[8]=="1024","Brain node primary condition differs");at=9;}
    else Need(t.size()==7||t[7].empty()||t[7][0]!='^',"Brain node has an unadmitted primary condition");
    Need((t.size()-at)%2==0,"Brain node property list is not key/value pairs");
    std::map<std::string,std::string> out;for(;at<t.size();at+=2)Need(out.insert(std::make_pair(t[at],t[at+1])).second,"Duplicate brain node property");
    for(unsigned i=2;i<=4;++i)Need(!t[i].empty(),"Brain node editor metadata is empty");return out;
}
std::string Property(const std::map<std::string,std::string>& p,const char* key){const auto f=p.find(key);Need(f!=p.end(),"Measured brain node property is absent");return f->second;}
struct Edge {std::uint32_t flags=0,header=0,target=0,aux=0;bool primary=false;};
Edge Single(const Output& output){
    const auto& t=output.tokens;Need(t.size()==99&&t[98]=="1.000000","Brain output is not the fixed 32-destination unit-decay layout");
    Edge edge;edge.flags=Unsigned(t[0]);edge.header=Unsigned(t[1]);unsigned populated=0;
    for(unsigned s=0;s<32;++s){const auto& id=t[2+3*s];if(id=="0")continue;Need(s==0,"Brain output has a secondary destination slot");
        Need(t[3+3*s]=="0","Brain output destination weight differs");edge.target=Unsigned(id);edge.aux=Unsigned(t[4+3*s]);++populated;}
    for(unsigned s=1;s<32;++s)Need(t[3+3*s]=="0"&&t[4+3*s]=="0","Brain output unused slot fields differ");
    Need(populated==1&&edge.target,"Brain output does not name exactly one destination");
    if(!output.conditions.empty()){Need(output.conditions.size()==1&&output.conditions[0]==std::vector<std::string>({"-1"}),"Brain output carries an unadmitted condition record");edge.primary=true;}
    return edge;
}
Edge Only(const Node& node){Need(node.outputs.size()==1,"Measured brain node output count differs");return Single(node.outputs[0]);}
void Expect(const Edge& e,std::uint32_t flags,std::uint32_t header,std::uint32_t target,std::uint32_t aux,bool primary){
    Need(e.flags==flags&&e.header==header&&e.target==target&&e.aux==aux&&e.primary==primary,"Measured brain output profile differs");}
void Name(const std::string& value){Need(!value.empty()&&value.size()<=256,"Brain literal name budget");
    for(unsigned char c:value)Need((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_',"Brain literal is not a plain entity/key name");}
bool FoldEqual(const std::string& a,const std::string& b){if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){unsigned char x=a[i],y=b[i];if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}return true;}
}

SourceWaitDuration ParseSourceWaitDurationLiteralV1(const std::string& raw){
    Need(raw.size()<=64,"Wait duration literal budget");const auto space=raw.find(' ');
    Need(space!=std::string::npos&&space>0&&raw.find(' ',space+1)==std::string::npos&&space+2==raw.size(),"Wait duration is not '<value> <unit>'");
    SourceWaitDuration out;out.raw=raw;out.value=raw.substr(0,space);const char u=raw[space+1];
    Need(u=='0'||u=='1',"Wait duration unit is outside the established 0/1 encodings");out.unit=u-'0';
    unsigned digits=0,dots=0;for(char c:out.value){if(c=='.')++dots;else{Need(c>='0'&&c<='9',"Wait duration value is not a plain nonnegative decimal");++digits;}}
    Need(digits>0&&dots<=1&&out.value.size()<=16,"Wait duration value is not a plain nonnegative decimal");
    // float32 value, x1000.0f iff unit 1, +0.5f, then round down (static 0x20210150).
    float value=float(std::strtod(out.value.c_str(),NULL));if(out.unit==1)value*=1000.0f;value+=0.5f;
    Need(std::isfinite(value)&&value>=0&&value<2147483520.0f,"Wait duration exceeds the integer clock");
    out.milliseconds=std::int32_t(std::floor(value));return out;
}

TrainyardMovetrainTail DecodeTrainyardMovetrainTailH1023(const std::vector<unsigned char>& brai){
    const auto nodes=Nodes(brai);TrainyardMovetrainTail out;
    const auto& relay=At(nodes,6,68);const auto r=Properties(relay,0);Need(r.size()==2,"Movetrain relay node property set differs");
    out.relayNode=6;out.relayTarget=Property(r,"mUseTarget");out.relayKey=Property(r,"mUseKey");Name(out.relayTarget);Name(out.relayKey);
    Expect(Only(relay),16,3,7,2,false);
    const auto& wait=At(nodes,7,30);const auto w=Properties(wait,65);Need(w.size()==4,"Movetrain Wait property set differs");
    out.waitNode=7;out.wait=ParseSourceWaitDurationLiteralV1(Property(w,"mDuration"));out.waitMax=ParseSourceWaitDurationLiteralV1(Property(w,"mDurationMax"));
    // mActualDuration/mTimeSet are serialized runtime samples, not inputs.
    Need(out.waitMax.milliseconds==0,"Movetrain Wait random maximum is not admitted");(void)Property(w,"mActualDuration");(void)Property(w,"mTimeSet");
    Expect(Only(wait),16,3,8,2,true);
    const auto& remove=At(nodes,8,70);const auto m=Properties(remove,0);Need(m.size()==1,"Movetrain RemoveTarget property set differs");
    out.removeNode=8;out.removeTarget=Property(m,"mRemoveTarget");Name(out.removeTarget);Expect(Only(remove),16,3,9,2,false);
    const auto& console=At(nodes,9,456);const auto c=Properties(console,0);Need(c.size()==1&&console.outputs.empty(),"Movetrain console node profile differs");
    out.consoleNode=9;out.consoleType=456;out.consoleCommand=Property(c,"mCommand");Name(out.consoleCommand);
    return out;
}

TrainyardShakeArming DecodeTrainyardShakeArmingH1023(const std::vector<unsigned char>& brai){
    const auto nodes=Nodes(brai);TrainyardShakeArming out;
    const auto& spawn=At(nodes,1,1);Need(Properties(spawn,74).empty(),"shake_train OnSpawn properties differ");out.onSpawnNode=1;Expect(Only(spawn),16,1,30,0,true);
    const auto& wait=At(nodes,30,30);const auto w=Properties(wait,65);Need(w.size()==4,"shake_train Wait property set differs");out.waitNode=30;
    out.wait=ParseSourceWaitDurationLiteralV1(Property(w,"mDuration"));out.waitMax=ParseSourceWaitDurationLiteralV1(Property(w,"mDurationMax"));
    Need(out.waitMax.milliseconds==0,"shake_train Wait random maximum is not admitted");Expect(Only(wait),16,1,19,0,true);
    const auto& use=At(nodes,19,68);const auto u=Properties(use,0);Need(u.size()==1,"shake_train UseTarget carries an actor/key field outside the measured profile");
    out.useNode=19;out.useTarget=Property(u,"mUseTarget");Name(out.useTarget);Expect(Only(use),16,1,31,0,false);
    const auto& stop=At(nodes,31,357);(void)Properties(stop,0);out.stopNode=31;out.stopType=357;
    // Only one other root may exist (OnUsed stop_shake); no other OnSpawn.
    unsigned spawns=0;for(const auto& n:nodes)if(n.second.type==1)++spawns;Need(spawns==1,"shake_train has another OnSpawn root");
    return out;
}

std::vector<SourceOnUsedRoot> DecodeSourceOnUsedRootsH1023(const std::vector<unsigned char>& brai){
    const auto nodes=Nodes(brai);std::vector<SourceOnUsedRoot> out;
    for(const auto& item:nodes){const auto& node=item.second;if(node.type!=3)continue;
        const auto p=Properties(node,64);Need(p.size()==1,"OnUsed root property set differs");SourceOnUsedRoot root;root.node=node.id;root.key=Property(p,"mUseKey");
        Need(root.key.size()<=256,"OnUsed key budget");
        for(const auto& output:node.outputs){const auto e=Single(output);root.outputFlags.push_back(e.flags);root.outputTargets.push_back(e.target);root.outputAux.push_back(e.aux);root.outputPrimaryCondition.push_back(e.primary);
            Need(nodes.count(e.target)!=0,"OnUsed output destination is absent");}
        out.push_back(root);}
    return out;
}
int SourceCanUseKey(const std::vector<SourceOnUsedRoot>& roots,const std::string& key){
    for(std::size_t i=0;i<roots.size();++i){if(!FoldEqual(roots[i].key,key))continue;
        for(auto flags:roots[i].outputFlags)if(!(flags&0x100000u))return int(i);}
    return -1;
}

SourceBoxBody DecodeSourceEntityBoxBody(const SourceMapBodyPlan& plan,std::size_t entityIndex,SourceBoxBodyProfile profile){
    Need(entityIndex<plan.entities.entities.size()&&entityIndex<plan.entityBodyIndices.size(),"Source box body entity index is outside the plan");
    const auto bodyIndex=plan.entityBodyIndices[entityIndex];Need(bodyIndex<plan.inventory.bodies.size(),"Source entity has no exact-name HKXS body");
    Need(!plan.inventory.source.empty(),"Source box body requires the retained HKXS bytes");
    const auto& record=plan.inventory.bodies[bodyIndex];const auto& entity=plan.entities.entities[entityIndex];
    const auto* name=entity.Find("name");Need(name&&name->kind==EntityProperty::Text&&record.hasName&&record.rawName==name->text,"Source box body name differs from its ENTS entity");
    const auto placement=ReadEntityPlacement(entity);Need(placement.hasOrigin&&!placement.hasAngle,"Source box body entity placement profile differs");
    detail::HavokPack pack(plan.inventory.source,plan.inventory.source.size(),100000,1000000);
    const std::uint32_t body=record.sourceBodyOffset;Need(pack.Type(body)=="hkpRigidBody","Source box body offset is not a rigid body");pack.DataRange(body,352);
    SourceBoxBody out;out.name=record.rawName;out.bodyOffset=body;out.rawFilter=pack.DU(body+44);Need(out.rawFilter==record.rawFilterInfo,"Source box body filter word differs from inventory");
    // Motion state: rotation basis words 0..11 (w words zero), translation 12..14.
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){const double v=pack.F(body+224+r*16+c*4);if(c<3)out.rotation[r*3+c]=v;
        Need(std::fabs(v-(r==c?1.0:0.0))<=1e-6,"Source box body basis is not identity; a rotated body is refused");}
    for(unsigned k=0;k<3;++k){out.origin[k]=pack.F(body+272+k*4)/MetresPerUnit;
        Need(std::fabs(out.origin[k]-placement.origin[k])<=0.002,"Source box body translation differs from its ENTS origin");}
    if(placement.hasRotation)for(unsigned i=0;i<9;++i)Need(placement.rotation[i]==(i%4==0?1.f:0.f),"Source box body ENTS rotation is not identity");
    const std::uint32_t root=pack.Pointer(body+16);out.rootType=pack.Type(root);
    auto Box=[&](std::uint32_t box,const double* offset){Need(pack.Type(box)=="hkpBoxShape","Source box body leaf is not hkpBoxShape");pack.DataRange(box,48);
        Need(pack.F(box+16)==0,"Source box body has a convex radius");
        for(unsigned k=0;k<3;++k){out.halfExtents[k]=pack.F(box+32+k*4)/MetresPerUnit;out.center[k]=offset[k];Need(out.halfExtents[k]>0&&out.halfExtents[k]<100000,"Source box half extent is out of range");}};
    auto Shape=[&](std::uint32_t shape){const std::string type=pack.Type(shape);const double zero[3]={0,0,0};
        if(type=="hkpBoxShape"){Box(shape,zero);return type;}
        Need(type=="hkpConvexTranslateShape","Source box body shape is outside the admitted Box/ConvexTranslate profile");pack.DataRange(shape,48);
        Need(pack.F(shape+16)==0,"Source translated box has a convex radius");double offset[3];
        for(unsigned k=0;k<3;++k)offset[k]=pack.F(shape+32+k*4)/MetresPerUnit;Box(pack.Pointer(shape+24),offset);return type;};
    if(profile==SourceBoxBodyProfile::PhantomTriggerBoxV1){
        Need(out.rawFilter==4096,"Source trigger body raw filter is not CONTENTS_TRIGGER");
        Need(out.rootType=="hkpBvShape","Source trigger body root is not hkpBvShape");pack.DataRange(root,28);
        out.boundingType=Shape(pack.Pointer(root+16));const std::uint32_t child=pack.Pointer(root+24);out.childType=pack.Type(child);
        Need(out.childType=="rvCallBackPhantom","Source trigger body child is not rvCallBackPhantom");
    }else{
        Need(profile==SourceBoxBodyProfile::SolidBoxV1,"Unknown source box body profile");
        Need(out.rawFilter==1,"Source solid box body raw filter is not CONTENTS_SOLID");out.boundingType=Shape(root);
    }
    return out;
}

namespace {
std::vector<unsigned char> Graph(const std::vector<std::vector<std::string>>& rows,const std::vector<unsigned>& depth){
    std::vector<unsigned char> out;const std::vector<std::string> header={"h1023","32","32","32","0"};
    for(const auto& t:header){out.insert(out.end(),t.begin(),t.end());out.push_back(0);}out.push_back('\n');
    for(std::size_t i=0;i<rows.size();++i){for(unsigned d=0;d<depth[i];++d)out.push_back('\t');
        for(const auto& t:rows[i]){out.insert(out.end(),t.begin(),t.end());out.push_back(0);}out.push_back('\n');}
    return out;
}
std::vector<std::string> Edge99(const char* flags,const char* header,const char* target,const char* aux){
    std::vector<std::string> t;t.push_back(flags);t.push_back(header);t.push_back(target);t.push_back("0");t.push_back(aux);
    for(unsigned i=1;i<32;++i){t.push_back("0");t.push_back("0");t.push_back("0");}t.push_back("1.000000");return t;}
template<class F> bool Throws(F f){try{f();}catch(const std::exception&){return true;}return false;}
}

int RunTrainyardExitTests(){
    unsigned passed=0;
    // Duration literal profile.
    Need(ParseSourceWaitDurationLiteralV1("6000 0").milliseconds==6000&&ParseSourceWaitDurationLiteralV1("1 1").milliseconds==1000,"Duration units");
    Need(ParseSourceWaitDurationLiteralV1("0.5 1").milliseconds==500&&ParseSourceWaitDurationLiteralV1("3.00 1").milliseconds==3000&&ParseSourceWaitDurationLiteralV1("100 0").milliseconds==100,"Duration decimals");
    Need(ParseSourceWaitDurationLiteralV1("0 0").milliseconds==0&&ParseSourceWaitDurationLiteralV1(".5 1").milliseconds==500,"Duration zero/leading dot");
    for(const char* bad:{"6000","6000 2","@x 0","-1 0","1e3 0","6000  0","1 1 1","","a 0","1.2.3 0"," 1"})Need(Throws([&]{ParseSourceWaitDurationLiteralV1(bad);}),"Unadmitted duration accepted");
    ++passed;
    // Authored movetrain-like tail.
    typedef std::vector<std::string> R;
    const auto tail=Graph({R{"68","6","1","2","3","^68","0","mUseTarget","relay_a","mUseKey","Key_A"},Edge99("16","3","7","2"),
        R{"30","7","1","2","3","^30","0","^65","1024","mDuration","2500 0","mDurationMax","0 0","mActualDuration","9","mTimeSet","0"},Edge99("16","3","8","2"),R{"-1"},
        R{"70","8","1","2","3","^70","0","mRemoveTarget","blocker_a"},Edge99("16","3","9","2"),
        R{"456","9","1","2","3","^456","0","mCommand","SomeCommand"}},{0,1,0,1,2,0,1,0});
    const auto t=DecodeTrainyardMovetrainTailH1023(tail);
    Need(t.relayTarget=="relay_a"&&t.relayKey=="Key_A"&&t.wait.milliseconds==2500&&t.wait.unit==0&&t.removeTarget=="blocker_a"&&t.consoleCommand=="SomeCommand"&&t.consoleType==456,"Authored tail decode");
    {auto bad=tail;const std::string from="2500 0",to="2500 3";auto at=std::search(bad.begin(),bad.end(),from.begin(),from.end());*(at+5)='3';(void)to;
        Need(Throws([&]{DecodeTrainyardMovetrainTailH1023(bad);}),"Unknown wait unit accepted");}
    {const auto unconditioned=Graph({R{"68","6","1","2","3","^68","0","mUseTarget","relay_a","mUseKey","Key_A"},Edge99("16","3","7","2"),
        R{"30","7","1","2","3","^30","0","^65","1024","mDuration","2500 0","mDurationMax","0 0","mActualDuration","9","mTimeSet","0"},Edge99("16","3","8","2"),
        R{"70","8","1","2","3","^70","0","mRemoveTarget","blocker_a"},Edge99("16","3","9","2"),R{"456","9","1","2","3","^456","0","mCommand","SomeCommand"}},{0,1,0,1,0,1,0});
        Need(Throws([&]{DecodeTrainyardMovetrainTailH1023(unconditioned);}),"Wait without its Timer condition accepted");}
    ++passed;
    // Authored shake-like prefix.
    const auto shake=Graph({R{"1","1","1","2","3","^1","1","^74","1024"},Edge99("16","1","30","0"),R{"-1"},
        R{"30","30","1","2","3","^30","16","^65","1024","mDuration","1 1","mDurationMax","0 0","mActualDuration","10000","mTimeSet","0"},Edge99("16","1","19","0"),R{"-1"},
        R{"68","19","1","2","3","^68","16","mUseTarget","volume_a"},Edge99("16","1","31","0"),R{"357","31","1","2","3","^357","0"}},{0,1,2,0,1,2,0,1,0});
    const auto s=DecodeTrainyardShakeArmingH1023(shake);Need(s.wait.milliseconds==1000&&s.useTarget=="volume_a"&&s.stopType==357,"Authored shake decode");
    {const auto keyed=Graph({R{"1","1","1","2","3","^1","1","^74","1024"},Edge99("16","1","30","0"),R{"-1"},
        R{"30","30","1","2","3","^30","16","^65","1024","mDuration","1 1","mDurationMax","0 0","mActualDuration","10000","mTimeSet","0"},Edge99("16","1","19","0"),R{"-1"},
        R{"68","19","1","2","3","^68","16","mUseTarget","volume_a","mUseKey","k"},Edge99("16","1","31","0"),R{"357","31","1","2","3","^357","0"}},{0,1,2,0,1,2,0,1,0});
        Need(Throws([&]{DecodeTrainyardShakeArmingH1023(keyed);}),"Keyed arming Use accepted");}
    ++passed;
    // OnUsed roots and the CanUseKey gate.
    const auto roots=DecodeSourceOnUsedRootsH1023(Graph({R{"3","3","1","2","3","^3","0","^64","1024","mUseKey","key_one"},Edge99("16","1","10","0"),R{"-1"},
        R{"3","4","1","2","3","^3","0","^64","1024","mUseKey","Key_Two"},Edge99("16","1","10","3"),R{"-1"},
        R{"3","5","1","2","3","^3","0","^64","1024","mUseKey","blocked"},Edge99("1048592","1","10","0"),R{"-1"},R{"243","10","1","2","3","^243","0"}},{0,1,2,0,1,2,0,1,2,0}));
    Need(roots.size()==3&&roots[1].node==4&&roots[1].key=="Key_Two"&&roots[1].outputTargets[0]==10&&roots[1].outputAux[0]==3,"OnUsed roots decode");
    Need(SourceCanUseKey(roots,"key_two")==1&&SourceCanUseKey(roots,"KEY_ONE")==0&&SourceCanUseKey(roots,"blocked")==-1&&SourceCanUseKey(roots,"")==-1&&SourceCanUseKey(roots,"key")==-1,"CanUseKey gate");
    ++passed;
    return int(passed);
}
}
