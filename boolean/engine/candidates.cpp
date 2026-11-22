#include "package.hpp"
#include "control.hpp"
#include "synthesis.hpp"
#include "target.hpp"
#include <algorithm>
#include <stdexcept>
namespace b2f {
namespace engine {
Problem::Problem(const TruthBits &t) : truth(t), mapping(support(t)) {
    validate_contract(t.json());
    unsigned n = mapping.local_to_external.size();
    reduced.resize(1U << n);
    for (uint32_t k = 0; k < reduced.size(); ++k) {
        uint32_t external = 0;
        for (unsigned i = 0; i < n; ++i)
            if (k & (1U << i))
                external |= 1U << mapping.local_to_external[i];
        reduced[k] = truth.get(external);
    }
}
void SearchBudget::spend(uint64_t count) {
    checkpoint();
    if (count > limit - used)
        throw std::length_error("work_limit");
    used += count;
}
SearchCandidate::SearchCandidate(Graph g, std::string name, bool phase, Json details)
    : graph(std::move(g)), method(std::move(name)), inverted_first(phase),
      evidence(std::move(details)) {}
Options Options::read(const Json &j) {
    if (!j.is_object())
        throw std::runtime_error("search options must be an object");
    Options o;
    for (auto i = j.begin(); i != j.end(); ++i) {
        if (i.key() == "allow_full_lut") {
            if (!i.value().is_boolean())
                throw std::runtime_error("allow_full_lut must be bool");
            o.allow_full_lut = i.value();
            continue;
        }
        if (!i.value().is_number_integer() || i.value().get<int64_t>() < 0)
            throw std::runtime_error("search option integer required");
        uint64_t v = i.value().get<uint64_t>();
        if (i.key() == "work_limit" && v <= 1000000000)
            o.work_limit = v;
        else if (i.key() == "max_nodes" && v >= 1 && v <= 100000)
            o.max_nodes = v;
        else if (i.key() == "table_bytes" && v <= 8192)
            o.table_bytes = v;
        else if (i.key() == "max_candidates" && v >= 1 && v <= 1024)
            o.max_candidates = v;
        else
            throw std::runtime_error("unknown or out-of-range search option: " + i.key());
    }
    if (o.table_bytes > 256 && !o.allow_full_lut)
        throw std::runtime_error("table_bytes above 256 requires allow_full_lut");
    return o;
}
std::vector<bool> reachable(const Graph &g) {
    std::vector<bool> used(g.nodes().size());
    std::function<void(Ref)> visit = [&](Ref r) {
        if (used.at(r.node))
            return;
        used[r.node] = true;
        for (auto a : g.nodes()[r.node].args)
            visit(a);
    };
    for (auto r : g.roots)
        visit(r);
    return used;
}
Graph prune(const Graph &g) {
    auto used = reachable(g);
    auto doc = g.json();
    Graph out(doc["inputs"].get<unsigned>());
    std::vector<Ref> map(g.nodes().size());
    auto remap = [&](Ref r) { return r.inverted ? out.invert(map.at(r.node)) : map.at(r.node); };
    for (size_t i = 0; i < g.nodes().size(); ++i)
        if (used[i]) {
            auto n = g.nodes()[i];
            std::vector<Ref> args;
            for (auto r : n.args)
                args.push_back(remap(r));
            map[i] = out.add(n.op, n.type, n.value, args);
        }
    for (auto r : g.roots)
        out.roots.push_back(remap(r));
    return out;
}
Json logical_metrics(const Graph &g) {
    auto used = reachable(g);
    std::set<size_t> inversions;
    unsigned operations = 0, inputs = 0, constants = 0, depth = 0;
    std::vector<unsigned> depths(g.nodes().size());
    auto edge = [&](Ref r) {
        bool real = r.inverted && g.nodes()[r.node].op != Op::Constant;
        if (real)
            inversions.insert(r.node);
        return depths[r.node] + (real ? 1 : 0);
    };
    for (size_t i = 0; i < g.nodes().size(); ++i)
        if (used[i]) {
            auto &n = g.nodes()[i];
            if (n.op == Op::Input)
                ++inputs;
            else if (n.op == Op::Constant)
                ++constants;
            else {
                ++operations;
                unsigned d = 0;
                for (auto r : n.args)
                    d = std::max(d, edge(r));
                depths[i] = d + 1;
            }
        }
    for (auto r : g.roots)
        depth = std::max(depth, edge(r));
    return {{"operations", operations + inversions.size()},
            {"logical_operations", operations},
            {"physical_not", inversions.size()},
            {"depth", depth},
            {"inputs", inputs},
            {"constants", constants}};
}
void Pool::add(Graph graph, const std::string &method, bool phase, Json details) {
    graph = prune(graph);
    auto tables = graph.tables();
    if (tables.size() != 2 || tables[0]["type"] != "BOOL" || tables[1]["type"] != "BOOL")
        throw std::runtime_error("candidate requires Q/QN");
    for (uint32_t k = 0; k < (1U << problem.truth.inputs); ++k)
        if (tables[0]["values"][k].get<unsigned>() != problem.truth.get(k) ||
            tables[1]["values"][k].get<unsigned>() == problem.truth.get(k))
            throw std::runtime_error("candidate mismatch " + method + " at " + std::to_string(k));
    details["checked"] = 1U << problem.truth.inputs;
    details["verified"] = true;
    candidates.emplace_back(std::move(graph), method, phase, details);
    // Bound optional candidates; required baseline/source candidates are protected.
    if (candidates.size() > capacity) {
        size_t worst = candidates.size();
        std::pair<unsigned, unsigned> score{0, 0};
        for (size_t i = 0; i < candidates.size(); ++i)
            if (!candidates[i].evidence.value("reserved", false) || (capacity == 1 && i > 0)) {
                auto m = logical_metrics(candidates[i].graph);
                auto rank =
                    std::make_pair(m["operations"].get<unsigned>(), m["depth"].get<unsigned>());
                if (worst == candidates.size() || rank >= score) {
                    worst = i;
                    score = rank;
                }
            }
        if (worst < candidates.size()) {
            backends.push_back({{"backend", "candidate-pool"},
                                {"stop_reason", "capacity_" + std::to_string(capacity)},
                                {"discarded", candidates[worst].method}});
            candidates.erase(candidates.begin() + worst);
        }
    }
}
Ref fold(Graph &g, Op op, std::vector<Ref> values) {
    if (values.empty())
        return g.add(Op::Constant, Type::Bool, op == Op::And ? 1 : 0);
    while (values.size() > 1) {
        std::vector<Ref> next;
        for (size_t i = 0; i < values.size(); i += 2)
            next.push_back(i + 1 < values.size()
                               ? g.add(op, Type::Bool, 0, {values[i], values[i + 1]})
                               : values[i]);
        values = std::move(next);
    }
    return values[0];
}
void pair_roots(Graph &g, Ref base, bool phase) {
    Ref other = g.invert(base);
    g.roots = phase ? std::vector<Ref>{other, base} : std::vector<Ref>{base, other};
}
Graph shannon(const Problem &p, const std::vector<int> &order, bool phase, SearchBudget *budget) {
    Graph g(p.truth.inputs);
    std::vector<Ref> inputs;
    for (int i : p.mapping.local_to_external)
        inputs.push_back(g.add(Op::Input, Type::Bool, i));
    std::map<std::pair<int, std::vector<uint8_t>>, Ref> cache;
    std::function<Ref(int, std::vector<uint8_t>)> build = [&](int level,
                                                              std::vector<uint8_t> values) -> Ref {
        if (budget)
            budget->spend(values.size() + 1);
        bool invert = values[0] != 0;
        if (invert)
            for (auto &v : values)
                v ^= 1;
        auto key = std::make_pair(level, values);
        auto found = cache.find(key);
        Ref result;
        if (found != cache.end())
            result = found->second;
        else if (std::all_of(values.begin(), values.end(), [](uint8_t v) { return v == 0; }))
            result = g.add(Op::Constant, Type::Bool, 0);
        else {
            std::vector<uint8_t> lo, hi;
            for (size_t k = 0; k < values.size(); ++k)
                (k & 1 ? hi : lo).push_back(values[k]);
            auto a = build(level + 1, lo), b = build(level + 1, hi);
            result = g.add(Op::Mux, Type::Bool, 0, {inputs.at(order.at(level)), a, b});
        }
        cache[key] = result;
        return invert ? g.invert(result) : result;
    };
    std::vector<uint8_t> permuted(p.reduced.size());
    for (uint32_t k = 0; k < permuted.size(); ++k) {
        uint32_t source = 0;
        for (size_t i = 0; i < order.size(); ++i)
            if (k & (1U << i))
                source |= 1U << order[i];
        permuted[k] = p.reduced[source] ^ phase;
    }
    auto root = build(0, permuted);
    pair_roots(g, root, phase);
    return g;
}
Json search(const Problem &p, int stage, const Options &options) {
    if (stage < 5 || stage > 10)
        throw std::runtime_error("search stage must be P05..P10");
    Pool pool(p, options.max_candidates);
    synth_basic(pool);
    SearchBudget budget(options.work_limit);
    if (stage >= 6) {
        SearchBudget cover(stage >= 7 ? budget.limit / 3 : budget.limit);
        synth_cover(pool, cover, options);
        budget.used += cover.used;
    }
    if (stage >= 7)
        synth_advanced(pool, budget, options);
    // Later stages append additional verified candidates; the mandatory baseline survives budgets.
    Json result = {
        {"stage", stage}, {"support", p.mapping.local_to_external}, {"baseline_reserved", true}};
    Json candidates = Json::array();
    size_t best = 0;
    std::pair<unsigned, unsigned> score{0xffffffffU, 0xffffffffU};
    for (size_t i = 0; i < pool.candidates.size(); ++i) {
        auto &c = pool.candidates[i];
        auto m = logical_metrics(c.graph);
        auto rank = std::make_pair(m["operations"].get<unsigned>(), m["depth"].get<unsigned>());
        if (rank < score) {
            score = rank;
            best = i;
        }
        candidates.push_back({{"method", c.method},
                              {"inverted_first", c.inverted_first},
                              {"ir", c.graph.json()},
                              {"metrics", m},
                              {"evidence", c.evidence}});
    }
    result["candidates"] = candidates;
    result["selected"] = best;
    result["backends"] = pool.backends;
    result["work_used"] = budget.used;
    result["work_limit"] = budget.limit;
    if (stage >= 8)
        result["targets"] = target_pool(pool);
    if (stage >= 9) {
        result["packages"] = Json::array();
        for (auto &t : result["targets"]) {
            auto selected = t["candidates"][t["selected"].get<size_t>()];
            auto graph = Graph::read(selected["ir"]);
            auto p = flat_package(graph, t["language"], 0,
                                  selected["metrics"]["table_bytes"].get<unsigned>());
            auto checked = inspect_package(p);
            checked.erase("tables");
            result["packages"].push_back(checked);
        }
    }
    if (stage >= 10) {
        result["packaging"] = Json::array();
        for (auto &target : result["targets"]) {
            auto selected = target["candidates"][target["selected"].get<size_t>()];
            result["packaging"].push_back(
                package_search(Graph::read(selected["ir"]), target["language"], 0,
                               selected["metrics"]["table_bytes"].get<unsigned>()));
        }
    }
    return result;
}
} // namespace engine
} // namespace b2f
