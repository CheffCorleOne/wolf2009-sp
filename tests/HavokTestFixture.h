// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TEST_HAVOK_FIXTURE_H
#define WOLF2009_TEST_HAVOK_FIXTURE_H
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace wolf2009_test {
using Bytes=std::vector<unsigned char>;
inline void Need(bool b,const char *s){if(!b)throw std::runtime_error(s);}
inline void U(Bytes &b,std::size_t p,std::uint32_t value) {
    Need(p+4<=b.size(),"Synthetic Havok writer range");
    for(int k=0;k<4;++k)b[p+k]=static_cast<unsigned char>(value>>(k*8));
}
inline void F(Bytes &b,std::size_t p,float value){std::uint32_t u;std::memcpy(&u,&value,4);U(b,p,u);}
inline void Text(Bytes &b,std::size_t p,const std::string &s) {
    Need(p+s.size()<b.size(),"Synthetic Havok text range");
    std::copy(s.begin(),s.end(),b.begin()+p);b[p+s.size()]=0;
}
inline std::size_t Align(std::size_t x){return (x+15)/16*16;}
// Reusable hand-authored packfile shell. No game fixture or Havok SDK data.
struct HavokFixture {
    Bytes data;
    std::map<std::uint32_t,std::uint32_t> links;
    std::map<std::uint32_t,std::string> types;
    explicit HavokFixture(std::size_t bytes=4096):data(bytes,0){types[0]="hkRootLevelContainer";}
    Bytes File() const {
        Bytes classes(16,0);std::map<std::string,std::uint32_t> names;
        for(const auto &t:types)if(!names.count(t.second)) {
            const auto at=classes.size();names[t.second]=static_cast<std::uint32_t>(at);
            classes.resize(at+t.second.size()+1);Text(classes,at,t.second);
        }
        classes.resize(Align(classes.size()),255);
        const auto absolute=208+classes.size(),local=data.size(),global=Align(local+links.size()*8),
            end=Align(global+types.size()*12);
        Bytes out(absolute+end,255);std::fill(out.begin(),out.begin()+208,0);
        U(out,0,0x57e0e057);U(out,4,0x10c0c010);U(out,12,5);out[16]=4;out[17]=1;out[19]=1;
        U(out,20,3);U(out,24,2);U(out,36,names["hkRootLevelContainer"]);Text(out,40,"Havok-5.6.0-r1");
        const char *tags[]={"__classnames__","__types__","__data__"};
        for(int i=0;i<3;++i) {
            const auto h=64+i*48;Text(out,h,tags[i]);U(out,h+20,static_cast<std::uint32_t>(i==0?208:absolute));
            for(int k=0;k<6;++k)U(out,h+24+k*4,static_cast<std::uint32_t>(i==0?classes.size():
                i==1?0:k==0?local:k<=2?global:end));
        }
        std::copy(classes.begin(),classes.end(),out.begin()+208);std::copy(data.begin(),data.end(),out.begin()+absolute);
        auto at=absolute+local;
        for(const auto &link:links){U(out,at,link.first);U(out,at+4,link.second);at+=8;}
        at=absolute+global;
        for(const auto &t:types){U(out,at,t.first);U(out,at+4,0);U(out,at+8,names[t.second]);at+=12;}
        return out;
    }
};
} // namespace wolf2009_test
#endif
