// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceSoundDecodeReceipt.h"
#include "SourceMp3ReceiptInternal.h"
#include "idlib/precompiled.h"
#include "sound/snd_local.h"
#include <utility>
namespace {
struct DecoderLock {
    DecoderLock(){Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);}
    ~DecoderLock(){Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);}
};
}
bool Wolf2009_InspectSourceMp3Cache(idSoundCache& cache,const WolfSourceMp3ReceiptRequest& request,
                                 WolfSourceMp3DecodeReceipt& receipt,idStr& reason) {
    receipt=WolfSourceMp3DecodeReceipt();reason.Clear();
    try {
        using namespace wolfSourceMp3Strict;
        Request(request);
        // cache ownership/destruction is serialized by the caller's ordinary
        // engine resource thread; this lock excludes realtime decoder reads.
        DecoderLock lock;
        Need(cache.GetNumObjects()>=0&&cache.GetNumObjects()<=65536,
            WolfSourceMp3ReceiptCode::LimitExceeded,"source MP3 native cache lookup budget exceeded");
        const idSoundSample* sample=NULL;
        for(int i=0;i<cache.GetNumObjects();++i) {
            const idSoundSample* candidate=cache.GetObject(i);
            if(candidate&&request.exactNativeSampleName==candidate->name.c_str()) {
                Need(sample==NULL,WolfSourceMp3ReceiptCode::IdentityMismatch,"source MP3 duplicate native cache name");sample=candidate;
            }
        }
        Need(sample!=NULL,WolfSourceMp3ReceiptCode::CacheAbsent,"source MP3 sample is not in native cache");
#if defined(_SND_MP3) && defined(WOLF2009_SOURCE_MP3_RECEIPT)
        Need(!sample->defaultSound&&!sample->purged&&!sample->hardwareBuffer&&sample->nonCacheData&&
            sample->objectInfo.wFormatTag==WAVE_FORMAT_TAG_MP3&&sample->objectInfo.nChannels==1&&
            sample->objectInfo.nSamplesPerSec==44100&&sample->objectInfo.wBitsPerSample==16&&sample->objectMemSize>0&&
            sample->objectSize>0&&std::uint64_t(sample->objectSize)<=request.maximumDecodedSamples,
            WolfSourceMp3ReceiptCode::UnsupportedCache,"source MP3 requires a resident encoded mono44100 native cache sample");
        Identity(request,sample->name.c_str(),sample->nonCacheData,std::size_t(sample->objectMemSize));
        WolfSourceMp3DecodeReceipt out;
        out.name_=sample->name.c_str();out.encoded_.assign(sample->nonCacheData,sample->nonCacheData+sample->objectMemSize);
        const auto counts=Wolf2009_InspectSourceMp3BytesLocked(*sample,request.maximumDecodedSamples);
        Need(counts.samples==std::uint64_t(sample->objectSize),WolfSourceMp3ReceiptCode::ShortDecode,
            "source MP3 full native count differs from cached sample count");
        Identity(request,sample->name.c_str(),sample->nonCacheData,std::size_t(sample->objectMemSize));
        Need(!sample->defaultSound&&!sample->purged&&!sample->hardwareBuffer&&
            out.encoded_.size()==std::size_t(sample->objectMemSize)&&
            std::memcmp(out.encoded_.data(),sample->nonCacheData,out.encoded_.size())==0,
            WolfSourceMp3ReceiptCode::CacheChanged,"source MP3 native cache changed during observation");
        out.fingerprint_=request.expectedFingerprint;out.samples_=counts.samples;out.frames_=counts.frames;
        out.code_=WolfSourceMp3ReceiptCode::Verified;receipt=std::move(out);return true;
#else
        throw Error(WolfSourceMp3ReceiptCode::UnsupportedCache,"source MP3 native codec/strict receipt hook is disabled");
#endif
    } catch(const wolfSourceMp3Strict::Error& error) {
        receipt=WolfSourceMp3DecodeReceipt();receipt.code_=error.code;reason=error.what();return false;
    } catch(const std::exception& error) {
        receipt=WolfSourceMp3DecodeReceipt();reason=error.what();return false;
    }
}
