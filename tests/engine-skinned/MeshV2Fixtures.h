// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_MESH_V2_FIXTURES_H
#define WOLF2009_MESH_V2_FIXTURES_H
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace meshv2fixtures {
typedef std::vector<unsigned char> Bytes;
inline void Put(Bytes& b,std::size_t at,std::uint32_t v){for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<unsigned char>(v>>(8*i));}
inline void Float(Bytes& b,std::size_t at,float f){std::uint32_t w;std::memcpy(&w,&f,4);Put(b,at,w);}
inline void Text(Bytes& b,const std::string& s){b.insert(b.end(),s.begin(),s.end());}
struct Pack {
    Bytes data;
    std::map<std::uint32_t,std::uint32_t> links;
    std::map<std::uint32_t,std::string> types;
    std::uint32_t Alloc(unsigned n){while(data.size()%4)data.push_back(0);const auto at=std::uint32_t(data.size());data.resize(data.size()+n);return at;}
    std::uint32_t String(const std::string& s){const auto at=std::uint32_t(data.size());Text(data,s);data.push_back(0);return at;}
    void Link(std::uint32_t at,std::uint32_t to){links[at]=to;}
    Bytes Finish()const{
        Bytes blob=data;while(blob.size()%4)blob.push_back(0);const auto local=std::uint32_t(blob.size());
        const auto append=[&](std::uint32_t w){const auto at=blob.size();blob.resize(at+4);Put(blob,at,w);};
        for(const auto& p:links){append(p.first);append(p.second);}const auto virtuals=std::uint32_t(blob.size());
        Bytes names;std::map<std::string,std::uint32_t> offsets;
        for(const auto& p:types)if(!offsets.count(p.second)){offsets[p.second]=std::uint32_t(names.size());Text(names,p.second);names.push_back(0);}
        for(const auto& p:types){append(p.first);append(0);append(offsets.at(p.second));}
        const auto end=std::uint32_t(blob.size());Bytes result(208);Put(result,0,0x57e0e057);Put(result,4,0x10c0c010);Put(result,12,5);
        result[16]=4;result[17]=1;result[19]=1;Put(result,20,3);Put(result,24,2);Put(result,36,offsets.at("hkRootLevelContainer"));
        const std::string version="Havok-5.6.0-r1";std::copy(version.begin(),version.end(),result.begin()+40);
        const char* sections[]={"__classnames__","__types__","__data__"};
        for(unsigned i=0;i<3;++i){const std::string s=sections[i];std::copy(s.begin(),s.end(),result.begin()+64+48*i);}
        Put(result,84,208);for(unsigned i=24;i<=44;i+=4)Put(result,64+i,std::uint32_t(names.size()));
        const auto absolute=208+std::uint32_t(names.size());Put(result,132,absolute);Put(result,180,absolute);Put(result,184,local);
        Put(result,188,virtuals);Put(result,192,virtuals);Put(result,196,end);Put(result,200,end);Put(result,204,end);
        result.insert(result.end(),names.begin(),names.end());result.insert(result.end(),blob.begin(),blob.end());return result;
    }
};
struct Skeletons {
    Pack pack;std::uint32_t parents=0,boneArray=0,reference=0,object=0;
    explicit Skeletons(unsigned count){
        pack.Alloc(8);pack.types[0]="hkRootLevelContainer";const auto variants=pack.Alloc(16),container=pack.Alloc(32);
        pack.Link(0,variants);Put(pack.data,4,1);pack.Link(variants,pack.String("Animation Container"));
        pack.Link(variants+4,pack.String("hkaAnimationContainer"));pack.Link(variants+8,container);pack.types[container]="hkaAnimationContainer";
        const auto array=pack.Alloc(8);pack.Link(container,array);Put(pack.data,container+4,2);
        for(unsigned variant=0;variant<2;++variant){const auto skel=pack.Alloc(28),par=pack.Alloc(2*count),bones=pack.Alloc(4*count),refs=pack.Alloc(48*count);
            pack.Link(array+4*variant,skel);pack.types[skel]="hkaSkeleton";pack.Link(skel,pack.String(variant?"rig-B":"rig-A"));
            pack.Link(skel+4,par);pack.Link(skel+12,bones);pack.Link(skel+20,refs);for(unsigned off:{8u,16u,24u})Put(pack.data,skel+off,count);
            pack.data[par]=pack.data[par+1]=255;
            for(unsigned i=0;i<count;++i){const auto bone=pack.Alloc(8);pack.types[bone]="hkaBone";pack.Link(bones+4*i,bone);pack.Link(bone,pack.String("own"+std::to_string(i)));Put(pack.data,bone+4,0x12345678);
                Float(pack.data,refs+48*i,float(100+100*variant+i));Put(pack.data,refs+48*i+12,0x89abcdef);Float(pack.data,refs+48*i+28,1);
                for(unsigned axis=0;axis<3;++axis)Float(pack.data,refs+48*i+32+4*axis,1);}
            if(!variant){parents=par;boneArray=bones;reference=refs;object=skel;}
        }
    }
};
inline Bytes Model(unsigned count,bool rigid=false,bool rootLod=true,bool meshLod=true,bool mixedMeshes=false){
    Bytes data;Text(data,"MemSize 1024\nMD5RVersion 1\nJoint ["+std::to_string(count)+"] {\n");
    for(unsigned i=0;i<count;++i)Text(data,"\"own"+std::to_string(i)+"\" "+(i?"0":"-1")+" "+std::to_string(i)+" 0 0 0 0 0\n");
    Text(data,"}\nVertexBuffer [1] { VertexBuffer { VertexFormat { ");
    Text(data,rigid?"Position 3 BlendIndex Normal Tangent Binormal DiffuseColor TexCoord 2 0":"Position 3 BlendIndex BlendWeight 3 4 Normal Tangent DiffuseColor TexCoord 2 0");
    Text(data," } SystemMemory VideoMemory Vertex [3] { bin 192\n");const auto at=data.size();data.resize(at+192);
    for(unsigned i=0;i<3;++i){Float(data,at+64*i,i==1?4.f:0.f);Float(data,at+64*i+4,i==2?4.f:0.f);
        data[at+64*i+12]=0;data[at+64*i+13]=1;data[at+64*i+14]=0;data[at+64*i+15]=1;
        if(!rigid){Float(data,at+64*i+16,-.25f);Float(data,at+64*i+20,.25f);Float(data,at+64*i+24,.25f);}
        Put(data,at+64*i+52,0x11223344);Float(data,at+64*i+56,.25f);Float(data,at+64*i+60,.75f);}
    Text(data,"\n} } } IndexBuffer [1] { IndexBuffer { SystemMemory BitDepth 16 Index [3] { bin 6\n");data.insert(data.end(),{0,0,1,0,2,0});
    Text(data,"\n} } } SilhouetteEdge [0] { } ");
    if(rootLod)Text(data,"LevelOfDetail [1] { 200 } ");
    Text(data,"Mesh ["+std::to_string(mixedMeshes?2:1)+"] { ");
    for(unsigned m=0;m<(mixedMeshes?2u:1u);++m){Text(data,"Mesh { ");
        if(m?!meshLod:meshLod)Text(data,"LevelOfDetail 0 ");
        Text(data,"Material \"materials/own\" SilTraceBuffers 0 0 DrawBuffers 0 0 ShadowVolumeBuffers -1 -1 PrimBatch [1] { PrimBatch { Transform [2] { 0 "+std::to_string(count-1)+" } SilTraceIndexedTriList 0 3 0 1 DrawIndexedTriList 0 3 0 1 ShadowVerts 0 SilhouetteEdge 0 0 } } } ");}
    Text(data,"} Bounds -1000 -1000 -1000 1000 1000 1000\n");return data;
}
}
#endif
