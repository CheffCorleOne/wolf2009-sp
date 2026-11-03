// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_SKINNED_MODEL_DATA_H
#define WOLF2009_ENGINE_SKINNED_MODEL_DATA_H
// Include before the engine precompiled header (legacy idStr STL macros).
#include "SkinnedModelData.h"
class idJointMat;
class idRenderModelMD5;
// Owning identity of the three original byte sources used by the resident
// model. Cleared on purge; this does not return a borrowed loader/stream view.
bool Wolf2009_GetSkinnedModelSources(const idRenderModelMD5* model,
                                    wolf2009::idtech4a::SkinnedModelSourceInfo& sources);
// Source quaternions rotate column vectors; native idQuat/idJointMat conversion
// uses the conjugate quaternion. Translation remains model-space game units.
// Throws on a nonfinite, nonunit, or count-mismatched supplied pose.
void Wolf2009_ToNativeModelJoints(const std::vector<wolf2009::GreeterJointPose>& source,
                                idJointMat* destination, int jointCount);
#endif
