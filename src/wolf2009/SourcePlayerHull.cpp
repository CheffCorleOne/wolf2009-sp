// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourcePlayerHull.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
void Need(bool value,const char* reason) { if(!value)throw std::runtime_error(reason); }
// A binary32 coordinate is a signed24-bit integer times2^e, -149<=e<=104.
// On a common dyadic grid, one coordinate difference fits278 bits. A3x3
// determinant needs at most837 bits including the six summands; 32words
// leave a checked bound. Arithmetic is unsigned, with explicit carries.
struct Integer {
    static const unsigned Capacity=32;
    std::uint32_t word[Capacity];
    unsigned used;
    int sign;
    Integer():used(0),sign(0) { std::memset(word,0,sizeof(word)); }
    void Normalize() { while(used&&word[used-1]==0)--used;if(!used)sign=0; }
};
int MagnitudeCompare(const Integer& a,const Integer& b) {
    if(a.used!=b.used)return a.used<b.used?-1:1;
    for(unsigned i=a.used;i>0;--i)if(a.word[i-1]!=b.word[i-1])return a.word[i-1]<b.word[i-1]?-1:1;
    return 0;
}
Integer Negate(Integer a) { a.sign=-a.sign;return a; }
Integer Add(const Integer& a,const Integer& b) {
    if(!a.sign)return b;if(!b.sign)return a;
    Integer r;
    if(a.sign==b.sign) {
        const unsigned count=std::max(a.used,b.used);std::uint64_t carry=0;
        for(unsigned i=0;i<count;++i) {
            const std::uint64_t v=std::uint64_t(a.word[i])+b.word[i]+carry;
            r.word[i]=std::uint32_t(v);carry=v>>32;
        }
        r.used=count;
        if(carry){Need(r.used<Integer::Capacity,"Exact hull integer overflow");r.word[r.used++]=std::uint32_t(carry);}
        r.sign=a.sign;
    } else {
        const int cmp=MagnitudeCompare(a,b);if(!cmp)return r;
        const Integer& greater=cmp>0?a:b;const Integer& lesser=cmp>0?b:a;
        std::uint64_t borrow=0;
        for(unsigned i=0;i<greater.used;++i) {
            const std::uint64_t sub=std::uint64_t(lesser.word[i])+borrow;
            r.word[i]=std::uint32_t(std::uint64_t(greater.word[i])-sub);
            borrow=std::uint64_t(greater.word[i])<sub?1:0;
        }
        r.used=greater.used;r.sign=greater.sign;r.Normalize();
    }
    return r;
}
Integer Subtract(const Integer& a,const Integer& b) { return Add(a,Negate(b)); }
Integer Multiply(const Integer& a,const Integer& b) {
    Integer r;if(!a.sign||!b.sign)return r;
    Need(a.used+b.used<=Integer::Capacity,"Exact hull integer product overflow");
    for(unsigned i=0;i<a.used;++i) {
        std::uint64_t carry=0;
        for(unsigned j=0;j<b.used;++j) {
            const std::uint64_t v=std::uint64_t(a.word[i])*b.word[j]+r.word[i+j]+carry;
            r.word[i+j]=std::uint32_t(v);carry=v>>32;
        }
        r.word[i+b.used]=std::uint32_t(carry);
    }
    r.used=a.used+b.used;r.sign=a.sign*b.sign;r.Normalize();return r;
}
struct FloatWord {std::uint32_t mantissa;int exponent;int sign;};
FloatWord Parts(float f) {
    static_assert(sizeof(float)==4&&sizeof(std::uint32_t)==4,"binary32 storage required");
    Need(std::numeric_limits<float>::is_iec559,"IEEE binary32 required");
    std::uint32_t bits;std::memcpy(&bits,&f,4);const unsigned exp=(bits>>23)&255;
    Need(exp!=255,"Nonfinite source player support");
    FloatWord out;out.mantissa=bits&0x7fffff;out.sign=bits>>31?-1:1;
    if(exp){out.mantissa|=0x800000;out.exponent=int(exp)-150;}else out.exponent=-149;
    if(!out.mantissa)out.sign=0;return out;
}
Integer OnGrid(float f,int exponent) {
    const auto p=Parts(f);Integer r;if(!p.sign)return r;
    const int shift=p.exponent-exponent;Need(shift>=0&&shift<=253,"Hull dyadic grid outside binary32 bound");
    const unsigned index=unsigned(shift)/32,bit=unsigned(shift)&31;
    const std::uint64_t v=std::uint64_t(p.mantissa)<<bit;
    r.word[index]=std::uint32_t(v);r.word[index+1]=std::uint32_t(v>>32);
    r.used=index+2;r.sign=p.sign;r.Normalize();return r;
}
class Predicate {
    typedef std::array<Integer,3> ExactPoint;
    const std::vector<std::array<float,3>>& points;
    std::vector<ExactPoint> exact;
public:
    std::size_t exactCalls=0;
    explicit Predicate(const std::vector<std::array<float,3>>& p):points(p) {
        int exponent=104;
        for(const auto& v:p)for(float f:v){const auto bits=Parts(f);if(bits.sign)exponent=std::min(exponent,bits.exponent);}
        exact.resize(p.size());
        for(std::size_t i=0;i<p.size();++i)for(unsigned j=0;j<3;++j)exact[i][j]=OnGrid(p[i][j],exponent);
    }
    int Sign(std::size_t a,std::size_t b,std::size_t c,std::size_t d) {
        double x[3],y[3],z[3];
        for(unsigned j=0;j<3;++j){x[j]=double(points[b][j])-points[a][j];y[j]=double(points[c][j])-points[a][j];z[j]=double(points[d][j])-points[a][j];}
        const double det=x[0]*(y[1]*z[2]-y[2]*z[1])-x[1]*(y[0]*z[2]-y[2]*z[0])+x[2]*(y[0]*z[1]-y[1]*z[0]);
        const double permanent=std::fabs(x[0])*(std::fabs(y[1]*z[2])+std::fabs(y[2]*z[1]))+
            std::fabs(x[1])*(std::fabs(y[0]*z[2])+std::fabs(y[2]*z[0]))+
            std::fabs(x[2])*(std::fabs(y[0]*z[1])+std::fabs(y[1]*z[0]));
        // Conservative IEEE round-to-nearest filter; core builds disable fast
        // math and contraction. Exact fallback determines every uncertain sign.
        if(std::fabs(det)>permanent*1e-13)return det>0?1:-1;
        ++exactCalls;Integer u[3],v[3],w[3];
        for(unsigned j=0;j<3;++j){u[j]=Subtract(exact[b][j],exact[a][j]);v[j]=Subtract(exact[c][j],exact[a][j]);w[j]=Subtract(exact[d][j],exact[a][j]);}
        const Integer t0=Multiply(u[0],Subtract(Multiply(v[1],w[2]),Multiply(v[2],w[1])));
        const Integer t1=Multiply(u[1],Subtract(Multiply(v[0],w[2]),Multiply(v[2],w[0])));
        const Integer t2=Multiply(u[2],Subtract(Multiply(v[0],w[1]),Multiply(v[1],w[0])));
        return Add(Subtract(t0,t1),t2).sign;
    }
};
typedef std::array<std::uint32_t,3> Triangle;
std::pair<std::uint32_t,std::uint32_t> EdgeKey(std::uint32_t a,std::uint32_t b) {return std::minmax(a,b);}
double Distance2(const std::array<float,3>& a,const std::array<float,3>& b) {
    double r=0;for(unsigned i=0;i<3;++i){const double d=double(a[i])-b[i];r+=d*d;}return r;
}
double TriangleArea2(const std::array<float,3>& a,const std::array<float,3>& b,const std::array<float,3>& c) {
    double u[3],v[3];for(unsigned i=0;i<3;++i){u[i]=double(b[i])-a[i];v[i]=double(c[i])-a[i];}
    const double x=u[1]*v[2]-u[2]*v[1],y=u[2]*v[0]-u[0]*v[2],z=u[0]*v[1]-u[1]*v[0];return x*x+y*y+z*z;
}
double ApproxVolume(const std::array<float,3>& a,const std::array<float,3>& b,const std::array<float,3>& c,const std::array<float,3>& d) {
    double u[3],v[3],w[3];for(unsigned i=0;i<3;++i){u[i]=double(b[i])-a[i];v[i]=double(c[i])-a[i];w[i]=double(d[i])-a[i];}
    return std::fabs(u[0]*(v[1]*w[2]-v[2]*w[1])-u[1]*(v[0]*w[2]-v[2]*w[0])+u[2]*(v[0]*w[1]-v[1]*w[0]));
}
}

int SourcePlayerHullOrientation(const std::array<float,3>& a,const std::array<float,3>& b,const std::array<float,3>& c,const std::array<float,3>& d) {
    const std::vector<std::array<float,3>> p={a,b,c,d};Predicate predicate(p);return predicate.Sign(0,1,2,3);
}

SourcePlayerHull BuildSourcePlayerHull(const std::vector<std::array<float,3>>& points,SourcePlayerHullPolicy policy,const SourcePlayerHullLimits& limits) {
    Need(policy==SourcePlayerHullPolicy::NativeFloatExactConvexSurfaceV1,"Explicit source-player hull policy required");
    Need(limits.maximumVertices>=4&&limits.maximumVertices<=64&&limits.maximumEdges>=6&&limits.maximumEdges<=160&&
        limits.maximumFaces>=4&&limits.maximumFaces<=96,"Source-player hull limits exceed bounded native profile");
    Need(points.size()>=4&&points.size()<=limits.maximumVertices,"Source-player hull support count outside native profile");
    Predicate predicate(points);
    for(std::size_t i=0;i<points.size();++i)for(std::size_t j=0;j<i;++j)Need(points[i]!=points[j],"Duplicate source-player hull support");
    std::uint32_t initial[4]={0,0,0,0};double best=-1;
    for(std::size_t i=1;i<points.size();++i){const double d=Distance2(points[0],points[i]);if(d>best){best=d;initial[1]=std::uint32_t(i);}}
    best=-1;
    for(std::size_t i=0;i<points.size();++i){const double d=TriangleArea2(points[initial[0]],points[initial[1]],points[i]);if(d>best){best=d;initial[2]=std::uint32_t(i);}}
    best=-1;
    for(std::size_t i=0;i<points.size();++i){const double d=ApproxVolume(points[initial[0]],points[initial[1]],points[initial[2]],points[i]);if(d>best){best=d;initial[3]=std::uint32_t(i);}}
    Need(predicate.Sign(initial[0],initial[1],initial[2],initial[3])!=0,"Source-player hull is coplanar or degenerate");
    std::vector<Triangle> faces;
    for(unsigned missing=0;missing<4;++missing){Triangle face;unsigned at=0;for(unsigned j=0;j<4;++j)if(j!=missing)face[at++]=initial[j];
        if(predicate.Sign(face[0],face[1],face[2],initial[missing])>0)std::swap(face[1],face[2]);faces.push_back(face);}
    struct Boundary {unsigned count=0;std::uint32_t a=0,b=0;};
    for(std::uint32_t i=0;i<points.size();++i) {
        if(std::find(initial,initial+4,i)!=initial+4)continue;
        std::vector<Triangle> kept;std::map<std::pair<std::uint32_t,std::uint32_t>,Boundary> boundary;
        for(const auto& f:faces) {
            if(predicate.Sign(f[0],f[1],f[2],i)>0) {
                for(unsigned j=0;j<3;++j){const auto a=f[j],b=f[(j+1)%3];auto& e=boundary[EdgeKey(a,b)];
                    if(e.count)Need(e.a==b&&e.b==a,"Inconsistent visible hull orientation");else{e.a=a;e.b=b;}++e.count;Need(e.count<=2,"Nonmanifold visible hull edge");}
            } else kept.push_back(f);
        }
        for(const auto& edge:boundary)if(edge.second.count==1)kept.push_back(Triangle{{edge.second.a,edge.second.b,i}});
        // A convex triangulated hull with<=64 vertices has<=124 triangles even
        // before its final selected96-face native limit is checked.
        Need(kept.size()<=124,"Source-player hull temporary face budget");faces.swap(kept);
    }
    Need(faces.size()>=4&&faces.size()<=limits.maximumFaces,"Source-player hull exceeds native face capacity");
    SourcePlayerHull result;result.points=points;
    struct FinalEdge {std::uint32_t a,b;unsigned count;int index;};
    std::map<std::pair<std::uint32_t,std::uint32_t>,FinalEdge> edges;
    std::vector<bool> used(points.size(),false);
    for(const auto& f:faces) {
        Need(TriangleArea2(points[f[0]],points[f[1]],points[f[2]])>0,"Degenerate source-player hull face");
        for(std::size_t j=0;j<points.size();++j)Need(predicate.Sign(f[0],f[1],f[2],j)<=0,"Source-player hull excluded a selected support");
        SourcePlayerHullFace face;face.vertices=f;
        for(unsigned j=0;j<3;++j) {
            const auto a=f[j],b=f[(j+1)%3];used[a]=true;const auto key=EdgeKey(a,b);auto found=edges.find(key);
            if(found==edges.end()) {
                Need(result.edges.size()<limits.maximumEdges,"Source-player hull exceeds native edge capacity");
                const int number=int(result.edges.size()+1);result.edges.push_back(std::array<std::uint32_t,2>{{a,b}});
                edges.insert(std::make_pair(key,FinalEdge{a,b,1,number}));face.edges[j]=number;
            } else {auto& e=found->second;Need(e.count==1&&e.a==b&&e.b==a,"Source-player hull surface is not closed/oriented");++e.count;face.edges[j]=-e.index;}
        }
        result.faces.push_back(face);
    }
    for(bool value:used)Need(value,"Selected source support is not a hull vertex; reduction is not admitted");
    for(const auto& e:edges)Need(e.second.count==2,"Open source-player hull boundary");
    Need(points.size()+result.faces.size()==result.edges.size()+2,"Source-player hull Euler invariant failed");
    result.exactPredicates=predicate.exactCalls;return result;
}
}
