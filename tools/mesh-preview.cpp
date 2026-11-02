// SPDX-License-Identifier: GPL-3.0-or-later
// Independent, CPU-only geometry diagnostic. This is not an engine renderer.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const std::size_t MaxInputBytes = 256u * 1024u * 1024u;
const std::size_t MaxVertices = 2000000u, MaxTriangles = 4000000u;
const double Pi = 3.14159265358979323846;
void Need(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
struct V3 { double x, y, z; };
V3 Add(V3 a, V3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V3 Sub(V3 a, V3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
V3 Mul(V3 a, double k) { return {a.x*k,a.y*k,a.z*k}; }
double Dot(V3 a, V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
V3 Cross(V3 a, V3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
V3 Unit(V3 v) { const double n=std::sqrt(Dot(v,v)); Need(std::isfinite(n)&&n>1e-12,"Degenerate direction"); return Mul(v,1.0/n); }
double Number(const std::string& s) {
    char* end=NULL; errno=0;
    const double n=std::strtod(s.c_str(),&end);
    Need(end!=s.c_str() && *end==0 && errno!=ERANGE && std::isfinite(n),"Invalid or non-finite number");
    Need(std::abs(n)<=1e12,"Coordinate exceeds diagnostic range"); return n;
}
std::size_t Index(const std::string& s) {
    Need(!s.empty(),"Missing OBJ index");
    std::size_t n=0;
    for(std::size_t i=0;i<s.size();++i) {
        Need(s[i]>='0'&&s[i]<='9',"Only positive absolute OBJ indices are supported");
        Need(n<=(std::numeric_limits<std::size_t>::max()-9)/10,"OBJ index overflow");
        n=n*10+std::size_t(s[i]-'0');
    }
    Need(n>0,"OBJ index zero is invalid"); return n-1;
}
struct Corner { std::size_t vertex, uv, normal; bool hasUv, hasNormal; };
Corner ParseCorner(const std::string& token) {
    Corner c={0,0,0,false,false};
    const std::size_t slash=token.find('/');
    c.vertex=Index(token.substr(0,slash));
    if(slash==std::string::npos) return c;
    const std::size_t second=token.find('/',slash+1);
    if(second==std::string::npos) { c.uv=Index(token.substr(slash+1)); c.hasUv=true; return c; }
    Need(token.find('/',second+1)==std::string::npos,"Malformed OBJ face corner");
    if(second>slash+1) { c.uv=Index(token.substr(slash+1,second-slash-1)); c.hasUv=true; }
    c.normal=Index(token.substr(second+1)); c.hasNormal=true; return c;
}
std::uint32_t Hash(const std::string& s) {
    std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<s.size();++i) { h^=static_cast<unsigned char>(s[i]); h*=16777619u; }
    return h;
}
struct Triangle { Corner c[3]; std::uint32_t color; };
struct Mesh { std::vector<V3> vertices; std::vector<Triangle> triangles; std::size_t uvs=0, normals=0; };

Mesh ReadObj(std::istream& in) {
    Mesh mesh; std::string line; std::size_t totalBytes=0; std::uint32_t color=0;
    char lineBuffer[16386];
    for(;;) {
        in.getline(lineBuffer,sizeof(lineBuffer));
        const std::streamsize consumed=in.gcount();
        if(consumed==0&&in.eof())break;
        Need(!in.bad(),"OBJ stream read failure");
        Need(!in.fail()||in.eof(),"OBJ line exceeds 16 KiB limit");
        const std::size_t lineBytes=std::size_t(consumed)-(in.eof()?0u:1u);
        line.assign(lineBuffer,lineBytes);totalBytes+=std::size_t(consumed);
        Need(line.find('\0')==std::string::npos,"Embedded NUL in OBJ text");
        Need(totalBytes<=MaxInputBytes,"OBJ exceeds 256 MiB input limit");
        Need(line.size()<=16384,"OBJ line exceeds 16 KiB limit");
        const std::size_t comment=line.find('#'); if(comment!=std::string::npos) line.resize(comment);
        std::istringstream fields(line); std::string tag; if(!(fields>>tag)) continue;
        std::vector<std::string> words; std::string word;
        while(fields>>word) { Need(words.size()<64,"Too many OBJ fields"); words.push_back(word); }
        if(tag=="v" || tag=="vn") {
            Need(words.size()==3,"Expected three components in OBJ v/vn record");
            const V3 v={Number(words[0]),Number(words[1]),Number(words[2])};
            if(tag=="v") { Need(mesh.vertices.size()<MaxVertices,"OBJ vertex limit exceeded"); mesh.vertices.push_back(v); }
            else { Need(mesh.normals<MaxVertices,"OBJ normal limit exceeded"); ++mesh.normals; }
        } else if(tag=="vt") {
            Need(words.size()==2 || words.size()==3,"Expected two/three OBJ texture components");
            for(std::size_t i=0;i<words.size();++i) Number(words[i]);
            Need(mesh.uvs<MaxVertices,"OBJ UV limit exceeded"); ++mesh.uvs;
        } else if(tag=="f") {
            Need(words.size()==3,"Only triangular OBJ faces are supported");
            Need(mesh.triangles.size()<MaxTriangles,"OBJ triangle limit exceeded");
            Triangle t; t.color=color;
            for(int i=0;i<3;++i) t.c[i]=ParseCorner(words[i]);
            mesh.triangles.push_back(t);
        } else if(tag=="g" || tag=="usemtl") { color=Hash(line); }
        else Need(tag=="o" || tag=="s" || tag=="mtllib","Unsupported OBJ record");
    }
    Need(!in.bad(),"OBJ stream read failure");
    Need(!mesh.vertices.empty()&&!mesh.triangles.empty(),"OBJ has no triangle geometry");
    for(std::size_t i=0;i<mesh.triangles.size();++i) for(int j=0;j<3;++j) {
        const Corner& c=mesh.triangles[i].c[j];
        Need(c.vertex<mesh.vertices.size(),"OBJ position index out of range");
        Need(!c.hasUv||c.uv<mesh.uvs,"OBJ UV index out of range");
        Need(!c.hasNormal||c.normal<mesh.normals,"OBJ normal index out of range");
    }
    return mesh;
}
Mesh ReadObjFile(const std::string& path) {
    std::ifstream in(path.c_str(),std::ios::binary); Need(bool(in),"Cannot open OBJ input");
    in.seekg(0,std::ios::end); const std::streamoff bytes=in.tellg();
    Need(bytes>=0&&static_cast<unsigned long long>(bytes)<=MaxInputBytes,"OBJ file exceeds 256 MiB limit or is not seekable");
    in.seekg(0,std::ios::beg); return ReadObj(in);
}
struct Camera { V3 eye, forward, right, up; double nearPlane; };
Camera LookAt(V3 eye,V3 center,double nearPlane) {
    Camera c; c.eye=eye; c.forward=Unit(Sub(center,eye));
    c.right=Unit(Cross(c.forward,{0,0,1})); c.up=Unit(Cross(c.right,c.forward)); c.nearPlane=nearPlane; return c;
}
Camera FitCamera(const Mesh& mesh,int width,int height,double azimuth,double elevation) {
    V3 lo=mesh.vertices[0],hi=lo;
    for(std::size_t i=1;i<mesh.vertices.size();++i) {
        const V3 v=mesh.vertices[i]; lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};
        hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};
    }
    const V3 center=Mul(Add(lo,hi),0.5); const double radius=std::max(0.01,std::sqrt(Dot(Sub(hi,lo),Sub(hi,lo)))*0.5);
    const double halfFov=std::min(Pi/8.0,std::atan(std::tan(Pi/8.0)*double(width)/height));
    const double distance=radius/std::sin(halfFov)*1.08;
    const double az=azimuth*Pi/180.0,el=elevation*Pi/180.0;
    const V3 direction={std::cos(az)*std::cos(el),std::sin(az)*std::cos(el),std::sin(el)};
    return LookAt(Add(center,Mul(direction,distance)),center,std::max(0.00001,radius*0.001));
}
struct Image { int width,height; std::vector<unsigned char> rgb; std::vector<double> depth; std::size_t projected=0,degenerate=0,nearSkipped=0,fragments=0; };
struct Point { double x,y,invZ; };
double Edge(Point a,Point b,double x,double y) { return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x); }
Image Render(const Mesh& mesh,int width,int height,const Camera& camera) {
    Need(width>0&&height>0&&static_cast<unsigned long long>(width)*height<=8388608,"Image pixel limit exceeded");
    Image out; out.width=width;out.height=height; const std::size_t pixels=std::size_t(width)*height;
    out.rgb.resize(pixels*3);out.depth.assign(pixels,0);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const std::size_t p=(std::size_t(y)*width+x)*3;
        out.rgb[p]=static_cast<unsigned char>(21+13*y/height);out.rgb[p+1]=static_cast<unsigned char>(31+17*y/height);out.rgb[p+2]=static_cast<unsigned char>(43+18*y/height);
    }
    const double focal=0.5*height/std::tan(Pi/8.0); const V3 light=Unit({0.35,-0.4,0.85});
    const unsigned char palette[8][3]={{126,177,184},{202,158,112},{148,174,124},{174,151,190},{130,161,202},{200,133,121},{184,178,127},{142,187,157}};
    for(std::size_t i=0;i<mesh.triangles.size();++i) {
        const Triangle& triangle=mesh.triangles[i];Point p[3];V3 v[3];bool behind=false;
        for(int j=0;j<3;++j) {
            v[j]=mesh.vertices[triangle.c[j].vertex];const V3 d=Sub(v[j],camera.eye);const double z=Dot(d,camera.forward);
            if(z<=camera.nearPlane){behind=true;break;}
            p[j]={0.5*width+Dot(d,camera.right)/z*focal,0.5*height-Dot(d,camera.up)/z*focal,1.0/z};
        }
        // Auto-fit keeps the complete mesh in front. Manual near-plane clipping
        // is deliberately outside this small diagnostic's supported contract.
        if(behind){++out.nearSkipped;continue;}
        const double area=Edge(p[0],p[1],p[2].x,p[2].y);
        if(!std::isfinite(area)||std::abs(area)<1e-9){++out.degenerate;continue;}
        const V3 rawNormal=Cross(Sub(v[1],v[0]),Sub(v[2],v[0]));const double normalLength=std::sqrt(Dot(rawNormal,rawNormal));
        if(normalLength<1e-12){++out.degenerate;continue;}
        const double shade=0.25+0.75*std::abs(Dot(Mul(rawNormal,1.0/normalLength),light));
        const unsigned char* base=palette[triangle.color%8];unsigned char color[3];
        for(int k=0;k<3;++k)color[k]=static_cast<unsigned char>(std::min(255.0,base[k]*shade));
        const double minXd=std::max(0.0,std::floor(std::min(p[0].x,std::min(p[1].x,p[2].x))));
        const double minYd=std::max(0.0,std::floor(std::min(p[0].y,std::min(p[1].y,p[2].y))));
        const double maxXd=std::min(double(width-1),std::ceil(std::max(p[0].x,std::max(p[1].x,p[2].x))));
        const double maxYd=std::min(double(height-1),std::ceil(std::max(p[0].y,std::max(p[1].y,p[2].y))));
        if(minXd>maxXd||minYd>maxYd)continue;
        const int minX=int(minXd),minY=int(minYd),maxX=int(maxXd),maxY=int(maxYd);++out.projected;
        for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){
            const double px=x+0.5,py=y+0.5;
            const double a=Edge(p[1],p[2],px,py)/area,b=Edge(p[2],p[0],px,py)/area,c=1.0-a-b;
            if(a<-1e-10||b<-1e-10||c<-1e-10)continue;
            const double depth=a*p[0].invZ+b*p[1].invZ+c*p[2].invZ;const std::size_t n=std::size_t(y)*width+x;
            if(depth<=out.depth[n])continue;
            out.depth[n]=depth;for(int k=0;k<3;++k)out.rgb[n*3+k]=color[k];++out.fragments;
        }
    }
    return out;
}
void Put16(std::vector<unsigned char>& b,std::size_t p,std::uint16_t v){b[p]=static_cast<unsigned char>(v);b[p+1]=static_cast<unsigned char>(v>>8);}
void Put32(std::vector<unsigned char>& b,std::size_t p,std::uint32_t v){for(int i=0;i<4;++i)b[p+i]=static_cast<unsigned char>(v>>(8*i));}
std::vector<unsigned char> Bmp(const Image& image){
    const std::size_t stride=(std::size_t(image.width)*3+3)&~std::size_t(3),bytes=54+stride*image.height;
    Need(bytes<=std::numeric_limits<std::uint32_t>::max(),"BMP size overflow");
    std::vector<unsigned char> data(bytes,0);data[0]='B';data[1]='M';Put32(data,2,std::uint32_t(bytes));Put32(data,10,54);Put32(data,14,40);
    Put32(data,18,std::uint32_t(image.width));Put32(data,22,std::uint32_t(image.height));Put16(data,26,1);Put16(data,28,24);Put32(data,34,std::uint32_t(stride*image.height));
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){
        const std::size_t src=(std::size_t(y)*image.width+x)*3,dst=54+std::size_t(image.height-y-1)*stride+x*3;
        data[dst]=image.rgb[src+2];data[dst+1]=image.rgb[src+1];data[dst+2]=image.rgb[src];
    }
    return data;
}
void Save(const Image& image,const std::string& path){
    const std::vector<unsigned char> data=Bmp(image);
    // C11 exclusive creation is supported by the tested Windows CRT. Existing
    // regular files and symlinks are rejected; no diagnostic output is truncated.
    std::FILE* out=std::fopen(path.c_str(),"wbx");
    Need(out!=NULL,"Cannot exclusively create output BMP; existing paths are never overwritten");
    const bool written=std::fwrite(data.data(),1,data.size(),out)==data.size();
    const bool closed=std::fclose(out)==0;
    Need(written&&closed,"BMP write failed; incomplete output retained");
}
Mesh ParseText(const std::string& source){std::istringstream input(source);return ReadObj(input);}
void Reject(const std::string& text){bool rejected=false;try{ParseText(text);}catch(const std::exception&){rejected=true;}Need(rejected,"Malformed OBJ unexpectedly accepted");}
int SelfTest(const std::string& output){
    int passed=0;
    const Mesh valid=ParseText("v 0 0 0\nv 1 0 0\nv 0 0 1\nvt 0 0\nvt 1 0\nvt 0 1\ng synthetic\nf 1/1 2/2 3/3\n");
    Need(valid.vertices.size()==3&&valid.triangles.size()==1,"Valid OBJ read failed");++passed;
    Reject("v 0 1\nf 1 1 1\n");Reject("v 0 0 0\nf 1 1 2\n");++passed;
    Reject("v nan 0 0\nf 1 1 1\n");Reject("v 1e100 0 0\nf 1 1 1\n");++passed;
    Reject("v 0 0 0\nf 0 1 1\n");Reject("v 0 0 0\nf -1 1 1\n");Reject("v 0 0 0\nf 999999999999999999999 1 1\n");++passed;
    Reject("v 0 0 0\nf 1/1 1/1 1/1\n");Reject("v 0 0 0\nf 1//1 1//1 1//1\n");Reject("v 0 0 0\nf 1 1 1 1\n");++passed;
    Reject(std::string(17000,'a')+"\n");Reject(std::string("v 0 0 0")+std::string(1,'\0')+"ignored\nf 1 1 1\n");++passed;
    Mesh overlap=ParseText("v -1 0 -1\nv 1 0 -1\nv 0 0 1\nv -.8 -1 -.8\nv .8 -1 -.8\nv 0 -1 .8\nf 1 2 3\nf 4 5 6\n");
    overlap.triangles[0].color=1;overlap.triangles[1].color=4;const Camera c=LookAt({0,-5,0},{0,0,0},0.001);
    const Image first=Render(overlap,128,128,c);std::reverse(overlap.triangles.begin(),overlap.triangles.end());const Image second=Render(overlap,128,128,c);
    const std::size_t center=64*128+64;Need(first.rgb==second.rgb,"Z-buffer image depends on triangle order");
    Need(std::abs(first.depth[center]-0.25)<1e-12,"Nearest overlapping triangle was not visible");
    Need(std::count_if(first.depth.begin(),first.depth.end(),[](double d){return d>0;})>100,"Rasterizer did not cover expected pixels");++passed;
    Image tiny;tiny.width=3;tiny.height=2;tiny.rgb.assign(18,127);const std::vector<unsigned char> bmp=Bmp(tiny);
    Need(bmp.size()==78&&bmp[0]=='B'&&bmp[1]=='M'&&bmp[10]==54&&bmp[28]==24,"BMP header/row padding test failed");++passed;
    const Mesh cube=ParseText("v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\ng floor\nf 1 3 2\nf 1 4 3\ng top\nf 5 6 7\nf 5 7 8\ng front\nf 1 2 6\nf 1 6 5\ng right\nf 2 3 7\nf 2 7 6\ng back\nf 3 4 8\nf 3 8 7\ng left\nf 4 1 5\nf 4 5 8\n");
    const Image image=Render(cube,480,320,FitCamera(cube,480,320,45,30));
    Need(image.nearSkipped==0&&image.projected==12&&image.fragments>1000,"Auto-fit cube preview failed");Save(image,output);++passed;
    bool overwriteBlocked=false;try{Save(tiny,output);}catch(const std::exception&){overwriteBlocked=true;}
    std::ifstream saved(output.c_str(),std::ios::binary);const std::vector<unsigned char> actual((std::istreambuf_iterator<char>(saved)),std::istreambuf_iterator<char>());
    Need(overwriteBlocked&&actual==Bmp(image),"Existing output was overwritten or changed");++passed;
    std::cout<<"mesh-preview self-test: "<<passed<<" passed, 0 failed; synthetic BMP="<<output<<"\n";return 0;
}
void Usage(){std::cout<<"mesh-preview INPUT.obj OUTPUT.bmp [--width N --height N --azimuth DEG --elevation DEG]\nmesh-preview --self-test [SYNTHETIC.bmp]\nCPU flat shading, Z-up auto-fit, positive absolute OBJ triangle indices. No textures/gameplay.\n";}
}
int main(int argc,char** argv){
    try{
        if(argc>1&&std::string(argv[1])=="--self-test"){Need(argc<=3,"Too many self-test arguments");return SelfTest(argc==3?argv[2]:"mesh-preview-self-test.bmp");}
        if(argc==2&&std::string(argv[1])=="--help"){Usage();return 0;}
        if(argc<3){Usage();return 2;}
        const std::string input=argv[1],output=argv[2];Need(input!=output,"Input and output paths must differ");
        int width=1280,height=720;double azimuth=45,elevation=30;
        for(int i=3;i<argc;i+=2){Need(i+1<argc,"Missing option value");const std::string name=argv[i];const double value=Number(argv[i+1]);
            if(name=="--width"||name=="--height"){Need(value>=64&&value<=4096&&std::floor(value)==value,"Image dimensions must be integers from 64 to 4096");if(name=="--width")width=int(value);else height=int(value);}
            else if(name=="--azimuth"){Need(std::abs(value)<=36000,"Azimuth out of range");azimuth=value;}
            else if(name=="--elevation"){Need(value>=-85&&value<=85,"Elevation must be between -85 and 85 degrees");elevation=value;}
            else throw std::runtime_error("Unknown option");
        }
        const Mesh mesh=ReadObjFile(input);const Image image=Render(mesh,width,height,FitCamera(mesh,width,height,azimuth,elevation));Save(image,output);
        const std::size_t visible=std::count_if(image.depth.begin(),image.depth.end(),[](double d){return d>0;});
        std::cout<<"vertices="<<mesh.vertices.size()<<" triangles="<<mesh.triangles.size()<<" projected="<<image.projected<<" degenerate="<<image.degenerate<<" near-skipped="<<image.nearSkipped<<" covered-pixels="<<visible<<" output="<<output<<"\n";
        Need(visible>0,"Preview has no visible triangles");return 0;
    }catch(const std::exception& e){std::cerr<<"mesh-preview: "<<e.what()<<"\n";return 1;}
}
