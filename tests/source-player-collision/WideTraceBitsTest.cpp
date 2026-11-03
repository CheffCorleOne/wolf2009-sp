// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone C++98 check of the actual proposed/native CM cache header.
#include <cstdio>
#include <cstring>
#include <stdexcept>
class idLib {
public:
    static void Error(const char*,...) { throw std::runtime_error("bounded native cache error"); }
};
#include "cm/CollisionModel_traceBits.h"

namespace {
int checks=0;
void Check(bool value) {++checks;if(!value)throw std::runtime_error("cache assertion");}
}
int main() {
    try {
        idCMTraceBits<161> bits,copy;
        std::memset(&bits,0xff,sizeof(bits));bits.Clear();
        for(int i=0;i<161;++i)Check(bits.Get(i)==0);
        const int boundary[]={0,1,30,31,32,33,63,64,95,96,127,128,143,144,159,160};
        bool expected[161]={false};
        for(unsigned k=0;k<sizeof(boundary)/sizeof(boundary[0]);++k) {
            const int bit=boundary[k];bits.Set(bit);expected[bit]=true;
            for(int i=0;i<161;++i)Check(bits.Get(i)==unsigned(expected[i]));
        }
        std::memcpy(&copy,&bits,sizeof(bits));
        for(int i=0;i<161;++i)Check(copy.Get(i)==bits.Get(i));
        for(unsigned k=0;k<sizeof(boundary)/sizeof(boundary[0]);++k) {
            const int bit=boundary[k];copy.Set(bit,0);expected[bit]=false;
            for(int i=0;i<161;++i)Check(copy.Get(i)==unsigned(expected[i]));
        }
        for(int i=0;i<161;++i){bits.Set(i);Check(bits.Get(i)==1);}bits.Clear();
        for(int i=0;i<161;++i)Check(bits.Get(i)==0);
        const int invalid[]={-1,161,2147483647};
        for(unsigned k=0;k<3;++k) {
            bool rejected=false;try{bits.Set(invalid[k]);}catch(const std::exception&){rejected=true;}Check(rejected);
            rejected=false;try{bits.Get(invalid[k]);}catch(const std::exception&){rejected=true;}Check(rejected);
        }
        for(int i=0;i<161;++i)Check(bits.Get(i)==0);
        std::printf("WIDE_TRACE_BITS_TEST PASS checks=%d bytes=%u words=%u\n",checks,unsigned(sizeof(bits)),unsigned(sizeof(bits)/sizeof(unsigned int)));
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"WIDE_TRACE_BITS_TEST FAIL: %s\n",e.what());return 1;}
}
