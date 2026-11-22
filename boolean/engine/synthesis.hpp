#pragma once
#include "ir.hpp"
#include <functional>
#include <set>
namespace b2f {
namespace engine {
struct Cube {
    uint32_t care, bits;
};
struct Problem {
    TruthBits truth;
    std::vector<Cube> original_on;
    Support mapping;
    std::vector<uint8_t> reduced;
    explicit Problem(const TruthBits &);
};
struct SearchBudget {
    uint64_t limit, used = 0;
    explicit SearchBudget(uint64_t maximum) : limit(maximum) {}
    void spend(uint64_t count = 1);
};
struct SearchCandidate {
    Graph graph;
    std::string method;
    bool inverted_first;
    Json evidence;
    SearchCandidate(Graph g, std::string name, bool phase, Json details);
};
struct Pool {
    const Problem &problem;
    std::vector<SearchCandidate> candidates;
    Json backends = Json::array();
    unsigned capacity;
    explicit Pool(const Problem &p, unsigned maximum=24) : problem(p), capacity(maximum) {}
    void add(Graph graph, const std::string &, bool phase, Json details = Json::object());
};
struct Options {
    uint64_t work_limit = 100000;
    unsigned max_nodes = 100000, table_bytes = 256, max_candidates = 24;
    bool allow_full_lut = false;
    static Options read(const Json &);
};
std::vector<bool> reachable(const Graph &);
Json logical_metrics(const Graph &);
Graph prune(const Graph &);
Ref fold(Graph &, Op, std::vector<Ref>);
void pair_roots(Graph &, Ref base, bool inverted_first);
Graph shannon(const Problem &, const std::vector<int> &order, bool phase,
              SearchBudget *budget = nullptr);
void synth_basic(Pool &);
void synth_cover(Pool &, SearchBudget &, const Options &);
void synth_advanced(Pool &, SearchBudget &, const Options &);
Json search(const Problem &, int stage, const Options &);
} // namespace engine
} // namespace b2f
