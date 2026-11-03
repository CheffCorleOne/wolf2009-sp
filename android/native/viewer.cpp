// SPDX-License-Identifier: GPL-3.0-or-later
// Thin Android GLES3 adapter. The resource/geometry core has no Android calls.
#include "Archive.h"
#include "Geometry.h"
#include "TextureBinding.h"
#include "PlayerSession.h"
#include "MapPhysics.h"
#include "WagonScene.h"
#include "TrainStopScene.h"
#include "SceneAssembly.h"
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
#include <chrono>
#include <iomanip>
#include <array>

namespace {
struct Vec { float x,y,z; Vec(float a=0,float b=0,float c=0):x(a),y(b),z(c){} };
Vec operator+(Vec a,Vec b){return Vec(a.x+b.x,a.y+b.y,a.z+b.z);}
Vec operator-(Vec a,Vec b){return Vec(a.x-b.x,a.y-b.y,a.z-b.z);}
Vec operator*(Vec a,float k){return Vec(a.x*k,a.y*k,a.z*k);}
float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec Cross(Vec a,Vec b){return Vec(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
Vec Unit(Vec a){const float length=std::sqrt(Dot(a,a));return length>1e-20f?a*(1/length):Vec(0,0,1);}
struct Vertex {float x,y,z,u,v;};
struct Batch {
    std::uint32_t first, count;
    Vec color;
    int texture = -1;
    float uvX = 1, uvY = 1, alphaCutoff = -1;
    bool actor = false, omitOverlay = false, door = false;
    int instance = -1;
    Batch(std::uint32_t f, std::uint32_t n, Vec c) : first(f), count(n), color(c) {}
};
struct Mesh {std::vector<Vertex> vertices;std::vector<std::uint32_t> indices;std::vector<Batch> batches;wolf2009android::TexturePlan textures;Vec center;float radius=1;bool demo=false,levelStartLogged=false,levelArrivedLogged=false,campaignActorLogged=false,doorStartLogged=false,doorEndLogged=false;std::string resource,startsIssue,collisionIssue,levelIssue;std::vector<wolf2009::PlayerStartCandidate> starts;std::vector<wolf2009::PlayerStartSupport> supports;std::shared_ptr<const wolf2009::StaticCollisionScene> collision;std::unique_ptr<wolf2009::WagonScene> wagon;std::unique_ptr<wolf2009::TrainStopScene> campaign;std::size_t worldVertices=0,worldTriangles=0,worldSurfaces=0,actorDraws=0,propDraws=0,doorDraws=0,firstDoorVertex=(std::numeric_limits<std::size_t>::max)();};
const wolf2009::WagonScene* Wagon(const Mesh& mesh){return mesh.campaign?&mesh.campaign->Wagon():mesh.wagon.get();}
std::mutex pendingMutex;
std::mutex materialMutex;
std::shared_ptr<const wolf2009::MaterialCatalog> materialCatalog;
std::shared_ptr<const wolf2009::SkinCatalog> skinCatalog;
std::shared_ptr<Mesh> pendingMesh,currentMesh;
std::uint64_t revision=0,uploadedRevision=0;
std::uint64_t campaignImportEpoch=0; // protected by pendingMutex
GLuint program=0,vao=0,vbo=0,ibo=0;
GLint mvpUniform=-1,colorUniform=-1,useTextureUniform=-1,uvScaleUniform=-1,offsetUniform=-1,alphaCutoffUniform=-1,modelUniform=-1;
const std::array<float,16> identityMatrix={{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
std::vector<GLuint> textureNames;
std::size_t gpuReadyBindings=0,gpuDdsTextures=0,gpuSolidTextures=0,gpuFailures=0;
std::size_t skippedBindings=0;
bool renderStatusPending=false;
Vec cameraTarget,freeEye;
float yaw=.78539816f,pitch=.61086524f,distance=4,viewAspect=1;
bool flight=false,riding=false,suspended=false;
double levelAccumulator=0;
wolf2009android::PlayerSession player;
float walkRight=0,walkForward=0;
std::chrono::steady_clock::time_point lastPhysicsFrame,lastPlayerStatus;
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
GLenum TakeGlErrors(){GLenum first=GL_NO_ERROR;for(unsigned i=0;i<16;i++){const GLenum error=glGetError();if(error==GL_NO_ERROR)break;if(first==GL_NO_ERROR)first=error;}return first;}
std::string Key(std::string key){for(char& c:key){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c=char(c+32);}return key;}
struct ResourceLocation {std::size_t chunk=0,entry=0;ResourceLocation(std::size_t c=0,std::size_t e=0):chunk(c),entry(e){}};
struct MapResources {std::map<std::string,ResourceLocation> buffers,textures;ResourceLocation proc;std::string name;};
wolf2009::GeometryLimits SceneLimits(){wolf2009::GeometryLimits limits;limits.vertices=limits.triangles=2000000;limits.totalBufferBytes=256u*1024u*1024u;return limits;}
wolf2009::Limits ArchiveLimits(){wolf2009::Limits limits;limits.inflatedChunkBytes=128u*1024u*1024u;return limits;}
MapResources IndexMap(const wolf2009::Archive& archive){
    MapResources found;unsigned procs=0;
    for(std::size_t c=0;c<archive.Chunks().size();++c){const auto& chunk=archive.Chunks()[c];if(chunk.type!="PROC"&&chunk.type!="PBBF"&&chunk.type!="TXTR")continue;
        for(std::size_t e=0;e<chunk.entries.size();++e){ResourceLocation here(c,e);if(chunk.type=="PROC"){++procs;found.proc=here;found.name=chunk.entries[e].name;}
            else if(chunk.type=="PBBF")Require(found.buffers.insert(std::make_pair(Key(chunk.entries[e].name),here)).second,"Duplicate PBBF name");
            else Require(found.textures.insert(std::make_pair(wolf2009::MaterialCatalog::NormalizeVirtualPath(chunk.entries[e].name),here)).second,"Duplicate TXTR texture name");
        }
    }
    Require(procs==1,"Select one map MPK with exactly one PROC resource");return found;
}
wolf2009::GeometryScene ReadWorld(std::istream& input,const wolf2009::Archive& archive,const MapResources& resources,const wolf2009::GeometryLimits& limits){
    return wolf2009::DecodeProc7(archive.ReadEntry(input,resources.proc.chunk,resources.proc.entry),resources.name,[&](const std::string& key){const auto it=resources.buffers.find(Key(key));Require(it!=resources.buffers.end(),"PBBF render buffer not found in selected MPK");return archive.ReadEntry(input,it->second.chunk,it->second.entry);},limits);
}
Vec Color(const std::string& name){std::uint32_t h=2166136261u;for(unsigned char c:name){h^=c;h*=16777619u;}return Vec(.3f+float(h&255)/480,.3f+float((h>>8)&255)/480,.3f+float((h>>16)&255)/480);}
void Bounds(Mesh& mesh){
    Require(!mesh.vertices.empty()&&!mesh.indices.empty(),"No static world triangles in this MPK");
    const float maximum=(std::numeric_limits<float>::max)();Vec low(maximum,maximum,maximum),high(-maximum,-maximum,-maximum);
    for(std::size_t i=0;i<mesh.vertices.size();++i){const Vertex& v=mesh.vertices[i];Vec p(v.x,v.y,v.z);if(mesh.campaign&&i>=mesh.firstDoorVertex){const auto& m=mesh.campaign->DoorWorldMatrix();p=Vec(m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]);}low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}
    mesh.center=(low+high)*.5f;mesh.radius=std::max(.01f,std::sqrt(Dot(high-low,high-low))*.5f);
}
std::shared_ptr<Mesh> World(const wolf2009::GeometryScene& scene,wolf2009android::TexturePlan textures,std::size_t firstAttachedSurface=(std::numeric_limits<std::size_t>::max)(),const std::vector<std::size_t>& staticStarts=std::vector<std::size_t>(),std::size_t firstDoorSurface=(std::numeric_limits<std::size_t>::max)()){
    std::shared_ptr<Mesh> mesh(new Mesh);
    mesh->textures=std::move(textures);
    const std::uint32_t missing=(std::numeric_limits<std::uint32_t>::max)();
    std::vector<std::uint32_t> remap(scene.vertices.size(),missing);
    for(std::size_t surfaceIndex=0;surfaceIndex<scene.surfaces.size();surfaceIndex++){
        const wolf2009::GeometrySurface& surface=scene.surfaces[surfaceIndex];
        if((surface.model.compare(0,5,"_area")!=0&&surfaceIndex<firstAttachedSurface)||surface.indices.empty())continue;
        Batch batch={std::uint32_t(mesh->indices.size()),std::uint32_t(surface.indices.size()),Color(surface.material)};
        batch.actor=surfaceIndex>=firstAttachedSurface;
        if(!staticStarts.empty()&&surfaceIndex>=staticStarts[0]){std::size_t instance=0;while(instance+1<staticStarts.size()&&surfaceIndex>=staticStarts[instance+1])++instance;batch.instance=int(instance);batch.actor=false;}
        if(surfaceIndex>=firstDoorSurface){batch.door=true;batch.actor=false;batch.instance=-1;}
        const wolf2009android::SurfaceBinding& binding=mesh->textures.surfaces[surfaceIndex];
        batch.texture=binding.texture;batch.uvX=binding.uvScale[0];batch.uvY=binding.uvScale[1];
        batch.omitOverlay=binding.omitOverlay;batch.alphaCutoff=binding.alphaCutoff;
        for(std::uint32_t index:surface.indices){
            Require(index<scene.vertices.size(),"Internal geometry index out of range");
            if(remap[index]==missing){remap[index]=std::uint32_t(mesh->vertices.size());if(batch.door)mesh->firstDoorVertex=std::min(mesh->firstDoorVertex,mesh->vertices.size());const wolf2009::GeometryVertex& v=scene.vertices[index];mesh->vertices.push_back(Vertex{v.x,v.y,v.z,v.u,v.v});}
            mesh->indices.push_back(remap[index]);
        }
        mesh->batches.push_back(batch);if(!batch.actor&&!batch.door&&batch.instance<0){mesh->worldTriangles+=surface.indices.size()/3;++mesh->worldSurfaces;}
    }
    if(firstAttachedSurface==(std::numeric_limits<std::size_t>::max)())mesh->worldVertices=mesh->vertices.size();
    else{std::vector<bool> used(scene.vertices.size(),false);for(std::size_t i=0;i<firstAttachedSurface;i++)if(scene.surfaces[i].model.compare(0,5,"_area")==0)for(auto index:scene.surfaces[i].indices)if(!used[index]){used[index]=true;++mesh->worldVertices;}}
    Bounds(*mesh);return mesh;
}
void Publish(const std::shared_ptr<Mesh>& mesh){std::lock_guard<std::mutex> guard(pendingMutex);pendingMesh=mesh;++revision;}
Vec Away(){return Vec(std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch));}
Vec Eye(){if(riding&&currentMesh&&Wagon(*currentMesh)){const auto p=currentMesh->campaign?currentMesh->campaign->PlayerEye():Wagon(*currentMesh)->State().cameraOrigin;return Vec(float(p.x),float(p.y),float(p.z));}if(player.Selected()){const auto p=player.Eye();return Vec(float(p.x),float(p.y),float(p.z));}return flight?freeEye:cameraTarget+Away()*distance;}
wolf2009::CollisionVec3 WalkDirection(float rightAmount,float forwardAmount){const Vec forward=Unit(Vec(-std::cos(yaw),-std::sin(yaw),0)),right=Cross(forward,Vec(0,0,1));return {double(right.x*rightAmount+forward.x*forwardAmount),double(right.y*rightAmount+forward.y*forwardAmount),0};}
void PauseMotion(){walkRight=walkForward=0;if(currentMesh&&currentMesh->campaign)currentMesh->campaign->SetRiderInput(wolf2009::RiderMovementInput::Released);lastPhysicsFrame=std::chrono::steady_clock::time_point();levelAccumulator=0;player.Pause();}
void StepFrame(){
    const auto now=std::chrono::steady_clock::now();
    if(suspended){lastPhysicsFrame=std::chrono::steady_clock::time_point();return;}
    const bool level=currentMesh&&Wagon(*currentMesh);
    if(!player.Walking()&&!level){lastPhysicsFrame=now;return;}
    if(lastPhysicsFrame.time_since_epoch().count()!=0){
        const double elapsed=std::min(.1,std::chrono::duration<double>(now-lastPhysicsFrame).count());
        if(player.Walking())player.Advance(currentMesh?currentMesh->collision.get():NULL,WalkDirection(walkRight,walkForward),elapsed);
        if (level && currentMesh->levelIssue.empty() && (currentMesh->campaign || !Wagon(*currentMesh)->State().arrived)) {
            try {
                levelAccumulator += elapsed;
                for (unsigned i = 0; i < 6 && levelAccumulator + 1e-10 >= 1.0 / 60; ++i) {
                    if(currentMesh->campaign)currentMesh->campaign->Advance(1.0/60);else currentMesh->wagon->Advance(1.0 / 60);
                    levelAccumulator -= 1.0 / 60;
                    if (!currentMesh->campaign&&Wagon(*currentMesh)->State().arrived) break;
                }
            } catch (const std::exception& e) {
                currentMesh->levelIssue = e.what();
                levelAccumulator = 0;
            }
        }
    }
    lastPhysicsFrame=now;
    if(lastPlayerStatus.time_since_epoch().count()==0||std::chrono::duration<double>(now-lastPlayerStatus).count()>=.2){lastPlayerStatus=now;renderStatusPending=true;}
}
void Reset(){if(!currentMesh)return;cameraTarget=currentMesh->center;yaw=.78539816f;pitch=.61086524f;const float halfFov=std::min(.55f,std::atan(std::tan(.55f)*viewAspect));distance=currentMesh->radius*std::max(3.1f,1.1f/std::sin(halfFov));freeEye=cameraTarget+Away()*distance;}
GLuint Shader(GLenum type,const char* text){
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&text,NULL);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char message[2048];glGetShaderInfoLog(shader,sizeof(message),NULL,message);glDeleteShader(shader);throw std::runtime_error(std::string("Shader: ")+message);}return shader;
}
void Initialize(){
    // Context loss invalidates all old object names, so recreate and re-upload.
    program=vao=vbo=ibo=0;uploadedRevision=0;textureNames.clear();renderStatusPending=false;PauseMotion();
    const char* vertex="#version 300 es\nprecision highp float;layout(location=0)in vec3 position;layout(location=1)in vec2 uv;uniform mat4 mvp;uniform mat4 model;uniform vec3 actorOffset;out highp vec3 world;out highp vec2 textureUv;void main(){world=(model*vec4(position,1.0)).xyz+actorOffset;textureUv=uv;gl_Position=mvp*vec4(world,1.0);}";
    const char* fragment =
        "#version 300 es\nprecision highp float;"
        "in highp vec3 world; in highp vec2 textureUv;"
        "uniform vec3 color; uniform bool useTexture; uniform sampler2D diffuse;"
        "uniform vec2 uvScale; uniform float alphaCutoff; out vec4 result;"
        "void main() {"
        "  vec3 n = normalize(cross(dFdx(world), dFdy(world)));"
        "  float light = 0.28 + 0.72 * abs(dot(n, normalize(vec3(0.3,-0.5,0.8))));"
        "  vec4 base = useTexture ? texture(diffuse, textureUv*uvScale) : vec4(color,1.0);"
        "  if (base.a < alphaCutoff) discard;"
        "  result = vec4(base.rgb*light, 1.0);"
        "}";
    const GLuint vs=Shader(GL_VERTEX_SHADER,vertex),fs=Shader(GL_FRAGMENT_SHADER,fragment);program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);Require(ok==GL_TRUE,"GLES3 program link failed");
    mvpUniform=glGetUniformLocation(program,"mvp");colorUniform=glGetUniformLocation(program,"color");useTextureUniform=glGetUniformLocation(program,"useTexture");uvScaleUniform=glGetUniformLocation(program,"uvScale");
    alphaCutoffUniform=glGetUniformLocation(program,"alphaCutoff");
    offsetUniform=glGetUniformLocation(program,"actorOffset");
    modelUniform=glGetUniformLocation(program,"model");Require(modelUniform>=0,"GLES model matrix uniform unavailable");
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
    const bool newScene=next!=currentMesh;currentMesh=next;
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(next->vertices.size()*sizeof(Vertex)),next->vertices.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(next->indices.size()*sizeof(std::uint32_t)),next->indices.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,x)));
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,u)));
    Require(TakeGlErrors()==GL_NO_ERROR,"GPU could not allocate scene buffers");UploadTextures();if(newScene){player.Clear();PauseMotion();flight=false;riding=Wagon(*currentMesh)!=nullptr;Reset();if(riding){yaw=float((Wagon(*currentMesh)->Description().playerYawDegrees+180)*3.14159265358979323846/180);pitch=0;}}uploadedRevision=nextRevision;renderStatusPending=true;
}
void Multiply(float* out,const float* a,const float* b){for(unsigned c=0;c<4;c++)for(unsigned r=0;r<4;r++){out[c*4+r]=0;for(unsigned k=0;k<4;k++)out[c*4+r]+=a[k*4+r]*b[c*4+k];}}
std::uint32_t ArrivalHash(const std::vector<wolf2009::TrainArrivalChange>& changes){std::uint32_t hash=2166136261u;for(const auto& change:changes){hash=(hash^std::uint32_t(change.kind))*16777619u;for(unsigned i=0;i<4;i++)hash=(hash^((change.sourceNodeId>>(i*8))&255))*16777619u;for(unsigned char c:change.name)hash=(hash^c)*16777619u;hash*=16777619u;}return hash;}
void Draw(int width,int height){
    if(!program)return;viewAspect=float(std::max(1,width))/float(std::max(1,height));glViewport(0,0,std::max(1,width),std::max(1,height));glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);Upload();if(!currentMesh)return;StepFrame();
    const Vec eye=Eye();
    const Vec forward=Unit(Away()*-1),right=Unit(Cross(forward,Vec(0,0,1))),up=Cross(right,forward);
    const float view[16]={right.x,up.x,-forward.x,0,right.y,up.y,-forward.y,0,right.z,up.z,-forward.z,0,-Dot(right,eye),-Dot(up,eye),Dot(forward,eye),1};
    const float nearPlane=std::max(.005f,currentMesh->radius*.0001f),farPlane=std::max(100.f,currentMesh->radius*100.f);
    const float scale=1/std::tan(.55f),aspect=float(std::max(1,width))/float(std::max(1,height));
    const float projection[16]={scale/aspect,0,0,0,0,scale,0,0,0,0,-(farPlane+nearPlane)/(farPlane-nearPlane),-1,0,0,-2*farPlane*nearPlane/(farPlane-nearPlane),0};
    float mvp[16];Multiply(mvp,projection,view);glUseProgram(program);glUniformMatrix4fv(mvpUniform,1,GL_FALSE,mvp);glBindVertexArray(vao);
    currentMesh->actorDraws = currentMesh->propDraws = currentMesh->doorDraws = 0;
    skippedBindings = 0;
    for (const Batch& batch : currentMesh->batches) {
        if(batch.instance>=0&&(!Wagon(*currentMesh)||!Wagon(*currentMesh)->IsArrivalInstanceVisible(std::size_t(batch.instance))))continue;
        const GLuint texture = batch.texture >= 0 && std::size_t(batch.texture) < textureNames.size()
            ? textureNames[batch.texture] : 0;
        if (batch.omitOverlay || (!texture && batch.alphaCutoff >= 0)) { ++skippedBindings; continue; }
        if(batch.instance>=0)++currentMesh->propDraws;
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(useTextureUniform, texture ? 1 : 0);
        glUniform1f(alphaCutoffUniform, batch.alphaCutoff);
        glUniform2f(uvScaleUniform, batch.uvX, batch.uvY);
        glUniform3f(colorUniform, batch.color.x, batch.color.y, batch.color.z);
        Vec offset;
        if (batch.actor && Wagon(*currentMesh)) {
            const auto p = Wagon(*currentMesh)->State().cartOffset;
            offset = Vec(float(p.x),float(p.y),float(p.z));
            ++currentMesh->actorDraws;
        }
        glUniform3f(offsetUniform, offset.x, offset.y, offset.z);
        const auto& model=batch.door&&currentMesh->campaign?currentMesh->campaign->DoorWorldMatrix():identityMatrix;
        glUniformMatrix4fv(modelUniform,1,GL_FALSE,model.data());
        if(batch.door)++currentMesh->doorDraws;
        glDrawElements(GL_TRIANGLES, GLsizei(batch.count), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(std::size_t(batch.first) * sizeof(std::uint32_t)));
    }
    Require(glGetError()==GL_NO_ERROR,"GLES3 draw failed");
    if(Wagon(*currentMesh)){const auto& state=Wagon(*currentMesh)->State();
        if(!currentMesh->levelStartLogged){__android_log_print(ANDROID_LOG_INFO,"Wolf2009","Level OnSpawn=%d generation=%llu t=%.3f cartY=%.3f playerY=%.3f actorDraws=%zu visible=%zu events=%zu propDraws=%zu",int(state.spawnDelivered),static_cast<unsigned long long>(state.levelGeneration),state.elapsedSeconds,state.cartOrigin.y,state.playerFeet.y,currentMesh->actorDraws,state.visibleArrivalInstances,state.arrivalChangeCount,currentMesh->propDraws);currentMesh->levelStartLogged=true;}
        if(state.arrived&&!currentMesh->levelArrivedLogged){__android_log_print(ANDROID_LOG_INFO,"Wolf2009","Level arrived generation=%llu t=%.3f cartY=%.3f playerY=%.3f actorDraws=%zu visible=%zu events=%zu propDraws=%zu batchHash=%08x unsupportedBoundary=%u",static_cast<unsigned long long>(state.levelGeneration),state.elapsedSeconds,state.cartOrigin.y,state.playerFeet.y,currentMesh->actorDraws,state.visibleArrivalInstances,state.arrivalChangeCount,currentMesh->propDraws,ArrivalHash(Wagon(*currentMesh)->ArrivalChanges()),Wagon(*currentMesh)->Description().arrivalBoundaryNode);currentMesh->levelArrivedLogged=true;}
    }
    if(currentMesh->campaign){const auto& state=currentMesh->campaign->State();
        if(state.actorStarted&&!currentMesh->campaignActorLogged){__android_log_print(ANDROID_LOG_INFO,"Wolf2009","Campaign actor started generation=%llu clock=%.3f voiceReady=%d joints=%zu",static_cast<unsigned long long>(state.generation),state.clock,int(state.voiceReady),currentMesh->campaign->ActorPose().pose.model.size());currentMesh->campaignActorLogged=true;}
        if(state.doorActivated&&!currentMesh->doorStartLogged){__android_log_print(ANDROID_LOG_INFO,"Wolf2009","Campaign door started generation=%llu clock=%.3f seconds=%.3f slide=%.3f doorDraws=%zu",static_cast<unsigned long long>(state.generation),state.clock,state.doorSeconds,state.doorSlide,currentMesh->doorDraws);currentMesh->doorStartLogged=true;}
        if(state.doorActivated&&state.actorStatus==wolf2009::GreeterPlaybackStatus::Finished&&!currentMesh->doorEndLogged){__android_log_print(ANDROID_LOG_INFO,"Wolf2009","Campaign clip finished generation=%llu clock=%.3f doorSeconds=%.3f slide=%.3f doorDraws=%zu",static_cast<unsigned long long>(state.generation),state.clock,state.doorSeconds,state.doorSlide,currentMesh->doorDraws);currentMesh->doorEndLogged=true;}
    }
}
std::string Load(const char* path,bool startLevel=false){
    std::ifstream input(path,std::ios::binary);Require(bool(input),"Cannot read private MPK copy");
    const auto archive=wolf2009::Archive::Scan(input,ArchiveLimits());
    const auto resources=IndexMap(archive);const auto& name=resources.name;const auto& textures=resources.textures;
    const auto limits=SceneLimits();auto scene=ReadWorld(input,archive,resources,limits);
    std::shared_ptr<const wolf2009::MaterialCatalog> catalog;std::shared_ptr<const wolf2009::SkinCatalog> skins;{std::lock_guard<std::mutex> guard(materialMutex);catalog=materialCatalog;skins=skinCatalog;}
    std::unique_ptr<wolf2009::WagonScene> wagon;std::size_t firstActor=(std::numeric_limits<std::size_t>::max)();std::vector<std::size_t> staticStarts;
    if(startLevel){Require(bool(skins),"Import assets.pk4 through Materials before starting this level");wolf2009::WagonSceneOptions options;options.geometry=limits;options.enableArrival=true;options.skins=skins.get();wagon=wolf2009::WagonScene::Load(input,archive,name,options);firstActor=wolf2009android::AppendPlacedActor(scene,wagon->PlacedGeometry(),limits);for(const auto& instance:wagon->ArrivalInstances())staticStarts.push_back(wolf2009android::AppendPlacedActor(scene,instance.geometry,limits));}
    wolf2009android::TexturePlan plan=wolf2009android::BindTextures(scene,catalog.get(),[&](const std::string& key){
        const auto it=textures.find(key);if(it==textures.end())return std::vector<unsigned char>();return archive.ReadEntry(input,it->second.chunk,it->second.entry);
    },wolf2009android::TextureLimits(),firstActor);
    const std::shared_ptr<Mesh> mesh=World(scene,std::move(plan),firstActor,staticStarts);mesh->resource=name;mesh->wagon=std::move(wagon);
    try{
        mesh->starts=wolf2009::ReadMapPlayerStarts(input,archive,name);Require(mesh->starts.size()<=128,"More than 128 player starts are unsupported by the selector");
        std::size_t remaining=8000000;
        for(const auto& candidate:mesh->starts){wolf2009::PlayerStartProbeOptions options;options.maxTriangles=remaining;const auto support=wolf2009::AssessPlayerStart(candidate,scene,options);mesh->supports.push_back(support);remaining-=std::min(remaining,support.checkedTriangles);}
    }catch(const std::bad_alloc&){throw;}catch(const std::exception& e){mesh->starts.clear();mesh->supports.clear();mesh->startsIssue=e.what();}
    try{mesh->collision=std::make_shared<const wolf2009::StaticCollisionScene>(wolf2009::ReadMapStaticCollision(input,archive,name));}
    catch(const std::bad_alloc&){throw;}catch(const std::exception& e){mesh->collisionIssue=e.what();}
    Publish(mesh);
    std::ostringstream message;message<<mesh->vertices.size()<<" vertices | "<<mesh->indices.size()/3<<" triangles | "<<mesh->batches.size()<<" draw surfaces";return message.str();
}
void CheckPrivateFile(std::ifstream& input,std::uint64_t cap){
    Require(bool(input),"Cannot open private campaign input");input.seekg(0,std::ios::end);const auto bytes=input.tellg();
    Require(bytes>=4&&std::uint64_t(bytes)<=cap,"Campaign input is empty or exceeds its byte limit");input.seekg(0,std::ios::beg);Require(bool(input),"Cannot seek private campaign input");
}
std::string LoadCampaign(const char* assetsPath,const char* mapPath,const char* globalPath,const char* voicePath){
    std::uint64_t importEpoch;{std::lock_guard<std::mutex> guard(pendingMutex);importEpoch=campaignImportEpoch;}
    std::ifstream assetsInput(assetsPath,std::ios::binary),mapInput(mapPath,std::ios::binary),globalInput(globalPath,std::ios::binary);
    CheckPrivateFile(assetsInput,128ull<<20);CheckPrivateFile(mapInput,512ull<<20);CheckPrivateFile(globalInput,128ull<<20);
    const auto archive=wolf2009::Archive::Scan(mapInput,ArchiveLimits());const auto resources=IndexMap(archive);const auto limits=SceneLimits();
    wolf2009::ZipLimits zipLimits;zipLimits.fileBytes=128u<<20;zipLimits.entries=50000;zipLimits.totalNameBytes=8u<<20;
    const auto assetsArchive=wolf2009::ZipArchive::Scan(assetsInput,zipLimits);
    const auto catalog=wolf2009android::ReadMaterialCatalog(assetsInput,assetsArchive);
    const auto skins=std::make_shared<const wolf2009::SkinCatalog>(wolf2009::SkinCatalog::Read(assetsInput,assetsArchive));
    const auto globalArchive=wolf2009::Archive::Scan(globalInput,ArchiveLimits());
    const std::string voicePackage="streampacks/scripted_voice_kc1_trn_vo_english.spk",voiceFile(voicePath);
    wolf2009::StreamMountLimits mountLimits;mountLimits.archive=ArchiveLimits();mountLimits.archiveBytes=32u<<20;mountLimits.totalArchiveBytes=32u<<20;mountLimits.mounts=1;mountLimits.readBytes=16u<<20;
    wolf2009::StreamMounts voices([&](const std::string& id){Require(id==voicePackage,"Unrecognized localized voice package");std::unique_ptr<std::ifstream> stream(new std::ifstream(voiceFile,std::ios::binary));CheckPrivateFile(*stream,32ull<<20);return std::unique_ptr<std::istream>(stream.release());},mountLimits);
    voices.Mount(voicePackage);
    const wolf2009::TrainStopSources sources={mapInput,archive,assetsInput,assetsArchive,globalInput,globalArchive,voices,*skins};
    wolf2009::WagonSceneOptions options;options.geometry=limits;options.enableArrival=true;options.skins=skins.get();
    auto campaign=wolf2009::TrainStopScene::Load(sources,options);
    auto scene=ReadWorld(mapInput,archive,resources,limits);const auto& wagon=campaign->Wagon();
    const auto firstActor=wolf2009android::AppendPlacedActor(scene,wagon.PlacedGeometry(),limits);std::vector<std::size_t> staticStarts;
    for(const auto& instance:wagon.ArrivalInstances())staticStarts.push_back(wolf2009android::AppendPlacedActor(scene,instance.geometry,limits));
    const auto firstDoor=wolf2009android::AppendPlacedActor(scene,campaign->DoorBindGeometry(),limits);
    auto plan=wolf2009android::BindTextures(scene,catalog.get(),[&](const std::string& key){const auto found=resources.textures.find(key);return found==resources.textures.end()?std::vector<unsigned char>():archive.ReadEntry(mapInput,found->second.chunk,found->second.entry);},wolf2009android::TextureLimits(),firstActor);
    const auto mesh=World(scene,std::move(plan),firstActor,staticStarts,firstDoor);mesh->resource=resources.name;mesh->campaign=std::move(campaign);Bounds(*mesh);
    try{mesh->starts=wolf2009::ReadMapPlayerStarts(mapInput,archive,resources.name);Require(mesh->starts.size()<=128,"More than 128 player starts are unsupported by the selector");}
    catch(const std::bad_alloc&){throw;}catch(const std::exception& e){mesh->starts.clear();mesh->startsIssue=e.what();}
    try{mesh->collision=std::make_shared<const wolf2009::StaticCollisionScene>(wolf2009::ReadMapStaticCollision(mapInput,archive,resources.name));}
    catch(const std::bad_alloc&){throw;}catch(const std::exception& e){mesh->collisionIssue=e.what();}
    std::ostringstream message;message<<(mesh->campaign->State().riderControlEnabled?"WALK|":"RIDE|")<<mesh->vertices.size()<<" vertices | "<<mesh->indices.size()/3<<" triangles | "<<mesh->batches.size()<<" draw surfaces | campaign + rigid door";const auto ready=message.str();
    // One commit: parser, package, model and texture failures retain both the
    // prior scene and prior catalogs. Runtime owns its data after this return.
    {std::unique_lock<std::mutex> pendingGuard(pendingMutex,std::defer_lock),materialGuard(materialMutex,std::defer_lock);std::lock(pendingGuard,materialGuard);Require(importEpoch==campaignImportEpoch,"Campaign import was cancelled");pendingMesh=mesh;materialCatalog=catalog;skinCatalog=skins;++revision;}
    return ready;
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
    out<<currentMesh->vertices.size()<<" vertices | "<<currentMesh->indices.size()/3<<" triangles | "<<currentMesh->batches.size()<<(Wagon(*currentMesh)?" draw surfaces\n":" world surfaces\n");
    out<<"Diffuse "<<gpuReadyBindings<<"/"<<plan.worldBindings<<" | DDS "<<gpuDdsTextures<<" | solid "<<gpuSolidTextures<<" | flat "<<(plan.worldBindings-gpuReadyBindings-skippedBindings);
    if(skippedBindings)out<<" | skipped "<<skippedBindings;
    if(plan.partialBindings)out<<" | partial "<<plan.partialBindings;
    if(plan.worldBindings>plan.resolvedBindings)out<<" | unresolved "<<(plan.worldBindings-plan.resolvedBindings);
    if(plan.missingBindings)out<<" | missing "<<plan.missingBindings;
    if(plan.decodeFallbacks)out<<" | DDS errors "<<plan.decodeFallbacks;
    if(plan.budgetFallbacks)out<<" | budget "<<plan.budgetFallbacks;
    if(gpuFailures)out<<" | GPU fallback "<<gpuFailures;
    if(Wagon(*currentMesh)){const auto& state=Wagon(*currentMesh)->State();out<<std::fixed<<std::setprecision(2)<<"\nLevel OnSpawn | t "<<state.elapsedSeconds<<" | wagon "<<state.cartOrigin.x<<","<<state.cartOrigin.y<<","<<state.cartOrigin.z<<" | offset "<<state.cartOffset.y<<"Y | actor draws "<<currentMesh->actorDraws;
        const auto& feet=currentMesh->campaign?currentMesh->campaign->PlayerFeet():state.playerFeet;
        out<<"\nRider "<<feet.x<<","<<feet.y<<","<<feet.z<<" | contact "<<state.supportContact<<" | props "<<state.visibleArrivalInstances<<"/"<<Wagon(*currentMesh)->ArrivalInstances().size()<<" | GPU "<<currentMesh->propDraws;
        if(state.arrivalApplied&&!currentMesh->campaign)out<<" | greeter/door unsupported";
        if(!currentMesh->levelIssue.empty())out<<" | stopped: "<<currentMesh->levelIssue;
    }
    if(currentMesh->campaign){const auto& state=currentMesh->campaign->State();out<<std::fixed<<std::setprecision(3)<<"\nCampaign | voice prepared "<<state.voiceReady<<" | actor started "<<state.actorStarted<<" | joints "<<currentMesh->campaign->ActorPose().pose.model.size()<<" | door active "<<state.doorActivated<<" | seconds "<<state.doorSeconds<<" | slide "<<state.doorSlide<<" | door GPU "<<currentMesh->doorDraws;const auto feet=currentMesh->campaign->PlayerFeet();
            out<<"\nWagon rider XYZ "<<feet.x<<","<<feet.y<<","<<feet.z<<" | central controls "<<state.riderControlEnabled<<" | blocked "<<state.riderBlocked<<" | end contact "<<state.riderEndContact;
            out<<"\nGreeter mesh, audio playback, station walking and later AI unsupported.";}
    if(player.Selected()){
        const auto feet=player.State().feet;out<<"\n"<<(player.Walking()?"AABB walking prototype: ":"ENTS inspection only: ")<<player.Candidate().placement.name;
        out<<std::fixed<<std::setprecision(3)<<" | feet "<<feet.x<<","<<feet.y<<","<<feet.z<<" | eye +64 | grounded "<<(player.State().grounded?1:0);
        if(!player.Walking())out<<" | movement disabled";
        if(!player.HasInitialHeading())out<<" | heading manual";
    }
    return out.str();
}
std::string SafeLabel(const std::string& text){std::string out;const char* hex="0123456789abcdef";for(unsigned char c:text){if(c>=32&&c<127)out+=char(c);else{out+="\\x";out+=hex[c>>4];out+=hex[c&15];}if(out.size()>512){out+="...";break;}}return out;}
bool SelectableName(const std::string& name){if(name.empty()||name.size()>256)return false;for(unsigned char c:name)if(c<32||c>=127)return false;return true;}
std::string StartDetails(){
    if(!currentMesh||currentMesh->demo)return "Load a map MPK first.";
    std::ostringstream out;out<<"Resource: "<<SafeLabel(currentMesh->resource)<<"\nNamed ENTS candidates: "<<currentMesh->starts.size();
    if(!currentMesh->startsIssue.empty())out<<"\nENTS: "<<currentMesh->startsIssue;
    if(currentMesh->collision){const auto& collision=*currentMesh->collision;out<<"\nHKXS boxes "<<collision.boxes.size()<<", complete "<<collision.coverageComplete<<", verified pose "<<collision.identityBodyPoseVerified<<", other bodies "<<collision.otherRigidBodies;}
    else out<<"\nHKXS unavailable: "<<currentMesh->collisionIssue;
    if(Wagon(*currentMesh)){const auto& d=Wagon(*currentMesh)->Description();const auto& s=Wagon(*currentMesh)->State();out<<"\nLevel actor: "<<SafeLabel(d.entityName)<<"\nMODL: "<<SafeLabel(d.modelResource)<<"\nSpawn: "<<SafeLabel(d.spawnName)<<" yaw "<<d.playerYawDegrees<<"\nCart authored world: "<<d.cartStart.x<<", "<<d.cartStart.y<<", "<<d.cartStart.z<<"\nCart offset: "<<s.cartOffset.x<<", "<<s.cartOffset.y<<", "<<s.cartOffset.z<<"\nRider camera: "<<s.cameraOrigin.x<<", "<<s.cameraOrigin.y<<", "<<s.cameraOrigin.z<<"\nWorld-only: "<<currentMesh->worldVertices<<" vertices, "<<currentMesh->worldTriangles<<" triangles, "<<currentMesh->worldSurfaces<<" surfaces\nActor GPU draws: "<<currentMesh->actorDraws<<", static props "<<currentMesh->propDraws<<"\nArrival generation "<<s.levelGeneration<<", events "<<s.arrivalChangeCount<<", batch hash "<<std::hex<<ArrivalHash(Wagon(*currentMesh)->ArrivalChanges())<<std::dec<<"\nUnexecuted nodes: "<<d.unexecutedBrainNodes<<"; boundary "<<d.arrivalBoundaryNode<<" "<<SafeLabel(d.arrivalUnsupportedClass)<<" "<<SafeLabel(d.arrivalUnsupportedTarget)<<" useKey "<<SafeLabel(d.arrivalUnsupportedUseKey)<<(currentMesh->campaign?". Greeter mesh/audio playback, walking and later AI remain unsupported.":". Greeter/door/AI/weapons remain unsupported. Ride does not certify world clearance.");}
    if(player.Selected()){
        const auto& placement=player.Candidate().placement;out<<"\nSelected: "<<SafeLabel(placement.name)<<"\nAuthored feet: "<<placement.origin[0]<<", "<<placement.origin[1]<<", "<<placement.origin[2];
        if(placement.hasAngle)out<<"\nAuthored angle: "<<placement.angle<<" (applied: yaw0 +X, yaw90 +Y, pitch0)";
        else if(!placement.hasRotation)out<<"\nNo angle/rotation: runtime default yaw0 (+X), pitch0 applied.";
        else out<<"\nRotation only: heading remains manual.";
        if(placement.hasRotation)out<<"\nAuthored rotation retained; rotation-only heading not applied.";
        out<<"\nSession: "<<player.Status();
    }
    out<<"\nEye height64: observed standing runtime default; no bob/spawn offset imitation.\nSupport is a render-geometry probe only, not collision/body clearance. No authored origin snap or mission entry selection.";return out.str();
}
}

extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_initialize(JNIEnv* env,jclass){try{Initialize();return Message(env,"");}catch(const std::exception& e){__android_log_print(ANDROID_LOG_ERROR,"Wolf2009","%s",e.what());return Message(env,e.what());}}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_loadMpk(JNIEnv* env,jclass,jstring path){
    const char* chars=env->GetStringUTFChars(path,NULL);if(!chars)return NULL;
    try{const std::string status=Load(chars);env->ReleaseStringUTFChars(path,chars);return Message(env,"OK|"+status);}
    catch(const std::exception& e){env->ReleaseStringUTFChars(path,chars);return Message(env,std::string("Load error: ")+e.what());}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_loadLevel(JNIEnv* env,jclass,jstring path){
    const char* chars=env->GetStringUTFChars(path,NULL);if(!chars)return NULL;
    try{const std::string status=Load(chars,true);env->ReleaseStringUTFChars(path,chars);return Message(env,"OK|"+status);}
    catch(const std::exception& e){env->ReleaseStringUTFChars(path,chars);return Message(env,std::string("Level startup refused: ")+e.what());}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_loadCampaign(JNIEnv* env,jclass,jstring assets,jstring map,jstring global,jstring voice){
    struct Path {JNIEnv* env;jstring value;const char* chars;Path(JNIEnv* e,jstring v):env(e),value(v),chars(nullptr){Require(value&&env->GetStringUTFLength(value)<=8192,"Campaign private path is invalid");chars=env->GetStringUTFChars(value,nullptr);Require(chars!=nullptr,"Cannot access campaign private path");}~Path(){if(chars)env->ReleaseStringUTFChars(value,chars);}};
    try{const Path a(env,assets),m(env,map),g(env,global),v(env,voice);return Message(env,"OK|"+LoadCampaign(a.chars,m.chars,g.chars,v.chars));}
    catch(const std::exception& error){if(env->ExceptionCheck())return nullptr;return Message(env,std::string("Campaign startup refused: ")+error.what());}
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_cancelCampaignLoad(JNIEnv*,jclass){std::lock_guard<std::mutex> guard(pendingMutex);++campaignImportEpoch;}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_loadMaterials(JNIEnv* env,jclass,jstring path){
    const char* chars=env->GetStringUTFChars(path,NULL);if(!chars)return NULL;
    try{
        std::ifstream input(chars,std::ios::binary);Require(bool(input),"Cannot read private PK4 copy");
        const std::shared_ptr<const wolf2009::MaterialCatalog> catalog=wolf2009android::ReadMaterialCatalog(input);
        const auto skins=wolf2009android::ReadSkinCatalog(input);
        {std::lock_guard<std::mutex> guard(materialMutex);materialCatalog=catalog;skinCatalog=skins;}
        std::ostringstream status;status<<"OK|Materials "<<catalog->MaterialCount()<<" | guides "<<catalog->GuideCount()<<" | skins "<<skins->SkinCount()<<". Open the map again to apply.";
        env->ReleaseStringUTFChars(path,chars);return Message(env,status.str());
    }catch(const std::exception& e){env->ReleaseStringUTFChars(path,chars);return Message(env,std::string("Material import error: ")+e.what());}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_demo(JNIEnv* env,jclass){try{return Message(env,Demo());}catch(const std::exception& e){return Message(env,e.what());}}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_draw(JNIEnv* env,jclass,jint width,jint height){try{Draw(width,height);if(renderStatusPending){renderStatusPending=false;return Message(env,"STATUS|"+RenderStatus());}return Message(env,"");}catch(const std::exception& e){return Message(env,e.what());}}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_gesture(JNIEnv*,jclass,jfloat dx,jfloat dy,jfloat zoom,jfloat panX,jfloat panY){
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(zoom)||!std::isfinite(panX)||!std::isfinite(panY)||zoom<=0)return;
    yaw+=dx;pitch=std::max(-1.5f,std::min(1.5f,pitch+dy));
    if(player.Selected()||riding)return; // No pan/pinch teleport can bypass the controller.
    const Vec forward=Away()*-1,right=Unit(Cross(forward,Vec(0,0,1))),up=Cross(right,forward);
    const Vec pan=(right*panX+up*panY)*distance*2;
    if(flight)freeEye=freeEye+pan+forward*(distance*(1-std::max(.2f,std::min(5.f,zoom))));else cameraTarget=cameraTarget+pan;
    const float radius=currentMesh?currentMesh->radius:1;distance=std::max(radius*.005f,std::min(radius*100.f,distance*zoom));
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_move(JNIEnv*,jclass,jfloat horizontal,jfloat forwardAmount){
    if(riding)return;
    if(player.Selected()){if(currentMesh&&player.Walking())player.Pulse(currentMesh->collision.get(),WalkDirection(horizontal,forwardAmount));renderStatusPending=true;return;}
    const Vec forward=Away()*-1,right=Unit(Cross(forward,Vec(0,0,1)));const float step=distance*.08f;
    if(flight)freeEye=freeEye+(right*horizontal+forward*forwardAmount)*step;
    else {cameraTarget=cameraTarget+right*(horizontal*step);distance=std::max(.001f,distance*(forwardAmount>0?.85f:1/.85f));}
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_camera(JNIEnv*,jclass,jboolean enabled){const Vec previous=Eye();const bool wasPlayer=player.Selected()||riding;if(bool(enabled)&&(!flight||wasPlayer))freeEye=previous;if(!bool(enabled)&&(flight||wasPlayer))cameraTarget=previous-Away()*distance;player.Clear();PauseMotion();flight=bool(enabled);riding=false;renderStatusPending=true;}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_reset(JNIEnv*,jclass){Reset();}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_requestStatus(JNIEnv*,jclass){renderStatusPending=true;}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_walkInput(JNIEnv*,jclass,jfloat horizontal,jfloat forwardAmount){if(!std::isfinite(horizontal)||!std::isfinite(forwardAmount))return;
    if(riding && currentMesh && currentMesh->campaign) {
        const auto input=forwardAmount>.5f?wolf2009::RiderMovementInput::Forward:
                         forwardAmount<-.5f?wolf2009::RiderMovementInput::Backward:wolf2009::RiderMovementInput::Released;
        currentMesh->campaign->SetRiderInput(input);
        const auto& feet=currentMesh->campaign->PlayerFeet();
        __android_log_print(ANDROID_LOG_INFO,"Wolf2009","Wagon input=%d generation=%llu feetY=%.3f localY=%.3f qualified=%d",
            static_cast<int>(input),static_cast<unsigned long long>(currentMesh->campaign->State().generation),
            feet.y,feet.y-currentMesh->campaign->Wagon().State().cartOrigin.y,
            int(currentMesh->campaign->State().riderControlEnabled));
        return;
    }
    walkRight=player.Walking()?std::max(-1.f,std::min(1.f,horizontal)):0;walkForward=player.Walking()?std::max(-1.f,std::min(1.f,forwardAmount)):0;}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_pauseMotion(JNIEnv*,jclass){PauseMotion();}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_viewer_NativeBridge_setSuspended(JNIEnv*,jclass,jboolean value){suspended=bool(value);PauseMotion();}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_levelCamera(
    JNIEnv* env, jclass, jboolean restart) {
    if (!currentMesh || !Wagon(*currentMesh)) return Message(env, "No supported level startup is loaded");
    try {
        if (restart) {
            if(currentMesh->campaign)currentMesh->campaign->Reset();else currentMesh->wagon->Reset();
            currentMesh->campaignActorLogged=currentMesh->doorStartLogged=currentMesh->doorEndLogged=false;
            currentMesh->levelStartLogged = currentMesh->levelArrivedLogged = false;
            currentMesh->levelIssue.clear();
            currentMesh->propDraws=0;
        }
        player.Clear();
        PauseMotion();
        riding = true;
        flight = false;
        yaw = float((Wagon(*currentMesh)->Description().playerYawDegrees + 180) * 3.14159265358979323846 / 180);
        pitch = 0;
        renderStatusPending = true;
        return Message(env, "OK|" + RenderStatus());
    } catch (const std::exception& error) {
        return Message(env, std::string("Level could not restart: ") + error.what());
    }
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_wolf2009_viewer_NativeBridge_startOptions(JNIEnv* env,jclass){
    const std::size_t count=currentMesh&&!currentMesh->demo?currentMesh->starts.size():0;
    jclass stringClass=env->FindClass("java/lang/String");jobjectArray out=env->NewObjectArray(jsize(1+count*2),stringClass,NULL);if(!out)return NULL;
    jstring generation=Message(env,std::to_string(uploadedRevision));env->SetObjectArrayElement(out,0,generation);env->DeleteLocalRef(generation);
    for(std::size_t i=0;i<count;i++){
        const auto& candidate=currentMesh->starts[i];std::ostringstream label;label<<SafeLabel(candidate.placement.name)<<" [";
        label<<(candidate.kind==wolf2009::PlayerStartKind::Primary?"player_start":candidate.kind==wolf2009::PlayerStartKind::Respawn?"respawn":"teleport");
        if(candidate.editorNameHint)label<<", editor-name hint";label<<"]";
        if(i<currentMesh->supports.size())label<<"\n"<<wolf2009::PlayerStartSupportStatusText(currentMesh->supports[i].status);
        if(!SelectableName(candidate.placement.name))label<<" | unsupported/unnamed selection";
        jstring name=Message(env,SelectableName(candidate.placement.name)?candidate.placement.name:"");jstring text=Message(env,label.str());
        env->SetObjectArrayElement(out,jsize(i*2+1),name);env->SetObjectArrayElement(out,jsize(i*2+2),text);env->DeleteLocalRef(name);env->DeleteLocalRef(text);
    }
    env->DeleteLocalRef(stringClass);return out;
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_enterStart(JNIEnv* env,jclass,jstring requested,jlong generation,jboolean walkingRequested){
    if(!currentMesh||currentMesh->demo||std::uint64_t(generation)!=uploadedRevision)return Message(env,"Start rejected: scene changed; choose the start again");
    const char* chars=env->GetStringUTFChars(requested,NULL);if(!chars)return NULL;
    std::string name(chars);env->ReleaseStringUTFChars(requested,chars);
    try{Require(SelectableName(name),"Start has no supported explicit name");std::string reason;
        if(!player.Start(currentMesh->starts,name,currentMesh->collision.get(),bool(walkingRequested),reason))return Message(env,"Start rejected: "+reason);
        PauseMotion();flight=false;riding=false;if(player.HasInitialHeading()){yaw=float(player.InitialAwayYaw());pitch=0;}lastPlayerStatus=std::chrono::steady_clock::time_point();renderStatusPending=true;
        return Message(env,"OK|"+RenderStatus());
    }catch(const std::exception& e){return Message(env,std::string("Start rejected: ")+e.what());}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_viewer_NativeBridge_startDetails(JNIEnv* env,jclass){return Message(env,StartDetails());}
