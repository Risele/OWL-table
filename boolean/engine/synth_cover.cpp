#include "synthesis.hpp"
#include <algorithm>
#include <limits>
#include <map>
namespace b2f {
namespace engine {
using Term = std::pair<uint32_t, uint32_t>;
static unsigned bits(uint32_t x) {
    unsigned n = 0;
    for (; x; x &= x - 1)
        ++n;
    return n;
}
static bool matches(Term c, uint32_t x) { return (x & c.first) == c.second; }
static Ref literal(Graph &g, const Problem &p, unsigned local, bool value) {
    auto r = g.add(Op::Input, Type::Bool, p.mapping.local_to_external.at(local));
    return value ? r : g.invert(r);
}
static Graph cover_graph(const Problem &p, const std::vector<Term> &terms, bool phase, bool cnf,
                         bool factor) {
    Graph g(p.truth.inputs);
    std::function<Ref(std::vector<Term>)> build = [&](std::vector<Term> rows) -> Ref {
        if (factor && rows.size() > 1) {
            std::map<std::pair<unsigned, bool>, unsigned> frequency;
            for (auto row : rows)
                for (unsigned i = 0; i < p.mapping.local_to_external.size(); ++i)
                    if (row.first & (1U << i))
                        ++frequency[{i, bool(row.second & (1U << i))}];
            std::pair<unsigned, bool> selected;
            unsigned count = 1;
            for (auto item : frequency)
                if (item.second > count) {
                    selected = item.first;
                    count = item.second;
                }
            if (count > 1) {
                uint32_t bit = 1U << selected.first;
                std::vector<Term> inside, outside;
                for (auto row : rows)
                    if ((row.first & bit) && bool(row.second & bit) == selected.second)
                        inside.push_back({row.first & ~bit, row.second & ~bit});
                    else
                        outside.push_back(row);
                Ref grouped =
                    g.add(Op::And, Type::Bool, 0,
                          {literal(g, p, selected.first, selected.second), build(inside)});
                return outside.empty() ? grouped
                                       : g.add(Op::Or, Type::Bool, 0, {grouped, build(outside)});
            }
        }
        std::vector<Ref> sums;
        for (auto c : rows) {
            std::vector<Ref> factors;
            for (unsigned i = 0; i < p.mapping.local_to_external.size(); ++i)
                if (c.first & (1U << i))
                    factors.push_back(literal(g, p, i, bool(c.second & (1U << i)) != cnf));
            sums.push_back(fold(g, cnf ? Op::Or : Op::And, factors));
        }
        return fold(g, cnf ? Op::And : Op::Or, sums);
    };
    auto r = build(terms);
    pair_roots(g, r, phase != cnf);
    return g;
}
static std::vector<Term> primes(const Problem &p, bool phase, SearchBudget &budget) {
    uint32_t mask = p.reduced.size() - 1;
    std::set<Term> current, result;
    for (uint32_t k = 0; k < p.reduced.size(); ++k)
        if (bool(p.reduced[k]) != phase)
            current.insert({mask, k});
    while (!current.empty()) {
        std::set<Term> next, merged;
        for (auto c : current)
            for (unsigned i = 0; i < p.mapping.local_to_external.size(); ++i) {
                budget.spend();
                uint32_t bit = 1U << i;
                if ((c.first & bit) && !(c.second & bit)) {
                    Term other{c.first, c.second | bit};
                    if (current.count(other)) {
                        next.insert({c.first & ~bit, c.second});
                        merged.insert(c);
                        merged.insert(other);
                    }
                }
            }
        for (auto c : current)
            if (!merged.count(c))
                result.insert(c);
        current = std::move(next);
    }
    return {result.begin(), result.end()};
}
static std::vector<Term> greedy_cover(const Problem &p, bool phase, const std::vector<Term> &primes,
                                      SearchBudget &budget) {
    std::set<uint32_t> uncovered;
    for (uint32_t k = 0; k < p.reduced.size(); ++k)
        if (bool(p.reduced[k]) != phase)
            uncovered.insert(k);
    std::vector<Term> result;
    while (!uncovered.empty()) {
        unsigned best = 0;
        Term chosen{0xffffffffU, 0};
        for (auto c : primes) {
            unsigned count = 0;
            for (auto k : uncovered) {
                budget.spend();
                count += matches(c, k);
            }
            if (count > best || (count == best && std::make_pair(bits(c.first), c) <
                                                      std::make_pair(bits(chosen.first), chosen))) {
                best = count;
                chosen = c;
            }
        }
        if (!best)
            throw std::runtime_error("incomplete prime cover");
        result.push_back(chosen);
        for (auto i = uncovered.begin(); i != uncovered.end();)
            if (matches(chosen, *i))
                i = uncovered.erase(i);
            else
                ++i;
    }
    return result;
}
static std::vector<Term> grow_cover(const Problem &p, bool phase, SearchBudget &budget) {
    std::set<uint32_t> uncovered;
    for (uint32_t k = 0; k < p.reduced.size(); ++k)
        if (bool(p.reduced[k]) != phase)
            uncovered.insert(k);
    std::vector<Term> result;
    uint32_t mask = p.reduced.size() - 1;
    while (!uncovered.empty()) {
        Term c{mask, *uncovered.begin()};
        for (unsigned i = 0; i < p.mapping.local_to_external.size(); ++i) {
            Term trial{c.first & ~(1U << i), c.second & ~(1U << i)};
            uint32_t free = mask & ~trial.first, subset = free;
            bool valid = true;
            for (;;) {
                budget.spend();
                if (bool(p.reduced[trial.second | subset]) == phase) {
                    valid = false;
                    break;
                }
                if (!subset)
                    break;
                subset = (subset - 1) & free;
            }
            if (valid)
                c = trial;
        }
        result.push_back(c);
        for (auto i = uncovered.begin(); i != uncovered.end();) {
            budget.spend();
            if (matches(c, *i))
                i = uncovered.erase(i);
            else
                ++i;
        }
    }
    return result;
}
void synth_cover(Pool &pool, SearchBudget &budget, const Options &options) {
    const auto &p = pool.problem;
    unsigned n = p.mapping.local_to_external.size();
    for (bool phase : {false, true}) {
        uint64_t start = budget.used;
        Json report = {{"backend", "cover"},
                       {"phase", phase},
                       {"exact", false},
                       {"objective", "cube_count_then_literal_count"}};
        try {
            std::vector<Term> choices, selected;
            if (n <= 6) {
                choices = primes(p, phase, budget);
                report["primes"] = choices.size();
                if (choices.size() > 256)
                    throw std::length_error("prime_limit");
                selected = greedy_cover(p, phase, choices, budget);
            } else
                selected = grow_cover(p, phase, budget);
            auto add = [&](const std::vector<Term> &terms, const std::string &name, bool exact) {
                Json data = {{"cover", Json::array()},
                             {"exact", exact},
                             {"objective", "cube_count_then_literal_count"},
                             {"literals", 0}};
                unsigned literals = 0;
                for (auto c : terms) {
                    data["cover"].push_back({{"care", c.first}, {"bits", c.second}});
                    literals += bits(c.first);
                }
                data["literals"] = literals;
                for (int mode = 0; mode < 3; ++mode) {
                    auto graph = cover_graph(p, terms, phase, mode == 1, mode == 2);
                    if (graph.nodes().size() > options.max_nodes)
                        throw std::length_error("node_limit");
                    pool.add(graph,
                             name + (mode == 0   ? "-sop"
                                     : mode == 1 ? "-cnf"
                                                 : "-factored"),
                             phase != (mode == 1), data);
                }
            };
            add(selected, "greedy-cover", false);
            if (n <= 6) {
                uint64_t target = 0;
                for (uint32_t k = 0; k < p.reduced.size(); ++k)
                    if (bool(p.reduced[k]) != phase)
                        target |= uint64_t(1) << k;
                std::vector<uint64_t> covers;
                for (auto c : choices) {
                    uint64_t cover = 0;
                    for (uint32_t k = 0; k < p.reduced.size(); ++k)
                        if (matches(c, k))
                            cover |= uint64_t(1) << k;
                    covers.push_back(cover);
                }
                auto best = selected;
                unsigned best_literals = 0;
                for (auto c : best)
                    best_literals += bits(c.first);
                uint64_t states = 0;
                std::vector<size_t> path;
                std::function<void(uint64_t, unsigned)> dfs = [&](uint64_t remaining,
                                                                  unsigned literals) {
                    budget.spend();
                    ++states;
                    if (!remaining) {
                        if (path.size() < best.size() ||
                            (path.size() == best.size() && literals < best_literals)) {
                            best.clear();
                            for (auto i : path)
                                best.push_back(choices[i]);
                            best_literals = literals;
                        }
                        return;
                    }
                    if (path.size() >= best.size())
                        return;
                    unsigned bit = 0;
                    while (!(remaining & (uint64_t(1) << bit)))
                        ++bit;
                    for (size_t i = 0; i < choices.size(); ++i)
                        if (covers[i] & (uint64_t(1) << bit)) {
                            path.push_back(i);
                            dfs(remaining & ~covers[i], literals + bits(choices[i].first));
                            path.pop_back();
                        }
                };
                dfs(target, 0);
                report["states"] = states;
                report["exact"] = true;
                report["minimum_cubes"] = best.size();
                report["minimum_literals"] = best_literals;
                add(best, "exact-cover", true);
            }
            report["stop_reason"] = "complete";
        } catch (const std::length_error &e) {
            report["stop_reason"] = e.what();
            report["exact"] = false;
        }
        report["work_used"] = budget.used - start;
        report["peak_terms_bound"] = n <= 6 ? 729 : 65536;
        pool.backends.push_back(report);
    }
}
} // namespace engine
} // namespace b2f
