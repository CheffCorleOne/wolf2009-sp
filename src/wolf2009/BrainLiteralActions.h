// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_BRAIN_LITERAL_ACTIONS_H
#define WOLF2009_BRAIN_LITERAL_ACTIONS_H
#include "BrainTask.h"
#include <map>

namespace wolf2009 {
// A deliberately bounded action profile, not a retail expression interpreter.
struct BrainLiteralSelector {
    enum Kind { Me, Name } kind = Me;
    std::string name;
};
struct BrainLiteralNode {
    std::uint32_t type = 0;
    BrainLiteralSelector actor, target;
    std::string key, variable;
    std::int32_t value = 0, minimum = 0, maximum = 0;
    bool inverted = false;
};
struct BrainLiteralPlan {
    bool supported = false;
    std::string reason;
    std::vector<BrainLiteralNode> nodes;
};
// The complete immutable graph is admitted before a host binds or executes it.
// Only OnSpawn/OnUsed/Wait/UseTarget/SetGlobalVariable and primary conditions
// Exists/Used/Timer are admitted. Explicit/nested condition bodies refuse.
BrainLiteralPlan PrepareBrainLiteralActions(const BrainGraph &graph);
bool BrainCanonicalNonnegativeInteger(const std::string &, std::int32_t &out);
bool BrainLiteralGlobalName(const std::string &);
typedef std::map<std::string, std::int32_t> BrainLiteralGlobals;
// Independent diagnostic defaults format, never a decoder for retail globalVars:
// wolf2009-brain-globals-v1\n followed by unique lowercase_name=integer\n rows.
// Mandatory nonempty defaults, bounded to 256 names/64KiB. Output is unchanged
// on rejection. Scope/reset/save policy belongs to the explicit native provider.
bool DecodeBrainLiteralGlobals(const std::vector<unsigned char> &, BrainLiteralGlobals &out,
                              std::string &reason);
int RunBrainLiteralActionTests();
} // namespace wolf2009
#endif
