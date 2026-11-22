#pragma once
#include "../cpp/core.hpp"
#include "engine/contracts.hpp"
#include <functional>
namespace b2f {
using namespace t2f;
struct Rule {
    uint32_t care, bits;
    bool output;
    S comment;
};
struct Spec {
    std::vector<S> inputs;
    S output, inverted;
    std::vector<Rule> rules;
    engine::TruthBits truth;
    bool dense = false;
    std::vector<S> comments;
    std::vector<S> input_comments;
    std::vector<unsigned> input_colors;
    std::vector<unsigned> row_order;
};
J canonical(const Spec &);
struct Plan {
    J nodes;
    int root;
    S method;
    size_t cost;
};
J fresh(int count = 5, bool complete = true);
Spec parse(const J &, const S &macro = "");
bool evaluate(const Spec &, uint32_t);
J resize(const J &, int);
J expand(const J &);
J compact(const J &);
Book load(const U &, const S &sheet = "");
void save(const U &, const J &);
Plan compile(const Spec &);
bool evaluate(const Plan &, uint32_t);
J generate(const J &, const U &, const S &, bool fbd = true, bool st = true);
int cli(const std::vector<S> &);
std::vector<S> arguments();
} // namespace b2f
