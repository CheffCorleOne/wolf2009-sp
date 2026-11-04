// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceCollisionBinding.h"
#include <limits>
#include <stdexcept>
#include <vector>
namespace wolf2009 { namespace {
const char requestPrefix[]="wolf2009/collision-body-request/v1/";
const char modelPrefix[]="wolf2009/collision-body/v1/";
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool Segment(const std::string& s){if(s.empty()||s=="."||s=="..")return false;
    for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;return true;}
void Check(const SourceCollisionBodyRequest& r){
    Need(r.policy==SourceCollisionBodyPolicy::DeclaredDoom3BitsV1||r.policy==SourceCollisionBodyPolicy::NativeCommonContentsCylinderEnvelopeV1||r.policy==SourceCollisionBodyPolicy::NativeSolidEntityCylinderEnvelopeV1,"Explicit source body collision policy required");
    Need(r.resourcePath.size()<=1024&&r.resourcePath.compare(0,9,"wolf2009/")==0,"Source body requires a bounded typed resource");
    std::vector<std::string> parts;std::size_t at=0;for(;;){const auto end=r.resourcePath.find('/',at);parts.push_back(r.resourcePath.substr(at,end==std::string::npos?end:end-at));Need(Segment(parts.back()),"Invalid source body resource segment");if(end==std::string::npos)break;at=end+1;}
    Need(parts.size()>=4&&parts[2]=="hkxs","Source body requires an exact lowercase HKXS resource");
    Need(!r.bodyName.empty()&&r.bodyName.size()<=512,"Source body exact name budget");
    for(unsigned char c:r.bodyName)Need(c>=32&&c<127,"Source body name is not printable ASCII");
    Need(r.expectedBytes<=(64u<<20),"Source body expected byte budget");
}
std::string Hex(const std::string& text){static const char digits[]="0123456789abcdef";std::string result;result.reserve(text.size()*2);for(unsigned char c:text){result+=digits[c>>4];result+=digits[c&15];}return result;}
unsigned Nibble(char c){if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw std::runtime_error("Noncanonical source collision hex");}
std::string Unhex(const std::string& text,std::size_t maximum){Need(!text.empty()&&text.size()%2==0&&text.size()/2<=maximum,"Source collision encoded byte budget");std::string result;result.reserve(text.size()/2);for(std::size_t i=0;i<text.size();i+=2)result+=char(Nibble(text[i])*16+Nibble(text[i+1]));return result;}
std::uint64_t Number(const std::string& text){Need(!text.empty()&&(text.size()==1||text[0]!='0')&&text.size()<=20,"Noncanonical source collision integer");std::uint64_t result=0;for(unsigned char c:text){Need(c>='0'&&c<='9',"Source collision integer byte");const unsigned digit=c-'0';Need(result<=((std::numeric_limits<std::uint64_t>::max)()-digit)/10,"Source collision integer overflow");result=result*10+digit;}return result;}
std::vector<std::string> Lines(const std::string& text,std::size_t count){Need(!text.empty()&&text.size()<=4096&&text.back()=='\n',"Source collision record framing/budget");std::vector<std::string> result;std::size_t at=0;while(at<text.size()){const auto end=text.find('\n',at);Need(end!=std::string::npos&&result.size()<count,"Source collision record field count");result.push_back(text.substr(at,end-at));at=end+1;}Need(result.size()==count,"Source collision record field count");return result;}
std::string RequestText(const SourceCollisionBodyRequest& r){Check(r);return "Wolf2009CollisionBodyRequest1\n"+std::to_string(unsigned(r.policy))+"\n"+Hex(r.resourcePath)+"\n"+Hex(r.bodyName)+"\n"+std::to_string(r.expectedEpoch)+"\n"+std::to_string(r.expectedBytes)+"\n"+std::to_string(r.expectedFingerprint)+"\n";}
SourceCollisionBodyRequest Request(const std::vector<std::string>& lines){Need(lines.size()>=7&&lines[0]=="Wolf2009CollisionBodyRequest1","Source collision request signature");const auto policy=Number(lines[1]);Need(policy>=1&&policy<=3,"Source collision policy value");SourceCollisionBodyRequest r;r.policy=SourceCollisionBodyPolicy(policy);r.resourcePath=Unhex(lines[2],1024);r.bodyName=Unhex(lines[3],512);r.expectedEpoch=Number(lines[4]);r.expectedBytes=Number(lines[5]);r.expectedFingerprint=Number(lines[6]);Check(r);return r;}
void Check(const SourceCollisionBodyBinding& b){Check(b.request);Need(b.epoch&&b.bytes&&b.bytes<=(64u<<20)&&b.shapes&&b.shapes<=300000&&b.corePoints>=b.shapes&&b.corePoints<=8000000&&b.nativeContents,"Source collision binding metadata budget");Need((!b.request.expectedEpoch||b.request.expectedEpoch==b.epoch)&&(!b.request.expectedBytes||b.request.expectedBytes==b.bytes)&&(!b.request.expectedFingerprint||b.request.expectedFingerprint==b.fingerprint),"Source collision binding differs from requested identity");}
}
const char* SourceCollisionBodyRequestPrefix(){return requestPrefix;}
const char* SourceCollisionBodyModelPrefix(){return modelPrefix;}
std::string SourceCollisionBodyRequestPath(const SourceCollisionBodyRequest& r){return std::string(requestPrefix)+Hex(RequestText(r));}
SourceCollisionBodyRequest ParseSourceCollisionBodyRequestPath(const std::string& path){Need(path.size()<=8192&&path.compare(0,sizeof(requestPrefix)-1,requestPrefix)==0,"Source collision request path prefix/budget");const auto text=Unhex(path.substr(sizeof(requestPrefix)-1),4096);auto r=Request(Lines(text,7));Need(RequestText(r)==text,"Noncanonical source collision request");return r;}
bool SameSourceCollisionBodyRequest(const SourceCollisionBodyRequest& a,const SourceCollisionBodyRequest& b){return a.policy==b.policy&&a.resourcePath==b.resourcePath&&a.bodyName==b.bodyName&&a.expectedEpoch==b.expectedEpoch&&a.expectedBytes==b.expectedBytes&&a.expectedFingerprint==b.expectedFingerprint;}
std::string SerializeSourceCollisionBodyBinding(const SourceCollisionBodyBinding& b){Check(b);return RequestText(b.request)+"Wolf2009CollisionBodyBinding1\n"+std::to_string(b.epoch)+"\n"+std::to_string(b.bytes)+"\n"+std::to_string(b.fingerprint)+"\n"+std::to_string(b.shapes)+"\n"+std::to_string(b.corePoints)+"\n"+std::to_string(b.nativeContents)+"\n";}
SourceCollisionBodyBinding ParseSourceCollisionBodyBinding(const std::string& text){const auto lines=Lines(text,14);Need(lines[7]=="Wolf2009CollisionBodyBinding1","Source collision binding signature");SourceCollisionBodyBinding b;b.request=Request(lines);b.epoch=Number(lines[8]);b.bytes=Number(lines[9]);b.fingerprint=Number(lines[10]);b.shapes=Number(lines[11]);b.corePoints=Number(lines[12]);const auto contents=Number(lines[13]);Need(contents<=(std::numeric_limits<std::uint32_t>::max)(),"Source collision binding contents overflow");b.nativeContents=std::uint32_t(contents);Check(b);Need(SerializeSourceCollisionBodyBinding(b)==text,"Noncanonical source collision binding");return b;}
std::string SourceCollisionBodyModelName(const SourceCollisionBodyBinding& b){return std::string(modelPrefix)+Hex(SerializeSourceCollisionBodyBinding(b));}
SourceCollisionBodyBinding ParseSourceCollisionBodyModelName(const std::string& name){Need(name.size()<=8192&&name.compare(0,sizeof(modelPrefix)-1,modelPrefix)==0,"Source collision model alias prefix/budget");return ParseSourceCollisionBodyBinding(Unhex(name.substr(sizeof(modelPrefix)-1),4096));}
}
