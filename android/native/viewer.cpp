// SPDX-License-Identifier: GPL-3.0-or-later
// Thin Android GLES3 adapter. The resource/geometry core has no Android calls.
#include "Archive.h"
#include "Geometry.h"
#include "TextureBinding.h"
#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {
struct Vec { float x,y,z; Vec(float a=0,float b=0,float c=0):x(a),y(b),z(c){} };
Vec operator+(Vec a,Vec b){return Vec(a.x+b.x,a.y+b.y,a.z+b.z);}
Vec operator-(Vec a,Vec b){return Vec(a.x-b.x,a.y-b.y,a.z-b.z);}
Vec operator*(Vec a,float k){return Vec(a.x*k,a.y*k,a.z*k);}
float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec Cross(Vec a,Vec b){return Vec(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
Vec Unit(Vec a){const float length=std::sqrt(Dot(a,a));return length>1e-20f?a*(1/length):Vec(0,0,1);}
struct Vertex {float x,y,z,u,v;};
struct Batch {std::uint32_t first,count;Vec color;int texture;float uvX,uvY;Batch(std::uint32_t f,std::uint32_t n,Vec c):first(f),count(n),color(c),texture(-1),uvX(1),uvY(1){}};
struct Mesh {std::vector<Vertex> vertices;std::vector<std::uint32_t> indices;std::vector<Batch> batches;wolf2009android::TexturePlan textures;Vec center;float radius=1;bool demo=false;};
std::mutex pendingMutex;
std::mutex materialMutex;
std::shared_ptr<const wolf2009::MaterialCatalog> materialCatalog;
std::shared_ptr<Mesh> pendingMesh,currentMesh;
std::uint64_t revision=0,uploadedRevision=0;
GLuint program=0,vao=0,vbo=0,ibo=0;
GLint mvpUniform=-1,colorUniform=-1,useTextureUniform=-1,uvScaleUniform=-1;
std::vector<GLuint> textureNames;
std::size_t gpuReadyBindings=0,gpuDdsTextures=0,gpuSolidTextures=0,gpuFailures=0;
bool renderStatusPending=false;
Vec cameraTarget,freeEye;
float yaw=.78539816f,pitch=.61086524f,distance=4,viewAspect=1;
bool flight=false;
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
GLenum TakeGlErrors(){GLenum first=GL_NO_ERROR;for(unsigned i=0;i<16;i++){const GLenum error=glGetError();if(error==GL_NO_ERROR)break;if(first==GL_NO_ERROR)first=error;}return first;}
std::string Key(std::string key){for(char& c:key){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c=char(c+32);}return key;}
Vec Color(const std::string& name){std::uint32_t h=2166136261u;for(unsigned char c:name){h^=c;h*=16777619u;}return Vec(.3f+float(h&255)/480,.3f+float((h>>8)&255)/480,.3f+float((h>>16)&255)/480);}
void Bounds(Mesh& mesh){
    Require(!mesh.vertices.empty()&&!mesh.indices.empty(),"No static world triangles in this MPK");
    Vec low(mesh.vertices[0].x,mesh.vertices[0].y,mesh.vertices[0].z),high=low;
    for(const Vertex& v:mesh.vertices){low.x=std::min(low.x,v.x);low.y=std::min(low.y,v.y);low.z=std::min(low.z,v.z);high.x=std::max(high.x,v.x);high.y=std::max(high.y,v.y);high.z=std::max(high.z,v.z);}
    mesh.center=(low+high)*.5f;mesh.radius=std::max(.01f,std::sqrt(Dot(high-low,high-low))*.5f);
}
std::shared_ptr<Mesh> World(const wolf2009::GeometryScene& scene,wolf2009android::TexturePlan textures){
    std::shared_ptr<Mesh> mesh(new Mesh);
    mesh->textures=std::move(textures);
    const std::uint32_t missing=(std::numeric_limits<std::uint32_t>::max)();
    std::vector<std::uint32_t> remap(scene.vertices.size(),missing);
    for(std::size_t surfaceIndex=0;surfaceIndex<scene.surfaces.size();surfaceIndex++){
        const wolf2009::GeometrySurface& surface=scene.surfaces[surfaceIndex];
        if(surface.model.compare(0,5,"_area")!=0||surface.indices.empty())continue;
        Batch batch={std::uint32_t(mesh->indices.size()),std::uint32_t(surface.indices.size()),Color(surface.material)};
        const wolf2009android::SurfaceBinding& binding=mesh->textures.surfaces[surfaceIndex];
        batch.texture=binding.texture;batch.uvX=binding.uvScale[0];batch.uvY=binding.uvScale[1];
        for(std::uint32_t index:surface.indices){
            Require(index<scene.vertices.size(),"Internal geometry index out of range");
            if(remap[index]==missing){remap[index]=std::uint32_t(mesh->vertices.size());const wolf2009::GeometryVertex& v=scene.vertices[index];mesh->vertices.push_back(Vertex{v.x,v.y,v.z,v.u,v.v});}
            mesh->indices.push_back(remap[index]);
        }
        mesh->batches.push_back(batch);
    }
    Bounds(*mesh);return mesh;
}
void Publish(const std::shared_ptr<Mesh>& mesh){std::lock_guard<std::mutex> guard(pendingMutex);pendingMesh=mesh;++revision;}
Vec Away(){return Vec(std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch));}
void Reset(){if(!currentMesh)return;cameraTarget=currentMesh->center;yaw=.78539816f;pitch=.61086524f;const float halfFov=std::min(.55f,std::atan(std::tan(.55f)*viewAspect));distance=currentMesh->radius*std::max(3.1f,1.1f/std::sin(halfFov));freeEye=cameraTarget+Away()*distance;}
GLuint Shader(GLenum type,const char* text){
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&text,NULL);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char message[2048];glGetShaderInfoLog(shader,sizeof(message),NULL,message);glDeleteShader(shader);throw std::runtime_error(std::string("Shader: ")+message);}return shader;
}
void Initialize(){
    // Context loss invalidates all old object names, so recreate and re-upload.
    program=vao=vbo=ibo=0;uploadedRevision=0;textureNames.clear();renderStatusPending=false;
    const char* vertex="#version 300 es\nprecision highp float;layout(location=0)in vec3 position;layout(location=1)in vec2 uv;uniform mat4 mvp;out highp vec3 world;out highp vec2 textureUv;void main(){world=position;textureUv=uv;gl_Position=mvp*vec4(position,1.0);}";
    const char* fragment="#version 300 es\nprecision highp float;in highp vec3 world;in highp vec2 textureUv;uniform vec3 color;uniform bool useTexture;uniform sampler2D diffuse;uniform vec2 uvScale;out vec4 result;void main(){vec3 n=normalize(cross(dFdx(world),dFdy(world)));float light=0.28+0.72*abs(dot(n,normalize(vec3(0.3,-0.5,0.8))));vec3 base=useTexture?texture(diffuse,textureUv*uvScale).rgb:color;result=vec4(base*light,1.0);}";
    const GLuint vs=Shader(GL_VERTEX_SHADER,vertex),fs=Shader(GL_FRAGMENT_SHADER,fragment);program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);Require(ok==GL_TRUE,"GLES3 program link failed");
    mvpUniform=glGetUniformLocation(program,"mvp");colorUniform=glGetUniformLocation(program,"color");useTextureUniform=glGetUniformLocation(program,"useTexture");uvScaleUniform=glGetUniformLocation(program,"uvScale");
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"diffuse"),0);
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glGenBuffers(1,&ibo);glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glClearColor(.035f,.055f,.075f,1);
}
void UploadTextures(){
    if(!textureNames.empty())glDeleteTextures(GLsizei(textureNames.size()),textureNames.data());
    textureNames.assign(currentMesh->textures.images.size(),0);gpuReadyBindings=gpuDdsTextures=gpuSolidTextures=gpuFailures=0;
    GLint maximum=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);glActiveTexture(GL_TEXTURE0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(std::size_t i=0;i<currentMesh->textures.images.size();i++){
        const wolf2009android::TextureImage& image=currentMesh->textures.images[i];
        if(image.width>std::uint32_t(maximum)||image.height>std::uint32_t(maximum)){++gpuFailures;continue;}
        GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,GLsizei(image.width),GLsizei(image.height),0,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data());glGenerateMipmap(GL_TEXTURE_2D);
        if(TakeGlErrors()!=GL_NO_ERROR){if(texture)glDeleteTextures(1,&texture);TakeGlErrors();++gpuFailures;continue;}
        textureNames[i]=texture;if(image.generated)++gpuSolidTextures;else ++gpuDdsTextures;
    }
    for(const Batch& batch:currentMesh->batches)if(batch.texture>=0&&std::size_t(batch.texture)<textureNames.size()&&textureNames[batch.texture])++gpuReadyBindings;
}
void Upload(){
    std::shared_ptr<Mesh> next;std::uint64_t nextRevision;
    {std::lock_guard<std::mutex> guard(pendingMutex);next=pendingMesh;nextRevision=revision;}
    if(!next||nextRevision==uploadedRevision)return;
    currentMesh=next;
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(next->vertices.size()*sizeof(Vertex)),next->vertices.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(next->indices.size()*sizeof(std::uint32_t)),next->indices.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,x)));
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,u)));
    Require(TakeGlErrors()==GL_NO_ERROR,"GPU could not allocate scene buffers");UploadTextures();Reset();uploadedRevision=nextRevision;renderStatusPending=true;
}
void Multiply(float* out,const float* a,const float* b){for(unsigned c=0;c<4;c++)for(unsigned r=0;r<4;r++){out[c*4+r]=0;for(unsigned k=0;k<4;k++)out[c*4+r]+=a[k*4+r]*b[c*4+k];}}
void Draw(int width,int height){
    if(!program)return;viewAspect=float(std::max(1,width))/float(std::max(1,height));glViewport(0,0,std::max(1,width),std::max(1,height));glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);Upload();if(!currentMesh)return;
    const Vec eye=flight?freeEye:cameraTarget+Away()*distance;
    const Vec forward=Unit(Away()*-1),right=Unit(Cross(forward,Vec(0,0,1))),up=Cross(right,forward);
    const float view[16]={right.x,up.x,-forward.x,0,right.y,up.y,-forward.y,0,right.z,up.z,-forward.z,0,-Dot(right,eye),-Dot(up,eye),Dot(forward,eye),1};
    const float nearPlane=std::max(.005f,currentMesh->radius*.0001f),farPlane=std::max(100.f,currentMesh->radius*100.f);
    const float scale=1/std::tan(.55f),aspect=float(std::max(1,width))/float(std::max(1,height));
    const float projection[16]={scale/aspect,0,0,0,0,scale,0,0,0,0,-(farPlane+nearPlane)/(farPlane-nearPlane),-1,0,0,-2*farPlane*nearPlane/(farPlane-nearPlane),0};
    float mvp[16];Multiply(mvp,projection,view);glUseProgram(program);glUniformMatrix4fv(mvpUniform,1,GL_FALSE,mvp);glBindVertexArray(vao);
    for(const Batch& batch:currentMesh->batches){const GLuint texture=batch.texture>=0&&std::size_t(batch.texture)<textureNames.size()?textureNames[batch.texture]:0;glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(useTextureUniform,texture?1:0);glUniform2f(uvScaleUniform,batch.uvX,batch.uvY);glUniform3f(colorUniform,batch.color.x,batch.color.y,batch.color.z);glDrawElements(GL_TRIANGLES,GLsizei(batch.count),GL_UNSIGNED_INT,reinterpret_cast<const void*>(std::size_t(batch.first)*sizeof(std::uint32_t)));}
    Require(glGetError()==GL_NO_ERROR,"GLES3 draw failed");
}
std::string Load(const char* path){
    std::ifstream input(path,std::ios::binary);Require(bool(input),"Cannot read private MPK copy");
    wolf2009::Limits archiveLimits;archiveLimits.inflatedChunkBytes=128u*1024u*1024u;
    const wolf2009::Archive archive=wolf2009::Archive::Scan(input,archiveLimits);
    struct Location{std::size_t chunk,entry;};std::map<std::string,Location> buffers,textures;Location proc={0,0};std::string name;unsigned procs=0;
    for(std::size_t c=0;c<archive.Chunks().size();c++){
        const wolf2009::Chunk& chunk=archive.Chunks()[c];if(chunk.type!="PROC"&&chunk.type!="PBBF"&&chunk.type!="TXTR")continue;
        for(std::size_t e=0;e<chunk.entries.size();e++){
            Location here={c,e};if(chunk.type=="PROC"){procs++;proc=here;name=chunk.entries[e].name;}
            else if(chunk.type=="PBBF")Require(buffers.insert(std::make_pair(Key(chunk.entries[e].name),here)).second,"Duplicate PBBF name");
            else Require(textures.insert(std::make_pair(wolf2009::MaterialCatalog::NormalizeVirtualPath(chunk.entries[e].name),here)).second,"Duplicate TXTR texture name");
        }
    }
    Require(procs==1,"Select one map MPK with exactly one PROC resource");
    wolf2009::GeometryLimits limits;limits.vertices=2000000;limits.triangles=2000000;limits.totalBufferBytes=256u*1024u*1024u;
    const wolf2009::GeometryScene scene=wolf2009::DecodeProc7(archive.ReadEntry(input,proc.chunk,proc.entry),name,[&](const std::string& key){
        const auto it=buffers.find(Key(key));Require(it!=buffers.end(),"PBBF render buffer not found in selected MPK");return archive.ReadEntry(input,it->second.chunk,it->second.entry);
    },limits);
    std::shared_ptr<const wolf2009::MaterialCatalog> catalog;{std::lock_guard<std::mutex> guard(materialMutex);catalog=materialCatalog;}
    wolf2009android::TexturePlan plan=wolf2009android::BindTextures(scene,catalog.get(),[&](const std::string& key){
        const auto it=textures.find(key);if(it==textures.end())return std::vector<unsigned char>();return archive.ReadEntry(input,it->second.chunk,it->second.entry);
    });
    const std::shared_ptr<Mesh> mesh=World(scene,std::move(plan));Publish(mesh);
    std::ostringstream message;message<<mesh->vertices.size()<<" vertices | "<<mesh->indices.size()/3<<" triangles | "<<mesh->batches.size()<<" world surfaces";return message.str();
}
std::string Demo(){
    std::shared_ptr<Mesh> mesh(new Mesh);mesh->demo=true;
    const float points[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    for(const auto& p:points)mesh->vertices.push_back(Vertex{p[0],p[1],p[2],0,0});
    const std::uint32_t indices[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7};
    mesh->indices.assign(indices,indices+36);for(unsigned i=0;i<6;i++){Batch batch={i*6,6,Color(std::to_string(i))};mesh->batches.push_back(batch);}Bounds(*mesh);Publish(mesh);return "Synthetic cube | 8 vertices | 12 triangles | no game assets";
}
jstring Message(JNIEnv* env,const std::string& text){return env->NewStringUTF(text.c_str());}
std::string RenderStatus(){
    if(!currentMesh)return "";
    if(currentMesh->demo)return "Synthetic cube | 8 vertices | 12 triangles | no game assets";
    const wolf2009android::TexturePlan& plan=currentMesh->textures;std::ostringstream out;
    out<<currentMesh->vertices.size()<<" vertices | "<<currentMesh->indices.size()/3<<" triangles | "<<currentMesh->batches.size()<<" world surfaces\n";
    out<<"Diffuse "<<gpuReadyBindings<<"/"<<plan.worldBindings<<" | DDS "<<gpuDdsTextures<<" | solid "<<gpuSolidTextures<<" | flat "<<(plan.worldBindings-gpuReadyBindings);
    if(plan.partialBindings)out<<" | partial "<<plan.partialBindings;
    if(plan.worldBindings>plan.resolvedBindings)out<<" | unresolved "<<(plan.worldBindings-plan.resolvedBindings);
    if(plan.missingBindings)out<<" | missing "<<plan.missingBindings;
    if(plan.decodeFallbacks)out<<" | DDS errors "<<plan.decodeFallbacks;
    if(plan.budgetFallbacks)out<<" | budget "<<plan.budgetFallbacks;
    if(gpuFailures)out<<" | GPU fallback "<<gpuFailures;
    return out.str();
}
}

extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_initialize(JNIEnv* env,jclass){try{Initialize();return Message(env,"");}catch(const std::exception& e){__android_log_print(ANDROID_LOG_ERROR,"Wolf2009","%s",e.what());return Message(env,e.what());}}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_loadMpk(JNIEnv* env,jclass,jstring path){
    const char* chars=env->GetStringUTFChars(path,NULL);if(!chars)return NULL;
    try{const std::string status=Load(chars);env->ReleaseStringUTFChars(path,chars);return Message(env,"OK|"+status);}
    catch(const std::exception& e){env->ReleaseStringUTFChars(path,chars);return Message(env,std::string("Load error: ")+e.what());}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_loadMaterials(JNIEnv* env,jclass,jstring path){
    const char* chars=env->GetStringUTFChars(path,NULL);if(!chars)return NULL;
    try{
        std::ifstream input(chars,std::ios::binary);Require(bool(input),"Cannot read private PK4 copy");
        const std::shared_ptr<const wolf2009::MaterialCatalog> catalog=wolf2009android::ReadMaterialCatalog(input);
        {std::lock_guard<std::mutex> guard(materialMutex);materialCatalog=catalog;}
        std::ostringstream status;status<<"OK|Materials "<<catalog->MaterialCount()<<" | guides "<<catalog->GuideCount()<<". Open the map again to apply.";
        env->ReleaseStringUTFChars(path,chars);return Message(env,status.str());
    }catch(const std::exception& e){env->ReleaseStringUTFChars(path,chars);return Message(env,std::string("Material import error: ")+e.what());}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_demo(JNIEnv* env,jclass){try{return Message(env,Demo());}catch(const std::exception& e){return Message(env,e.what());}}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_draw(JNIEnv* env,jclass,jint width,jint height){try{Draw(width,height);if(renderStatusPending){renderStatusPending=false;return Message(env,"STATUS|"+RenderStatus());}return Message(env,"");}catch(const std::exception& e){return Message(env,e.what());}}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_gesture(JNIEnv*,jclass,jfloat dx,jfloat dy,jfloat zoom,jfloat panX,jfloat panY){
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(zoom)||!std::isfinite(panX)||!std::isfinite(panY)||zoom<=0)return;
    yaw+=dx;pitch=std::max(-1.5f,std::min(1.5f,pitch+dy));
    const Vec forward=Away()*-1,right=Unit(Cross(forward,Vec(0,0,1))),up=Cross(right,forward);
    const Vec pan=(right*panX+up*panY)*distance*2;
    if(flight)freeEye=freeEye+pan+forward*(distance*(1-std::max(.2f,std::min(5.f,zoom))));else cameraTarget=cameraTarget+pan;
    const float radius=currentMesh?currentMesh->radius:1;distance=std::max(radius*.005f,std::min(radius*100.f,distance*zoom));
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_move(JNIEnv*,jclass,jfloat horizontal,jfloat forwardAmount){
    const Vec forward=Away()*-1,right=Unit(Cross(forward,Vec(0,0,1)));const float step=distance*.08f;
    if(flight)freeEye=freeEye+(right*horizontal+forward*forwardAmount)*step;
    else {cameraTarget=cameraTarget+right*(horizontal*step);distance=std::max(.001f,distance*(forwardAmount>0?.85f:1/.85f));}
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_camera(JNIEnv*,jclass,jboolean enabled){if(bool(enabled)&&!flight)freeEye=cameraTarget+Away()*distance;if(!bool(enabled)&&flight)cameraTarget=freeEye-Away()*distance;flight=bool(enabled);}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_reset(JNIEnv*,jclass){Reset();}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_requestStatus(JNIEnv*,jclass){renderStatusPending=true;}
