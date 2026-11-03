// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrainLiteralActions.h"
#include <iostream>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool v, const char *s) { if (!v) throw std::runtime_error(s); }
std::vector<unsigned char> Bytes(std::string s) { return {s.begin(), s.end()}; }
BrainGraph Graph(std::string s) {
    s = "h1023|32|32|32|0|\n" + s;
    for (auto &c : s) if (c == '|') c = 0;
    return DecodeBrainGraph(Bytes(s));
}
std::string Node(unsigned type, unsigned id, const std::string &fields = "") {
    return std::to_string(type) + '|' + std::to_string(id) + "|0|0|0|^" + std::to_string(type) + "|0|" + fields + '\n';
}
std::string Output(unsigned target, const std::string &condition = "\t\t-1|\n") {
    std::string s = "\t";
    for (unsigned i = 0; i < 99; ++i)
        s += (i == 0 ? "16" : i == 1 ? "1" : i == 2 ? std::to_string(target) : i == 98 ? "1" : "0") + std::string("|");
    return s + '\n' + condition;
}
}
int RunBrainLiteralActionTests() {
    unsigned checks = 0;
    const auto check = [&](bool v, const char *s) { Need(v,s); ++checks; };
    try {
        for (const auto &s : {"0", "1", "2147483647"}) {
            std::int32_t out = -1; check(BrainCanonicalNonnegativeInteger(s,out) && out >= 0,"canonical literal refused");
        }
        for (const auto &s : {"", "-1", "+1", "01", " 1", "1 ", "1x", "2147483648", "9999999999999999", "1.0", "1+2", "@x", "0x10"}) {
            std::int32_t out = 73; check(!BrainCanonicalNonnegativeInteger(s,out) && out == 73,"noncanonical literal admitted/output changed");
        }
        const auto spawn = Node(1,1,"^74|1024|") + Output(2);
        const auto set = Node(220,2,"mVar|test_value|mExpression|7|");
        auto p = PrepareBrainLiteralActions(Graph(spawn + set));
        check(p.supported && p.nodes[1].value == 7,"own literal set graph refused");
        for (const auto &field : {"mExpression|1 + 2|", "mExpression|@test|", "mExpression|01|", "mExpression|-1|"})
            check(!PrepareBrainLiteralActions(Graph(spawn + Node(220,2,"mVar|test_value|" + std::string(field)))).supported,
                "expression admission broadened");
        check(!PrepareBrainLiteralActions(Graph(spawn + set + Node(999,3))).supported,"unreachable unknown node admitted");
        check(!PrepareBrainLiteralActions(Graph(spawn + Node(220,2,"mVar|test_value|mExpression|7|other|0|"))).supported,
            "unknown field admitted");
        check(!PrepareBrainLiteralActions(Graph(spawn + Node(220,2,"mVar|test_value|mExpression|7|mExpression|7|"))).supported,
            "duplicate field admitted");
        for (const auto &selector : {"MyTarget 1", "mytarget", "Player", "@chosen", "bad/name", "two words", "23"})
            check(!PrepareBrainLiteralActions(Graph(Node(68,1,"mUseTarget|" + std::string(selector) + '|'))).supported,
                "unqualified selector admitted");
        p = PrepareBrainLiteralActions(Graph(Node(68,1,"mActor|probe_actor|mUseTarget|probe_receiver|mUseKey||")));
        check(p.supported && p.nodes[0].actor.name == "probe_actor" && p.nodes[0].target.name == "probe_receiver",
            "actor and target literal binding confused");
        p = PrepareBrainLiteralActions(Graph(Node(68,1)));
        check(p.supported && p.nodes[0].actor.kind == BrainLiteralSelector::Me &&
            p.nodes[0].target.kind == BrainLiteralSelector::Me,"constructor Me defaults lost");
        for (const auto &duration : {"-1 0", "0 0", "32 0"})
            check(PrepareBrainLiteralActions(Graph(Node(30,1,"^65|1024|mDuration|"+std::string(duration)+'|'))).supported,
                "fixed caller-clock timer refused");
        for (const auto &duration : {"32 1", "1.5 0", "-2 0", "@clock 0", "8388609 0"})
            check(!PrepareBrainLiteralActions(Graph(Node(30,1,"mDuration|"+std::string(duration)+'|'))).supported,
                "unqualified duration conversion admitted");
        check(!PrepareBrainLiteralActions(Graph(Node(30,1,"mActualDuration|5|"))).supported,"serialized armed timer admitted");
        check(!PrepareBrainLiteralActions(Graph(Node(1,1) + Output(2,"\t\t221|mExpression|x == 1|\n") + set)).supported,
            "TestHost external expression admitted");
        check(!PrepareBrainLiteralActions(Graph(Node(1,1) + Output(2) + Node(3,2))).supported,"root destination admitted");
        auto invalid = Graph(spawn + set);
        invalid.nodes[0].outputs[0] = invalid.outputs.size();
        check(!PrepareBrainLiteralActions(invalid).supported,"invalid output index reached flow decoder");
        invalid = Graph(spawn + set); invalid.outputs[0].record = invalid.records.size();
        check(!PrepareBrainLiteralActions(invalid).supported,"invalid output record reached flow decoder");
        invalid = Graph(spawn + set); invalid.outputs[0].destinations[0].slot = static_cast<std::size_t>(-1);
        check(!PrepareBrainLiteralActions(invalid).supported,"invalid slot reached output token arithmetic");
        BrainLiteralGlobals globals; globals["unchanged"] = 11; std::string reason;
        check(DecodeBrainLiteralGlobals(Bytes("wolf2009-brain-globals-v1\nvalue=7\nzero=0\n"),globals,reason) &&
            globals.size() == 2 && globals["value"] == 7,"explicit defaults failed");
        for (const auto &s : {"", "wolf2009-brain-globals-v1\n", "value=1\n", "wolf2009-brain-globals-v1\nvalue=1",
                "wolf2009-brain-globals-v1\nvalue=1\nvalue=2\n", "wolf2009-brain-globals-v1\nValue=1\n",
                "wolf2009-brain-globals-v1\nvalue=2147483648\n", "wolf2009-brain-globals-v1\nvalue=1 + 2\n",
                "wolf2009-brain-globals-v1\nvalue=1\n\n"}) {
            const auto before = globals;
            check(!DecodeBrainLiteralGlobals(Bytes(s),globals,reason) && globals == before && !reason.empty(),
                "bad defaults accepted or failed parse changed globals");
        }
        std::string many = "wolf2009-brain-globals-v1\n";
        for (unsigned i = 0; i < 257; ++i) many += "value_" + std::to_string(i) + "=0\n";
        check(!DecodeBrainLiteralGlobals(Bytes(many),globals,reason),"defaults name budget ignored");
        check(!DecodeBrainLiteralGlobals(std::vector<unsigned char>(65537,'x'),globals,reason),"defaults byte budget ignored");
        std::cout << "Brain literal action admission: " << checks << " checks PASS\n"; return 0;
    } catch (const std::exception &e) {
        std::cerr << "Brain literal action admission failed after " << checks << ": " << e.what() << '\n'; return 1;
    }
}
} // namespace wolf2009
#ifdef WOLF2009_BRAIN_LITERAL_TEST_MAIN
int main() { return wolf2009::RunBrainLiteralActionTests(); }
#endif
