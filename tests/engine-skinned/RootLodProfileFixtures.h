// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ROOT_LOD_PROFILE_FIXTURES_H
#define WOLF2009_ROOT_LOD_PROFILE_FIXTURES_H
#include "MeshV2Fixtures.h"
namespace rootlodfixtures {
using meshv2fixtures::Bytes;
using meshv2fixtures::Float;
using meshv2fixtures::Put;
using meshv2fixtures::Text;
inline void Buffer(Bytes& data,bool rigid) {
    Text(data,"VertexBuffer { VertexFormat { ");
    Text(data,rigid?"Position 3 BlendIndex Normal Tangent Binormal DiffuseColor TexCoord 2 0":
        "Position 3 BlendIndex BlendWeight 3 4 Normal Tangent DiffuseColor TexCoord 2 0");
    Text(data," } SystemMemory VideoMemory Vertex [3] { bin 192\n");
    const auto at=data.size();data.resize(at+192);
    for(unsigned v=0;v<3;++v) {
        Float(data,at+64*v,v==1?4.f:0.f);Float(data,at+64*v+4,v==2?4.f:0.f);
        data[at+64*v+12]=static_cast<unsigned char>(rigid?1:0);
        data[at+64*v+13]=1;data[at+64*v+15]=1;
        if(!rigid) {Float(data,at+64*v+16,-.25f);Float(data,at+64*v+20,.25f);Float(data,at+64*v+24,.25f);}
        Put(data,at+64*v+52,0x11223344);Float(data,at+64*v+56,.25f);Float(data,at+64*v+60,.75f);
    }
    Text(data,"\n} } ");
}
// Two independently declared render buffers, weighted64 and rigid64. Marker
// -1 means absent. This produces only authored test bytes, never source assets.
inline Bytes Model(unsigned joints,const std::vector<int>& markers={-1,0},
                   const std::vector<std::string>& distances={"200"},bool root=true,bool reverse=false) {
    Bytes data;Text(data,"MemSize 1024 MD5RVersion 1 Joint ["+std::to_string(joints)+"] { ");
    for(unsigned i=0;i<joints;++i)
        Text(data,"\"own"+std::to_string(i)+"\" "+(i?"0":"-1")+" "+std::to_string(i)+" 0 0 0 0 0 ");
    Text(data,"} VertexBuffer [2] { ");Buffer(data,false);Buffer(data,true);
    Text(data,"} IndexBuffer [1] { IndexBuffer { SystemMemory BitDepth 16 Index [3] { bin 6\n");
    data.insert(data.end(),{0,0,1,0,2,0});Text(data,"\n} } } SilhouetteEdge [0] { } ");
    if(root) {
        Text(data,"LevelOfDetail ["+std::to_string(distances.size())+"] { ");
        for(const auto& value:distances)Text(data,value+" ");
        Text(data,"} ");
    }
    Text(data,"Mesh ["+std::to_string(markers.size())+"] { ");
    for(std::size_t mesh=0;mesh<markers.size();++mesh) {
        Text(data,"Mesh { ");
        if(markers[mesh]!=-1)Text(data,"LevelOfDetail "+std::to_string(markers[mesh])+" ");
        const auto buffer=(mesh+(reverse?1:0))%2;
        Text(data,"Material \"materials/own"+std::to_string(mesh)+"\" SilTraceBuffers 0 0 DrawBuffers "+
            std::to_string(buffer)+" 0 ShadowVolumeBuffers -1 -1 PrimBatch [1] { PrimBatch { Transform [2] { 0 "+
            std::to_string(joints-1)+" } SilTraceIndexedTriList 0 3 0 1 DrawIndexedTriList 0 3 0 1 ShadowVerts 0 SilhouetteEdge 0 0 } } } ");
    }
    Text(data,"} Bounds -1000 -1000 -1000 1000 1000 1000");return data;
}
}
#endif
