// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceEntityRuntime.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace wolf2009 { namespace {
void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::string Fold(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
std::int32_t Signed(std::uint32_t word){return word<=UINT32_C(0x7fffffff)?std::int32_t(word):-1-std::int32_t(UINT32_C(0xffffffff)-word);}
bool Resource(const std::string& path){
    if(path.size()<17||path.size()>4096||path.compare(0,9,"wolf2009/"))return false;
    const auto typed=path.find('/',9);if(typed==std::string::npos||typed==9||path.compare(typed,6,"/ENTS/"))return false;
    for(std::size_t start=9;start<path.size();){const auto end=path.find('/',start);const auto part=path.substr(start,end==std::string::npos?end:end-start);
        if(part.empty()||part=="."||part=="..")return false;
        for(unsigned char c:part)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
        if(end==std::string::npos)return true;start=end+1;
    }return false;
}
std::uint64_t Hash(const std::vector<unsigned char>& bytes){std::uint64_t h=UINT64_C(14695981039346656037);for(auto c:bytes){h^=c;h*=UINT64_C(1099511628211);}return h;}
}
bool SourceEntityValue::Text(std::string& out)const {
    if(instance){if(instance->kind!=EntityProperty::Text)return false;out=instance->text;return true;}
    if(!declaration||declaration->encoding!=SourceLiteralEncoding::CompiledString||declaration->value.size()!=1)return false;
    out=declaration->value[0].text;return true;
}
bool SourceEntityValue::Integer(std::int32_t& out)const {
    if(instance){if(instance->kind!=EntityProperty::Integer)return false;out=instance->integer;return true;}
    if(!declaration||declaration->encoding!=SourceLiteralEncoding::CompiledSignedInteger||declaration->numericWords.size()!=1)return false;
    out=Signed(declaration->numericWords[0]);return true;
}
bool SourceEntityValue::Floats(std::vector<float>& out)const {
    std::vector<float> value;
    if(instance){if(instance->kind!=EntityProperty::Floats)return false;value=instance->numbers;}
    else{
        if(!declaration||(declaration->encoding!=SourceLiteralEncoding::CompiledFloat32&&declaration->encoding!=SourceLiteralEncoding::CompiledFloatArray)||declaration->numericWords.empty())return false;
        static_assert(sizeof(float)==sizeof(std::uint32_t)&&std::numeric_limits<float>::is_iec559,"IEEE754 binary32 is required");
        value.resize(declaration->numericWords.size());for(std::size_t i=0;i<value.size();++i)std::memcpy(&value[i],&declaration->numericWords[i],sizeof(float));
    }
    for(const auto x:value)if(!std::isfinite(x))return false;out.swap(value);return true;
}
struct SourceEntityPlan::Impl {
    SourceEntityIdentity identity;
    EntityRecord instance;
    ResolvedSourceDefinition defaults;
    SourceEntityPolicy policy=SourceEntityPolicy::Unspecified;
    std::vector<std::string> keys;
    std::string classname,spawnclass,name,model;
};
SourceEntityPlan::SourceEntityPlan(std::unique_ptr<Impl> data):impl_(std::move(data)){}
SourceEntityPlan::~SourceEntityPlan(){}
const SourceEntityIdentity& SourceEntityPlan::Identity()const{return impl_->identity;}
const EntityRecord& SourceEntityPlan::Instance()const{return impl_->instance;}
const ResolvedSourceDefinition& SourceEntityPlan::Defaults()const{return impl_->defaults;}
SourceEntityValue SourceEntityPlan::Find(const std::string& key)const{SourceEntityValue value;value.instance=impl_->instance.Find(key);value.declaration=impl_->defaults.Find(key);return value;}
const std::vector<std::string>& SourceEntityPlan::Keys()const{return impl_->keys;}
const std::string& SourceEntityPlan::Classname()const{return impl_->classname;}
const std::string& SourceEntityPlan::Spawnclass()const{return impl_->spawnclass;}
const std::string& SourceEntityPlan::Name()const{return impl_->name;}
const std::string& SourceEntityPlan::ModelDefinition()const{return impl_->model;}
SourceEntityPolicy SourceEntityPlan::Policy()const{return impl_->policy;}
SourceEntityPlacement PrepareSourceEntityPlacement(const SourceEntityPlan& plan,SourceEntityPlacementPolicy policy){
    Need(policy==SourceEntityPlacementPolicy::NativeAffinePlacementV1||policy==SourceEntityPlacementPolicy::NativeNearUnitPlacementV1,"An explicit source placement profile is required");
    SourceEntityPlacement out;std::vector<float> values;const auto origin=plan.Find("origin");
    if(origin.instance||origin.declaration){Need(origin.Floats(values)&&values.size()==3,"Source origin requires three finite float words");for(unsigned i=0;i<3;++i){Need(std::abs(values[i])<=10000000,"Source origin domain exceeded");out.origin[i]=values[i];}}
    const auto rotation=plan.Find("rotation");
    if(rotation.instance||rotation.declaration){Need(rotation.Floats(values)&&values.size()==9,"Source rotation requires nine finite float words");std::copy(values.begin(),values.end(),out.authoredAxis.begin());}
    else{const auto angle=plan.Find("angle");if(angle.instance||angle.declaration){std::int32_t integer=0;bool zero=false;
        if(angle.Floats(values))zero=values.size()==1&&values[0]==0;else if(angle.Integer(integer))zero=integer==0;
        Need(zero,"Nonzero angle-only source placement is outside the explicit matrix profile");}}
    double a[3][3],r[3][3];for(unsigned i=0;i<3;++i){double squared=0;for(unsigned j=0;j<3;++j){a[i][j]=out.authoredAxis[3*i+j];squared+=a[i][j]*a[i][j];}
        const auto length=std::sqrt(squared);Need(std::isfinite(length)&&length>=1e-4&&length<=1024,"Source affine axis scale is outside the bounded profile");out.authoredRowLengths[i]=length;
        out.maximumScaleError=std::max(out.maximumScaleError,std::abs(length-1));for(unsigned j=0;j<3;++j)r[i][j]=a[i][j]/length;
    }
    Need(policy!=SourceEntityPlacementPolicy::NativeNearUnitPlacementV1||out.maximumScaleError<=0.005,"Source scale exceeds NativeNearUnitPlacementV1's explicit half-percent limit");
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j){double dot=0;for(unsigned k=0;k<3;++k)dot+=r[i][k]*r[j][k];Need(std::abs(dot)<=1e-4,"Source affine shear exceeds the bounded near-orthogonal profile");}
    const double handed=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    Need(handed>0,"Source affine reflection is unimplemented");
    double dot=0;for(unsigned k=0;k<3;++k)dot+=r[1][k]*r[0][k];double squared=0;
    for(unsigned k=0;k<3;++k){r[1][k]-=dot*r[0][k];squared+=r[1][k]*r[1][k];}const auto length=std::sqrt(squared);Need(length>0,"Source physics basis is degenerate");
    for(unsigned k=0;k<3;++k)r[1][k]/=length;
    r[2][0]=r[0][1]*r[1][2]-r[0][2]*r[1][1];r[2][1]=r[0][2]*r[1][0]-r[0][0]*r[1][2];r[2][2]=r[0][0]*r[1][1]-r[0][1]*r[1][0];
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){out.physicsAxis[3*i+j]=static_cast<float>(r[i][j]);r[i][j]=out.physicsAxis[3*i+j];}
    const double determinant=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    Need(std::isfinite(determinant)&&determinant>0.9999&&determinant<1.0001,"Source physics basis rounding is invalid");
    double inverse[3][3];for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)inverse[i][j]=(r[(j+1)%3][(i+1)%3]*r[(j+2)%3][(i+2)%3]-r[(j+1)%3][(i+2)%3]*r[(j+2)%3][(i+1)%3])/determinant;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){double value=0;for(unsigned k=0;k<3;++k)value+=a[i][k]*inverse[k][j];out.visualToPhysics[3*i+j]=static_cast<float>(value);}
    double difference=0;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){double visual=0,gram=0;for(unsigned k=0;k<3;++k){visual+=double(out.visualToPhysics[3*i+k])*r[k][j];gram+=a[i][k]*a[j][k];}
        Need(std::isfinite(visual)&&std::abs(visual-a[i][j])<=32*std::numeric_limits<float>::epsilon()*std::max(1.0,std::abs(a[i][j])),"Source affine reconstruction rounding is invalid");
        out.authoredNonUnitAxes|=std::abs(gram-(i==j?1.0:0.0))>1e-5;const double delta=a[i][j]-r[i][j];difference+=delta*delta;
    }out.matrixDifferenceNorm=std::sqrt(difference);out.placementApproximated=policy==SourceEntityPlacementPolicy::NativeNearUnitPlacementV1&&difference>0;return out;
}
std::shared_ptr<const SourceEntityPlan> PrepareSourceEntity(const std::vector<unsigned char>& bytes,const std::string& path,std::size_t index,
    const SourceDefinitions& definitions,SourceEntityPolicy policy,const SourceDefinitionContext& context,const SourceEntityLimits& limits){
    Need(policy==SourceEntityPolicy::AnimatedContainerV1,"An explicit supported source entity class policy is required");
    Need(Resource(path),"Source entity requires an exact typed ENTS path");
    Need(limits.sourceBytes&&limits.sourceBytes<=64u<<20&&limits.instanceProperties&&limits.instanceProperties<=4096&&limits.effectiveFields&&limits.effectiveFields<=20000,
        "Invalid source entity budgets");
    Need(!bytes.empty()&&bytes.size()<=limits.sourceBytes,"Source entity ENTS byte budget exceeded");
    EntityLimits entityLimits;entityLimits.sourceBytes=limits.sourceBytes;auto document=DecodeEntities4(bytes,entityLimits);
    Need(index<document.entities.size(),"Selected ENTS entity index is out of range");
    std::unique_ptr<SourceEntityPlan::Impl> data(new SourceEntityPlan::Impl);data->instance=std::move(document.entities[index]);data->policy=policy;
    Need(data->instance.properties.size()<=limits.instanceProperties,"Selected ENTS property budget exceeded");
    const auto* classname=data->instance.Find("classname");Need(classname&&classname->kind==EntityProperty::Text&&!classname->text.empty(),"Selected ENTS classname is not a literal text fact");
    data->classname=classname->text;data->defaults=definitions.Resolve(SourceDefinitionKind::EntityDef,data->classname,context);
    auto value=[&](const char* key){SourceEntityValue result;result.instance=data->instance.Find(key);result.declaration=data->defaults.Find(key);return result;};
    Need(value("spawnclass").Text(data->spawnclass)&&data->spawnclass=="rvAnimatedContainer","Selected source spawnclass has no registered implementation");
    Need(value("name").Text(data->name)&&!data->name.empty()&&data->name.size()<=127,"Selected source entity name is missing or excessive");
    Need(value("model").Text(data->model)&&!data->model.empty()&&data->model.size()<=4096,"Selected source model declaration is missing or excessive");
    std::set<std::string> keys;for(const auto& item:data->instance.properties){Need(keys.size()<limits.effectiveFields,"Source effective field budget exceeded");keys.insert(Fold(item.key));}
    for(const auto& item:data->defaults.literalFields){Need(keys.count(item.first)||keys.size()<limits.effectiveFields,"Source effective field budget exceeded");keys.insert(item.first);}
    data->keys.assign(keys.begin(),keys.end());data->identity.resourcePath=path;data->identity.entityIndex=index;data->identity.sourceBytes=bytes.size();data->identity.sourceFingerprint=Hash(bytes);
    return std::shared_ptr<const SourceEntityPlan>(new SourceEntityPlan(std::move(data)));
}
}
