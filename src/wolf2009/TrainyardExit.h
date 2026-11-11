// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_TRAINYARD_EXIT_H
#define WOLF2009_TRAINYARD_EXIT_H
// Portable data qualification for the measured trainyard exit slice: the
// shake_train arming prefix, the movetrain Wait/RemoveTarget tail, the HKXS
// boxes of the source trigger volumes and the doorway blocker, and the OnUsed
// roots a brain receiver checks before it latches a key. Nothing here executes
// a node; native owners act on these facts under explicit profiles.
#include "Entities.h"
#include "SourceMapBodies.h"
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
// Wait/Timer duration literal "<value> <unit>". Static evidence (Gamex86
// 7cb1cbed..., private notes): the property parser reads "%s %d", resets a
// failed parse to "0"/0 and any unit >= 2 to 0; the Timer arming resolver
// converts the expanded value to float32, multiplies by 1000.0f only when the
// unit is 1, adds 0.5f and rounds down, in the integer brain clock (ms). This
// profile admits only plain nonnegative decimal values and units 0/1; '@'
// expressions, signs, exponents and other units are refused, not defaulted.
struct SourceWaitDuration {
    std::string raw,value;
    int unit=0;
    std::int32_t milliseconds=0;
};
SourceWaitDuration ParseSourceWaitDurationLiteralV1(const std::string& raw);

// Measured movetrain continuation after the relay node: 6 UseTarget ->
// 7 Wait (Timer primary) -> 8 RemoveTarget -> 9 ExecConsoleCommand.
struct TrainyardMovetrainTail {
    std::uint32_t relayNode=0,waitNode=0,removeNode=0,consoleNode=0,consoleType=0;
    std::string relayTarget,relayKey,removeTarget,consoleCommand;
    SourceWaitDuration wait,waitMax;
};
TrainyardMovetrainTail DecodeTrainyardMovetrainTailH1023(const std::vector<unsigned char>& brai);

// Measured shake_train prefix from root OnSpawn: 1 OnSpawn -> 30 Wait ->
// 19 UseTarget (no key) -> 31 (first unimplemented node, type 357).
struct TrainyardShakeArming {
    std::uint32_t onSpawnNode=0,waitNode=0,useNode=0,stopNode=0,stopType=0;
    std::string useTarget;
    SourceWaitDuration wait,waitMax;
};
TrainyardShakeArming DecodeTrainyardShakeArmingH1023(const std::vector<unsigned char>& brai);

// OnUsed (type 3) roots with their configured key and normal outputs. The
// static CanUseKey gate visits roots, compares the configured key ASCII-folded
// and requires an output without raw flag 0x100000. Keys are kept verbatim.
struct SourceOnUsedRoot {
    std::uint32_t node=0;
    std::string key;
    std::vector<std::uint32_t> outputFlags,outputTargets,outputAux;
    std::vector<bool> outputPrimaryCondition;
};
std::vector<SourceOnUsedRoot> DecodeSourceOnUsedRootsH1023(const std::vector<unsigned char>& brai);
// Returns the matching root index or -1. Empty matches empty only.
int SourceCanUseKey(const std::vector<SourceOnUsedRoot>&,const std::string& key);

// One named entity body reduced to a single axis box in its body frame.
enum class SourceBoxBodyProfile {
    // Root hkpBvShape: bounding Box or ConvexTranslate(Box), child
    // rvCallBackPhantom, raw filter word 4096 (CONTENTS_TRIGGER).
    PhantomTriggerBoxV1,
    // Root ConvexTranslate(Box) or Box, raw filter word 1 (CONTENTS_SOLID).
    SolidBoxV1,
    // SourceRotatingDoorOldV1 hinged slab: root ConvexTranslate(Box) only, raw
    // filter word 3 (bits 0|1, consistent with physics contents 0x10003),
    // identity rotation; the body origin is the hinge (TRAPDOOR 1.3).
    HingedSolidBoxV1
};
struct SourceBoxBody {
    std::string name,rootType,boundingType,childType;
    std::uint32_t rawFilter=0,bodyOffset=0;
    // Game units (1 unit = 0.0254 m). origin is the decoded body translation
    // and equals the ENTS origin; rotation is the decoded body matrix and is
    // required to be identity (any other basis is refused).
    double origin[3]={0,0,0},rotation[9]={1,0,0,0,1,0,0,0,1};
    double center[3]={0,0,0},halfExtents[3]={0,0,0};
};
SourceBoxBody DecodeSourceEntityBoxBody(const SourceMapBodyPlan&,std::size_t entityIndex,SourceBoxBodyProfile);

int RunTrainyardExitTests();
}
#endif
