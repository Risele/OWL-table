#include "synthesis.hpp"
#include <algorithm>
#include <map>
#include <numeric>
namespace b2f {
namespace engine {
static Ref function(Graph &g, const std::vector<Ref> &vars, const std::vector<uint8_t> &truth,
                    SearchBudget &budget) {
    std::map<std::pair<unsigned, std::vector<uint8_t>>, Ref> memo;
    std::function<Ref(unsigned, std::vector<uint8_t>)> rec =
        [&](unsigned level, std::vector<uint8_t> values) -> Ref {
        budget.spend(values.size() + 1);
        auto key = std::make_pair(level, values);
        auto it = memo.find(key);
        if (it != memo.end())
            return it->second;
        Ref r;
        if (std::all_of(values.begin(), values.end(), [&](uint8_t v) { return v == values[0]; }))
            r = g.add(Op::Constant, Type::Bool, values[0]);
        else {
            std::vector<uint8_t> lo, hi;
            for (size_t i = 0; i < values.size(); ++i)
                (i & 1 ? hi : lo).push_back(values[i]);
            auto a = rec(level + 1, lo), b = rec(level + 1, hi);
            r = g.add(Op::Mux, Type::Bool, 0, {vars.at(level), a, b});
        }
        memo[key] = r;
        return r;
    };
    return rec(0, truth);
}
static std::vector<Ref> inputs(Graph &g, const Problem &p) {
    std::vector<Ref> r;
    for (auto i : p.mapping.local_to_external)
        r.push_back(g.add(Op::Input, Type::Bool, i));
    return r;
}
// For a priority code reserve bit zero for the idle state.
static Ref packed(Graph &g, const std::vector<Ref> &vars, bool reserve_idle_bit = false) {
    auto zero = g.add(Op::Constant, Type::Udint, 0);
    Ref word = reserve_idle_bit ? g.add(Op::Constant, Type::Udint, 1) : zero;
    for (unsigned i = 0; i < vars.size(); ++i) {
        auto bit = g.add(Op::Constant, Type::Udint, 1U << (i + unsigned(reserve_idle_bit)));
        auto term = g.add(Op::Sel, Type::Udint, 0, {vars[i], zero, bit});
        word = (i || reserve_idle_bit) ? g.add(Op::WordOr, Type::Udint, 0, {word, term}) : term;
    }
    return word;
}
void synth_advanced(Pool &pool, SearchBudget &total, const Options &options) {
    const auto &p = pool.problem;
    unsigned n = p.mapping.local_to_external.size();
    auto attempt = [&](const std::string &name,
                       const std::function<void(SearchBudget &, Json &)> &run) {
        SearchBudget budget(
            std::min(total.limit - total.used, std::max(uint64_t(1), total.limit / 8)));
        Json report = {{"backend", name},
                       {"stop_reason", "complete"},
                       {"truth_workspace_bound_bytes", (n + 1) * p.reduced.size() * 4},
                       {"ir_node_hard_limit", 100000},
                       {"candidate_node_limit", options.max_nodes},
                       {"peak_rss_bytes", nullptr},
                       {"allocator_overhead_bytes", nullptr}};
        size_t start = pool.candidates.size();
        try {
            run(budget, report);
        } catch (const std::length_error &e) {
            report["stop_reason"] = e.what();
        }
        total.used += budget.used;
        report["work_used"] = budget.used;
        report["candidate_attempts_added"] = pool.candidates.size() - start;
        pool.backends.push_back(report);
    };
    auto add = [&](Graph g, const std::string &name, bool phase, const Json &data) {
        if (g.nodes().size() > options.max_nodes)
            throw std::length_error("node_limit");
        pool.add(std::move(g), name, phase, data);
    };
    // Both phases include the implicit zero rows. Source cubes are available only for Q.
    for (bool phase : {false, true}) {
        attempt(phase ? "row-masks-inverse" : "row-masks", [&](SearchBudget &budget, Json &report) {
            std::set<std::pair<unsigned, unsigned>> rows;
            unsigned full = unsigned(p.reduced.size()) - 1;
            if (!phase && !p.original_on.empty()) {
                for (const auto &cube : p.original_on) {
                    budget.spend();
                    unsigned mask = 0, bits = 0;
                    for (unsigned i = 0; i < n; ++i) {
                        unsigned bit = 1U << p.mapping.local_to_external[i];
                        if (cube.care & bit)
                            mask |= 1U << i;
                        if (cube.care & cube.bits & bit)
                            bits |= 1U << i;
                    }
                    rows.emplace(mask, bits);
                }
            } else {
                // A finished cover already includes the implicit default for this phase.
                const Json *cover = nullptr;
                for (const auto &candidate : pool.candidates) {
                    if (candidate.inverted_first != phase ||
                        !candidate.evidence.contains("cover") ||
                        candidate.method.find("-sop") == std::string::npos)
                        continue;
                    const auto &terms = candidate.evidence.at("cover");
                    if (!cover || terms.size() < cover->size())
                        cover = &terms;
                }
                if (cover) {
                    for (const auto &term : *cover) {
                        budget.spend();
                        rows.emplace(term.at("care").get<unsigned>(),
                                     term.at("bits").get<unsigned>());
                    }
                } else {
                    // Partition runs of matching keys into aligned power-of-two intervals.
                    for (unsigned k = 0; k <= full;) {
                        if ((k & 31U) == 0)
                            budget.spend();
                        if (bool(p.reduced[k]) == phase) {
                            ++k;
                            continue;
                        }
                        unsigned end = k + 1;
                        while (end <= full && bool(p.reduced[end]) != phase) {
                            if ((end & 31U) == 0)
                                budget.spend();
                            ++end;
                        }
                        while (k < end) {
                            unsigned width = 1;
                            while (2 * width <= end - k && !(k & (2 * width - 1)))
                                width *= 2;
                            budget.spend();
                            rows.emplace(full & ~(width - 1), k);
                            k += width;
                        }
                    }
                }
            }
            // Word graphs use the scalar verifier on every external input combination.
            if (rows.size() > 1 &&
                uint64_t(rows.size()) * (1U << p.truth.inputs) >
                    std::max<uint64_t>(1U << p.truth.inputs, options.work_limit * 32))
                throw std::length_error("row_mask_verification_budget");
            Graph g(p.truth.inputs);
            auto constant = [&](unsigned v) { return g.add(Op::Constant, Type::Udint, v); };
            auto word = constant(0);
            auto vars = inputs(g, p);
            for (unsigned i = 0; i < n; ++i)
                word = g.add(Op::Putbit, Type::Udint, 0, {word, constant(i), vars[i]});
            std::map<unsigned, Ref> masked;
            masked.emplace(full, word);
            std::vector<Ref> matches;
            for (auto row : rows) {
                budget.spend();
                if (!row.first) {
                    matches = {g.add(Op::Constant, Type::Bool, 1)};
                    break;
                }
                if (!masked.count(row.first))
                    masked[row.first] =
                        g.add(Op::WordAnd, Type::Udint, 0, {word, constant(row.first)});
                matches.push_back(g.add(Op::WordEqual, Type::Bool, 0,
                                        {masked.at(row.first), constant(row.second)}));
                if (g.nodes().size() > options.max_nodes)
                    throw std::length_error("node_limit");
            }
            pair_roots(g, fold(g, Op::Or, matches), phase);
            report["rows"] = rows.size();
            report["packing_bits"] = n;
            add(std::move(g), phase ? "row-masks-inverse" : "row-masks", phase, report);
        });
    }
    attempt("anf", [&](SearchBudget &budget, Json &report) {
        auto a = p.reduced;
        for (unsigned i = 0; i < n; ++i)
            for (uint32_t k = 0; k < a.size(); ++k)
                if (k & (1U << i)) {
                    budget.spend();
                    a[k] ^= a[k ^ (1U << i)];
                }
        std::vector<uint32_t> terms;
        for (uint32_t k = 0; k < a.size(); ++k)
            if (a[k])
                terms.push_back(k);
        report["monomials"] = terms.size();
        if (terms.size() > options.max_nodes / 2)
            throw std::length_error("node_preflight");
        Graph g(p.truth.inputs);
        auto vars = inputs(g, p);
        std::function<Ref(std::vector<uint32_t>)> poly = [&](std::vector<uint32_t> rows) -> Ref {
            budget.spend(rows.size() + 1);
            if (rows.empty())
                return g.add(Op::Constant, Type::Bool, 0);
            if (rows.size() == 1 && !rows[0])
                return g.add(Op::Constant, Type::Bool, 1);
            unsigned best = 0, count = 0;
            for (unsigned i = 0; i < n; ++i) {
                unsigned c = 0;
                for (auto r : rows)
                    c += (r >> i) & 1;
                if (c > count) {
                    best = i;
                    count = c;
                }
            }
            std::vector<uint32_t> lo, hi;
            for (auto r : rows)
                if (r & (1U << best))
                    hi.push_back(r & ~(1U << best));
                else
                    lo.push_back(r);
            auto high = poly(hi);
            Ref product = hi.size() == 1 && hi[0] == 0
                              ? vars[best]
                              : g.add(Op::And, Type::Bool, 0, {vars[best], high});
            return lo.empty() ? product : g.add(Op::Xor, Type::Bool, 0, {poly(lo), product});
        };
        auto r = poly(terms);
        pair_roots(g, r, false);
        add(g, "anf-factored", false, report);
    });
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    for (unsigned variant = 0; variant < 3; ++variant)
        attempt("bdd-" + std::to_string(variant), [&](SearchBudget &budget, Json &report) {
            auto current = order;
            if (variant == 1)
                std::reverse(current.begin(), current.end());
            if (variant == 2 && n)
                std::rotate(current.begin(), current.begin() + n / 2, current.end());
            report["local_order"] = current;
            report["complemented_cofactors"] = true;
            auto g = shannon(p, current, false, &budget);
            add(g, "bdd-" + std::to_string(variant), false, report);
        });
    for (unsigned leaf_size : {5U, 4U})
        attempt("lut-" + std::to_string(leaf_size), [&](SearchBudget &budget, Json &report) {
            unsigned cut = std::min(n, leaf_size), leaves = 1U << (n - cut), bytes = 4 * leaves;
            report["table_bytes"] = bytes;
            if (bytes > options.table_bytes)
                throw std::length_error("aggregate_table_budget");
            Graph g(p.truth.inputs);
            auto vars = inputs(g, p);
            std::vector<Ref> leaf_inputs(vars.begin(), vars.begin() + cut);
            auto index = packed(g, leaf_inputs);
            std::vector<Ref> level;
            std::set<uint32_t> words;
            for (unsigned slice = 0; slice < leaves; ++slice) {
                uint32_t word = 0;
                for (unsigned k = 0; k < (1U << cut); ++k) {
                    budget.spend();
                    if (p.reduced[(slice << cut) | k])
                        word |= 1U << k;
                }
                words.insert(word);
                auto data = g.add(Op::Constant, Type::Udint, word);
                level.push_back(g.add(Op::Extract, Type::Bool, 0, {data, index}));
            }
            for (unsigned bit = cut; level.size() > 1; ++bit) {
                std::vector<Ref> next;
                for (size_t i = 0; i < level.size(); i += 2) {
                    budget.spend();
                    next.push_back(
                        g.add(Op::Mux, Type::Bool, 0, {vars[bit], level[i], level[i + 1]}));
                }
                level = std::move(next);
            }
            report["unique_words"] = words.size();
            report["leaf_instances"] = leaves;
            report["index_bits"] = cut;
            pair_roots(g, level[0], false);
            add(g, "small-lut-" + std::to_string(cut), false, report);
        });
    attempt("cofactor-encoding", [&](SearchBudget &budget, Json &report) {
        if (n < 4) {
            report["stop_reason"] = "support_too_small";
            return;
        }
        unsigned a = n / 2, b = n - a;
        std::map<std::vector<uint8_t>, unsigned> classes;
        std::vector<std::vector<uint8_t>> functions;
        std::vector<unsigned> codes;
        std::vector<uint8_t> phases;
        for (unsigned x = 0; x < (1U << a); ++x) {
            std::vector<uint8_t> cofactor;
            for (unsigned y = 0; y < (1U << b); ++y) {
                budget.spend();
                cofactor.push_back(p.reduced[x | (y << a)]);
            }
            uint8_t phase = cofactor[0];
            for (auto &v : cofactor)
                v ^= phase;
            auto it = classes.find(cofactor);
            unsigned code;
            if (it == classes.end()) {
                code = functions.size();
                classes[cofactor] = code;
                functions.push_back(cofactor);
            } else
                code = it->second;
            codes.push_back(code);
            phases.push_back(phase);
        }
        unsigned width = 0;
        while ((1U << width) < functions.size())
            ++width;
        report["class_count"] = functions.size();
        report["code_bits"] = width;
        if (width >= a) {
            report["stop_reason"] = "no_compression";
            return;
        }
        Graph g(p.truth.inputs);
        auto vars = inputs(g, p);
        std::vector<Ref> avars(vars.begin(), vars.begin() + a), decoder;
        for (unsigned bit = 0; bit < width; ++bit) {
            std::vector<uint8_t> values;
            for (auto code : codes)
                values.push_back((code >> bit) & 1);
            decoder.push_back(function(g, avars, values, budget));
        }
        decoder.insert(decoder.end(), vars.begin() + a, vars.end());
        std::vector<uint8_t> decoded(1U << (width + b));
        for (unsigned k = 0; k < decoded.size(); ++k) {
            budget.spend();
            unsigned code = k & ((1U << width) - 1);
            decoded[k] = code < functions.size() ? functions[code][k >> width] : 0;
        }
        auto value = function(g, decoder, decoded, budget),
             phase = function(g, avars, phases, budget);
        auto r = g.add(Op::Xor, Type::Bool, 0, {value, phase});
        pair_roots(g, r, false);
        report["a_external"] = std::vector<int>(p.mapping.local_to_external.begin(),
                                                p.mapping.local_to_external.begin() + a);
        report["b_external"] = std::vector<int>(p.mapping.local_to_external.begin() + a,
                                                p.mapping.local_to_external.end());
        report["class_by_a"] = codes;
        report["phase_by_a"] = phases;
        report["unreachable_codes"] = Json::array();
        for (unsigned c = functions.size(); c < (1U << width); ++c)
            report["unreachable_codes"].push_back(c);
        report["unreachable_value"] = 0;
        add(g, "cofactor-encoding", false, report);
    });
    attempt("udint-masks", [&](SearchBudget &budget, Json &report) {
        // Search exact masked reductions, including bit-parallel pair products.
        unsigned tested = 0;
        for (unsigned mode = 0; mode < 3; ++mode)
            for (bool phase : {false, true}) {
                bool matches = true;
                for (unsigned k = 0; k < p.reduced.size(); ++k) {
                    budget.spend();
                    uint32_t word = k;
                    if (mode == 1)
                        word = (word ^ (word >> (n / 2))) & ((1U << (n / 2)) - 1);
                    if (mode == 2)
                        word = (word & (word >> (n / 2))) & ((1U << (n / 2)) - 1);
                    bool value = false;
                    if (mode == 0) {
                        for (auto x = word; x; x &= x - 1)
                            value = !value;
                    } else
                        value = word != 0;
                    if ((value != phase) != bool(p.reduced[k])) {
                        matches = false;
                        break;
                    }
                }
                ++tested;
                if (!matches)
                    continue;
                Graph g(p.truth.inputs);
                auto vars = inputs(g, p);
                auto word = packed(g, vars);
                auto constant = [&](uint32_t v) { return g.add(Op::Constant, Type::Udint, v); };
                if (mode) {
                    auto shifted = g.add(Op::Shr, Type::Udint, 0, {word, constant(n / 2)});
                    word = g.add(mode == 1 ? Op::WordXor : Op::WordAnd, Type::Udint, 0,
                                 {word, shifted});
                    word =
                        g.add(Op::WordAnd, Type::Udint, 0, {word, constant((1U << (n / 2)) - 1)});
                }
                for (unsigned shift = 1; !mode && shift < n; shift *= 2) {
                    auto shifted = g.add(Op::Shr, Type::Udint, 0, {word, constant(shift)});
                    word = g.add(Op::WordXor, Type::Udint, 0,
                                 {word, shifted});
                }
                auto r = mode ? g.add(Op::Nonzero, Type::Bool, 0, {word})
                              : g.add(Op::Extract, Type::Bool, 0, {word, constant(0)});
                pair_roots(g, r, phase);
                add(g, "udint-mask-" + std::to_string(mode), phase,
                    {{"packing_bits", n}, {"table_bytes", 0}, {"mode", mode}});
            }
        report["patterns_tested"] = tested;
        if (n < 2) return;
        // Keys with the same highest active input form one contiguous interval.
        uint32_t enabled = p.reduced[0];
        for (unsigned bit = 0; bit < n; ++bit) {
            unsigned first = 1U << bit;
            for (unsigned key = first; key < 2 * first; ++key) {
                budget.spend();
                if (p.reduced[key] != p.reduced[first]) return;
            }
            enabled |= uint32_t(p.reduced[first]) << (bit + 1);
        }
        Graph g(p.truth.inputs);
        auto constant = [&](uint32_t v) { return g.add(Op::Constant, Type::Udint, v); };
        auto word = packed(g, inputs(g, p), true);
        auto code = g.add(Op::Encode32, Type::Udint, 0, {word});
        auto result = g.add(Op::Extract, Type::Bool, 0, {constant(enabled), code});
        pair_roots(g, result, false);
        add(g, "priority-code", false, {{"packing_bits", n}, {"class_mask", enabled}});
        auto position = g.add(Op::Decode32, Type::Udint, 0, {code});
        auto allowed = g.add(Op::WordAnd, Type::Udint, 0, {position, constant(enabled)});
        g.roots.clear();
        pair_roots(g, g.add(Op::Nonzero, Type::Bool, 0, {allowed}), false);
        add(g, "priority-mask", false, {{"packing_bits", n}, {"class_mask", enabled}});
    });
}
} // namespace engine
} // namespace b2f
