// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_MP3_STRICT_H
#define WOLF2009_SOURCE_MP3_STRICT_H
#include "SourceSoundDecodeReceipt.h"
#include <climits>
#include <cstring>
#include <stdexcept>

// Engine-independent accounting shared by the native leaf and authored tests.
// It supplies no codec: readers call the already pinned native minimp3.
namespace wolfSourceMp3Strict {
struct Error:std::runtime_error {
    WolfSourceMp3ReceiptCode code;
    Error(WolfSourceMp3ReceiptCode c,const char* text):std::runtime_error(text),code(c){}
};
inline void Need(bool value,WolfSourceMp3ReceiptCode code,const char* why) {
    if(!value)throw Error(code,why);
}
inline std::uint64_t Fingerprint(const unsigned char* bytes,std::size_t size) {
    std::uint64_t hash=UINT64_C(14695981039346656037);
    for(std::size_t i=0;i<size;++i)hash=(hash^bytes[i])*UINT64_C(1099511628211);
    return hash;
}
inline void Request(const WolfSourceMp3ReceiptRequest& r) {
    Need(!r.exactNativeSampleName.empty()&&r.exactNativeSampleName.size()<=4608,
        WolfSourceMp3ReceiptCode::InvalidRequest,"source MP3 exact native cache name required");
    Need(r.expectedEncoded&&!r.expectedEncoded->empty(),WolfSourceMp3ReceiptCode::InvalidRequest,
        "source MP3 expected owning bytes required");
    Need(r.maximumEncodedBytes&&r.maximumEncodedBytes<=16u*1024u*1024u&&
        r.expectedEncoded->size()<=r.maximumEncodedBytes&&r.maximumDecodedSamples&&
        r.maximumDecodedSamples<=std::uint64_t(INT_MAX-1),WolfSourceMp3ReceiptCode::LimitExceeded,
        "source MP3 byte/PCM work budget exceeded");
    Need(Fingerprint(r.expectedEncoded->data(),r.expectedEncoded->size())==r.expectedFingerprint,
        WolfSourceMp3ReceiptCode::IdentityMismatch,"source MP3 expected fingerprint differs from bytes");
}
inline void Identity(const WolfSourceMp3ReceiptRequest& r,const char* name,
                     const unsigned char* bytes,std::size_t size) {
    Need(name&&r.exactNativeSampleName==name,WolfSourceMp3ReceiptCode::IdentityMismatch,
        "source MP3 native cache name differs");
    Need(bytes&&size==r.expectedEncoded->size()&&size<=r.maximumEncodedBytes&&
        Fingerprint(bytes,size)==r.expectedFingerprint&&
        std::memcmp(bytes,r.expectedEncoded->data(),size)==0,
        WolfSourceMp3ReceiptCode::IdentityMismatch,"source MP3 exact encoded cache bytes differ");
}
struct Frame {
    int bytes=0,samples=0,rate=0,channels=0,layer=0;
};
struct Coverage {std::uint64_t frames=0,samples=0;};
// A deliberately narrow framing profile: no skipped tags, free-format frames,
// leading junk or incomplete/trailing bytes. This is not an MP3 decoder.
template<class FrameReader>
Coverage Framing(const unsigned char* data,std::size_t size,std::uint64_t maximumSamples,
                 FrameReader& reader) {
    static const int rates[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    Need(data&&size&&size<=16u*1024u*1024u&&maximumSamples&&maximumSamples<std::uint64_t(INT_MAX),
        WolfSourceMp3ReceiptCode::LimitExceeded,"source MP3 framing input/work budget invalid");
    Coverage out;std::size_t at=0;
    while(at<size) {
        Need(size-at>=4,WolfSourceMp3ReceiptCode::InvalidFraming,"source MP3 truncated frame header");
        const unsigned char* p=data+at;const int bitrate=rates[p[2]>>4];
        Need(p[0]==0xff&&(p[1]&0xfe)==0xfa&&(p[2]&0x0c)==0&&(p[3]&0xc0)==0xc0&&bitrate,
            WolfSourceMp3ReceiptCode::InvalidFraming,"source MP3 unsupported or skipped MPEG frame");
        const std::size_t bytes=std::size_t(144000*bitrate/44100+((p[2]>>1)&1));
        Need(bytes<=size-at,WolfSourceMp3ReceiptCode::InvalidFraming,"source MP3 incomplete frame payload");
        const Frame actual=reader(p,int(size-at));
        Need(actual.bytes==int(bytes)&&actual.samples==1152&&actual.rate==44100&&actual.channels==1&&actual.layer==3,
            WolfSourceMp3ReceiptCode::InvalidFraming,"source MP3 native frame coverage failed");
        Need(out.samples<=maximumSamples&&1152<=maximumSamples-out.samples,
            WolfSourceMp3ReceiptCode::LimitExceeded,"source MP3 decoded sample budget exceeded");
        out.samples+=1152;++out.frames;at+=bytes;
    }
    Need(out.frames!=0,WolfSourceMp3ReceiptCode::InvalidFraming,"source MP3 has no complete frames");return out;
}
struct Read {
    int actual=0,error=0;
    bool failed=false,eof=false;
};
struct Pcm {std::uint64_t actual=0;bool normalEof=false;};
template<class PcmReader>
Pcm DecodeAll(std::uint64_t expected,std::uint64_t maximum,PcmReader& reader) {
    Need(expected&&expected<=maximum&&expected<std::uint64_t(INT_MAX),
        WolfSourceMp3ReceiptCode::LimitExceeded,"source MP3 PCM count/budget invalid");
    Pcm out;float scratch[2048];
    while(out.actual<expected) {
        const int request=int((expected-out.actual)<2048?(expected-out.actual):2048);
        const Read result=reader(int(out.actual),request,scratch);
        Need(!result.failed&&result.error==0,WolfSourceMp3ReceiptCode::CodecError,"source MP3 native read reported failure");
        Need(result.actual>0&&result.actual<=request,WolfSourceMp3ReceiptCode::ShortDecode,"source MP3 actual PCM read count differs");
        out.actual+=std::uint64_t(result.actual);
        Need(!result.eof||out.actual==expected,WolfSourceMp3ReceiptCode::ShortDecode,"source MP3 early native EOF");
    }
    const Read end=reader(int(out.actual),1,scratch);
    Need(!end.failed&&end.error==0,WolfSourceMp3ReceiptCode::CodecError,"source MP3 final native read reported failure");
    Need(end.actual==0&&end.eof,WolfSourceMp3ReceiptCode::ShortDecode,"source MP3 missing exact native EOF");
    out.normalEof=true;return out;
}
}
#endif
