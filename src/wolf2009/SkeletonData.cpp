// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkeletonData.h"
#include "HavokPack.h"
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#ifdef __FAST_MATH__
#error Skeleton_data_requires_finite_IEEE_arithmetic
#endif
namespace wolf2009 { namespace {
void Need(bool v,const char* s){if(!v)throw std::runtime_error(s);}
double Float(std::uint32_t word){float v;std::memcpy(&v,&word,4);return v;}
GreeterJointPose Compose(const GreeterJointPose& a,const GreeterJointPose& b){
    const auto& q=a.rotation;const auto& v=b.translation;
    const std::array<double,3> t{{2*(q[1]*v[2]-q[2]*v[1]),2*(q[2]*v[0]-q[0]*v[2]),2*(q[0]*v[1]-q[1]*v[0])}};
    GreeterJointPose out;
    out.translation={{a.translation[0]+v[0]+q[3]*t[0]+q[1]*t[2]-q[2]*t[1],a.translation[1]+v[1]+q[3]*t[1]+q[2]*t[0]-q[0]*t[2],a.translation[2]+v[2]+q[3]*t[2]+q[0]*t[1]-q[1]*t[0]}};
    const auto& c=b.rotation;
    out.rotation={{q[3]*c[0]+q[0]*c[3]+q[1]*c[2]-q[2]*c[1],q[3]*c[1]-q[0]*c[2]+q[1]*c[3]+q[2]*c[0],q[3]*c[2]+q[0]*c[1]-q[1]*c[0]+q[2]*c[3],q[3]*c[3]-q[0]*c[0]-q[1]*c[1]-q[2]*c[2]}};
    double n=0;for(double x:out.rotation)n+=x*x;for(auto& x:out.rotation)x/=std::sqrt(n);return out;
}
bool Reference(SkeletonJointData& joint){
    bool known=true;double norm=0;
    for(unsigned i=0;i<3;++i){const double t=Float(joint.rawReference[i]),scale=Float(joint.rawReference[8+i]);known=known&&std::isfinite(t)&&std::abs(t)<=1e8&&std::isfinite(scale)&&std::abs(scale-1)<1e-5;joint.referenceLocal.translation[i]=t;}
    for(unsigned i=0;i<4;++i){const double q=Float(joint.rawReference[4+i]);known=known&&std::isfinite(q);joint.referenceLocal.rotation[i]=q;norm+=q*q;}
    known=known&&std::isfinite(norm)&&std::abs(norm-1)<.001;
    if(!known){joint.referenceLocal=GreeterJointPose();return false;}
    for(auto& q:joint.referenceLocal.rotation)q/=std::sqrt(norm);return true;
}
} // namespace
SkeletonData ReadSkeletonData(const std::vector<unsigned char>& bytes,const std::string& name,const SkeletonDataLimits& limits){
    Need(!name.empty()&&name.size()<=4096,"invalid exact skeleton object name");
    for(unsigned char c:name)Need(c>=32&&c<=126,"invalid skeleton object name byte");
    detail::HavokPack p(bytes,limits.sourceBytes,limits.objects,limits.fixups);
    const auto root=p.U(28),variantCount=p.DU(root+4),variants=p.Pointer(root);
    Need(variantCount&&variantCount<=limits.skeletons,"skeleton variant table budget");p.ArrayRange(variants,variantCount,16);
    std::uint32_t container=0;
    for(std::uint32_t i=0;i<variantCount;++i){const auto at=variants+16*i;
        if(p.DataString(p.Pointer(at))!="Animation Container")continue;
        Need(!container&&p.DataString(p.Pointer(at+4))=="hkaAnimationContainer","ambiguous/mistyped animation container");
        container=p.Pointer(at+8);Need(p.Type(container)=="hkaAnimationContainer","mistyped animation container object");}
    Need(container!=0,"named animation container absent");const auto count=p.DU(container+4),array=p.Pointer(container);
    Need(count&&count<=limits.skeletons,"skeleton table budget");p.ArrayRange(array,count,4);
    std::uint32_t chosen=0;bool found=false;
    for(std::uint32_t i=0;i<count;++i){const auto object=p.Pointer(array+4*i);Need(p.Type(object)=="hkaSkeleton","mistyped skeleton array object");
        if(p.DataString(p.Pointer(object))!=name)continue;Need(!found,"duplicate exact skeleton name");found=true;chosen=object;}
    Need(found,"exact named skeleton absent");p.DataRange(chosen,28);const auto n=p.DU(chosen+8);
    Need(n&&n<=limits.joints&&n<=256&&p.DU(chosen+16)==n&&p.DU(chosen+24)==n,"skeleton joint arrays mismatch/budget");
    const auto parents=p.Pointer(chosen+4),bones=p.Pointer(chosen+12),references=p.Pointer(chosen+20);
    p.ArrayRange(parents,n,2);p.ArrayRange(bones,n,4);p.ArrayRange(references,n,48);
    SkeletonData out;out.name=name;out.sourceObjectOffset=chosen;out.referencePoseQualified=true;
    std::size_t names=name.size();std::set<std::string> unique;
    for(std::uint32_t i=0;i<n;++i){const auto bone=p.Pointer(bones+4*i);Need(p.Type(bone)=="hkaBone","mistyped skeleton bone");p.DataRange(bone,8);
        SkeletonJointData joint;joint.name=p.DataString(p.Pointer(bone));
        Need(!joint.name.empty()&&joint.name.size()<=255&&unique.insert(joint.name).second&&names<=limits.nameBytes&&joint.name.size()<=limits.nameBytes-names,"duplicate/oversized skeleton bone name");names+=joint.name.size();
        const auto absolute=p.sections[2].absolute+parents+2*i;const unsigned parent=unsigned(p.bytes[absolute])|(unsigned(p.bytes[absolute+1])<<8);
        joint.parent=parent==65535?-1:int(parent);Need(i?joint.parent>=0&&std::uint32_t(joint.parent)<i:joint.parent==-1,"unsupported skeleton hierarchy: expected one ordered tree");
        for(unsigned c=0;c<12;++c)joint.rawReference[c]=p.DU(references+48*i+4*c);joint.rawBoneWord=p.DU(bone+4);
        joint.referencePoseQualified=Reference(joint)&&(i==0||out.joints[std::size_t(joint.parent)].referencePoseQualified);
        if(joint.referencePoseQualified)joint.referenceModel=i?Compose(out.joints[std::size_t(joint.parent)].referenceModel,joint.referenceLocal):joint.referenceLocal;
        out.referencePoseQualified=out.referencePoseQualified&&joint.referencePoseQualified;out.joints.push_back(std::move(joint));}
    return out;
}
}
