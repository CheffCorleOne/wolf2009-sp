// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceMp3Strict.h"
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#define MP3D_ALLOW_MONO_STEREO_TRANSITION 0
#include "minimp3_ex.h"
#include <iostream>

namespace {
using namespace wolfSourceMp3Strict;
int checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F> void Reject(WolfSourceMp3ReceiptCode code,F work){
    bool rejected=false;try{work();}catch(const Error& error){rejected=error.code==code;}
    Check(rejected,"strict MP3 refusal code differs");
}
// Authored MPEG1 LayerIII silence: 128kbps, 44100Hz, mono, unprotected,
// zero side-info/main-data. No original game bytes or encoder are needed.
std::vector<unsigned char> Silence(int frames=4){
    std::vector<unsigned char> bytes(std::size_t(frames)*417,0);
    for(int i=0;i<frames;++i){auto* p=bytes.data()+i*417;p[0]=0xff;p[1]=0xfb;p[2]=0x90;p[3]=0xc0;}
    return bytes;
}
struct Frames {
    mp3dec_t codec;
    Frames(){mp3dec_init(&codec);}
    Frame operator()(const unsigned char* bytes,int count){
        mp3dec_frame_info_t info={};Frame out;
        out.samples=mp3dec_decode_frame(&codec,bytes,count,NULL,&info);
        out.bytes=info.frame_bytes;out.rate=info.hz;out.channels=info.channels;out.layer=info.layer;return out;
    }
};
struct PcmReader {
    mp3dec_ex_t codec={};
    int opened=0,offset=0;std::uint64_t nonzero=0;
    explicit PcmReader(const std::vector<unsigned char>& bytes){
        opened=mp3dec_ex_open_buf(&codec,bytes.data(),bytes.size(),MP3D_SEEK_TO_SAMPLE|MP3D_ALLOW_MONO_STEREO_TRANSITION);
    }
    ~PcmReader(){mp3dec_ex_close(&codec);}
    Read operator()(int at,int count,float* output){
        Read out;
        if(opened<0){out.failed=true;out.error=opened;return out;}
        Check(at==offset,"strict decode did not read sequentially");
        short pcm[2048]={};
        const std::size_t actual=mp3dec_ex_read(&codec,pcm,std::size_t(count));
        out.actual=int(actual);out.error=codec.last_error;out.failed=out.error!=0;
        out.eof=actual==0&&!out.failed;
        for(std::size_t i=0;i<actual;++i){output[i]=pcm[i];nonzero+=pcm[i]!=0;}
        offset+=out.actual;return out;
    }
};
Coverage Cover(const std::vector<unsigned char>& bytes,std::uint64_t max=100000){Frames frames;return Framing(bytes.data(),bytes.size(),max,frames);}
WolfSourceMp3ReceiptRequest RequestFor(const std::vector<unsigned char>& bytes){
    WolfSourceMp3ReceiptRequest out;out.exactNativeSampleName="authored-receipt.mp3";
    out.expectedEncoded=&bytes;out.expectedFingerprint=Fingerprint(bytes.data(),bytes.size());return out;
}
void PositiveAndIdentity(){
    const auto bytes=Silence();const auto copy=bytes;auto request=RequestFor(bytes);Request(request);
    Identity(request,request.exactNativeSampleName.c_str(),bytes.data(),bytes.size());
    const auto coverage=Cover(bytes);Check(coverage.frames==4&&coverage.samples==4608,"authored frame coverage differs");
    PcmReader reader(bytes);Check(reader.opened==0,"pinned native MP3 open failed on authored silence");
    const auto result=DecodeAll(coverage.samples,100000,reader);
    Check(result.actual==4608&&result.normalEof&&reader.nonzero==0,"actual complete PCM/silence/EOF differs");
    Check(bytes==copy,"strict native codec changed source bytes");
    auto other=bytes;other.back()=1;
    Reject(WolfSourceMp3ReceiptCode::IdentityMismatch,[&]{Identity(request,request.exactNativeSampleName.c_str(),other.data(),other.size());});
    Reject(WolfSourceMp3ReceiptCode::IdentityMismatch,[&]{Identity(request,"other.mp3",bytes.data(),bytes.size());});
    Reject(WolfSourceMp3ReceiptCode::IdentityMismatch,[&]{Identity(request,request.exactNativeSampleName.c_str(),bytes.data(),bytes.size()-1);});
    auto wrong=request;wrong.expectedFingerprint^=1;
    Reject(WolfSourceMp3ReceiptCode::IdentityMismatch,[&]{Request(wrong);});
    wrong=request;wrong.expectedEncoded=NULL;
    Reject(WolfSourceMp3ReceiptCode::InvalidRequest,[&]{Request(wrong);});
    wrong=request;wrong.maximumEncodedBytes=bytes.size()-1;
    Reject(WolfSourceMp3ReceiptCode::LimitExceeded,[&]{Request(wrong);});
    wrong=request;wrong.maximumDecodedSamples=std::uint64_t(INT_MAX);
    Reject(WolfSourceMp3ReceiptCode::LimitExceeded,[&]{Request(wrong);});
    WolfSourceMp3DecodeReceipt empty;
    Check(empty.Code()==WolfSourceMp3ReceiptCode::Unverified&&empty.EncodedSnapshot().empty()&&empty.DecodedSamples()==0,
          "default receipt fabricates verification");
}
void FramingRefusals(){
    auto shortPayload=Silence();shortPayload.pop_back();
    Reject(WolfSourceMp3ReceiptCode::InvalidFraming,[&]{Cover(shortPayload);});
    auto trailing=Silence();trailing.push_back(0);
    Reject(WolfSourceMp3ReceiptCode::InvalidFraming,[&]{Cover(trailing);});
    auto leading=Silence();leading.insert(leading.begin(),{'I','D','3',0});
    Reject(WolfSourceMp3ReceiptCode::InvalidFraming,[&]{Cover(leading);});
    for(int mutation=0;mutation<5;++mutation){
        auto bad=Silence();
        if(mutation==0)bad[417]=0;
        if(mutation==1)bad[3]=0; // stereo profile
        if(mutation==2)bad[2]=0x94; // 48kHz
        if(mutation==3)bad[1]=0xfd; // LayerII
        if(mutation==4)bad[2]=0; // free-format
        Reject(WolfSourceMp3ReceiptCode::InvalidFraming,[&]{Cover(bad);});
    }
    const auto valid=Silence();
    Reject(WolfSourceMp3ReceiptCode::LimitExceeded,[&]{Cover(valid,4607);});
    Frames reader;
    Reject(WolfSourceMp3ReceiptCode::LimitExceeded,[&]{Framing(NULL,0,100,reader);});
}
void ActualReadRefusals(){
    const auto bytes=Silence();
    {PcmReader reader(bytes);Reject(WolfSourceMp3ReceiptCode::ShortDecode,[&]{DecodeAll(4609,100000,reader);});}
    {PcmReader reader(bytes);Reject(WolfSourceMp3ReceiptCode::ShortDecode,[&]{DecodeAll(4607,100000,reader);});}
    struct Fault {
        PcmReader codec;int calls=0;
        explicit Fault(const std::vector<unsigned char>& b):codec(b){}
        Read operator()(int offset,int count,float* out){
            auto result=codec(offset,count,out);
            if(++calls==2){result.failed=true;result.error=MP3D_E_DECODE;}
            return result;
        }
    } fault(bytes);
    Reject(WolfSourceMp3ReceiptCode::CodecError,[&]{DecodeAll(4608,100000,fault);});
    struct Early {Read operator()(int,int,float*){Read r;r.eof=true;return r;}} early;
    Reject(WolfSourceMp3ReceiptCode::ShortDecode,[&]{DecodeAll(4608,100000,early);});
    struct FinalError {
        PcmReader codec;
        explicit FinalError(const std::vector<unsigned char>& b):codec(b){}
        Read operator()(int offset,int count,float* out){auto r=codec(offset,count,out);if(!r.actual){r.error=MP3D_E_IOERROR;r.failed=true;}return r;}
    } finalError(bytes);
    Reject(WolfSourceMp3ReceiptCode::CodecError,[&]{DecodeAll(4608,100000,finalError);});
    const std::vector<unsigned char> invalid(64,0x42);PcmReader invalidReader(invalid);
    // Some decoder versions defer invalid input to zero-sample EOF. Either
    // actual open/read failure or short decode must refuse the full receipt.
    bool refused=false;
    try{DecodeAll(1152,100000,invalidReader);}catch(const Error& e){refused=e.code==WolfSourceMp3ReceiptCode::CodecError||e.code==WolfSourceMp3ReceiptCode::ShortDecode;}
    Check(refused,"pinned codec accepted an invalid whole sample");
}
}
int main(){try{PositiveAndIdentity();FramingRefusals();ActualReadRefusals();
    std::cout<<"WOLF2009_SOURCE_MP3_RECEIPT_PORTABLE_OK checks="<<checks<<" authored_mp3=1 actual_codec=1 native_cache_runtime=0 bank_ready=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"WOLF2009_SOURCE_MP3_RECEIPT_PORTABLE_FAIL "<<error.what()<<"\n";return 1;}}
