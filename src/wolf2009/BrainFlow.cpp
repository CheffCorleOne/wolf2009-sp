// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainFlow.h"
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace wolf2009 {
namespace {
bool Unsigned(const std::string &text, std::uint32_t maximum, std::uint32_t &value) {
    if (text.empty())
        return false;
    value = 0;
    for (unsigned char c : text) {
        if (c < '0' || c > '9')
            return false;
        const unsigned digit = c - '0';
        if (digit > maximum || value > (maximum - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    return true;
}
bool UnitDecay(const std::string &text) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double value = 0;
    input >> std::noskipws >> value;
    return input && input.eof() && std::isfinite(value) && value == 1.0;
}
BrainFlowDecision Refuse(std::size_t output, const char *reason) {
    BrainFlowDecision result;
    result.status = BrainFlowDecision::Unsupported;
    result.output = output;
    result.reason = reason;
    return result;
}
BrainConditionValue Evaluate(const BrainGraph &graph, const BrainOutput &output,
                             BrainFlowConditions &conditions) {
    for (auto recordIndex : output.conditions) {
        const auto &record = graph.records[recordIndex];
        if (recordIndex + 1 < graph.records.size() && graph.records[recordIndex + 1].depth > 2)
            return BrainConditionValue::Unsupported;
        const bool primary = record.tokens == std::vector<std::string>({"-1"});
        if (!primary && !record.tokens.empty() && record.tokens[0] == "-1")
            return BrainConditionValue::Unsupported;
        const auto value = primary ? conditions.Primary(graph, output.sourceNode)
                                   : conditions.Explicit(graph, recordIndex);
        if (value == BrainConditionValue::False)
            return value;
        if (value != BrainConditionValue::True &&
            !(value == BrainConditionValue::MissingPrimary && primary))
            return BrainConditionValue::Unsupported;
    }
    return BrainConditionValue::True;
}
// Common weighted destination selection and processed marking, shared by the
// normal pass and the probe flush (one fire path in the inspected Network).
// On refusal the output state is unchanged.
BrainFlowDecision Choose(const BrainGraph &graph, std::size_t index, BrainFlowOutputState &current,
                         BrainFlowRandom &random) {
    const auto &output = graph.outputs[index];
    std::uint64_t total = 0;
    for (auto weight : current.weights)
        total += weight;
    // The observed selector multiplies a 15-bit sample by the signed sum.
    // Refuse overflow profiles rather than choosing a new arithmetic policy.
    if (total > 65535)
        return Refuse(index, "Destination weight sum exceeds proven arithmetic range");
    auto weights = current.weights;
    if (weights.size() > 1 && total == 0) {
        weights.assign(weights.size(), 1);
        total = weights.size();
    }
    std::size_t chosen = 0;
    if (weights.size() > 1) {
        const auto sample = random.Next15();
        if (sample > 32767)
            return Refuse(index, "Random sample is outside the 15-bit contract");
        const std::uint64_t bucket = (sample * total) >> 15;
        std::uint64_t cumulative = 0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            cumulative += weights[i];
            if (bucket < cumulative) {
                chosen = i;
                break;
            }
        }
    }
    current.weights.swap(weights);
    current.flags |= 0x80000u;
    BrainFlowDecision result;
    result.status = BrainFlowDecision::Transition;
    result.output = index;
    result.destination = chosen;
    result.targetNode = output.destinations[chosen].targetNode;
    return result;
}
} // namespace

BrainFlowState MakeBrainFlowState(const BrainGraph &graph) {
    BrainFlowState state;
    state.graph = &graph;
    for (const auto &output : graph.outputs) {
        BrainFlowOutputState item;
        const auto &tokens = graph.records[output.record].tokens;
        item.flagsKnown = !tokens.empty() &&
                          Unsigned(tokens[0], std::numeric_limits<std::uint32_t>::max(), item.flags);
        if (!item.flagsKnown)
            item.unsupportedReason = "Unknown output flags";
        else if (!output.destinationsComplete || tokens.size() != 99)
            item.unsupportedReason = "Unknown output destination layout";
        else if (!UnitDecay(tokens[98]))
            item.unsupportedReason = "Weight decay other than one is unsupported";
        else {
            item.supported = true;
            for (const auto &destination : output.destinations) {
                std::uint32_t weight = 0;
                if (!Unsigned(tokens[3 + destination.slot * 3], 65535, weight)) {
                    item.supported = false;
                    item.unsupportedReason = "Unsupported destination weight";
                    break;
                }
                item.weights.push_back(weight);
            }
        }
        state.outputs.push_back(item);
    }
    return state;
}

BrainFlowDecision SelectBrainTransition(const BrainGraph &graph, std::size_t node,
                                       BrainFlowState &state, BrainFlowConditions &conditions,
                                       BrainFlowRandom &random) {
    if (state.graph != &graph || node >= graph.nodes.size() ||
        state.outputs.size() != graph.outputs.size())
        return Refuse(BrainRecord::None(), "Invalid graph/state binding");
    for (auto index : graph.nodes[node].outputs) {
        const auto &output = graph.outputs[index];
        auto &current = state.outputs[index];
        if (!current.flagsKnown)
            return Refuse(index, "Unknown output flags");
        const auto flags = current.flags;
        // Probe-only links are excluded from the normal pass. Disabled links and
        // previously processed one-shot links cannot transition in this pass.
        if ((flags & 0x100000u) || (flags & 0x20u) || ((flags & 0x80000u) && (flags & 1u)))
            continue;
        const std::uint32_t known = 0x10u | 1u | 0x80000u | 0x40000u;
        if (flags & ~known)
            return Refuse(index, "Unsupported output activation mode or flags");
        if (!current.supported)
            return Refuse(index, current.unsupportedReason.c_str());
        if (current.weights.size() != output.destinations.size())
            return Refuse(index, "Invalid output weight state");
        if (output.destinations.empty())
            continue;
        const auto condition = Evaluate(graph, output, conditions);
        if (condition == BrainConditionValue::Unsupported)
            return Refuse(index, "Unsupported condition or missing condition context");
        if (condition == BrainConditionValue::False)
            continue;
        return Choose(graph, index, current, random);
    }
    return BrainFlowDecision();
}

BrainFlowDecision MarkBrainProbeOutputs(const BrainGraph &graph, std::size_t node,
                                        BrainFlowState &state, BrainFlowConditions &conditions,
                                        std::size_t &marked) {
    marked = 0;
    if (state.graph != &graph || node >= graph.nodes.size() ||
        state.outputs.size() != graph.outputs.size())
        return Refuse(BrainRecord::None(), "Invalid graph/state binding");
    for (auto index : graph.nodes[node].outputs) {
        const auto &output = graph.outputs[index];
        auto &current = state.outputs[index];
        if (!current.flagsKnown)
            return Refuse(index, "Unknown output flags");
        const auto flags = current.flags;
        // Eligibility order of the inspected procedure: disabled, no destination,
        // processed once-or-probe, then the pass kind. Non-probe links are not
        // considered by this pass.
        if ((flags & 0x100000u) || output.destinations.empty() ||
            ((flags & 0x80000u) && (flags & 0x21u)) || !(flags & 0x20u))
            continue;
        const std::uint32_t known = 0x20u | 1u | 0x80000u | 0x40000u;
        if (flags & ~known)
            return Refuse(index, "Unsupported probe output activation mode or flags");
        if (!current.supported)
            return Refuse(index, current.unsupportedReason.c_str());
        if (current.weights.size() != output.destinations.size())
            return Refuse(index, "Invalid output weight state");
        std::uint64_t total = 0;
        for (auto weight : current.weights)
            total += weight;
        if (total > 65535)
            return Refuse(index, "Destination weight sum exceeds proven arithmetic range");
        const auto condition = Evaluate(graph, output, conditions);
        if (condition == BrainConditionValue::Unsupported)
            return Refuse(index, "Unsupported condition or missing condition context");
        if (condition == BrainConditionValue::False)
            continue;
        current.flags |= 0x40000u;
        ++marked;
    }
    return BrainFlowDecision();
}

BrainFlowDecision FireBrainMarkedProbeOutput(const BrainGraph &graph, std::size_t output,
                                             BrainFlowState &state, BrainFlowRandom &random) {
    if (state.graph != &graph || output >= graph.outputs.size() ||
        state.outputs.size() != graph.outputs.size())
        return Refuse(BrainRecord::None(), "Invalid graph/state binding");
    auto &current = state.outputs[output];
    if (!current.flagsKnown || !(current.flags & 0x40000u) || !(current.flags & 0x20u) ||
        (current.flags & ~(0x20u | 1u | 0x80000u | 0x40000u)))
        return Refuse(output, "Output is not a marked probe output");
    if (!current.supported)
        return Refuse(output, current.unsupportedReason.c_str());
    if (current.weights.size() != graph.outputs[output].destinations.size() ||
        current.weights.empty())
        return Refuse(output, "Invalid output weight state");
    BrainFlowOutputState next = current;
    next.flags &= ~0x40000u;
    const auto result = Choose(graph, output, next, random);
    if (result.status == BrainFlowDecision::Transition)
        current = std::move(next);
    return result;
}

BrainConditionValue BrainTimerElapsed(std::int32_t now, std::int32_t activeTaskStart,
                                     std::int32_t duration, bool inverted) {
    bool elapsed = false;
    if (duration != -1) {
        const auto deadline = static_cast<std::int64_t>(activeTaskStart) + duration;
        if (deadline < std::numeric_limits<std::int32_t>::min() ||
            deadline > std::numeric_limits<std::int32_t>::max())
            return BrainConditionValue::Unsupported;
        elapsed = now > deadline;
    }
    return elapsed != inverted ? BrainConditionValue::True : BrainConditionValue::False;
}
} // namespace wolf2009
