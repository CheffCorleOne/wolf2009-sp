// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainFlow.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace wolf2009 {
namespace {
void Need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string Line(std::string text) {
    for (auto &c : text)
        if (c == '|')
            c = 0;
    return text;
}
std::string Output(unsigned flags, const std::vector<std::pair<unsigned, unsigned>> &links,
                   const std::string &conditions = "", const std::string &decay = "1.000000") {
    std::vector<std::string> tokens(99, "0");
    tokens[0] = std::to_string(flags);
    tokens[1] = "1";
    tokens[98] = decay;
    for (std::size_t i = 0; i < links.size(); ++i) {
        tokens[2 + i * 3] = std::to_string(links[i].first);
        tokens[3 + i * 3] = std::to_string(links[i].second);
    }
    std::string result = "\t";
    for (const auto &token : tokens)
        result += token + '|';
    return Line(result + "\n" + conditions);
}
BrainGraph Graph(const std::string &outputs, unsigned sourceType = 1) {
    const auto text = Line("h1023|32|32|32|0|\n" + std::to_string(sourceType) + "|1|\n") +
                      outputs + Line("30|2|\n68|3|\n");
    return DecodeBrainGraph({text.begin(), text.end()});
}
struct Conditions : BrainFlowConditions {
    BrainConditionValue primary = BrainConditionValue::True;
    std::map<std::string, BrainConditionValue> explicitValues;
    unsigned primaryCalls = 0, explicitCalls = 0;
    BrainConditionValue Primary(const BrainGraph &, std::size_t) override {
        ++primaryCalls;
        return primary;
    }
    BrainConditionValue Explicit(const BrainGraph &graph, std::size_t record) override {
        ++explicitCalls;
        const auto found = explicitValues.find(graph.records[record].tokens[0]);
        return found == explicitValues.end() ? BrainConditionValue::Unsupported : found->second;
    }
};
struct Random : BrainFlowRandom {
    std::uint32_t sample = 0;
    unsigned calls = 0;
    std::uint32_t Next15() override {
        ++calls;
        return sample;
    }
};
} // namespace

int RunBrainFlowTests() {
    int passed = 0;
    try {
        Conditions conditions;
        Random random;
        const auto primaryGraph = Graph(Output(16, {{2, 0}}, "\t\t-1|\n"));
        auto state = MakeBrainFlowState(primaryGraph);
        conditions.primary = BrainConditionValue::False;
        Need(SelectBrainTransition(primaryGraph, 0, state, conditions, random).status ==
                 BrainFlowDecision::NoTransition && conditions.primaryCalls == 1,
             "Primary reference was treated as unconditional");
        conditions.primary = BrainConditionValue::True;
        auto decision = SelectBrainTransition(primaryGraph, 0, state, conditions, random);
        Need(decision.status == BrainFlowDecision::Transition && decision.targetNode == 1 &&
                 random.calls == 0 && state.outputs[0].weights[0] == 0,
             "Single destination changed RNG or selector weights");
        ++passed;
        const auto absent = Graph(Output(16, {{2, 0}}, "\t\t-1|\n"), 68);
        state = MakeBrainFlowState(absent);
        conditions.primary = BrainConditionValue::MissingPrimary;
        Need(SelectBrainTransition(absent, 0, state, conditions, random).status ==
                 BrainFlowDecision::Transition,
             "Absent native primary condition was not omitted");
        state = MakeBrainFlowState(primaryGraph);
        conditions.primary = BrainConditionValue::Unsupported;
        Need(SelectBrainTransition(primaryGraph, 0, state, conditions, random).status ==
                 BrainFlowDecision::Unsupported,
             "Missing condition context did not fail closed");
        ++passed;

        const auto conjunction = Graph(Output(16, {{2, 0}}, "\t\t41|0|\n\t\t65|0|\n"));
        state = MakeBrainFlowState(conjunction);
        conditions = Conditions();
        conditions.explicitValues["41"] = BrainConditionValue::False;
        Need(SelectBrainTransition(conjunction, 0, state, conditions, random).status ==
                 BrainFlowDecision::NoTransition && conditions.explicitCalls == 1,
             "AND did not short circuit before an unknown condition");
        conditions.explicitValues["41"] = BrainConditionValue::True;
        conditions.explicitValues["65"] = BrainConditionValue::True;
        Need(SelectBrainTransition(conjunction, 0, state, conditions, random).status ==
                 BrainFlowDecision::Transition && conditions.explicitCalls == 3,
             "AND did not evaluate every required condition");
        ++passed;

        const auto ordered = Graph(Output(16, {{2, 0}}, "\t\t-1|\n") + Output(16, {{3, 0}}));
        state = MakeBrainFlowState(ordered);
        conditions = Conditions();
        conditions.primary = BrainConditionValue::False;
        decision = SelectBrainTransition(ordered, 0, state, conditions, random);
        Need(decision.output == 1 && decision.targetNode == 2 && state.outputs[0].flags == 16,
             "False first output blocked a later passing output");
        conditions.primary = BrainConditionValue::True;
        decision = SelectBrainTransition(ordered, 0, state, conditions, random);
        Need(decision.output == 0 && decision.targetNode == 1,
             "Output source order did not select the first match");
        ++passed;
        for (unsigned flags : {0u, 16u, 16u | 0x40000u}) {
            const auto tagged = Graph(Output(flags, {{2, 0}}) + Output(16, {{3, 0}}));
            auto taggedState = MakeBrainFlowState(tagged);
            const auto selected = SelectBrainTransition(tagged, 0, taggedState, conditions, random);
            Need(selected.output == 0 && selected.targetNode == 1 &&
                     taggedState.outputs[0].flags == (flags | 0x80000u),
                 "Preserved normal-pass markers changed first-success selection");
        }
        ++passed;

        const auto weighted = Graph(Output(16, {{2, 1}, {3, 3}}));
        state = MakeBrainFlowState(weighted);
        random.sample = 8191;
        Need(SelectBrainTransition(weighted, 0, state, conditions, random).targetNode == 1,
             "Weighted interval lower endpoint changed");
        random.sample = 8192;
        Need(SelectBrainTransition(weighted, 0, state, conditions, random).targetNode == 2,
             "Weighted interval boundary changed");
        random.sample = 32767;
        Need(SelectBrainTransition(weighted, 0, state, conditions, random).targetNode == 2,
             "Weighted interval upper endpoint changed");
        ++passed;
        const auto uniform = Graph(Output(16, {{2, 0}, {3, 0}}));
        state = MakeBrainFlowState(uniform);
        random.sample = 0;
        Need(SelectBrainTransition(uniform, 0, state, conditions, random).targetNode == 1 &&
                 state.outputs[0].weights == std::vector<std::uint32_t>({1, 1}),
             "All-zero weights did not become uniform");
        random.sample = 32767;
        Need(SelectBrainTransition(uniform, 0, state, conditions, random).targetNode == 2,
             "Uniform selection omitted a destination");
        ++passed;

        const auto once = Graph(Output(17, {{2, 0}}));
        state = MakeBrainFlowState(once);
        Need(SelectBrainTransition(once, 0, state, conditions, random).status == BrainFlowDecision::Transition &&
                 SelectBrainTransition(once, 0, state, conditions, random).status == BrainFlowDecision::NoTransition,
             "Processed one-shot output fired again");
        const auto probe = Graph(Output(32, {{2, 0}}, "\t\t-1|\n") + Output(16, {{3, 0}}));
        state = MakeBrainFlowState(probe);
        conditions = Conditions();
        conditions.primary = BrainConditionValue::Unsupported;
        Need(SelectBrainTransition(probe, 0, state, conditions, random).targetNode == 2 &&
                 conditions.primaryCalls == 0,
             "Probe-only output entered a normal pass");
        ++passed;

        const auto alternate = Graph(Output(64, {{2, 0}}));
        state = MakeBrainFlowState(alternate);
        Need(SelectBrainTransition(alternate, 0, state, conditions, random).status == BrainFlowDecision::Unsupported,
             "Unknown activation mode was executed");
        const auto decay = Graph(Output(16, {{2, 1}, {3, 1}}, "", "0.5"));
        state = MakeBrainFlowState(decay);
        const auto calls = random.calls;
        Need(SelectBrainTransition(decay, 0, state, conditions, random).status == BrainFlowDecision::Unsupported &&
                 random.calls == calls && state.outputs[0].flags == 16,
             "Unsupported decay changed selector state");
        ++passed;
        const auto overflow = Graph(Output(16, {{2, 65535}, {3, 1}}));
        state = MakeBrainFlowState(overflow);
        Need(SelectBrainTransition(overflow, 0, state, conditions, random).status == BrainFlowDecision::Unsupported &&
                 random.calls == calls,
             "Unproven weight overflow was evaluated");
        state = MakeBrainFlowState(uniform);
        random.sample = 32768;
        Need(SelectBrainTransition(uniform, 0, state, conditions, random).status == BrainFlowDecision::Unsupported &&
                 state.outputs[0].flags == 16 && state.outputs[0].weights[0] == 0,
             "Invalid random input committed a transition");
        ++passed;
        const auto nested = Graph(Output(16, {{2, 0}}, "\t\t65|0|\n\t\t\t74|0|\n"));
        state = MakeBrainFlowState(nested);
        Need(SelectBrainTransition(nested, 0, state, conditions, random).status == BrainFlowDecision::Unsupported,
             "Nested condition semantics were guessed");
        const auto copy = uniform;
        state = MakeBrainFlowState(uniform);
        Need(SelectBrainTransition(copy, 0, state, conditions, random).status == BrainFlowDecision::Unsupported,
             "Selector state was reused with another document");
        ++passed;

        Need(BrainTimerElapsed(100, 100, 0) == BrainConditionValue::False &&
                 BrainTimerElapsed(101, 100, 0) == BrainConditionValue::True &&
                 BrainTimerElapsed(200, 100, 100) == BrainConditionValue::False &&
                 BrainTimerElapsed(201, 100, 100) == BrainConditionValue::True,
             "Timer lost the strict greater-than boundary");
        Need(BrainTimerElapsed(100000, 0, -1) == BrainConditionValue::False &&
                 BrainTimerElapsed(100000, 0, -1, true) == BrainConditionValue::True &&
                 BrainTimerElapsed(0, std::numeric_limits<std::int32_t>::max(), 1) ==
                     BrainConditionValue::Unsupported,
             "Timer sentinel, inversion or overflow boundary changed");
        ++passed;

        // Probe pass: marks every eligible probe in source order, selects nothing.
        const auto probes = Graph(Output(32, {{2, 0}}, "\t\t41|0|\n") + Output(16, {{3, 0}}) +
                                  Output(32, {{3, 0}}) + Output(33, {{2, 0}}) +
                                  Output(0x100020, {{2, 0}}) + Output(32, {}) +
                                  Output(32, {{2, 0}, {3, 0}}));
        conditions.explicitValues.clear();
        conditions.explicitValues["41"] = BrainConditionValue::True;
        state = MakeBrainFlowState(probes);
        random.calls = 0;
        random.sample = 0;
        std::size_t marked = 0;
        Need(MarkBrainProbeOutputs(probes, 0, state, conditions, marked).status == BrainFlowDecision::NoTransition &&
                 marked == 4 && random.calls == 0,
             "Probe pass did not mark all eligible probes without selection");
        const std::uint32_t expectedFlags[] = {0x40020, 16, 0x40020, 0x40021, 0x100020, 32, 0x40020};
        for (std::size_t i = 0; i < 7; ++i)
            Need(state.outputs[i].flags == expectedFlags[i], "Probe marker placement changed");
        decision = SelectBrainTransition(probes, 0, state, conditions, random);
        Need(decision.status == BrainFlowDecision::Transition && decision.output == 1 &&
                 state.outputs[0].flags == 0x40020,
             "Normal pass consumed or refused a probe marker");
        decision = FireBrainMarkedProbeOutput(probes, 0, state, random);
        Need(decision.status == BrainFlowDecision::Transition && decision.targetNode == 1 &&
                 state.outputs[0].flags == 0x80020 && random.calls == 0,
             "Probe flush did not clear its marker and mark processed");
        Need(FireBrainMarkedProbeOutput(probes, 0, state, random).status == BrainFlowDecision::Unsupported &&
                 state.outputs[0].flags == 0x80020 &&
                 FireBrainMarkedProbeOutput(probes, 1, state, random).status == BrainFlowDecision::Unsupported,
             "An unmarked or normal output was fired as a probe");
        random.sample = 32767;
        decision = FireBrainMarkedProbeOutput(probes, 6, state, random);
        Need(decision.status == BrainFlowDecision::Transition && decision.destination == 1 &&
                 decision.targetNode == 2 && random.calls == 1,
             "Probe flush skipped the common weighted selection");
        random.sample = 0;
        FireBrainMarkedProbeOutput(probes, 3, state, random);
        state.outputs[2].flags &= ~0x40000u;
        Need(MarkBrainProbeOutputs(probes, 0, state, conditions, marked).status == BrainFlowDecision::NoTransition &&
                 marked == 1 && state.outputs[2].flags == 0x40020 && state.outputs[3].flags == 0x80021,
             "Processed probe outputs stayed eligible");
        state = MakeBrainFlowState(probes);
        conditions.explicitValues["41"] = BrainConditionValue::False;
        Need(MarkBrainProbeOutputs(probes, 0, state, conditions, marked).status == BrainFlowDecision::NoTransition &&
                 marked == 3 && state.outputs[0].flags == 32,
             "False probe condition was marked");
        conditions.explicitValues.erase("41");
        state = MakeBrainFlowState(probes);
        Need(MarkBrainProbeOutputs(probes, 0, state, conditions, marked).status == BrainFlowDecision::Unsupported,
             "Unsupported probe condition was treated as false");
        ++passed;
        for (const unsigned flags : {0x30u, 0x60u, 0x220u}) {
            const auto odd = Graph(Output(flags, {{2, 0}}));
            state = MakeBrainFlowState(odd);
            Need(MarkBrainProbeOutputs(odd, 0, state, conditions, marked).status == BrainFlowDecision::Unsupported &&
                     state.outputs[0].flags == flags,
                 "Unknown probe activation flags were accepted");
        }
        const auto heavy = Graph(Output(32, {{2, 40000}, {3, 40000}}));
        state = MakeBrainFlowState(heavy);
        Need(MarkBrainProbeOutputs(heavy, 0, state, conditions, marked).status == BrainFlowDecision::Unsupported &&
                 state.outputs[0].flags == 32,
             "Unproven probe weight overflow was marked");
        ++passed;
        std::cout << "BRAI flow self-tests: " << passed << " passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "BRAI flow test failed after " << passed << ": " << error.what() << '\n';
        return 1;
    }
}
} // namespace wolf2009

#ifdef WOLF2009_BRAIN_FLOW_TEST_MAIN
int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--self-test"))
        return wolf2009::RunBrainFlowTests();
    if (argc != 3 || std::string(argv[1]) != "--corpus-list") {
        std::cerr << "Usage: wolf2009-brain-flow --self-test | --corpus-list <private fixture list>\n";
        return 2;
    }
    try {
        std::ifstream list(argv[2]);
        if (!list)
            throw std::runtime_error("Cannot open private fixture list");
        std::size_t graphs = 0, nodes = 0, outputs = 0, destinations = 0, secondary = 0, multiple = 0;
        std::string path;
        while (std::getline(list, path)) {
            if (!path.empty() && path.back() == '\r')
                path.pop_back();
            if (path.empty())
                continue;
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot open private BRAI fixture");
            const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
                                                   std::istreambuf_iterator<char>()};
            const auto graph = wolf2009::DecodeBrainGraph(bytes);
            const auto state = wolf2009::MakeBrainFlowState(graph);
            ++graphs;
            nodes += graph.nodes.size();
            outputs += graph.outputs.size();
            for (std::size_t i = 0; i < graph.outputs.size(); ++i) {
                const auto &output = graph.outputs[i];
                if (!output.destinationsComplete || !state.outputs[i].supported)
                    throw std::runtime_error("Unsupported selector layout in private corpus");
                destinations += output.destinations.size();
                multiple += output.destinations.size() > 1 ? 1 : 0;
                for (const auto &destination : output.destinations)
                    secondary += destination.slot > 0 ? 1 : 0;
            }
        }
        std::cout << "BRAI corpus graphs=" << graphs << " nodes=" << nodes << " outputs=" << outputs
                  << " destinations=" << destinations << " secondary=" << secondary
                  << " multiTargetOutputs=" << multiple << '\n';
        std::cout << "IR/selector admission only; condition execution and Task lifecycle are not implied.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "BRAI corpus validation failed: " << error.what() << '\n';
        return 1;
    }
}
#endif
