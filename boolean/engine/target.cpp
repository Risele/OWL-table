#include "target.hpp"
#include <algorithm>
#include <stdexcept>
namespace b2f {
namespace engine {
Graph lower_target(const Graph &source, const std::string &language, const Json &profile) {
    if (language != "ST" && language != "FBD")
        throw std::runtime_error("unsupported target language");
    Graph src = prune(source), g(src.json()["inputs"].get<unsigned>());
    std::vector<Ref> map;
    auto inverted = [&](Ref r) {
        const auto &n = g.nodes().at(r.node);
        if (n.op == Op::Constant && n.type == Type::Bool)
            return g.add(Op::Constant, Type::Bool, n.value ^ unsigned(r.inverted) ^ 1);
        return g.invert(r);
    };
    auto remap = [&](Ref r) {
        auto v = map.at(r.node);
        return r.inverted ? inverted(v) : v;
    };
    auto binary = [&](Op op, Ref a, Ref b) {
        auto na = g.nodes()[a.node], nb = g.nodes()[b.node];
        auto value = [](const Node &n, Ref r) { return bool(n.value) != r.inverted; };
        if (na.op == Op::Constant) {
            bool v = value(na, a);
            if (op == Op::And)
                return v ? b : a;
            if (op == Op::Or)
                return v ? a : b;
            if (op == Op::Xor)
                return v ? inverted(b) : b;
        }
        if (nb.op == Op::Constant) {
            bool v = value(nb, b);
            if (op == Op::And)
                return v ? a : b;
            if (op == Op::Or)
                return v ? b : a;
            if (op == Op::Xor)
                return v ? inverted(a) : a;
        }
        return g.add(op, Type::Bool, 0, {a, b});
    };
    auto constant = [&](uint32_t v) { return g.add(Op::Constant, Type::Udint, v); };
    auto gatedBit = [&](const Node &term) -> uint32_t {
        if (term.op != Op::Sel) return 0;
        const auto &off = src.nodes()[term.args[1].node], &on = src.nodes()[term.args[2].node];
        return off.op == Op::Constant && !off.value && on.op == Op::Constant &&
               !(on.value & (on.value - 1)) ? on.value : 0;
    };
    auto bitIndex = [](uint32_t mask) {
        unsigned pos = 0;
        while ((mask >>= 1) != 0) ++pos;
        return pos;
    };
    const bool fbd = language == "FBD";
    const bool nativeCode = fbd && profile.at("code32").get<bool>();
    const bool nativeExtract = fbd && profile.at("extract").get<bool>();
    const bool nativePutbit = fbd && profile.at("putbit").get<bool>();
    const bool legacy = fbd && std::none_of(profile.begin(), profile.end(),
                                            [](const Json &v) { return v.get<bool>(); });
    // Track possible one bits in an address. Native EXTRACT/PUTBIT only need
    // indices 0..31. Keep shift-based semantics for unrestricted indices.
    std::vector<uint32_t> possible;
    for (const auto &n : src.nodes()) {
        uint32_t bits = 0xffffffffU;
        if (n.op == Op::Constant)
            bits = n.value;
        else if (n.op == Op::Sel)
            bits = possible[n.args[1].node] | possible[n.args[2].node];
        else if (n.op == Op::WordOr || n.op == Op::WordXor)
            bits = possible[n.args[0].node] | possible[n.args[1].node];
        else if (n.op == Op::WordAnd)
            bits = possible[n.args[0].node] & possible[n.args[1].node];
        else if (n.op == Op::Encode32)
            bits = 31;
        else if (n.op == Op::Putbit && src.nodes()[n.args[1].node].op == Op::Constant)
            bits = possible[n.args[0].node] | shl32(1, src.nodes()[n.args[1].node].value);
        possible.push_back(bits);
        if (legacy && (n.type == Type::Udint || n.op == Op::Extract || n.op == Op::Nonzero))
            throw std::runtime_error("FBD profile disables word primitives");
        std::vector<Ref> a;
        for (auto r : n.args)
            a.push_back(remap(r));
        Ref r;
        if (n.op == Op::Mux) {
            auto low = binary(Op::And, inverted(a[0]), a[1]), high = binary(Op::And, a[0], a[2]);
            r = binary(Op::Or, low, high);
        } else if (n.op == Op::Encode32 && !nativeCode) {
            // Binary search for the highest one bit using ST shifts and selections.
            Ref word = a[0];
            r = constant(0);
            for (unsigned step : {16U, 8U, 4U, 2U, 1U}) {
                auto high = g.add(Op::Shr, Type::Udint, 0, {word, constant(step)});
                auto set = g.add(Op::Nonzero, Type::Bool, 0, {high});
                word = g.add(Op::Sel, Type::Udint, 0, {set, word, high});
                auto bit = g.add(Op::Sel, Type::Udint, 0, {set, constant(0), constant(step)});
                r = g.add(Op::WordOr, Type::Udint, 0, {r, bit});
            }
        } else if (n.op == Op::Decode32 && !nativeCode) {
            auto index = g.add(Op::WordAnd, Type::Udint, 0, {a[0], constant(31)});
            r = g.add(Op::Shl, Type::Udint, 0, {constant(1), index});
        } else if (nativeCode && n.op == Op::Shl && possible[n.args[1].node] <= 31 &&
                   src.nodes()[n.args[0].node].op == Op::Constant &&
                   src.nodes()[n.args[0].node].value == 1) {
            r = g.add(Op::Decode32, Type::Udint, 0, {a[1]});
        } else if (n.op == Op::Xor && fbd && !profile.at("xor").get<bool>()) {
            auto low = binary(Op::And, a[0], inverted(a[1])),
                 high = binary(Op::And, inverted(a[0]), a[1]);
            r = binary(Op::Or, low, high);
        } else if (n.op == Op::And || n.op == Op::Or || n.op == Op::Xor)
            r = binary(n.op, a[0], a[1]);
        else if (!fbd && n.op == Op::Extract) {
            const auto index = g.nodes()[a[1].node];
            if (index.op == Op::Constant) {
                const auto word = g.nodes()[a[0].node];
                if (index.value >= 32 || word.op == Op::Constant)
                    r = g.add(Op::Constant, Type::Bool, shr32(word.value, index.value) & 1U);
                else
                    r = g.add(Op::Extract, Type::Bool, 0, a);
            } else {
                auto shifted = g.add(Op::Shr, Type::Udint, 0, a);
                r = g.add(Op::Extract, Type::Bool, 0, {shifted, constant(0)});
            }
        } else if (!fbd && n.op == Op::Putbit && g.nodes()[a[1].node].op == Op::Constant) {
            r = g.nodes()[a[1].node].value < 32 ? g.add(n.op, n.type, 0, a) : a[0];
        } else if (!fbd && gatedBit(n)) {
            r = g.add(Op::Putbit, Type::Udint, 0, {constant(0), constant(bitIndex(gatedBit(n))), a[0]});
        } else if (n.op == Op::Extract && (!nativeExtract || possible[n.args[1].node]>31)) {
            auto shifted = g.add(Op::Shr, Type::Udint, 0, {a[0], a[1]});
            auto bit = g.add(Op::WordAnd, Type::Udint, 0, {shifted, constant(1)});
            r = g.add(Op::Nonzero, Type::Bool, 0, {bit});
        } else if (n.op == Op::Putbit && (!nativePutbit || possible[n.args[1].node]>31)) {
            auto mask = g.add(Op::Shl, Type::Udint, 0, {constant(1), a[1]});
            auto inv = g.add(Op::WordNot, Type::Udint, 0, {mask});
            auto cleared = g.add(Op::WordAnd, Type::Udint, 0, {a[0], inv});
            auto selected = g.add(Op::Sel, Type::Udint, 0, {a[2], constant(0), mask});
            r = g.add(Op::WordOr, Type::Udint, 0, {cleared, selected});
        } else if((!fbd || nativePutbit) && n.op==Op::WordOr) {
            r=g.add(n.op,n.type,n.value,a);
            // A gated power of two sets one previously clear bit in a packed address.
            for (unsigned j : {0U, 1U}) {
                unsigned i = fbd ? j : 1 - j;
                const auto &term = src.nodes()[n.args[i].node];
                uint32_t mask = gatedBit(term);
                if (!mask || (possible[n.args[1 - i].node] & mask))
                    continue;
                r = g.add(Op::Putbit, Type::Udint, 0,
                          {a[1 - i], constant(bitIndex(mask)), remap(term.args[0])});
                break;
            }
        } else
            r = g.add(n.op, n.type, n.value, a);
        map.push_back(r);
    }
    for (auto r : src.roots)
        g.roots.push_back(remap(r));
    Graph result = prune(g);
    if (fbd) {
        for (const auto &node : result.nodes()) {
            if (node.op == Op::Sel && !profile.at("sel").get<bool>())
                throw std::runtime_error("FBD profile disables SEL");
            if (node.op == Op::WordXor && !profile.at("xor").get<bool>())
                throw std::runtime_error("FBD profile disables word XOR");
        }
    }
    return result;
}
// Relative Thumb instruction costs for normalized C _Bool and uint32_t in registers.
unsigned m0_primitive(const Graph &g, Op op, const std::vector<Ref> &args) {
    if (op == Op::Constant || op == Op::Input) return 0;
    const auto &nodes = g.nodes();
    const auto &a = nodes.at(args[0].node);
    if (op == Op::Putbit) {
        const auto &index = nodes.at(args[1].node);
        if (index.op != Op::Constant || index.value >= 32) return 6;
        unsigned shift = unsigned(index.value != 0);
        // A chain starting at zero never has to clear a previously unwritten bit.
        const Node *prior = &a;
        while (prior->op == Op::Putbit) {
            const auto &offset = nodes.at(prior->args[1].node);
            if (offset.op != Op::Constant || offset.value == index.value) break;
            prior = &nodes.at(prior->args[0].node);
        }
        if (prior->op == Op::Constant && !(prior->value & (1U << index.value)))
            return shift + unsigned(a.op != Op::Constant || a.value != 0);
        return 3 + shift; // materialize clear mask, BIC, optional shift, OR
    }
    if (op == Op::Extract)
        return nodes.at(args[1].node).value == 31 ? 1 : 2;
    if (op == Op::Sel || op == Op::Mux) return 3;
    if (op == Op::Nonzero) return 2;
    if (op == Op::WordEqual) {
        const auto &b = nodes.at(args[1].node);
        return 3 + unsigned(b.op == Op::Constant && b.value > 255);
    }
    if (op == Op::WordAnd || op == Op::WordOr || op == Op::WordXor)
        return 1 + unsigned(a.op == Op::Constant || nodes.at(args[1].node).op == Op::Constant);
    return 1;
}
Json target_metrics(const Graph &g, const std::string &language, unsigned table_bytes) {
    auto metrics = logical_metrics(g);
    auto count = g.nodes().size();
    std::vector<size_t> last(count);
    std::map<size_t, std::pair<size_t, size_t>> inverse;
    unsigned edges = 0, data = 0;
    Json histogram = Json::object();
    auto doc = g.json();
    auto use = [&](Ref r, size_t at) {
        last[r.node] = std::max(last[r.node], at);
        if (r.inverted && g.nodes()[r.node].op != Op::Constant) {
            auto it = inverse.find(r.node);
            if (it == inverse.end())
                inverse[r.node] = {at, at};
            else
                it->second.second = at;
        }
    };
    for (size_t i = 0; i < count; ++i) {
        const auto &n = g.nodes()[i];
        last[i] = std::max(last[i], i);
        std::string op = doc["nodes"][i]["op"];
        histogram[op] = histogram.value(op, 0) + 1;
        for (auto r : n.args) {
            ++edges;
            use(r, i);
        }
        if (n.op == Op::Constant)
            data += n.type == Type::Bool ? 1 : 4;
    }
    unsigned zeroConstants = language == "FBD" ? histogram.value("nonzero", 0U) : 0U;
    data += 4 * zeroConstants;
    edges += zeroConstants;
    for (auto r : g.roots) {
        ++edges;
        use(r, count);
    }
    std::vector<int64_t> events(count + 2);
    for (size_t i = 0; i < count; ++i) {
        auto &n = g.nodes()[i];
        if (n.op != Op::Input && n.op != Op::Constant) {
            unsigned size = n.type == Type::Bool ? 1 : 4;
            events[i] += size;
            events[last[i] + 1] -= size;
        }
    }
    for (auto v : inverse) {
        ++events[v.second.first];
        --events[v.second.second + 1];
    }
    int64_t live = 0, peak = 0;
    for (auto delta : events) {
        live += delta;
        peak = std::max(peak, live);
    }
    metrics["expanded_operations"] = metrics["operations"];
    if (language == "ST") {
        uint64_t estimate = inverse.size();
        for (const auto &n : g.nodes()) estimate += m0_primitive(g,n.op,n.args);
        metrics["cortex_m0_cost"] = estimate;
    }
    metrics["dependency_depth"] = metrics["depth"];
    metrics["constant_payload_bytes"] = data;
    metrics["table_bytes"] = table_bytes;
    metrics["connections"] = edges + inverse.size();
    metrics["ports"] = 2 * (edges + inverse.size());
    metrics["canvas_elements"] =
        language == "FBD"
            ? Json(g.json()["inputs"].get<unsigned>() + g.roots.size() +
                   metrics["operations"].get<unsigned>() + metrics["constants"].get<unsigned>() + zeroConstants)
            : Json(nullptr);
    metrics["peak_live_bytes_model"] = peak;
    metrics["definition_count"] = 1;
    metrics["instance_count"] = 0;
    metrics["call_count"] = 0;
    metrics["device_time"] = nullptr;
    metrics["device_ram"] = nullptr;
    metrics["device_flash"] = nullptr;
    metrics["operation_histogram"] = histogram;
    metrics["evidence"] = {{"operations", "target DAG plus one NOT per shared inverse signal"},
                           {"memory", "structural temporary model BOOL=1 UDINT=4; not relay RAM"},
                           {"data", "constant payload only; not compiled code size"},
                           {"profile_version", 1},
                           {"native_numeric_fbd", "subject to active FBD policy"},
                           {"device_measurements", "unknown"}};
    return metrics;
}
Json target_pool(const Pool &pool, const Json &profile) {
    Json targets = Json::array();
    for (auto language : {std::string("ST"), std::string("FBD")}) {
        Json accepted = Json::array(), rejected = Json::array();
        size_t best = 0;
        std::vector<uint64_t> best_rank;
        for (size_t i = 0; i < pool.candidates.size(); ++i) {
            const auto &candidate = pool.candidates[i];
            try {
                auto graph = lower_target(candidate.graph, language, profile);
                Pool proof(pool.problem);
                proof.add(graph, "lowered", candidate.inverted_first);
                auto m =
                    target_metrics(graph, language, candidate.evidence.value("table_bytes", 0U));
                std::vector<uint64_t> rank = {m["expanded_operations"].get<uint64_t>(),
                                              m["dependency_depth"].get<uint64_t>(),
                                              m["table_bytes"].get<uint64_t>(), uint64_t(i)};
                if (language == "ST") rank.insert(rank.begin(), m.at("cortex_m0_cost").get<uint64_t>());
                if (best_rank.empty() || rank < best_rank) {
                    best_rank = rank;
                    best = accepted.size();
                }
                accepted.push_back({{"source_candidate", i},
                                    {"ir", graph.json()},
                                    {"metrics", m},
                                    {"checked", 1U << pool.problem.truth.inputs},
                                    {"verified", true}});
            } catch (const std::runtime_error &e) {
                if (std::string(e.what()) != "IR node budget" &&
                    (language != "FBD" ||
                     std::string(e.what()).find("FBD profile disables") != 0))
                    throw;
                rejected.push_back({{"source_candidate", i}, {"reason", e.what()}});
            }
        }
        if (accepted.empty())
            throw std::runtime_error("target has no verified candidate");
        targets.push_back({{"language", language},
                           {"profile_version", 1},
                           {"candidates", accepted},
                           {"rejected", rejected},
                           {"selected", best}});
    }
    return targets;
}
} // namespace engine
} // namespace b2f
