#include "package.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace b2f {
namespace engine {
static void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
static void fields(const Json &j, std::initializer_list<const char *> required,
                   std::initializer_list<const char *> optional = {}) {
    require(j.is_object(), "package object required");
    std::set<std::string> allowed;
    for (auto k : required) {
        allowed.insert(k);
        require(j.count(k), std::string("missing package field: ") + k);
    }
    for (auto k : optional)
        allowed.insert(k);
    for (auto i = j.begin(); i != j.end(); ++i)
        require(allowed.count(i.key()), "unknown package field: " + i.key());
}
static unsigned number(const Json &j, unsigned max) {
    require(j.is_number_integer() && j.get<int64_t>() >= 0 && j.get<uint64_t>() <= max,
            "package integer out of range");
    return j.get<unsigned>();
}
static std::string text(const Json &j) {
    require(j.is_string() && !j.get<std::string>().empty(), "package nonempty string required");
    return j.get<std::string>();
}
static Json endpoint(Ref r) { return {{"node", r.node}, {"port", 0}, {"inverted", r.inverted}}; }
Json flat_package(const Graph &source, const std::string &language, unsigned external,
                  unsigned table_bytes) {
    auto g = prune(source);
    auto source_json = g.json();
    Json body = Json::array();
    for (auto n : source_json["nodes"]) {
        n["kind"] = "primitive";
        for (auto &a : n["args"])
            a["port"] = 0;
        body.push_back(n);
    }
    Json outputs = Json::array();
    for (auto r : g.roots)
        outputs.push_back(endpoint(r));
    Json d = {{"id", "D0"},
              {"name", "Root"},
              {"language", language},
              {"pure", true},
              {"input_types", std::vector<std::string>(g.json()["inputs"].get<unsigned>(), "BOOL")},
              {"nodes", body},
              {"outputs", outputs}};
    return {{"schema_version", 1},
            {"kind", "macro_package"},
            {"language", language},
            {"root", "D0"},
            {"definitions", Json::array({d})},
            {"external_levels", external},
            {"table_bytes", table_bytes}};
}
PackageResult check_package(const Json &package, bool permit_depth_overflow) {
    fields(package, {"schema_version", "kind", "language", "root", "definitions", "external_levels",
                     "table_bytes"});
    require(number(package["schema_version"], 1) == 1 && package["kind"] == "macro_package",
            "macro package version/kind");
    auto language = text(package["language"]);
    require(language == "ST" || language == "FBD", "package language");
    unsigned external = number(package["external_levels"], 2),
             table_bytes = number(package["table_bytes"], 8192);
    require(package["definitions"].is_array() && !package["definitions"].empty() &&
                package["definitions"].size() <= 256,
            "package definition limit");
    std::map<std::string, Json> defs;
    std::set<std::string> names;
    size_t total_nodes = 0;
    for (auto &d : package["definitions"]) {
        fields(d, {"id", "name", "language", "pure", "input_types", "nodes", "outputs"},
               {"imported_height"});
        auto id = text(d["id"]);
        require(defs.emplace(id, d).second, "duplicate definition id: " + id);
        require(names.insert(text(d["name"])).second, "duplicate definition name");
        require(d["pure"].is_boolean() && d["pure"].get<bool>(),
                "stateful definition forbidden: " + id);
        require(d["input_types"].is_array() && d["input_types"].size() <= 64, "formal port limit");
        for (auto &t : d["input_types"])
            require(t == "BOOL" || t == "UDINT", "formal port type");
        require(d["nodes"].is_array() && d["outputs"].is_array() && !d["outputs"].empty() &&
                    d["outputs"].size() <= 64,
                "definition body/outputs");
        total_nodes += d["nodes"].size();
    }
    require(total_nodes <= 100000, "package total node limit");
    auto root = text(package["root"]);
    require(defs.count(root), "missing package root");
    std::map<std::string, int> colors, heights;
    std::vector<std::string> stack;
    std::map<std::string, std::vector<std::string>> paths;
    auto path_text = [&](const std::vector<std::string> &path) {
        std::string s;
        for (auto &v : path) {
            if (!s.empty())
                s += " -> ";
            s += v;
        }
        return s;
    };
    std::function<int(const std::string &)> visit = [&](const std::string &id) {
        require(defs.count(id), "missing dependency: " + path_text(stack) + " -> " + id);
        if (colors[id] == 1)
            throw std::runtime_error("cycle: " + path_text(stack) + " -> " + id);
        if (colors[id] == 2)
            return heights[id];
        colors[id] = 1;
        stack.push_back(id);
        require(defs[id]["language"] == language, "cross-language edge: " + path_text(stack));
        int height = 1;
        std::vector<std::string> path{id};
        std::set<std::string> instances;
        for (auto &n : defs[id]["nodes"]) {
            require(n.is_object() && n.count("kind"), "instruction kind required");
            if (n["kind"] == "call") {
                fields(n, {"kind", "id", "definition", "args"});
                auto instance = text(n["id"]);
                require(!defs.count(instance) && instances.insert(instance).second,
                        "instance id collides: " + id + "/" + instance);
                auto child = text(n["definition"]);
                int h = visit(child) + 1;
                if (h > height) {
                    height = h;
                    path = {id};
                    path.insert(path.end(), paths[child].begin(), paths[child].end());
                }
            } else
                require(n["kind"] == "primitive", "unknown instruction kind");
        }
        if (defs[id].count("imported_height")) {
            require(!defs[id]["imported_height"].is_null(),
                    "unknown imported dependency height: " + id);
            require(number(defs[id]["imported_height"], 256) == unsigned(height),
                    "imported height differs from body: " + id);
        }
        stack.pop_back();
        colors[id] = 2;
        heights[id] = height;
        paths[id] = path;
        return height;
    };
    for (auto d : defs)
        visit(d.first);
    for (auto h : heights)
        if (!permit_depth_overflow)
            require(h.second <= 3, "height exceeds 3: " + path_text(paths[h.first]));
    if (!permit_depth_overflow)
        require(heights[root] + external <= 3,
                "height exceeds 3 with external container: " + path_text(paths[root]));
    auto root_types = defs[root]["input_types"];
    require(root_types.size() >= 1 && root_types.size() <= 16, "public input count 1..16");
    for (auto t : root_types)
        require(t == "BOOL", "public inputs must be BOOL");
    Graph graph(root_types.size());
    uint64_t expanded_ops = 0, calls = 0, constant_bytes = 0, validation_work = 0, m0_cost = 0;
    std::set<std::string> used;
    std::function<std::vector<Ref>(const std::string &, const std::vector<Ref> &, Graph &, bool)>
        expand;
    expand = [&](const std::string &id, const std::vector<Ref> &actual, Graph &g,
                 bool measure) -> std::vector<Ref> {
        const auto &d = defs.at(id);
        require(actual.size() == d["input_types"].size(), "formal/actual arity: " + id);
        for (size_t i = 0; i < actual.size(); ++i)
            require((g.nodes().at(actual[i].node).type == Type::Bool ? "BOOL" : "UDINT") ==
                        d["input_types"][i],
                    "formal/actual type: " + id);
        if (measure)
            used.insert(id);
        std::vector<std::vector<Ref>> values;
        std::set<std::pair<unsigned, unsigned>> inversions;
        auto resolve = [&](const Json &r) -> Ref {
            fields(r, {"node", "port", "inverted"});
            unsigned index = number(r["node"], 100000), port = number(r["port"], 63);
            require(index < values.size() && port < values[index].size(),
                    "forward/missing endpoint: " + id);
            require(r["inverted"].is_boolean(), "endpoint inversion must be bool");
            auto value = values[index][port];
            if (r["inverted"].get<bool>()) {
                const auto &owner = d["nodes"][index];
                if (owner["kind"] == "call" || owner["op"] != "const")
                    inversions.insert({index, port});
                value = g.invert(value);
            }
            return value;
        };
        for (auto &n : d["nodes"]) {
            require(++validation_work <= 1000000, "package expansion work limit");
            if (n["kind"] == "primitive")
                fields(n, {"kind", "op", "type", "value", "args"});
            require(n["args"].is_array(), "instruction arguments array");
            std::vector<Ref> args;
            for (auto r : n["args"])
                args.push_back(resolve(r));
            if (n["kind"] == "call") {
                if (measure) {
                    ++calls;
                    if (language == "ST") m0_cost += 2; // call and return
                    require(calls <= 100000, "expanded call budget");
                }
                values.push_back(expand(n["definition"], args, g, measure));
            } else {
                fields(n, {"kind", "op", "type", "value", "args"});
                auto op = parse_operation(text(n["op"]));
                require(n["type"] == "BOOL" || n["type"] == "UDINT", "primitive type");
                auto type = n["type"] == "BOOL" ? Type::Bool : Type::Udint;
                uint32_t value = number(n["value"], 0xffffffffU);
                require(op != Op::Mux && op != Op::Subfunction &&
                            (language=="FBD" || (op!=Op::Encode32 && op!=Op::Decode32)),
                        "package requires lowered primitives");
                if (language == "ST" && (op == Op::Extract || op == Op::Putbit)) {
                    require(args.size() >= 2, "missing ST bit index");
                    const auto &index = d["nodes"].at(n["args"][1]["node"].get<size_t>());
                    require(index.value("op", std::string()) == "const" && index["value"] < 32,
                            "ST bit index must be a literal from 0 through 31");
                }
                Ref r;
                if (op == Op::Input) {
                    require(args.empty() && value < actual.size() &&
                                (type == Type::Bool ? "BOOL" : "UDINT") == d["input_types"][value],
                            "formal input node");
                    r = actual[value];
                } else {
                    r = g.add(op, type, value, args);
                    if (measure) {
                        if (language == "ST") m0_cost += m0_primitive(g,op,args);
                        if (op == Op::Constant)
                            constant_bytes += type == Type::Bool ? 1 : 4;
                        else {
                            ++expanded_ops;
                            if (language == "FBD" && op == Op::Nonzero) constant_bytes += 4;
                        }
                    }
                }
                values.push_back({r});
            }
        }
        std::vector<Ref> outputs;
        for (auto r : d["outputs"])
            outputs.push_back(resolve(r));
        if (measure) {
            expanded_ops += inversions.size();
            if (language == "ST") m0_cost += inversions.size();
        }
        return outputs;
    };
    // Validate even unused definitions with typed dummy bindings; never trust their labels.
    for (auto d : defs) {
        Graph probe(1);
        std::vector<Ref> args;
        for (auto t : d.second["input_types"])
            args.push_back(probe.add(Op::Constant, t == "BOOL" ? Type::Bool : Type::Udint, 0));
        probe.roots = expand(d.first, args, probe, false);
    }
    std::vector<Ref> inputs;
    for (unsigned i = 0; i < root_types.size(); ++i)
        inputs.push_back(graph.add(Op::Input, Type::Bool, i));
    graph.roots = expand(root, inputs, graph, true);
    graph = prune(graph);
    Json report = {{"height", heights[root]},
                   {"total_height", heights[root] + external},
                   {"definition_count", used.size()},
                   {"stored_definition_count", defs.size()},
                   {"instance_count", calls},
                   {"call_count", calls},
                   {"root_executions", 1},
                   {"expanded_operations", expanded_ops},
                   {"expanded_constant_payload_bytes", constant_bytes},
                   {"table_bytes", table_bytes},
                   {"device_time", nullptr},
                   {"device_ram", nullptr},
                   {"device_flash", nullptr},
                   {"longest_path", paths[root]}};
    Json definition_metrics = Json::array();
    if (language == "ST") report["cortex_m0_cost"] = m0_cost;
    uint64_t unique_operations = 0, unique_constants = 0, max_elements = 0;
    for (auto id : used) {
        const auto &d = defs.at(id);
        unsigned local_ops = 0, constants = 0,
                 elements = d["input_types"].size() + d["outputs"].size(), local_calls = 0;
        std::set<std::pair<unsigned, unsigned>> neg;
        auto inverse = [&](const Json &r) {
            if (r["inverted"].get<bool>()) {
                const auto &owner = d["nodes"][r["node"].get<unsigned>()];
                if (owner["kind"] == "call" || owner["op"] != "const")
                    neg.insert({r["node"].get<unsigned>(), r["port"].get<unsigned>()});
            }
        };
        for (auto &n : d["nodes"]) {
            for (auto &r : n["args"])
                inverse(r);
            if (n["kind"] == "call") {
                ++local_calls;
                ++elements;
            } else if (n["op"] != "input") {
                ++elements;
                if (language == "FBD" && n["op"] == "nonzero") {
                    ++elements;
                    constants += 4;
                }
                if (n["op"] == "const")
                    constants += n["type"] == "BOOL" ? 1 : 4;
                else
                    ++local_ops;
            }
        }
        for (auto &r : d["outputs"])
            inverse(r);
        local_ops += neg.size();
        elements += neg.size();
        unique_operations += local_ops;
        unique_constants += constants;
        max_elements = std::max(max_elements, uint64_t(elements));
        definition_metrics.push_back({{"id", id},
                                      {"local_operations", local_ops},
                                      {"call_sites", local_calls},
                                      {"elements", elements},
                                      {"constant_payload_bytes", constants}});
    }
    report["unique_definition_operations"] = unique_operations;
    report["unique_constant_payload_bytes"] = unique_constants;
    report["max_definition_elements"] = max_elements;
    report["definitions"] = definition_metrics;
    unsigned maximum = 0;
    for (auto h : heights)
        maximum = std::max(maximum, unsigned(h.second));
    report["stored_max_height"] = maximum;
    report["flattened_cse_operations"] = logical_metrics(graph)["operations"];
    return PackageResult(std::move(graph), report);
}
Json inspect_package(const Json &package, bool repair) {
    auto result = check_package(package, repair);
    Json normalized = package;
    auto before = result.report;
    bool repaired = result.report["total_height"].get<unsigned>() > 3 ||
                    result.report["stored_max_height"].get<unsigned>() > 3;
    if (repaired) {
        normalized = flat_package(result.expanded, package["language"], package["external_levels"],
                                  package["table_bytes"]);
        auto after = check_package(normalized);
        require(after.expanded.tables() == result.expanded.tables(), "inlining changed function");
        result = std::move(after);
        result.report["repair_before"] = before;
    }
    result.report["repaired"] = repaired;
    result.report["repair_method"] =
        repaired ? Json("inline_all_boundaries_then_cse") : Json(nullptr);
    return {{"package", normalized},
            {"metrics", result.report},
            {"ir", result.expanded.json()},
            {"tables", result.expanded.tables()}};
}
} // namespace engine
} // namespace b2f
