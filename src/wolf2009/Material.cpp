// SPDX-License-Identifier: GPL-3.0-or-later
#include "Material.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>
namespace wolf2009 {
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string Lower(std::string text){for(std::size_t i=0;i<text.size();++i)if(text[i]>='A'&&text[i]<='Z')text[i]=char(text[i]-'A'+'a');return text;}
bool Space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
bool Punctuation(char c){return c=='{'||c=='}'||c=='('||c==')'||c==','||c==';';}
struct Token{std::string value;std::size_t line;bool quoted;};
typedef std::vector<Token> Tokens;
Tokens Lex(const std::string& source,const MaterialLimits& limits){
    Tokens out;std::size_t p=0,line=1;
    while(p<source.size()){
        const char c=source[p];Need(c!=0,"NUL in material source");
        if(Space(c)){if(c=='\n')++line;++p;continue;}
        if(c=='/'&&p+1<source.size()&&source[p+1]=='/') {while(p<source.size()&&source[p]!='\n')++p;continue;}
        if(c=='/'&&p+1<source.size()&&source[p+1]=='*') {
            p+=2;bool ended=false;while(p<source.size()){if(source[p]=='\n')++line;if(source[p]=='*'&&p+1<source.size()&&source[p+1]=='/'){p+=2;ended=true;break;}++p;}Need(ended,"Unterminated material block comment");continue;
        }
        Need(out.size()<limits.tokens,"Material token limit exceeded");Token token;token.line=line;token.quoted=c=='"';
        if(token.quoted){
            ++p;bool ended=false;while(p<source.size()){
                char v=source[p++];Need(v!=0,"NUL in material string");
                if(v=='"'){ended=true;break;}if(v=='\n')++line;
                if(v=='\\'&&p<source.size()&&(source[p]=='"'||source[p]=='\\'))v=source[p++];
                Need(token.value.size()<limits.tokenBytes,"Material token byte limit exceeded");token.value+=v;
            }Need(ended,"Unterminated material string");
        }else if(Punctuation(c)){token.value+=c;++p;}
        else while(p<source.size()&&!Space(source[p])&&!Punctuation(source[p])&&source[p]!='"'){
            if(source[p]=='/'&&p+1<source.size()&&(source[p+1]=='/'||source[p+1]=='*'))break;
            Need(source[p]!=0,"NUL in material token");Need(token.value.size()<limits.tokenBytes,"Material token byte limit exceeded");token.value+=source[p++];
        }
        Need(!token.value.empty()||token.quoted,"Empty material token");out.push_back(token);
    }return out;
}
bool Is(const Tokens& tokens,std::size_t at,const char* value){return at<tokens.size()&&!tokens[at].quoted&&tokens[at].value==value;}
Tokens Balanced(const Tokens& tokens,std::size_t& pos,const char* open,const char* close,const MaterialLimits& limits){
    Need(Is(tokens,pos,open),"Material delimiter missing");++pos;std::size_t level=1;Tokens body;
    while(pos<tokens.size()){
        if(open[0]=='('&&(Is(tokens,pos,"{")||Is(tokens,pos,"}")))throw std::runtime_error("Brace inside material guide argument list");
        if(Is(tokens,pos,open)){Need(level<limits.nesting,"Material nesting limit exceeded");++level;}
        if(Is(tokens,pos,close)&&--level==0){++pos;return body;}
        body.push_back(tokens[pos++]);
    }throw std::runtime_error("Unterminated material declaration");
}
// Braces bound declarations even when an ignored shader expression is malformed.
// Parentheses are validated by Balanced() only for guide signatures/calls;
// unsupported alpha/vector expressions do not determine diffuse-path semantics.
void CheckNesting(const Tokens& tokens,const MaterialLimits& limits){
    std::size_t level=0;
    for(std::size_t i=0;i<tokens.size();++i){if(tokens[i].quoted)continue;const std::string& v=tokens[i].value;
        if(v=="{"){Need(level<limits.nesting,"Material nesting limit exceeded");++level;}
        else if(v=="}"){if(!level)throw std::runtime_error("Material brace mismatch at line "+std::to_string(tokens[i].line));--level;}
    }Need(level==0,"Unterminated material declaration");
}
bool Atom(const Token& token){return token.quoted||!(token.value=="{"||token.value=="}"||token.value=="("||token.value==")"||token.value==","||token.value==";");}
std::vector<std::string> Atoms(const Tokens& tokens,bool& simple){
    std::vector<std::string> result;simple=true;
    for(std::size_t i=0;i<tokens.size();++i){if(Is(tokens,i,","))continue;if(!Atom(tokens[i])){simple=false;continue;}result.push_back(tokens[i].value);}return result;
}
std::vector<std::string> Parameters(const Tokens& tokens,bool& simple){
    std::vector<std::string> names;simple=true;Tokens segment;
    for(std::size_t i=0;i<=tokens.size();++i){
        if(i==tokens.size()||Is(tokens,i,",")){
            if(segment.empty()){if(i!=tokens.size()||!names.empty())simple=false;}
            else if(segment.size()==1&&Atom(segment[0]))names.push_back(segment[0].value);
            else if(segment.size()==2&&Lower(segment[0].value)=="texture"&&Atom(segment[1]))names.push_back(segment[1].value);
            else simple=false;
            segment.clear();
        }else segment.push_back(tokens[i]);
    }return names;
}
struct Guide{std::vector<std::string> params;Tokens body;bool simple;bool inlineOnly=false;};
struct Material{std::string guide,error;std::vector<std::string> args;Tokens body;};
bool Identifier(char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
std::string Substitute(const std::string& expression,const std::map<std::string,std::string>& values,std::size_t maximum=4096){
    std::string result;std::size_t pos=0;
    while(pos<expression.size()){
        bool replaced=false;
        for(std::map<std::string,std::string>::const_iterator it=values.begin();it!=values.end();++it){
            const std::size_t n=it->first.size();
            if(pos+n>expression.size()||Lower(expression.substr(pos,n))!=it->first)continue;
            const bool left=pos==0||!Identifier(expression[pos-1]);
            const bool right=pos+n==expression.size()||expression[pos+n]=='_'||!Identifier(expression[pos+n]);
            if(left&&right){Need(result.size()<=maximum&&it->second.size()<=maximum-result.size(),"Expanded material token byte limit exceeded");result+=it->second;pos+=n;replaced=true;break;}
        }
        if(!replaced){Need(result.size()<maximum,"Expanded material token byte limit exceeded");result+=expression[pos++];}
    }return result;
}
bool KnownCommand(const std::string& token){
    const std::string t=Lower(token);return t=="image"||t=="uvscale"||t=="uvscale2"||t=="technique"||t=="materialtype"||t=="editorimage"||t=="vector"||t=="vertexcolor"||t=="noshadows"||t=="alwaysinside"||t=="alwaysoutside"||t=="alphatest"||t=="twosided"||t=="noclipmodel"||t=="inlineguide"||t=="polygonoffset"||t=="contents"||t=="translucent"||t=="blend"||t=="blendfunc"||t=="alphablend"||t=="notfix"||t=="nolighting"||t=="nooverlays"||t=="nofragment";
}
bool Boundary(const Tokens& body,std::size_t pos,std::size_t line){return pos>=body.size()||body[pos].line>line||Is(body,pos,";")||Is(body,pos,"}")||(!body[pos].quoted&&KnownCommand(body[pos].value));}
std::size_t NextLine(const Tokens& body,std::size_t pos,const MaterialLimits& limits){
    const std::size_t line=body[pos].line,begin=pos;
    while(pos<body.size()&&body[pos].line==line&&!Is(body,pos,";")){
        if(pos>begin&&!body[pos].quoted&&KnownCommand(body[pos].value))break;
        if(Is(body,pos,"{"))Balanced(body,pos,"{","}",limits);
        else if(Is(body,pos,"("))Balanced(body,pos,"(",")",limits);
        else ++pos;
    }if(Is(body,pos,";"))++pos;return pos;
}
bool Path(const std::string& expression,std::string& normalized){
    if(expression.empty()||expression.find_first_of(" \t\r\n()+*%,;{}[]$=\"")!=std::string::npos)return false;
    try{normalized=MaterialCatalog::NormalizeVirtualPath(expression);return !normalized.empty();}catch(const std::exception&){return false;}
}
bool Scalar(const std::string& value,float& result){char* end=NULL;errno=0;const double v=std::strtod(value.c_str(),&end);if(end==value.c_str()||*end||errno||!std::isfinite(v)||std::abs(v)>1000000)return false;result=static_cast<float>(v);return true;}
struct ExpansionBudget{std::size_t tokens=0,bytes=0;};
Tokens ExpandInline(const Tokens& body,const std::map<std::string,std::string>& args,const std::map<std::string,Guide>& guides,const MaterialLimits& limits,ExpansionBudget& budget,std::vector<std::string>& stack){
    Tokens output;std::size_t line=1,previous=(std::numeric_limits<std::size_t>::max)();
    for(std::size_t i=0;i<body.size();){
        if(!body[i].quoted&&Lower(body[i].value)=="inlineguide"){
            Need(i+2<body.size()&&Atom(body[i+1]),"Truncated inlineGuide invocation");const std::string name=Lower(body[i+1].value);std::size_t pos=i+2;bool simple=false;const auto actual=Atoms(Balanced(body,pos,"(",")",limits),simple);
            const auto guide=guides.find(name);Need(guide!=guides.end()&&guide->second.inlineOnly,"Inline guide definition not found or wrong guide kind");Need(simple&&guide->second.simple&&actual.size()==guide->second.params.size(),"Inline guide parameter/argument mismatch");Need(stack.size()<limits.nesting&&std::find(stack.begin(),stack.end(),name)==stack.end(),"Inline guide expansion cycle/nesting limit");
            std::map<std::string,std::string> values;for(std::size_t p=0;p<actual.size();++p)values[guide->second.params[p]]=Substitute(actual[p],args,limits.tokenBytes);
            stack.push_back(name);Tokens expanded=ExpandInline(guide->second.body,values,guides,limits,budget,stack);stack.pop_back();std::size_t childLine=(std::numeric_limits<std::size_t>::max)();
            for(auto& token:expanded){if(token.line!=childLine){childLine=token.line;++line;}token.line=line;output.push_back(std::move(token));}previous=(std::numeric_limits<std::size_t>::max)();i=pos;continue;
        }
        Token token=body[i++];if(token.line!=previous){previous=token.line;++line;}token.line=line;token.value=Substitute(token.value,args,limits.tokenBytes);Need(budget.tokens<limits.tokens,"Expanded material token count limit exceeded");++budget.tokens;Need(budget.bytes<=limits.sourceBytes&&token.value.size()<=limits.sourceBytes-budget.bytes,"Expanded material source byte limit exceeded");budget.bytes+=token.value.size();output.push_back(std::move(token));
    }return output;
}
MaterialResult ResolveBody(const std::string& name,const Tokens& body,const std::map<std::string,std::string>& args,const MaterialLimits& limits){
    MaterialResult result;result.material=name;bool diffuseBad=false,uvBad=false,colorBad=false,alphaBad=false,compositionBad=false,offset=false,nonsolid=false;std::string note;
    for(std::size_t i=0;i<body.size();){
        if(Is(body,i,";")){++i;continue;}
        if(Is(body,i,"{")){Balanced(body,i,"{","}",limits);result.partial=true;compositionBad=true;note="unsupported nested shader block";continue;}
        const std::string command=Lower(body[i].value);
        if(command=="image"){
            const std::size_t begin=i,line=body[i].line;bool supported=i+2<body.size()&&Atom(body[i+1])&&Atom(body[i+2]);
            const std::string semantic=i+1<body.size()?Lower(body[i+1].value):"";std::string path;
            if(supported)supported=Boundary(body,i+3,body[i+2].line)&&Path(Substitute(body[i+2].value,args,limits.tokenBytes),path);
            std::string* target=semantic=="diffusemap"?&result.diffusePath:semantic=="normalmap"?&result.normalPath:semantic=="specularmap"?&result.specularPath:semantic=="colormap"?&result.colorPath:NULL;
            if(!target)result.partial=true;
            else if(!supported){if(semantic=="diffusemap")diffuseBad=true;else result.partial=true;if(semantic=="colormap")colorBad=true;note="unsupported image expression";}
            else if(!target->empty()&&*target!=path){if(semantic=="diffusemap")diffuseBad=true;else result.partial=true;if(semantic=="colormap")colorBad=true;note="multiple different image bindings";target->clear();}
            else *target=path;
            i=supported?i+3:NextLine(body,begin,limits);(void)line;continue;
        }
        if(command=="uvscale"||command=="uvscale2"){
            const std::size_t begin=i;float u=1,v=1;const bool valid=i+2<body.size()&&Boundary(body,i+3,body[i+2].line)&&Scalar(Substitute(body[i+1].value,args,limits.tokenBytes),u)&&Scalar(Substitute(body[i+2].value,args,limits.tokenBytes),v);
            if(!valid){uvBad=true;note="unsupported UV scale expression";i=NextLine(body,begin,limits);}
            else{if(result.hasUvScale&&(result.uvScale[0]!=u||result.uvScale[1]!=v)){uvBad=true;note="multiple different UV scales";}result.hasUvScale=true;result.uvScale[0]=u;result.uvScale[1]=v;i+=3;}continue;
        }
        if(command=="technique"){
            const std::size_t begin=i;const bool valid=i+1<body.size()&&Atom(body[i+1])&&Boundary(body,i+2,body[i+1].line);
            if(!valid){compositionBad=true;note="unsupported technique expression";i=NextLine(body,begin,limits);}else{if(!result.technique.empty()&&Lower(result.technique)!=Lower(body[i+1].value)){compositionBad=true;note="multiple different techniques";}result.technique=body[i+1].value;i+=2;}continue;
        }
        if(command=="alphatest"){
            const std::size_t begin=i;float value=0;const bool valid=i+1<body.size()&&Atom(body[i+1])&&Boundary(body,i+2,body[i+1].line)&&Scalar(body[i+1].value,value)&&value>=0&&value<=1;
            if(!valid){alphaBad=true;note="unsupported AlphaTest expression";i=NextLine(body,begin,limits);}else{if(result.hasAlphaTest&&result.alphaTest!=value){alphaBad=true;note="multiple different AlphaTest values";}result.hasAlphaTest=true;result.alphaTest=value;i+=2;}continue;
        }
        if(command=="vertexcolor"){result.vertexColor=true;++i;continue;}
        if(command=="vector"){
            if(i+1<body.size()&&Atom(body[i+1])){
                const std::string semantic=Lower(body[i+1].value);
                if(semantic=="colormodulate")result.explicitColorModulate=true;
                if(semantic=="coloradd")result.explicitColorAdd=true;
            }
            i=NextLine(body,i,limits);continue;
        }
        if(command=="polygonoffset"){
            const std::size_t begin=i;float a=0,b=0;const bool valid=i+2<body.size()&&Boundary(body,i+3,body[i+2].line)&&Scalar(body[i+1].value,a)&&Scalar(body[i+2].value,b);
            if(valid){offset=true;i+=3;}else{compositionBad=true;note="unsupported PolygonOffset expression";i=NextLine(body,begin,limits);}continue;
        }
        if(command=="contents"){
            const auto line=body[i].line;++i;while(i<body.size()&&body[i].line==line&&!Is(body,i,";")&&!Is(body,i,"{")&&!Is(body,i,"}")&&(body[i].quoted||!KnownCommand(body[i].value))){if(Lower(body[i].value)=="nonsolid")nonsolid=true;++i;}continue;
        }
        if(command=="translucent"||command=="blend"||command=="blendfunc"||command=="alphablend"){compositionBad=true;note="explicit blending is not implemented";i=NextLine(body,i,limits);continue;}
        // Shader vectors and other clauses are not
        // interpreted. Only top-level simple bindings have a path meaning here.
        i=NextLine(body,i,limits);
    }
    if(diffuseBad)result.diffusePath.clear();
    if(colorBad)result.colorPath.clear();
    result.resolved=!result.diffusePath.empty()&&!diffuseBad&&!uvBad;
    if(!result.resolved)result.reason=!note.empty()?note:"no simple DiffuseMap binding";
    else result.reason=result.partial?"simple diffuse binding resolved; other bindings omitted":"simple material paths resolved";
    const std::string technique=Lower(result.technique);const bool modulate=technique=="ambientdecalmodulate",twice=technique=="ambientdecal2xmodulate";
    if((modulate||twice)&&!compositionBad&&!alphaBad&&!colorBad&&!result.colorPath.empty()&&result.diffusePath.empty()&&offset&&nonsolid&&!result.hasAlphaTest){result.shaderClass=modulate?MaterialShaderClass::AmbientDecalModulate:MaterialShaderClass::AmbientDecal2xModulate;result.partial=true;result.reason="verified ColorMap decal composition; opaque rendering unsupported";}
    else if(compositionBad||alphaBad||colorBad||!result.colorPath.empty()||(!technique.empty()&&technique!="dsaddgbuffer%r_gbuffermode%_detailnormal_depth")){result.shaderClass=MaterialShaderClass::Unsupported;result.partial=true;result.reason=result.resolved?"simple DiffuseMap retained; "+(note.empty()?std::string("explicit shader composition unsupported"):note):(note.empty()?std::string("explicit shader composition unsupported"):note);}
    else if(result.resolved)result.shaderClass=result.hasAlphaTest?MaterialShaderClass::AlphaTestDiffuse:MaterialShaderClass::OpaqueDiffuse;
    else if(result.hasAlphaTest||uvBad||diffuseBad)result.shaderClass=MaterialShaderClass::Unsupported;
    return result;
}
}
struct MaterialCatalog::Impl{
    MaterialLimits limits;std::size_t bytes=0,tokens=0;std::map<std::string,Guide> guides;std::map<std::string,Material> materials;
    explicit Impl(const MaterialLimits& value):limits(value){}
    Tokens Source(const std::string& name,const std::string& text){
        Need(name.size()<=4096,"Material source name limit exceeded");Need(text.size()<=limits.sourceBytes,"Material source byte limit exceeded");
        Need(bytes<=limits.totalSourceBytes&&text.size()<=limits.totalSourceBytes-bytes,"Total material source byte limit exceeded");
        const Tokens lexed=Lex(text,limits);Need(tokens<=limits.tokens&&lexed.size()<=limits.tokens-tokens,"Total material token limit exceeded");CheckNesting(lexed,limits);return lexed;
    }
};
MaterialCatalog::MaterialCatalog(const MaterialLimits& limits):impl_(new Impl(limits)){}
MaterialCatalog::~MaterialCatalog(){}
std::string MaterialCatalog::NormalizeVirtualPath(const std::string& value){
    Need(!value.empty()&&value.size()<=4096,"Virtual material/texture name is empty or too long");
    Need(value[0]!='/'&&value[0]!='\\'&&value.find(':')==std::string::npos&&value.find('\0')==std::string::npos,"Absolute/drive-qualified virtual path is unsupported");
    std::string path=Lower(value);for(std::size_t i=0;i<path.size();++i)if(path[i]=='\\')path[i]='/';
    std::string result,part;for(std::size_t i=0;i<=path.size();++i){if(i==path.size()||path[i]=='/'){
        Need(part!="..","Parent traversal in virtual path");if(!part.empty()&&part!="."){if(!result.empty())result+='/';result+=part;}part.clear();
    }else{Need(static_cast<unsigned char>(path[i])>=32,"Control character in virtual path");part+=path[i];}}
    if(result.size()>=4){const std::string ext=result.substr(result.size()-4);if(ext==".dds"||ext==".tga")result.resize(result.size()-4);}Need(!result.empty(),"Empty normalized virtual path");return result;
}
void MaterialCatalog::AddGuideSource(const std::string& sourceName,const std::string& text){
    const Tokens tokens=impl_->Source(sourceName,text);std::map<std::string,Guide> added;
    for(std::size_t i=0;i<tokens.size();){
        const std::string command=Lower(tokens[i].value);if(command!="guide"&&command!="inlineguide"){if(Is(tokens,i,"{"))Balanced(tokens,i,"{","}",impl_->limits);else i=NextLine(tokens,i,impl_->limits);continue;}
        ++i;Need(i<tokens.size(),"Guide name missing");const std::string name=Lower(tokens[i++].value);bool simple=false;
        Tokens params=Balanced(tokens,i,"(",")",impl_->limits);Guide guide;guide.inlineOnly=command=="inlineguide";guide.params=Parameters(params,simple);guide.simple=simple;
        std::map<std::string,bool> seen;for(std::size_t p=0;p<guide.params.size();++p){guide.params[p]=Lower(guide.params[p]);if(seen[guide.params[p]])guide.simple=false;seen[guide.params[p]]=true;}
        while(i<tokens.size()&&!Is(tokens,i,"{")){Need(tokens[i].line<=tokens[i-1].line+1,"Guide body missing");++i;}
        guide.body=Balanced(tokens,i,"{","}",impl_->limits);added[name]=guide;
    }
    std::size_t newNames=0;for(std::map<std::string,Guide>::const_iterator i=added.begin();i!=added.end();++i)if(!impl_->guides.count(i->first))++newNames;
    Need(impl_->guides.size()<=impl_->limits.guides&&newNames<=impl_->limits.guides-impl_->guides.size(),"Material guide count limit exceeded");
    for(std::map<std::string,Guide>::const_iterator i=added.begin();i!=added.end();++i)impl_->guides[i->first]=i->second;impl_->bytes+=text.size();impl_->tokens+=tokens.size();
}
void MaterialCatalog::AddMaterialSource(const std::string& sourceName,const std::string& text){
    const Tokens tokens=impl_->Source(sourceName,text);std::map<std::string,Material> added;
    for(std::size_t i=0;i<tokens.size();){
        if(Is(tokens,i,";")){++i;continue;}Material material;std::string name;
        if(Lower(tokens[i].value)=="guide"){
            ++i;Need(i+1<tokens.size(),"Guided material declaration truncated");name=NormalizeVirtualPath(tokens[i++].value);material.guide=Lower(tokens[i++].value);
            bool simple=false;material.args=Atoms(Balanced(tokens,i,"(",")",impl_->limits),simple);if(!simple)material.error="complex guide argument expression";
            if(Is(tokens,i,"{")){material.body=Balanced(tokens,i,"{","}",impl_->limits);material.error="guided material override body is unsupported";}
        }else{
            name=NormalizeVirtualPath(tokens[i++].value);if(!Is(tokens,i,"{")){if(i<tokens.size())i=NextLine(tokens,i,impl_->limits);continue;}material.body=Balanced(tokens,i,"{","}",impl_->limits);
        }
        added[name]=material;
    }
    std::size_t newNames=0;for(std::map<std::string,Material>::const_iterator i=added.begin();i!=added.end();++i)if(!impl_->materials.count(i->first))++newNames;
    Need(impl_->materials.size()<=impl_->limits.materials&&newNames<=impl_->limits.materials-impl_->materials.size(),"Material definition count limit exceeded");
    for(std::map<std::string,Material>::const_iterator i=added.begin();i!=added.end();++i)impl_->materials[i->first]=i->second;impl_->bytes+=text.size();impl_->tokens+=tokens.size();
}
MaterialResult MaterialCatalog::Resolve(const std::string& materialName)const{
    MaterialResult result;try{result.material=NormalizeVirtualPath(materialName);}catch(const std::exception& e){result.reason=e.what();return result;}
    const std::map<std::string,Material>::const_iterator found=impl_->materials.find(result.material);if(found==impl_->materials.end()){result.reason="material declaration not found";return result;}
    const Material& material=found->second;if(!material.error.empty()){result.reason=material.error;result.shaderClass=MaterialShaderClass::Unsupported;return result;}
    try{
        std::map<std::string,std::string> args;const Tokens* body=&material.body;
        if(!material.guide.empty()){
            const auto guide=impl_->guides.find(material.guide);Need(guide!=impl_->guides.end()&&!guide->second.inlineOnly,"Guide definition not found or wrong guide kind");Need(guide->second.simple&&guide->second.params.size()==material.args.size(),"Guide parameter/argument mismatch");
            for(std::size_t i=0;i<material.args.size();++i)args[guide->second.params[i]]=material.args[i];body=&guide->second.body;
        }
        ExpansionBudget budget;std::vector<std::string> stack;const Tokens expanded=ExpandInline(*body,args,impl_->guides,impl_->limits,budget,stack);return ResolveBody(result.material,expanded,std::map<std::string,std::string>(),impl_->limits);
    }catch(const std::bad_alloc&){throw;}catch(const std::exception& e){result.shaderClass=MaterialShaderClass::Unsupported;result.reason=e.what();return result;}
}
std::size_t MaterialCatalog::GuideCount()const{return impl_->guides.size();}
std::size_t MaterialCatalog::MaterialCount()const{return impl_->materials.size();}
}
