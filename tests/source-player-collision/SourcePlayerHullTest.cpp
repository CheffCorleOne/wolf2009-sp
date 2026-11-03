// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourcePlayerHull.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks=0,refusals=0;
void Check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void Refuse(F fn,const char* why){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}++refusals;Check(rejected,why);}
using namespace wolf2009;
typedef std::array<float,3> Point;
typedef std::vector<Point> Points;
SourcePlayerHull Hull(const Points& p){return BuildSourcePlayerHull(p,SourcePlayerHullPolicy::NativeFloatExactConvexSurfaceV1);}
void Shape(const SourcePlayerHull& h,const Points& p,std::size_t edges,std::size_t faces){
    Check(h.points==p,"Every selected native-f32 support must remain unchanged");Check(h.edges.size()==edges,"Hull edge count");Check(h.faces.size()==faces,"Hull face count");
    for(const auto& f:h.faces)for(std::size_t i=0;i<p.size();++i)Check(SourcePlayerHullOrientation(p[f.vertices[0]],p[f.vertices[1]],p[f.vertices[2]],p[i])<=0,"Selected support lies behind outward face");
    const auto again=Hull(p);Check(again.edges==h.edges,"Deterministic edge indexing");Check(again.faces.size()==h.faces.size(),"Deterministic face count");
    for(std::size_t i=0;i<h.faces.size();++i){Check(again.faces[i].vertices==h.faces[i].vertices,"Deterministic face vertex order");Check(again.faces[i].edges==h.faces[i].edges,"Deterministic signed face edge order");}
}
void Synthetic(){
    const Points tetra={Point{{0,0,0}},Point{{1,0,0}},Point{{0,1,0}},Point{{0,0,1}}};Shape(Hull(tetra),tetra,6,4);
    Points cube;for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)cube.push_back(Point{{float(x),float(y),float(z)}});
    Shape(Hull(cube),cube,18,12);
    Points wide;for(int z=0;z<2;++z)for(int i=0;i<24;++i){const double a=i*6.2831853071795864769/24;wide.push_back(Point{{float(13*std::cos(a)),float(13*std::sin(a)),float(z?57:13)}});}
    wide.push_back(Point{{0,0,0}});wide.push_back(Point{{0,0,70}});Shape(Hull(wide),wide,144,96);
    Refuse([&]{BuildSourcePlayerHull(tetra,SourcePlayerHullPolicy::Unspecified);},"Explicit hull profile required");
    Refuse([&]{Points q=tetra;q.push_back(tetra[0]);Hull(q);},"Duplicate selected support rejected");
    Refuse([&]{Points q=cube;q.push_back(Point{{.5f,.5f,.5f}});Hull(q);},"Interior selected support cannot be silently dropped");
    Refuse([&]{Hull(Points{Point{{0,0,0}},Point{{1,0,0}},Point{{0,1,0}},Point{{1,1,0}}});},"Coplanar hull rejected");
    Refuse([&]{Points q=tetra;q[0][0]=std::numeric_limits<float>::infinity();Hull(q);},"Infinite support rejected");
    Refuse([&]{Points q=tetra;q[0][0]=std::numeric_limits<float>::quiet_NaN();Hull(q);},"NaN support rejected");
    Refuse([&]{Points q(65,Point{{1,1,1}});Hull(q);},"Input count bound before convexification");
    Refuse([&]{SourcePlayerHullLimits l;l.maximumFaces=95;BuildSourcePlayerHull(wide,SourcePlayerHullPolicy::NativeFloatExactConvexSurfaceV1,l);},"Native face bound refuses without simplification");
    Refuse([&]{SourcePlayerHullLimits l;l.maximumEdges=143;BuildSourcePlayerHull(wide,SourcePlayerHullPolicy::NativeFloatExactConvexSurfaceV1,l);},"Native edge bound refuses without simplification");
    Refuse([&]{SourcePlayerHullLimits l;l.maximumVertices=65;BuildSourcePlayerHull(tetra,SourcePlayerHullPolicy::NativeFloatExactConvexSurfaceV1,l);},"Unbounded profile rejected");
    // Determinants at the full binary32 exponent spread force exact arithmetic.
    const float lo=std::numeric_limits<float>::denorm_min(),hi=std::numeric_limits<float>::max();
    const Point a{{0,0,0}},b{{hi,0,0}},c{{0,hi,0}},d{{0,0,lo}};
    Check(SourcePlayerHullOrientation(a,b,c,d)==1,"Wide dyadic exponent orientation");
    Check(SourcePlayerHullOrientation(a,c,b,d)==-1,"Wide dyadic exponent inverse orientation");
    const Point u{{hi,lo,1}},v{{lo,hi,1}},w{{hi,hi,1}},p{{0,0,1}};
    Check(SourcePlayerHullOrientation(u,v,w,p)==0,"Exact coplanarity across exponent spread");
    Point above=p;above[2]=std::nextafter(1.0f,2.0f);
    Check(SourcePlayerHullOrientation(u,v,w,above)<0,"One-f32-step plane side is preserved");
}
void SourcePoints(const char* path){
    std::ifstream in(path);Check(bool(in),"Private source points input absent");unsigned groups=0;in>>groups;Check(groups==2,"Exactly selected stand and crouch shapes required");
    for(unsigned s=0;s<groups;++s){unsigned count=0;in>>count;Check(count==50,"Selected source support count");Points p(count);for(auto& point:p)for(float& v:point)in>>v;
        Check(bool(in),"Private source points truncated");const auto h=Hull(p);Shape(h,p,144,96);std::printf("SOURCE_PLAYER_HULL shape=%u vertices=%zu edges=%zu faces=%zu exact=%zu\n",s,h.points.size(),h.edges.size(),h.faces.size(),h.exactPredicates);}
    std::string extra;Check(!(in>>extra),"Private source point file trailing data");
}
}
int main(int argc,char** argv){try{Synthetic();if(argc==3&&std::string(argv[1])=="--source-points")SourcePoints(argv[2]);else Check(argc==1,"Unknown source-player hull arguments");
    std::printf("SOURCE_PLAYER_HULL_TEST PASS checks=%zu refusals=%zu\n",checks,refusals);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"SOURCE_PLAYER_HULL_TEST FAIL: %s\n",e.what());return 1;}}
