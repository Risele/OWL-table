#include "synthesis.hpp"
#include <algorithm>
#include <numeric>
namespace b2f {
namespace engine {
void synth_basic(Pool &pool) {
    const auto &p = pool.problem;
    unsigned n = p.mapping.local_to_external.size();
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    pool.add(shannon(p, order, false), "baseline-shannon", false, {{"reserved", true}});
    if (!p.original_on.empty()) {
        Graph g(p.truth.inputs);
        std::vector<Ref> terms;
        for (auto cube : p.original_on) {
            std::vector<Ref> factors;
            for (unsigned i = 0; i < p.truth.inputs; ++i)
                if (cube.care & (1U << i)) {
                    auto r = g.add(Op::Input, Type::Bool, i);
                    factors.push_back(cube.bits & (1U << i) ? r : g.invert(r));
                }
            terms.push_back(fold(g, Op::And, factors));
        }
        auto r = fold(g, Op::Or, terms);
        pair_roots(g, r, false);
        pool.add(g, "original-cover", false, {{"reserved", true}});
    }
    for (bool phase : {false, true}) {
        uint32_t first = 0, care = (1U << n) - 1, count = 0;
        for (uint32_t k = 0; k < p.reduced.size(); ++k)
            if (bool(p.reduced[k]) != phase) {
                if (!count)
                    first = k;
                else
                    care &= ~(k ^ first);
                ++count;
            }
        unsigned literals = 0;
        for (unsigned i = 0; i < n; ++i)
            literals += (care >> i) & 1;
        if (count == 0 || count == (1U << (n - literals))) {
            Graph g(p.truth.inputs);
            std::vector<Ref> factors;
            for (unsigned i = 0; i < n; ++i)
                if (care & (1U << i)) {
                    auto r = g.add(Op::Input, Type::Bool, p.mapping.local_to_external[i]);
                    factors.push_back(first & (1U << i) ? r : g.invert(r));
                }
            Ref r = count ? fold(g, Op::And, factors) : g.add(Op::Constant, Type::Bool, 0);
            pair_roots(g, r, phase);
            pool.add(g, count ? "basic-cube" : "basic-constant", phase);
        }
        std::vector<unsigned> linear;
        bool constant = bool(p.reduced[0]) != phase;
        for (unsigned i = 0; i < n; ++i)
            if ((bool(p.reduced[1U << i]) != phase) != constant)
                linear.push_back(i);
        bool affine = true;
        for (uint32_t k = 0; k < p.reduced.size(); ++k) {
            bool v = constant;
            for (auto i : linear)
                v ^= (k >> i) & 1;
            if (v != (bool(p.reduced[k]) != phase)) {
                affine = false;
                break;
            }
        }
        if (affine) {
            Graph g(p.truth.inputs);
            std::vector<Ref> factors;
            for (auto i : linear)
                factors.push_back(g.add(Op::Input, Type::Bool, p.mapping.local_to_external[i]));
            auto r = fold(g, Op::Xor, factors);
            if (constant)
                r = g.invert(r);
            pair_roots(g, r, phase);
            pool.add(g, "basic-affine", phase);
        }
    }
    pool.backends.push_back({{"backend", "basic"},
                             {"stop_reason", "complete"},
                             {"verified_candidates", pool.candidates.size()}});
}
} // namespace engine
} // namespace b2f
