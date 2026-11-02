// SPDX-License-Identifier: GPL-3.0-or-later
#include "Dds.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

// Independent implementation from the public DDS/BC layout descriptions:
// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header
// https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-pixelformat
// https://learn.microsoft.com/en-us/windows/win32/direct3d9/opaque-and-1-bit-alpha-textures
// https://learn.microsoft.com/en-us/windows/win32/direct3d9/textures-with-alpha-channels
namespace wolf2009 {
namespace {
void Need(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
std::uint32_t U32(const unsigned char* p) {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
unsigned int U16(const unsigned char* p) { return unsigned(p[0])|(unsigned(p[1])<<8); }
std::size_t Product(std::size_t a,std::size_t b) {
    Need(a==0||b<=(std::numeric_limits<std::size_t>::max)()/a,"DDS size multiplication overflow");return a*b;
}
std::size_t Sum(std::size_t a,std::size_t b) {
    Need(b<=(std::numeric_limits<std::size_t>::max)()-a,"DDS size addition overflow");return a+b;
}
struct Color { unsigned char r,g,b,a; };
Color Rgb565(unsigned int x) {
    const unsigned int r=(x>>11)&31,g=(x>>5)&63,b=x&31;
    return {static_cast<unsigned char>((r<<3)|(r>>2)),static_cast<unsigned char>((g<<2)|(g>>4)),static_cast<unsigned char>((b<<3)|(b>>2)),255};
}
void Colors(const unsigned char* block,bool bc1,Color palette[4]) {
    const unsigned int a=U16(block),b=U16(block+2);
    palette[0]=Rgb565(a);palette[1]=Rgb565(b);
    if(!bc1||a>b) {
        // D3D9's documented integer color interpolation rounds thirds with +1.
        palette[2]={static_cast<unsigned char>((2*palette[0].r+palette[1].r+1)/3),static_cast<unsigned char>((2*palette[0].g+palette[1].g+1)/3),static_cast<unsigned char>((2*palette[0].b+palette[1].b+1)/3),255};
        palette[3]={static_cast<unsigned char>((palette[0].r+2*palette[1].r+1)/3),static_cast<unsigned char>((palette[0].g+2*palette[1].g+1)/3),static_cast<unsigned char>((palette[0].b+2*palette[1].b+1)/3),255};
    } else {
        palette[2]={static_cast<unsigned char>((palette[0].r+palette[1].r)/2),static_cast<unsigned char>((palette[0].g+palette[1].g)/2),static_cast<unsigned char>((palette[0].b+palette[1].b)/2),255};
        palette[3]={0,0,0,0};
    }
}
void DecodeBlock(const unsigned char* block,unsigned int kind,Color result[16]) {
    const unsigned char* colorBlock=block+(kind==1?0:8);Color palette[4];Colors(colorBlock,kind==1,palette);
    const std::uint32_t indices=U32(colorBlock+4);
    for(unsigned int i=0;i<16;++i)result[i]=palette[(indices>>(2*i))&3];
    if(kind==2) {
        for(unsigned int i=0;i<16;++i)result[i].a=static_cast<unsigned char>(((block[i/2]>>(4*(i&1)))&15)*17);
    } else if(kind==3) {
        unsigned int alphas[8]={block[0],block[1],0,0,0,0,0,0};
        if(alphas[0]>alphas[1]) for(unsigned int i=2;i<8;++i)alphas[i]=((8-i)*alphas[0]+(i-1)*alphas[1])/7;
        else {for(unsigned int i=2;i<6;++i)alphas[i]=((6-i)*alphas[0]+(i-1)*alphas[1])/5;alphas[6]=0;alphas[7]=255;}
        std::uint64_t alphaIndices=0;for(unsigned int i=0;i<6;++i)alphaIndices|=std::uint64_t(block[2+i])<<(8*i);
        for(unsigned int i=0;i<16;++i)result[i].a=static_cast<unsigned char>(alphas[(alphaIndices>>(3*i))&7]);
    }
}
std::size_t MipBytes(std::uint32_t width,std::uint32_t height,unsigned int kind) {
    if(kind==0)return Product(width,height); // Observed legacy A8: tightly packed rows.
    const std::size_t blocksX=std::size_t(width)/4+(width%4?1:0),blocksY=std::size_t(height)/4+(height%4?1:0);
    return Product(Product(blocksX,blocksY),kind==1?8:16);
}
}
DdsImage DecodeDdsMip0(const std::vector<unsigned char>& data,const DdsLimits& limits) {
    Need(data.size()>=128,"DDS header is truncated");Need(data.size()<=limits.sourceBytes,"DDS source byte limit exceeded");
    const unsigned char* h=data.data();Need(U32(h)==0x20534444,"Expected DDS magic");
    Need(U32(h+4)==124&&U32(h+76)==32,"Invalid DDS header or pixel-format size");
    DdsImage image;image.width=U32(h+16);image.height=U32(h+12);image.mipCount=U32(h+28);
    Need(image.width&&image.height&&image.width<=limits.maximumWidth&&image.height<=limits.maximumHeight,"DDS dimensions are zero or exceed configured limits");
    Need(U32(h+112)==0&&U32(h+24)<=1&&!(U32(h+8)&0x800000),"DDS cube/volume/other caps2 surfaces are unsupported");
    const std::size_t decodedBytes=Product(Product(image.width,image.height),4);
    Need(decodedBytes<=limits.decodedBytes,"DDS decoded byte limit exceeded");
    const std::uint32_t pfFlags=U32(h+80),fourCC=U32(h+84);image.sourceFormatMarker=U32(h+88);
    unsigned int kind=0;
    if(pfFlags&4) {
        Need((pfFlags&~5u)==0,"Unsupported DDS compressed pixel flags");
        if(fourCC==0x31545844){kind=1;image.format="BC1/DXT1";}
        else if(fourCC==0x33545844){kind=2;image.format="BC2/DXT3";}
        else if(fourCC==0x35545844){kind=3;image.format="BC3/DXT5";}
        else throw std::runtime_error("DDS compression is unsupported (DX10/DXT2/DXT4/other format)");
    } else {
        Need(pfFlags==2&&U32(h+88)==8&&fourCC==0&&U32(h+92)==0&&U32(h+96)==0&&U32(h+100)==0&&U32(h+104)==255,"DDS RGB masks or non-A8 uncompressed format are unsupported");
        image.format="A8";
    }
    if(image.mipCount==0)image.mipCount=1; // Some writers omit mip flags/count for a single level.
    std::uint32_t maxDimension=std::max(image.width,image.height),maxMips=1;
    while(maxDimension>1){maxDimension/=2;++maxMips;}
    Need(image.mipCount<=maxMips,"DDS mip count exceeds dimension chain");
    std::size_t required=128;std::uint32_t w=image.width,t=image.height;
    for(std::uint32_t mip=0;mip<image.mipCount;++mip){required=Sum(required,MipBytes(w,t,kind));w=std::max(1u,w/2);t=std::max(1u,t/2);}
    Need(required==data.size(),"DDS tightly packed mip-chain size mismatch or trailing bytes");
    // Header pitch/linear-size fields are deliberately not trusted. Observed
    // Wolf A8 files mark total/incorrect values as pitch, while their entire
    // tightly packed mip chain matches exactly. Padding variants fail closed.
    image.rgba.resize(decodedBytes);
    if(kind==0) {
        for(std::size_t i=0;i<Product(image.width,image.height);++i){image.rgba[i*4]=255;image.rgba[i*4+1]=255;image.rgba[i*4+2]=255;image.rgba[i*4+3]=data[128+i];}
    } else {
        const std::size_t blockBytes=kind==1?8:16,blocksX=std::size_t(image.width)/4+(image.width%4?1:0),blocksY=std::size_t(image.height)/4+(image.height%4?1:0);
        for(std::size_t by=0;by<blocksY;++by)for(std::size_t bx=0;bx<blocksX;++bx){
            Color block[16];DecodeBlock(h+128+(by*blocksX+bx)*blockBytes,kind,block);
            for(std::size_t y=0;y<4&&by*4+y<image.height;++y)for(std::size_t x=0;x<4&&bx*4+x<image.width;++x){
                const Color c=block[y*4+x];const std::size_t out=((by*4+y)*image.width+bx*4+x)*4;
                image.rgba[out]=c.r;image.rgba[out+1]=c.g;image.rgba[out+2]=c.b;image.rgba[out+3]=c.a;
            }
        }
    }
    return image;
}
}
