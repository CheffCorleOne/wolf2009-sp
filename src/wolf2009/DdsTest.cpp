// SPDX-License-Identifier: GPL-3.0-or-later
#include "Dds.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace wolf2009 {
namespace {
typedef std::vector<unsigned char> Bytes;
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Put(Bytes& b,std::size_t p,std::uint32_t v){for(int i=0;i<4;++i)b[p+i]=static_cast<unsigned char>(v>>(8*i));}
Bytes Header(std::uint32_t width,std::uint32_t height,const char* format,std::size_t payload,std::uint32_t mips=1){
    Bytes d(128+payload,0);Put(d,0,0x20534444);Put(d,4,124);Put(d,8,0x1007);Put(d,12,height);Put(d,16,width);Put(d,28,mips);Put(d,76,32);Put(d,80,4);Put(d,108,0x1000);
    for(int i=0;i<4;++i)d[84+i]=static_cast<unsigned char>(format[i]);return d;
}
void ColorBlock(Bytes& d,std::size_t offset,unsigned int a,unsigned int b,std::uint32_t indices){d[offset]=static_cast<unsigned char>(a);d[offset+1]=static_cast<unsigned char>(a>>8);d[offset+2]=static_cast<unsigned char>(b);d[offset+3]=static_cast<unsigned char>(b>>8);Put(d,offset+4,indices);}
void Pixel(const DdsImage& d,std::size_t index,unsigned int r,unsigned int g,unsigned int b,unsigned int a){Need(d.rgba[index*4]==r&&d.rgba[index*4+1]==g&&d.rgba[index*4+2]==b&&d.rgba[index*4+3]==a,"DDS golden pixel mismatch");}
void Reject(const Bytes& b,const DdsLimits& limits=DdsLimits()){bool bad=false;try{DecodeDdsMip0(b,limits);}catch(const std::exception&){bad=true;}Need(bad,"Malformed DDS was accepted");}
void AlphaIndices(Bytes& b,std::uint64_t pattern){for(int i=0;i<6;++i)b[130+i]=static_cast<unsigned char>(pattern>>(8*i));}
std::uint64_t AlphaPattern(){std::uint64_t pattern=0;for(unsigned int i=0;i<16;++i)pattern|=std::uint64_t(i%8)<<(3*i);return pattern;}
}
int RunDdsTests(){
    int passed=0;
    try{
        Bytes bc1=Header(4,4,"DXT1",8);ColorBlock(bc1,128,0xf800,0x07e0,0xe4e4e4e4);DdsImage im=DecodeDdsMip0(bc1);
        Pixel(im,0,255,0,0,255);Pixel(im,1,0,255,0,255);Pixel(im,2,170,85,0,255);Pixel(im,3,85,170,0,255);Need(im.rgba.size()==64,"BC1 output size");++passed;
        Bytes transparent=Header(4,4,"DXT1",8);ColorBlock(transparent,128,0,0xffff,0xe4e4e4e4);im=DecodeDdsMip0(transparent);
        Pixel(im,0,0,0,0,255);Pixel(im,1,255,255,255,255);Pixel(im,2,127,127,127,255);Pixel(im,3,0,0,0,0);++passed;
        ColorBlock(transparent,128,0xffff,0xffff,0xffffffff);im=DecodeDdsMip0(transparent);Pixel(im,0,0,0,0,0);++passed;
        Bytes round=Header(4,4,"DXT1",8);ColorBlock(round,128,0x0800,0,0xeeeeeeee);im=DecodeDdsMip0(round);Pixel(im,0,5,0,0,255);Pixel(im,1,3,0,0,255);++passed;
        Bytes bc2=Header(4,4,"DXT3",16);for(unsigned int i=0;i<8;++i)bc2[128+i]=static_cast<unsigned char>(2*i|((2*i+1)<<4));ColorBlock(bc2,136,0,0xffff,0xffffffff);im=DecodeDdsMip0(bc2);
        for(unsigned int i=0;i<16;++i)Pixel(im,i,170,170,170,i*17);++passed;
        Bytes bc3=Header(4,4,"DXT5",16);bc3[128]=210;bc3[129]=0;AlphaIndices(bc3,AlphaPattern());ColorBlock(bc3,136,0,0xffff,0xffffffff);im=DecodeDdsMip0(bc3);
        const unsigned int a7[]={210,0,180,150,120,90,60,30};for(unsigned int i=0;i<16;++i)Pixel(im,i,170,170,170,a7[i%8]);++passed;
        bc3[128]=10;bc3[129]=110;im=DecodeDdsMip0(bc3);const unsigned int a5[]={10,110,30,50,70,90,0,255};for(unsigned int i=0;i<16;++i)Pixel(im,i,170,170,170,a5[i%8]);++passed;
        bc3[128]=bc3[129]=42;im=DecodeDdsMip0(bc3);Pixel(im,5,170,170,170,42);Pixel(im,6,170,170,170,0);Pixel(im,7,170,170,170,255);++passed;
        Bytes odd=Header(5,3,"DXT1",16);ColorBlock(odd,128,0xf800,0,0);ColorBlock(odd,136,0x07e0,0,0);im=DecodeDdsMip0(odd);Need(im.rgba.size()==60,"Odd width size");for(unsigned int y=0;y<3;++y){Pixel(im,y*5+3,255,0,0,255);Pixel(im,y*5+4,0,255,0,255);}++passed;
        odd=Header(3,5,"DXT1",16);ColorBlock(odd,128,0xf800,0,0);ColorBlock(odd,136,0x07e0,0,0);im=DecodeDdsMip0(odd);for(unsigned int x=0;x<3;++x)Pixel(im,12+x,0,255,0,255);++passed;
        Bytes chain=Header(4,4,"DXT1",24,3);ColorBlock(chain,128,0xf800,0,0);im=DecodeDdsMip0(chain);Need(im.mipCount==3,"Mip metadata");Pixel(im,0,255,0,0,255);chain.pop_back();Reject(chain);++passed;
        Bytes noCount=bc1;Put(noCount,28,0);im=DecodeDdsMip0(noCount);Need(im.mipCount==1,"Missing mip count");++passed;
        for(std::size_t n=0;n<128;n+=7)Reject(Bytes(n));Bytes bad=bc1;Put(bad,0,0);Reject(bad);bad=bc1;Put(bad,4,123);Reject(bad);bad=bc1;Put(bad,76,31);Reject(bad);++passed;
        bad=bc1;Put(bad,16,0);Reject(bad);bad=bc1;Put(bad,12,0xffffffff);Reject(bad);bad=bc1;Put(bad,28,4);Reject(bad);++passed;
        DdsLimits limited;limited.decodedBytes=63;Reject(bc1,limited);limited=DdsLimits();limited.sourceBytes=135;Reject(bc1,limited);++passed;
        limited=DdsLimits();limited.maximumWidth=limited.maximumHeight=0xffffffff;limited.decodedBytes=(std::numeric_limits<std::size_t>::max)();bad=bc1;Put(bad,16,0xffffffff);Put(bad,12,0xffffffff);Reject(bad,limited);++passed;
        for(const char* f:{"DXT2","DXT4","DX10","BC7 "})Reject(Header(4,4,f,16));bad=bc1;Put(bad,80,0x40);Reject(bad);++passed;
        bad=bc1;Put(bad,112,0x200);Reject(bad);bad=bc1;Put(bad,8,0x801007);Reject(bad);bad=bc1;Put(bad,24,2);Reject(bad);++passed;
        bad=bc1;bad.pop_back();Reject(bad);bad=bc1;bad.push_back(0);Reject(bad);++passed;
        Bytes a8=Header(2,2,"\0\0\0\0",4);Put(a8,80,2);Put(a8,88,8);Put(a8,104,255);Put(a8,8,0x100f);Put(a8,20,12345);a8[128]=0;a8[129]=85;a8[130]=170;a8[131]=255;im=DecodeDdsMip0(a8);
        for(unsigned int i=0;i<4;++i)Pixel(im,i,255,255,255,i*85);Need(im.format=="A8","A8 metadata");++passed;
        bad=a8;Put(bad,92,255);Reject(bad);bad=a8;Put(bad,88,16);Reject(bad);bad=a8;bad.push_back(0);Reject(bad);++passed;
        bad=bc3;Put(bad,88,0x52424778);im=DecodeDdsMip0(bad);Need(im.sourceFormatMarker==0x52424778,"Custom normal marker not retained");++passed;
        std::cout<<"DDS self-test: "<<passed<<" passed, 0 failed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"DDS self-test failed after "<<passed<<" checks: "<<e.what()<<'\n';return 1;}
}
}
