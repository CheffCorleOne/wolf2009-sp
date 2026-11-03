// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_SOURCE_RAGDOLL_H
#define WOLF2009_SOURCE_RAGDOLL_H
#include "WorldCollisionGeometry.h"
#include "SkeletonData.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wolf2009 {
struct SourceRagdollLimits {
    std::size_t sourceBytes=64u<<20,objects=100000,fixups=1000000;
    std::size_t bodies=256,constraints=512,joints=256,shapeVisits=10000;
    std::size_t boxes=4096,nesting=32,names=65536;
    double coordinateGameUnits=10000000,maximumRadiusGameUnits=1000;
};
struct SourceRagdollBox {
    std::uint32_t sourceShapeOffset=0;
    std::uint64_t occurrence=0;
    std::array<std::uint32_t,12> rawHeader{{}};
    CollisionRigidTransform local;
    CollisionVec3 halfExtents;
    double radius=0;
    std::vector<WorldCollisionTransformRecord> transforms;
    std::uint32_t rawChildFilter=0;
    bool hasChildFilter=false,radiusHierarchyVerified=true;
};
struct SourceRagdollOmission {
    std::uint32_t sourceObjectOffset=0;
    std::string type;
    CollisionRigidTransform placement;
};
struct SourceRagdollBody {
    std::uint32_t sourceObjectOffset=0,rootShapeOffset=0;
    std::string name;
    // Complete observed hkpRigidBody serialized prefix, not a native object.
    std::array<std::uint32_t,136> rawHeader{{}};
    CollisionRigidTransform pose; // asset space, GAME UNITS
    std::uint32_t rawFilterInfo=0;
    unsigned motionType=0,responseType=0;
    double inverseMass=0;
    // Original Havok values (kg,m,s). Conversion is a consumer policy.
    CollisionVec3 inverseInertiaMetres;
    double friction=0,restitution=0,linearDamping=0,angularDamping=0;
    CollisionVec3 linearVelocity,angularVelocity; // game units/s, radians/s
    CollisionVec3 centerOfMassLocal; // game units, obtained from swept COM
    std::vector<SourceRagdollBox> boxes;
    std::vector<SourceRagdollOmission> omissions;
    bool geometryComplete=false;
};
struct SourceRagdollMotor {
    bool present=false,enabled=false;
    std::uint32_t sourceObjectOffset=0;
    std::string type;
    std::array<std::uint32_t,9> rawHeader{{}};
    double targetAngle=0,minForce=0,maxForce=0,tau=0,damping=0;
    double proportionalRecoveryVelocity=0,constantRecoveryVelocity=0;
};
struct SourceRagdollHinge {
    std::uint32_t sourceInstanceOffset=0,sourceDataOffset=0;
    std::string name;
    std::size_t body[2]={0,0}; // native source instance entities[0/1], no reordering
    std::array<std::uint32_t,11> rawInstance{{}};
    std::array<std::uint32_t,56> rawData{{}};
    CollisionRigidTransform localFrame[2];
    unsigned priority=0,limitAxis=0,frictionAxis=0,frictionAxes=0,motorAxis=0;
    bool limitEnabled=false,frictionEnabled=false;
    double minimumAngle=0,maximumAngle=0,limitTau=0,maxFrictionTorque=0;
    SourceRagdollMotor motor;
};
struct SourceRagdoll {
    std::uint64_t sourceFingerprint=0;
    std::size_t sourceBytes=0;
    std::uint32_t sourceObjectOffset=0;
    std::vector<unsigned char> rawSource; // all padding/fixups/uninterpreted data
    // Named root variant and its instance are required. No first-physics-system
    // heuristic: the system and ragdoll can contain different hinge/motor data.
    std::string rootVariantName;
    SkeletonData skeleton;
    std::vector<std::int32_t> boneToBody;
    std::vector<SourceRagdollBody> bodies;
    std::vector<SourceRagdollHinge> hinges;
    std::vector<SourceRagdollOmission> omissions;
    bool complete=false;
};
// Bounded owning Havok5.6/LE32 HKXR reader. One named hkaRagdollInstance is
// selected explicitly; only Box/List/ConvexTranslate/ConvexTransform and
// LimitedHinge(+optional PositionMotor) are interpreted. Other selected shapes
// or constraints remain counted omissions. Broken pointers/indices/nonfinite
// interpreted geometry and budgets throw; no native physics is installed.
// Source filter bits are facts, never inferred native contents.
SourceRagdoll DecodeSourceRagdoll(const std::vector<unsigned char>&,
    const std::string& exactRootVariantName="RagdollInstance",
    const SourceRagdollLimits& = SourceRagdollLimits());

enum class SourceRagdollHullPolicy { Unspecified=0, NativeConservativeHullV1=1 };
struct SourceRagdollNativeBox {
    CollisionRigidTransform local;
    CollisionVec3 halfExtents;
    double sourceRadius=0,maximumRoundingExcess=0;
    // V1 contains core-box+sphere(radius) with one expanded local box.
    // No compound convexification: exactly one source box is required.
    std::uint32_t sourceShapeOffset=0;
};
SourceRagdollNativeBox BuildSourceRagdollNativeBox(const SourceRagdollBody&,
    SourceRagdollHullPolicy,const SourceRagdollLimits& = SourceRagdollLimits());
// Native compatibility limit conversion: mathematical right-handed signed
// angle atan2(axis dot (reference cross moving), reference dot moving).
// Returns its cone midpoint; the cone's full width is maximum-minimum.
CollisionVec3 SourceRagdollHingeLimitCenter(CollisionVec3 axis,CollisionVec3 reference,
    double minimumAngle,double maximumAngle);
// NativeRestDeltaV1 may derive a new moving local anchor around the fixed
// source world pivot without changing the visual pose. The explicit native
// compatibility budget is at most0.5 GAME UNIT (12.7mm), not a Havok tolerance
// or a claim about the original part-offset formula.
bool SourceRagdollNativeAnchorRetargetAllowed(double residualGameUnits);
}
#endif
