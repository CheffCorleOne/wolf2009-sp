// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourcePlayerHull.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include "idlib/precompiled.h"
#include "EngineSourcePlayerHull.h"

bool Wolf2009_BuildSourcePlayerTraceModel(const float* xyz,int count,
    idTraceModel& out,idStr& reason) {
    try {
        static_assert(MAX_TRACEMODEL_VERTS>=64&&MAX_TRACEMODEL_EDGES>=160&&MAX_TRACEMODEL_POLYS>=96,
            "Source player requires the shared wide trace-model ABI");
        if(!xyz||count<4||count>64)throw std::runtime_error("Source player support count outside native hull profile");
        std::vector<std::array<float,3>> points;
        points.reserve(static_cast<std::size_t>(count));
        for(int i=0;i<count;++i)points.push_back(std::array<float,3>{{xyz[3*i],xyz[3*i+1],xyz[3*i+2]}});
        const auto hull=wolf2009::BuildSourcePlayerHull(points,wolf2009::SourcePlayerHullPolicy::NativeFloatExactConvexSurfaceV1);
        idTraceModel prepared;
        std::memset(&prepared,0,sizeof(prepared));
        prepared.type=TRM_CUSTOM;prepared.numVerts=int(hull.points.size());
        prepared.numEdges=int(hull.edges.size());prepared.numPolys=int(hull.faces.size());prepared.isConvex=true;
        prepared.bounds.Clear();
        for(int i=0;i<prepared.numVerts;++i){const auto& p=hull.points[static_cast<std::size_t>(i)];
            prepared.verts[i].Set(p[0],p[1],p[2]);prepared.bounds.AddPoint(prepared.verts[i]);}
        for(int i=1;i<=prepared.numEdges;++i){const auto& e=hull.edges[static_cast<std::size_t>(i-1)];prepared.edges[i].v[0]=int(e[0]);prepared.edges[i].v[1]=int(e[1]);}
        for(int i=0;i<prepared.numPolys;++i){const auto& f=hull.faces[static_cast<std::size_t>(i)];auto& p=prepared.polys[i];
            const auto& a=hull.points[f.vertices[0]];const auto& b=hull.points[f.vertices[1]];const auto& c=hull.points[f.vertices[2]];
            double u[3],v[3];for(unsigned j=0;j<3;++j){u[j]=double(b[j])-a[j];v[j]=double(c[j])-a[j];}
            const double x=u[1]*v[2]-u[2]*v[1],y=u[2]*v[0]-u[0]*v[2],z=u[0]*v[1]-u[1]*v[0];
            const double length=std::sqrt(x*x+y*y+z*z);
            if(!(length>0)||!std::isfinite(length))throw std::runtime_error("Degenerate native source-player hull plane");
            p.normal.Set(float(x/length),float(y/length),float(z/length));p.dist=p.normal*prepared.verts[f.vertices[0]];
            if(!std::isfinite(p.dist)||std::fabs(p.normal.LengthSqr()-1.0f)>0.0001f)throw std::runtime_error("Source-player plane is not representable in native float");
            p.numEdges=3;p.bounds.Clear();
            for(unsigned j=0;j<3;++j){p.edges[j]=f.edges[j];p.bounds.AddPoint(prepared.verts[f.vertices[j]]);}
            // Exact combinatorial convexity was already proven. This bounds
            // only plane conversion error for the native float CM interface.
            for(int j=0;j<prepared.numVerts;++j)if(p.normal*prepared.verts[j]-p.dist>0.001f)
                throw std::runtime_error("Source-player hull plane exceeds native float conversion bound");
        }
        prepared.offset=(prepared.bounds[0]+prepared.bounds[1])*0.5f;
        prepared.GenerateEdgeNormals();
        for(int i=1;i<=prepared.numEdges;++i)for(unsigned j=0;j<3;++j)
            if(!std::isfinite(prepared.edges[i].normal[j]))throw std::runtime_error("Source-player native edge normal is nonfinite");
        out=prepared;reason.Clear();return true;
    } catch(const std::exception& error) {reason=error.what();return false;}
}
