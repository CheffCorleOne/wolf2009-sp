// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceSoundDecodeReceipt.h"
#include "SourceMp3Strict.h"
#include "idlib/precompiled.h"
#include "sound/snd_local.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

// Core QA entry only. The host stages GenerateFixtures.mjs outputs and starts
// its ordinary encoded-MP3/headless audio profile before calling this function.
// It neither changes production cvars nor starts an emitter.
namespace {
int checks=0;
void Need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void NeedReason(bool value,const idStr& why){Need(value,why.IsEmpty()?"native MP3 receipt call failed":why.c_str());}
struct CloseFile {void operator()(idFile* file)const{if(file)fileSystem->CloseFile(file);}};
std::vector<unsigned char> ReadFixture(const char* path){
    std::unique_ptr<idFile,CloseFile> file(fileSystem->OpenFileRead(path,false));
    Need(file.get()!=NULL&&file->Length()>0&&file->Length()<=4096,"authored receipt fixture missing/budget");
    std::vector<unsigned char> out(std::size_t(file->Length()));
    Need(file->Read(out.data(),int(out.size()))==int(out.size()),"authored receipt fixture short read");return out;
}
}
int Wolf2009_CheckSourceSoundDecodeReceiptQA(const char* valid,const char* truncated,const char* error){
    checks=0;
    try {
        Need(valid&&truncated&&error&&soundSystemLocal.soundCache,"native receipt QA requires initialized native sound cache");
        auto bytes=ReadFixture(valid);const auto authored=bytes;
        Need(bytes.size()==1668,"native receipt QA requires the authored four-frame fixture");
        idSoundSample* sample=soundSystemLocal.soundCache->FindSound(valid,false);
        Need(sample&&!sample->defaultSound&&!sample->purged&&!sample->hardwareBuffer&&sample->nonCacheData,
             "native receipt QA host must provide encoded MP3 cache profile");
        const auto nativeName=std::string(sample->name.c_str());
        const std::vector<unsigned char> cached(sample->nonCacheData,sample->nonCacheData+sample->objectMemSize);
        const auto originalFormat=sample->objectInfo.wFormatTag;const int originalCount=sample->objectSize;
        WolfSourceMp3ReceiptRequest request;request.exactNativeSampleName=nativeName;request.expectedEncoded=&bytes;
        request.expectedFingerprint=wolfSourceMp3Strict::Fingerprint(bytes.data(),bytes.size());
        WolfSourceMp3DecodeReceipt receipt;idStr reason;
        bool ok=Wolf2009_InspectSourceMp3Cache(*soundSystemLocal.soundCache,request,receipt,reason);NeedReason(ok,reason);
        Need(receipt.Code()==WolfSourceMp3ReceiptCode::Verified&&receipt.CompleteFrames()==4&&receipt.DecodedSamples()==4608&&
             receipt.NativeSampleName()==nativeName&&receipt.EncodedSnapshot()==authored&&bytes==authored,
             "native receipt lost actual PCM/frame/source identity");
        Need(sample->objectInfo.wFormatTag==originalFormat&&sample->objectSize==originalCount&&
             sample->name==nativeName.c_str()&&!sample->defaultSound&&!sample->purged&&!sample->hardwareBuffer&&
             std::vector<unsigned char>(sample->nonCacheData,sample->nonCacheData+sample->objectMemSize)==cached,
             "receipt mutated native cache or its bytes");
        bytes.back()^=1;
        Need(receipt.EncodedSnapshot()==authored,"receipt snapshot borrowed mutable expected bytes");
        ok=Wolf2009_InspectSourceMp3Cache(*soundSystemLocal.soundCache,request,receipt,reason);
        Need(!ok&&receipt.Code()==WolfSourceMp3ReceiptCode::IdentityMismatch&&receipt.EncodedSnapshot().empty()&&receipt.DecodedSamples()==0,
             "failed identity check retained a positive receipt");
        bytes=authored;
        ok=Wolf2009_InspectSourceMp3Cache(*soundSystemLocal.soundCache,request,receipt,reason);NeedReason(ok,reason);
        Need(receipt.DecodedSamples()==4608,"subsequent native receipt did not recover");
        for(const char* negative:{truncated,error}){
            const auto wrong=ReadFixture(negative);auto* rejected=soundSystemLocal.soundCache->FindSound(negative,false);
            Need(rejected!=NULL,"native receipt negative sample missing cache object");
            auto bad=request;bad.exactNativeSampleName=rejected->name.c_str();bad.expectedEncoded=&wrong;
            bad.expectedFingerprint=wolfSourceMp3Strict::Fingerprint(wrong.data(),wrong.size());
            ok=Wolf2009_InspectSourceMp3Cache(*soundSystemLocal.soundCache,bad,receipt,reason);
            Need(!ok&&receipt.Code()!=WolfSourceMp3ReceiptCode::Verified&&receipt.EncodedSnapshot().empty()&&!reason.IsEmpty(),
                 "authored truncated/error native sample produced a verified receipt");
        }
        printf("WOLF2009_SOURCE_MP3_RECEIPT_NATIVE_OK checks=%d authored_frames=4 actual_pcm=4608 immutable_cache=1 bank_ready=0 playback=0\n",checks);
        return 1;
    }catch(const std::exception& e){fprintf(stderr,"WOLF2009_SOURCE_MP3_RECEIPT_NATIVE_FAIL checks=%d reason=%s\n",checks,e.what());return 0;}
}
