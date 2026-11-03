// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_ROUNDED_COLLISION_H
#define WOLF2009_ENGINE_ROUNDED_COLLISION_H
// C++98 boundary. No Bullet/STL types or changes to the game-import ABI.
struct trace_s;
class idTraceModel;
class idVec3;
class idVec6;
class idMat3;
class idRotation;
class idBounds;
class idFile;

enum WolfCMStatus { WolfCM_NoSidecar, WolfCM_Complete, WolfCM_Indeterminate, WolfCM_Invalid };
struct WolfCMQuery {
    int handle, contentMask;
    const idTraceModel *trm;
    const idMat3 *trmAxis;
    const idVec3 *modelOrigin;
    const idMat3 *modelAxis;
    // Query clearance only. Source radii and numeric tolerance are separate.
    float clipEpsilon;
    WolfCMQuery() : handle(-1), contentMask(-1), trm(0), trmAxis(0),
                    modelOrigin(0), modelAxis(0), clipEpsilon(.25f) {}
};

// Explicitly gated, independently authored diagnostic model only:
// wolf2009/_qa/rounded-support[.cm]. No main-world or original-source binding.
void Wolf2009_EnableRoundedCollisionFixture(bool enabled);
bool Wolf2009_IsRoundedCollisionFixturePath(const char *path);
idFile *Wolf2009_OpenRoundedCollisionFixturePath(const char *path);
WolfCMStatus Wolf2009_CMModelReady(int handle, const char *actualModelName);
void Wolf2009_CMFreeMap();
// Returns sidecar LOCAL bounds/contents; caller unions with native metadata.
WolfCMStatus Wolf2009_CMMetadata(int handle, idBounds &bounds, int &contents);

// Output is usable only for Complete. NoSidecar means this handle is unbound;
// Indeterminate/Invalid MUST NOT fall back to native-only or become a miss.
// The caller handles failure outside the native contact-collection scope.
// trm==0 is an explicit point profile for this fixture, not an HKXS ray policy.
WolfCMStatus Wolf2009_CMTranslation(trace_s &, const idVec3 &start, const idVec3 &end,
                                   const WolfCMQuery &);
WolfCMStatus Wolf2009_CMPosition(trace_s &, const idVec3 &start, const WolfCMQuery &);
// Linear, positive-depth contacts. Every fixture candidate is queried before
// writing up to capacity entries, so an uncertain later candidate cannot hide.
WolfCMStatus Wolf2009_CMContacts(trace_s *out, int capacity, int &count,
                                const idVec3 &start, const idVec6 &dir, float depth,
                                const WolfCMQuery &);
// Actual pivot arc, <=90-degree segments; explicit bounded range [-360,360].
// Point rotation endAxis is Identity, matching the native CM convention.
// Some clear rotating motions still lack a certificate and return Indeterminate.
WolfCMStatus Wolf2009_CMRotation(trace_s &, const idVec3 &start, const idRotation &,
                                const WolfCMQuery &);
// Engine/CM thread only. Stable until another adapter operation; no OS calls.
const char *Wolf2009_CMLastDiagnostic();
#endif
