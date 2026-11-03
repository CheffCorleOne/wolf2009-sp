// SPDX-License-Identifier: GPL-3.0-or-later
// JNI transport for the pinned GPL engine. No renderer, physics or frame loop.
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <pthread.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "sys_android.h"
extern "C" {
#include "q3eeventqueue.h"
#include "q3eenv.h"
JavaVM* jVM = NULL; // Required by the unchanged upstream JNI TLS helper.
}

namespace {
const char* const tag = "Wolf2009Engine";
Q3E_Interface_t api = {};
void* engineLibrary = NULL;
jobject callback = NULL;
jmethodID cbState, cbGrab, cbSmooth, cbCopy, cbClipboard, cbKeyboardOpen,
    cbKeyboardClose, cbUrl, cbFinish, cbCursor, cbToast, cbDialog,
    cbAudioInit, cbAudioWrite, cbAudioShutdown;
std::atomic<bool> started(false), apiReady(false), inputSeen(false), stopping(false), clearPending(false);
std::atomic<int> state(0);
std::atomic<unsigned long> keyCount(0), analogCount(0), motionCount(0), pullCount(0), surfaceCount(0);
std::mutex startMutex, surfaceMutex;
ANativeWindow* heldWindow = NULL; // Own JNI reference, separate from engine's acquire.
std::string dataRoot, saveRoot, libraryRoot, gamePath;
std::vector<std::string> arguments;
std::vector<char*> argv;
std::set<int> heldKeys; // Accessed exclusively by the engine's input-pull thread.
std::atomic<bool> wantView(false);
std::mutex viewMutex;
double viewSnapshot[5]={};

void Log(const char* text) { __android_log_write(ANDROID_LOG_INFO, tag, text ? text : ""); }
JNIEnv* Env() { return Q3E_GetEnv(); }
bool JavaOK(JNIEnv* env) {
    if (!env || !env->ExceptionCheck()) return env != NULL;
    env->ExceptionDescribe(); env->ExceptionClear();
    __android_log_write(ANDROID_LOG_ERROR, tag, "ENGINE_HOST_JAVA_CALLBACK_FAILED");
    return false;
}
std::string UTF(JNIEnv* env, jstring text) {
    if (!text) return std::string();
    const char* bytes = env->GetStringUTFChars(text, NULL);
    if (!bytes) return std::string();
    std::string value(bytes); env->ReleaseStringUTFChars(text, bytes); return value;
}
void Void(jmethodID method) { JNIEnv* env=Env(); if(env){env->CallVoidMethod(callback,method);JavaOK(env);} }
void Bool(jmethodID method, int value) { JNIEnv* env=Env(); if(env){env->CallVoidMethod(callback,method,jboolean(value!=0));JavaOK(env);} }
void String(jmethodID method, const char* value) {
    JNIEnv* env=Env(); if(!env)return;
    jstring text=env->NewStringUTF(value?value:"");
    if(text){env->CallVoidMethod(callback,method,text);env->DeleteLocalRef(text);} JavaOK(env);
}
void SendKey(int down,int key,int chr) {
    if(down)heldKeys.insert(key);else heldKeys.erase(key);
    api.keyEvent(down,key,chr); ++keyCount;
}
void SendAnalog(int enabled,float x,float y) { api.analogEvent(enabled,x,y); ++analogCount; }
void SendMotion(float x,float y) { api.motionEvent(x,y); ++motionCount; }
void SendMouse(float x,float y,int relative) { if(api.mouseEvent)api.mouseEvent(x,y,relative); }
void ReleaseInput() {
    for(std::set<int>::const_iterator i=heldKeys.begin();i!=heldKeys.end();++i)api.keyEvent(0,*i,0);
    heldKeys.clear(); api.analogEvent(0,0,0); Q3E_PullEvent(0);
}
int PullInput(int count) {
    inputSeen.store(true); ++pullCount;
    if(clearPending.exchange(false) || stopping.load())ReleaseInput();
    if(stopping.load()) { api.requestThreadQuit(); return 0; }
    return Q3E_PullEvent(count);
}
void SetState(int value) {
    state.store(value);
    __android_log_print(ANDROID_LOG_INFO,tag,"ENGINE_HOST_STATE value=%d game=%d",value,(value&2)!=0);
    JNIEnv* env=Env();if(env){env->CallVoidMethod(callback,cbState,value);JavaOK(env);}
}
void Grab(int value){Bool(cbGrab,value);}
void Smooth(int value){Bool(cbSmooth,value);}
void Attach(){(void)Env();}
void Copy(const char* text){String(cbCopy,text);}
char* Clipboard(){
    JNIEnv* env=Env();if(!env)return NULL;
    jstring result=static_cast<jstring>(env->CallObjectMethod(callback,cbClipboard));
    if(!JavaOK(env)||!result)return NULL;
    const std::string text=UTF(env,result);env->DeleteLocalRef(result);return strdup(text.c_str());
}
void KeyboardOpen(){Void(cbKeyboardOpen);}
void KeyboardClose(){Void(cbKeyboardClose);}
void URL(const char* text){String(cbUrl,text);}
void Finish(){Void(cbFinish);}
void Cursor(int value){Bool(cbCursor,value);}
void Toast(const char* text){String(cbToast,text);}
int Dialog(const char* title,const char* text,int count,const char* buttons[]) {
    if(count<0||count>16)return -1;
    JNIEnv* env=Env();if(!env)return -1;
    jclass strings=env->FindClass("java/lang/String");
    if(!strings)return JavaOK(env),-1;
    jobjectArray labels=env->NewObjectArray(count,strings,NULL);env->DeleteLocalRef(strings);
    if(!labels)return JavaOK(env),-1;
    for(int i=0;i<count;++i){jstring s=env->NewStringUTF(buttons[i]?buttons[i]:"");env->SetObjectArrayElement(labels,i,s);env->DeleteLocalRef(s);}
    jstring heading=env->NewStringUTF(title?title:"");jstring body=env->NewStringUTF(text?text:"");
    const int result=env->CallIntMethod(callback,cbDialog,heading,body,labels);
    env->DeleteLocalRef(heading);env->DeleteLocalRef(body);env->DeleteLocalRef(labels);
    return JavaOK(env)?result:-1;
}
void AudioInit(void* buffer,int size){
    if(!buffer||size<=0||size>(16<<20))return;
    JNIEnv* env=Env();if(!env)return;
    jobject bytes=env->NewDirectByteBuffer(buffer,size);
    if(bytes){env->CallVoidMethod(callback,cbAudioInit,bytes,size);env->DeleteLocalRef(bytes);}JavaOK(env);
}
int AudioWrite(int offset,int size){JNIEnv* env=Env();if(!env)return 0;int n=env->CallIntMethod(callback,cbAudioWrite,offset,size);return JavaOK(env)?n:0;}
void AudioShutdown(){Void(cbAudioShutdown);}
FILE* Temporary(){
    std::string name=saveRoot+"/engine-tmp-XXXXXX";
    std::vector<char> path(name.begin(),name.end());path.push_back(0);
    const int fd=mkstemp(path.data());if(fd<0)return NULL;
    unlink(path.data());FILE* f=fdopen(fd,"w+b");if(!f)close(fd);return f;
}
void EngineLog(const char* label,const char* text,...){
    // sys_android passes an already formatted string. Never reinterpret '%'.
    __android_log_write(ANDROID_LOG_INFO,label?label:tag,text?text:"");
    // Optional APK QA reads the stock getviewpos command's real idPlayer view.
    // It never writes game state or calls a second GetGameAPI/RunFrame.
    if(text&&wantView.load()){
        float x,y,z,yaw;char tail;
        const char* candidate=strchr(text,'(');
        if(candidate&&sscanf(candidate," (%f %f %f) %f %c",&x,&y,&z,&yaw,&tail)==4&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::isfinite(yaw)){
            std::lock_guard<std::mutex> lock(viewMutex);++viewSnapshot[0];
            viewSnapshot[1]=x;viewSnapshot[2]=y;viewSnapshot[3]=z;viewSnapshot[4]=yaw;wantView.store(false);
            __android_log_print(ANDROID_LOG_INFO,tag,"ENGINE_HOST_PLAYER_VIEW sequence=%.0f x=%g y=%g z=%g yaw=%g",viewSnapshot[0],x,y,z,yaw);
        }
    }
}
struct OutputPipe { int read;FILE* file; };
void* ReadOutput(void* opaque){
    OutputPipe* pipe=static_cast<OutputPipe*>(opaque);
    char buffer[4096];std::string line;
    for(;;){
        ssize_t bytes=read(pipe->read,buffer,sizeof(buffer));if(bytes<=0)break;
        if(pipe->file){fwrite(buffer,1,static_cast<std::size_t>(bytes),pipe->file);fflush(pipe->file);}
        for(ssize_t i=0;i<bytes;++i){
            if(buffer[i]=='\n'||line.size()>=4000){EngineLog("Wolf2009Native",line.c_str());line.clear();}
            if(buffer[i]!='\n'&&buffer[i]!='\r')line.push_back(buffer[i]);
        }
    }
    if(!line.empty())EngineLog("Wolf2009Native",line.c_str());
    if(pipe->file)fclose(pipe->file);close(pipe->read);delete pipe;return NULL;
}
bool CaptureOutput(){
    // Stock Sys_Printf uses libc stdout rather than Q3E Log_Print. Capture only
    // this isolated :engine process, retaining normal native diagnostic output.
    int descriptors[2];if(pipe(descriptors)!=0)return false;
    OutputPipe* output=new OutputPipe{descriptors[0],fopen((saveRoot+"/engine.log").c_str(),"ab")};
    pthread_attr_t attrs;pthread_attr_init(&attrs);pthread_attr_setstacksize(&attrs,256u<<10);pthread_attr_setdetachstate(&attrs,PTHREAD_CREATE_DETACHED);
    pthread_t reader;int result=pthread_create(&reader,&attrs,ReadOutput,output);pthread_attr_destroy(&attrs);
    if(result){if(output->file)fclose(output->file);close(descriptors[0]);close(descriptors[1]);delete output;return false;}
    fflush(stdout);fflush(stderr);
    const bool attached=dup2(descriptors[1],STDOUT_FILENO)>=0&&dup2(descriptors[1],STDERR_FILENO)>=0;
    close(descriptors[1]);setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);return attached;
}
bool BindCallbacks(JNIEnv* env,jobject obj){
    jclass type=env->GetObjectClass(obj);if(!type)return false;
#define METHOD(field,name,signature) field=env->GetMethodID(type,name,signature);if(!field){env->DeleteLocalRef(type);return false;}
    METHOD(cbState,"state","(I)V")
    METHOD(cbGrab,"grabMouse","(Z)V") METHOD(cbSmooth,"smoothJoystick","(Z)V")
    METHOD(cbCopy,"copyClipboard","(Ljava/lang/String;)V") METHOD(cbClipboard,"clipboard","()Ljava/lang/String;")
    METHOD(cbKeyboardOpen,"openKeyboard","()V") METHOD(cbKeyboardClose,"closeKeyboard","()V")
    METHOD(cbUrl,"openUrl","(Ljava/lang/String;)V") METHOD(cbFinish,"finish","()V") METHOD(cbCursor,"showCursor","(Z)V")
    METHOD(cbToast,"toast","(Ljava/lang/String;)V") METHOD(cbDialog,"dialog","(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)I")
    METHOD(cbAudioInit,"audioInit","(Ljava/nio/ByteBuffer;I)V") METHOD(cbAudioWrite,"audioWrite","(II)I") METHOD(cbAudioShutdown,"audioShutdown","()V")
#undef METHOD
    env->DeleteLocalRef(type);callback=env->NewGlobalRef(obj);return callback!=NULL;
}
bool Regular(const std::string& name){struct stat info;return stat(name.c_str(),&info)==0&&S_ISREG(info.st_mode)&&info.st_size>0;}
bool Directory(const std::string& name){struct stat info;return stat(name.c_str(),&info)==0&&S_ISDIR(info.st_mode);}
void Arg(const char* name,const std::string& value){arguments.push_back("+set");arguments.push_back(name);arguments.push_back(value);}
void* Main(void*){
    (void)Env();
    if(!stopping.load()){
        Log("ENGINE_HOST_MAIN enter=1 normal_q3e_main=1");
        const int result=api.main(static_cast<int>(argv.size()-1),argv.data());
        __android_log_print(ANDROID_LOG_INFO,tag,"ENGINE_HOST_MAIN returned=%d",result);
    }
    stopping.store(true);Finish();return NULL;
}
jstring Error(JNIEnv* env,const char* message){__android_log_write(ANDROID_LOG_ERROR,tag,message);return env->NewStringUTF(message);}
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm,void*){jVM=vm;Q3E_InitEnvKey();return JNI_VERSION_1_6;}

extern "C" JNIEXPORT jstring JNICALL Java_org_wolf2009_engine_EngineNative_start(
    JNIEnv* env,jclass,jstring libraries,jstring data,jstring save,jobject surface,jint width,jint height,jint rate,jobject cb){
    std::lock_guard<std::mutex> lock(startMutex);
    if(started.load())return Error(env,"Движок уже запущен. Для нового запуска откройте приложение заново.");
    if(!surface||!cb||width<1||height<1||width>16384||height>16384)return Error(env,"Некорректная Android Surface");
    libraryRoot=UTF(env,libraries);dataRoot=UTF(env,data);saveRoot=UTF(env,save);
    if(libraryRoot.empty()||libraryRoot[0]!='/'||dataRoot.empty()||dataRoot[0]!='/'||saveRoot.empty()||saveRoot[0]!='/')return Error(env,"Нужны приватные абсолютные пути приложения");
    gamePath=libraryRoot+"/libgame.so";
    if(!Regular(libraryRoot+"/libdoom3.so")||!Regular(gamePath))return Error(env,"APK не извлёк engine/game библиотеки. Требуется extractNativeLibs=true.");
    if(!Directory(saveRoot)||!Regular(dataRoot+"/base/fixtures/wolf-materials.bin")||!Regular(dataRoot+"/base/maps/game/empty/empty.mpk"))return Error(env,"Сначала импортируйте assets.pk4 и empty.mpk своей игры");
    engineLibrary=dlopen((libraryRoot+"/libdoom3.so").c_str(),RTLD_NOW|RTLD_GLOBAL);
    if(!engineLibrary){const char* e=dlerror();return Error(env,e?e:"dlopen engine failed");}
    typedef void (*GetAPI)(void*);
    GetAPI get=reinterpret_cast<GetAPI>(dlsym(engineLibrary,"GetIDTechAPI"));
    if(!get)return Error(env,"Engine не экспортирует GetIDTechAPI");
    get(&api);
    if(!api.main||!api.setCallbacks||!api.setInitialContext||!api.pause||!api.resume||!api.setGLContext||!api.requestThreadQuit||!api.keyEvent||!api.analogEvent||!api.motionEvent)return Error(env,"Неполный pinned Q3E API");
    if(!BindCallbacks(env,cb)){JavaOK(env);return Error(env,"Не совпадают Java callbacks движка");}
    heldWindow=ANativeWindow_fromSurface(env,surface);
    if(!heldWindow)return Error(env,"Android не вернул native window");
    if(chdir(dataRoot.c_str())!=0)return Error(env,"Не удалось открыть приватный runtime каталог");
    Q3E_Callback_t callbacks={};
    callbacks.AudioTrack_init=AudioInit;callbacks.AudioTrack_write=AudioWrite;callbacks.AudioTrack_shutdown=AudioShutdown;
    callbacks.Input_grabMouse=Grab;callbacks.Input_pullEvent=PullInput;callbacks.Input_setupSmoothJoystick=Smooth;
    callbacks.Sys_attachThread=Attach;callbacks.Sys_tmpfile=Temporary;callbacks.Sys_copyToClipboard=Copy;callbacks.Sys_getClipboardText=Clipboard;
    callbacks.Sys_openKeyboard=KeyboardOpen;callbacks.Sys_closeKeyboard=KeyboardClose;callbacks.Sys_openURL=URL;callbacks.Sys_exitFinish=Finish;callbacks.Sys_showCursor=Cursor;
    callbacks.Gui_ShowToast=Toast;callbacks.Gui_openDialog=Dialog;callbacks.set_state=SetState;callbacks.Log_Print=EngineLog;
    api.setCallbacks(&callbacks);
    Q3E_InitialContext_t context={};
    context.openGL_format=0x8888;context.openGL_depth=24;context.openGL_version=0x30000;
    context.nativeLibraryDir=libraryRoot.c_str();context.gameDataDir=dataRoot.c_str();context.appHomeDir=saveRoot.c_str();
    context.noHandleSignals=1;context.refreshRate=rate>0?rate:60;context.consoleMaxHeightFrac=75;context.usingExternalLibs=1;
    context.window=heldWindow;context.width=width;context.height=height;
    api.setInitialContext(&context);
    Q3E_InitEventManager(SendKey,SendMotion,SendAnalog,SendMouse);
    arguments.push_back("wolf2009");
    Arg("net_serverDedicated","0");Arg("net_ip","127.0.0.1");Arg("net_port","0");
    Arg("fs_basepath",dataRoot);Arg("fs_savepath",saveRoot);Arg("fs_cdpath",dataRoot);Arg("fs_devpath",saveRoot);Arg("fs_restrict","0");Arg("fs_game","");
    Arg("harm_fs_gameLibPath",gamePath);Arg("harm_r_openglVersion","GLES3.0");Arg("r_multithread","0");
    Arg("harm_com_consoleHistory","0");Arg("com_allowConsole","1");Arg("in_tty","0");Arg("s_noSound","1");
    arguments.push_back("+wolf2009Materials");arguments.push_back("fixtures/wolf-materials.bin");
    arguments.push_back("+wolf2009Mount");arguments.push_back("main");arguments.push_back("maps/game/empty/empty.mpk");
    arguments.push_back("+devmap");arguments.push_back("wolf2009/main/PROC/maps/game/empty/empty");
    for(std::size_t i=0;i<arguments.size();++i)argv.push_back(&arguments[i][0]);argv.push_back(NULL);
    if(!CaptureOutput())return Error(env,"Не удалось открыть журнал native engine");
    started.store(true);apiReady.store(true);
    pthread_attr_t attrs;pthread_attr_init(&attrs);pthread_attr_setstacksize(&attrs,8u<<20);pthread_attr_setdetachstate(&attrs,PTHREAD_CREATE_DETACHED);
    pthread_t thread;const int result=pthread_create(&thread,&attrs,Main,NULL);pthread_attr_destroy(&attrs);
    if(result){stopping.store(true);return Error(env,"Не удалось создать game thread");}
    Log("ENGINE_HOST_STARTED callbacks=19 normal_entry=1 source_map=empty sound_disabled=1");
    return env->NewStringUTF("");
}

extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_surface(JNIEnv* env,jclass,jobject surface){
    if(!apiReady.load()||stopping.load())return;
    std::lock_guard<std::mutex> lock(surfaceMutex);
    ANativeWindow* next=surface?ANativeWindow_fromSurface(env,surface):NULL;
    api.setGLContext(next); // Native handshake acknowledges engine acquire/detach.
    ANativeWindow* old=heldWindow;heldWindow=next;if(old)ANativeWindow_release(old);
    ++surfaceCount;
    __android_log_print(ANDROID_LOG_INFO,tag,"ENGINE_HOST_SURFACE present=%d changes=%lu",next!=NULL,surfaceCount.load());
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_pause(JNIEnv*,jclass){if(apiReady.load()&&!stopping.load()){clearPending.store(true);api.pause();Log("ENGINE_HOST_PAUSE");}}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_resume(JNIEnv*,jclass){if(apiReady.load()&&!stopping.load()){api.resume();Log("ENGINE_HOST_RESUME");}}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_requestQuit(JNIEnv*,jclass){
    stopping.store(true);clearPending.store(true);Log("ENGINE_HOST_QUIT_REQUEST");
    if(apiReady.load()&&inputSeen.load())api.requestThreadQuit();
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_clearInput(JNIEnv*,jclass){clearPending.store(true);}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_key(JNIEnv*,jclass,jboolean down,jint key,jint chr){if(apiReady.load()&&!stopping.load()&&key>0&&key<256&&chr>=0&&chr<=0x10ffff)Q3E_PushKeyEvent(down,key,chr);}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_analog(JNIEnv*,jclass,jboolean enabled,jfloat x,jfloat y){
    if(apiReady.load()&&!stopping.load()&&std::isfinite(x)&&std::isfinite(y)&&std::fabs(x)<=1&&std::fabs(y)<=1)Q3E_PushAnalogEvent(enabled,x,y);
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_motion(JNIEnv*,jclass,jfloat x,jfloat y){
    if(apiReady.load()&&!stopping.load()&&std::isfinite(x)&&std::isfinite(y)&&std::fabs(x)<=8192&&std::fabs(y)<=8192)Q3E_PushMotionEvent(x,y);
}
extern "C" JNIEXPORT jlongArray JNICALL Java_org_wolf2009_engine_EngineNative_stats(JNIEnv* env,jclass){
    const jlong values[]={state.load(),jlong(keyCount.load()),jlong(analogCount.load()),jlong(motionCount.load()),jlong(pullCount.load()),jlong(surfaceCount.load()),started.load(),stopping.load()};
    jlongArray result=env->NewLongArray(8);if(result)env->SetLongArrayRegion(result,0,8,values);return result;
}
extern "C" JNIEXPORT void JNICALL Java_org_wolf2009_engine_EngineNative_queryView(JNIEnv*,jclass){
    if(!apiReady.load()||stopping.load()||!(state.load()&2)||(state.load()&16))return;
    wantView.store(true);
    Q3E_PushKeyEvent(1,96,0);Q3E_PushKeyEvent(0,96,0);
    for(const char* p="getviewpos";*p;++p){Q3E_PushKeyEvent(1,*p,*p);Q3E_PushKeyEvent(0,*p,0);}
    Q3E_PushKeyEvent(1,13,0);Q3E_PushKeyEvent(0,13,0);
    Q3E_PushKeyEvent(1,96,0);Q3E_PushKeyEvent(0,96,0);
}
extern "C" JNIEXPORT jdoubleArray JNICALL Java_org_wolf2009_engine_EngineNative_view(JNIEnv* env,jclass){
    std::lock_guard<std::mutex> lock(viewMutex);jdoubleArray result=env->NewDoubleArray(5);if(result)env->SetDoubleArrayRegion(result,0,5,viewSnapshot);return result;
}
