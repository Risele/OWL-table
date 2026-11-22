#include "package.hpp"
#include <algorithm>
#include <stdexcept>
namespace b2f {
namespace engine {
namespace {
Json ep(size_t node, unsigned port = 0, bool inverted = false) {
    return {{"node", node}, {"port", port}, {"inverted", inverted}};
}
struct Fragment {
    std::set<size_t> inside;
    std::vector<Ref> actual;
    std::vector<size_t> outputs;
    Json definition;
    std::string signature;
};
Fragment fragment(const Graph &g, const Json &source, size_t root, unsigned depth,
                  const std::string &language) {
    Fragment f;
    std::function<void(size_t, unsigned)> visit = [&](size_t id, unsigned left) {
        const auto &n = g.nodes()[id];
        if (!left || n.op == Op::Input || n.op == Op::Constant)
            return;
        f.inside.insert(id);
        for (auto r : n.args)
            visit(r.node, left - 1);
    };
    visit(root, depth);
    std::map<size_t, std::pair<unsigned, bool>> ports;
    std::map<size_t, size_t> internal;
    Json operations = Json::array(), types = Json::array();
    // The bit number is part of ST syntax. Keep it literal inside a shared definition.
    std::set<size_t> bitNumbers;
    for (auto id : f.inside) {
        const auto &n = g.nodes()[id];
        if (language == "ST" && (n.op == Op::Extract || n.op == Op::Putbit))
            bitNumbers.insert(n.args.at(1).node);
    }
    // First establish every cut port, absorbing its first phase into the actual binding.
    for (auto id : f.inside)
        for (auto r : g.nodes()[id].args)
            if (!f.inside.count(r.node) && !bitNumbers.count(r.node) && !ports.count(r.node)) {
                ports[r.node] = {unsigned(f.actual.size()), r.inverted};
                f.actual.push_back(r);
                types.push_back(g.nodes()[r.node].type == Type::Bool ? "BOOL" : "UDINT");
            }
    for (unsigned i = 0; i < f.actual.size(); ++i)
        operations.push_back({{"kind", "primitive"},
                              {"op", "input"},
                              {"type", types[i]},
                              {"value", i},
                              {"args", Json::array()}});
    for (auto id : bitNumbers) {
        auto literal = source["nodes"][id];
        literal["kind"] = "primitive";
        internal[id] = operations.size();
        operations.push_back(literal);
    }
    for (auto id : f.inside) {
        auto n = source["nodes"][id];
        n["kind"] = "primitive";
        n["args"] = Json::array();
        for (auto r : g.nodes()[id].args) {
            if (internal.count(r.node))
                n["args"].push_back(ep(internal.at(r.node), 0, r.inverted));
            else {
                auto port = ports.at(r.node);
                n["args"].push_back(ep(port.first, 0, r.inverted != port.second));
            }
        }
        internal[id] = operations.size();
        operations.push_back(n);
    }
    // Expose internal signals as well: an outside consumer must not lose its wire.
    f.outputs.push_back(root);
    for (auto id : f.inside)
        if (id != root)
            f.outputs.push_back(id);
    Json outputs = Json::array();
    for (auto id : f.outputs)
        outputs.push_back(ep(internal[id]));
    f.definition = {{"language", language},
                    {"pure", true},
                    {"input_types", types},
                    {"nodes", operations},
                    {"outputs", outputs}};
    f.signature = f.definition.dump();
    return f;
}
Json extract(const Graph &g, const std::string &language, unsigned depth, unsigned external,
             unsigned table_bytes) {
    std::map<std::string, std::vector<Fragment>> families;
    auto fanout = g.fanout();
    auto source = g.json();
    for (size_t i = 0; i < g.nodes().size(); ++i) {
        auto op = g.nodes()[i].op;
        if (op == Op::Input || op == Op::Constant)
            continue;
        auto f = fragment(g, source, i, depth, language);
        if (depth == 2 && f.inside.size() < 2)
            continue;
        families[f.signature].push_back(std::move(f));
    }
    std::set<size_t> occupied;
    std::vector<Fragment> chosen;
    std::vector<std::string> definitions;
    Json helpers = Json::array();
    for (auto &family : families) {
        std::vector<Fragment> disjoint;
        auto taken = occupied;
        for (auto &f : family.second) {
            bool overlap = false;
            for (auto id : f.inside)
                overlap |= taken.count(id) != 0;
            if (!overlap) {
                disjoint.push_back(f);
                taken.insert(f.inside.begin(), f.inside.end());
            }
        }
        bool shared = disjoint.size() == 1 && fanout[disjoint[0].outputs[0]] > 1;
        if (disjoint.size() < 2 && !shared)
            continue;
        std::string id = "D" + std::to_string(helpers.size() + 1);
        auto d = disjoint[0].definition;
        d["id"] = id;
        d["name"] = "Helper" + std::to_string(helpers.size() + 1);
        helpers.push_back(d);
        for (auto f : disjoint) {
            occupied.insert(f.inside.begin(), f.inside.end());
            chosen.push_back(f);
            definitions.push_back(id);
        }
    }
    if (chosen.empty())
        throw std::length_error("no_repeated_or_shared_fragment");
    auto result = flat_package(g, language, external, table_bytes);
    auto &root = result["definitions"][0];
    Json body = Json::array();
    std::map<size_t, Json> mapped;
    std::vector<bool> done(chosen.size());
    std::set<size_t> pending;
    for (size_t i = 0; i < g.nodes().size(); ++i)
        if (!occupied.count(i))
            pending.insert(i);
    auto remap = [&](Ref r) {
        Json v = mapped.at(r.node);
        v["inverted"] = v["inverted"].get<bool>() != r.inverted;
        return v;
    };
    size_t remaining = pending.size() + chosen.size();
    while (remaining) {
        size_t before = remaining;
        for (auto i = pending.begin(); i != pending.end();) {
            const auto &n = g.nodes()[*i];
            bool ready = true;
            for (auto r : n.args)
                ready &= mapped.count(r.node) != 0;
            if (!ready) {
                ++i;
                continue;
            }
            auto primitive = source["nodes"][*i];
            primitive["kind"] = "primitive";
            primitive["args"] = Json::array();
            for (auto r : n.args)
                primitive["args"].push_back(remap(r));
            mapped[*i] = ep(body.size());
            body.push_back(primitive);
            i = pending.erase(i);
            --remaining;
        }
        for (size_t k = 0; k < chosen.size(); ++k)
            if (!done[k]) {
                bool ready = true;
                for (auto r : chosen[k].actual)
                    ready &= mapped.count(r.node) != 0;
                if (!ready)
                    continue;
                Json args = Json::array();
                for (auto r : chosen[k].actual)
                    args.push_back(remap(r));
                for (unsigned port = 0; port < chosen[k].outputs.size(); ++port)
                    mapped[chosen[k].outputs[port]] = ep(body.size(), port);
                body.push_back({{"kind", "call"},
                                {"id", "Instance" + std::to_string(k)},
                                {"definition", definitions[k]},
                                {"args", args}});
                done[k] = true;
                --remaining;
            }
        if (before == remaining)
            throw std::length_error("nonconvex_boundary_cycle");
    }
    root["nodes"] = body;
    root["outputs"] = Json::array();
    for (auto r : g.roots)
        root["outputs"].push_back(remap(r));
    for (auto d : helpers)
        result["definitions"].push_back(d);
    return result;
}
Json group_wrapper(Json package) {
    auto old = package["definitions"][0];
    old["id"] = "DGroup";
    old["name"] = "Group";
    auto &root = package["definitions"][0];
    Json body = Json::array(), actual = Json::array(), outputs = Json::array();
    for (unsigned i = 0; i < root["input_types"].size(); ++i) {
        body.push_back({{"kind", "primitive"},
                        {"op", "input"},
                        {"type", root["input_types"][i]},
                        {"value", i},
                        {"args", Json::array()}});
        actual.push_back(ep(i));
    }
    for (unsigned i = 0; i < root["outputs"].size(); ++i)
        outputs.push_back(ep(body.size(), i));
    body.push_back({{"kind", "call"},
                    {"id", "RootGroupInstance"},
                    {"definition", "DGroup"},
                    {"args", actual}});
    root["nodes"] = body;
    root["outputs"] = outputs;
    package["definitions"].push_back(old);
    return package;
}
} // namespace
Json package_search(const Graph &g, const std::string &language, unsigned external,
                    unsigned table_bytes) {
    auto reference = g.tables();
    Json variants = Json::array(), rejected = Json::array();
    auto add = [&](const std::string &name, const Json &package) {
        auto result = inspect_package(package, true);
        if (result["tables"] != reference)
            throw std::runtime_error("package search equivalence failure: " + name);
        result.erase("tables");
        result["method"] = name;
        result["verified"] = true;
        result["checked"] = reference[0]["values"].size();
        variants.push_back(result);
    };
    add("inline", flat_package(g, language, external, table_bytes));
    for (unsigned depth : {1U, 2U})
        try {
            auto package = extract(g, language, depth, external, table_bytes);
            add(depth == 1 ? "shared-instances" : "multi-output-groups", package);
            if (depth == 2)
                add("nested-groups", group_wrapper(package));
        } catch (const std::length_error &e) {
            rejected.push_back({{"fragment_depth", depth}, {"reason", e.what()}});
        }
    return {{"language", language},
            {"variants", variants},
            {"rejected", rejected},
            {"source_target_metrics", target_metrics(g, language, table_bytes)}};
}
} // namespace engine
} // namespace b2f
