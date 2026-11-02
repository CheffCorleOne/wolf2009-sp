// SPDX-License-Identifier: GPL-3.0-or-later
#include "Material.h"
#include <iostream>
#include <stdexcept>
namespace wolf2009 {
namespace{
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void Reject(F fn){bool bad=false;try{fn();}catch(const std::exception&){bad=true;}Need(bad,"Malformed material source was accepted");}
}
int RunMaterialTests(){int passed=0;try{
    MaterialCatalog catalog;catalog.AddGuideSource("synthetic.guide","// no original game source\nguide sampled(Base, Type, U, V) editor {\nImage DiffuseMap Base_d\nImage NormalMap Base_local\nImage SpecularMap Base_s\nUVScale U V\n}\n");
    catalog.AddMaterialSource("synthetic.mtr","guide Textures\\Synthetic sampled(\"Textures\\Sample\",\"wood\",2,3)\n");
    MaterialResult r=catalog.Resolve("TEXTURES/SYNTHETIC");Need(r.resolved&&r.diffusePath=="textures/sample_d"&&r.normalPath=="textures/sample_local"&&r.specularPath=="textures/sample_s"&&r.hasUvScale&&r.uvScale[0]==2&&r.uvScale[1]==3,"Guide path/UV expansion failed");++passed;
    catalog.AddMaterialSource("missing-comma.mtr","guide textures/missingcomma sampled(\"textures/base\" \"stone\"2,4)\n");r=catalog.Resolve("textures/missingcomma");Need(r.resolved&&r.uvScale[0]==2&&r.uvScale[1]==4,"Missing argument comma compatibility failed");++passed;
    catalog.AddMaterialSource("direct.mtr","textures/direct { Image DiffuseMap \"Textures\\TGA.TGA\"; UVScale 4 5; }\n");r=catalog.Resolve("Textures\\Direct.dds");Need(r.resolved&&r.diffusePath=="textures/tga"&&r.uvScale[1]==5,"Direct material binding failed");++passed;
    catalog.AddGuideSource("prefix.guide","guide prefix(Name) {\nImage DiffuseMap folder/Name_d.dds\n}\n");catalog.AddMaterialSource("prefix.mtr","guide textures/prefix prefix(\"asset\")\n");r=catalog.Resolve("textures/prefix");Need(r.resolved&&r.diffusePath=="folder/asset_d","Guide parameter prefix/suffix failed");++passed;
    catalog.AddGuideSource("typed.guide","guide typed(texture Name, Scale) {\nImage DiffuseMap Name_d\nUVScale Scale Scale\n}\n");catalog.AddMaterialSource("typed.mtr","guide textures/typed typed(\"textures/typed\",2)\n");r=catalog.Resolve("textures/typed");Need(r.resolved&&r.diffusePath=="textures/typed_d"&&r.uvScale[0]==2,"Typed texture parameter expansion failed");++passed;
    catalog.AddMaterialSource("comments.mtr","/* block\n comment */ textures/comments\n{ // texture\nImage DiffuseMap textures/comment /* ignored */\n}\n");r=catalog.Resolve("textures/comments");Need(r.resolved&&r.diffusePath=="textures/comment","Material comments failed");++passed;
    catalog.AddMaterialSource("complex.mtr","textures/complex {\nImage DiffuseMap add(textures/one,textures/two)\n}\n");r=catalog.Resolve("textures/complex");Need(!r.resolved&&r.diffusePath.empty(),"Complex image expression guessed a path");++passed;
    catalog.AddMaterialSource("uv.mtr","textures/complexuv {\nImage DiffuseMap textures/plain\nUVScale time*2 1\n}\n");Need(!catalog.Resolve("textures/complexuv").resolved,"Complex UV expression was accepted");++passed;
    catalog.AddMaterialSource("partial.mtr","textures/partial {\nImage DiffuseMap textures/plain\nImage NormalMap add(foo,bar)\n}\n");r=catalog.Resolve("textures/partial");Need(r.resolved&&r.partial&&r.normalPath.empty(),"Partial normal binding status failed");++passed;
    catalog.AddMaterialSource("duplicate.mtr","textures/different {\nImage DiffuseMap textures/one\nImage DiffuseMap textures/two\n}\n");Need(!catalog.Resolve("textures/different").resolved,"Ambiguous diffuse binding accepted");++passed;
    catalog.AddMaterialSource("unknown.mtr","guide textures/unknown missing(\"textures/x\")\n");Need(!catalog.Resolve("textures/unknown").resolved&&!catalog.Resolve("does/not/exist").resolved,"Unknown declaration guessed a path");++passed;
    Need(MaterialCatalog::NormalizeVirtualPath("Textures\\Folder//./Image.DDS")=="textures/folder/image","Virtual normalization failed");Reject([]{MaterialCatalog::NormalizeVirtualPath("../escape");});Reject([]{MaterialCatalog::NormalizeVirtualPath("C:\\outside");});Reject([]{MaterialCatalog::NormalizeVirtualPath("/absolute");});++passed;
    const std::size_t before=catalog.MaterialCount();Reject([&]{catalog.AddMaterialSource("broken.mtr","textures/broken { Image DiffuseMap \"unterminated\n");});Need(catalog.MaterialCount()==before,"Failed source partially changed catalog");Reject([&]{catalog.AddGuideSource("bad.guide","guide b(A { } ");});++passed;
    catalog.AddMaterialSource("unknown-expression.mtr","textures/known {\nImage DiffuseMap textures/known_d\nalphaTest { unknown::Parm[4]) }\n}\n");Need(catalog.Resolve("textures/known").resolved,"Ignored shader expression discarded known diffuse binding");++passed;
    MaterialLimits limit;limit.sourceBytes=5;MaterialCatalog shortSource(limit);Reject([&]{shortSource.AddMaterialSource("long","textures/x { } ");});++passed;
    limit=MaterialLimits();limit.tokens=3;MaterialCatalog shortTokens(limit);Reject([&]{shortTokens.AddMaterialSource("long","textures/x { Image DiffuseMap a }");});++passed;
    limit=MaterialLimits();limit.materials=1;MaterialCatalog one(limit);one.AddMaterialSource("one","x { Image DiffuseMap a }");Reject([&]{one.AddMaterialSource("two","y { Image DiffuseMap b }");});Need(one.MaterialCount()==1,"Definition count limit changed catalog");++passed;
    std::cout<<"Material self-test: "<<passed<<" passed, 0 failed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Material self-test failed after "<<passed<<" checks: "<<e.what()<<'\n';return 1;}}
}
