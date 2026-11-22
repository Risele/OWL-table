#include <vcl.h>
#pragma hdrstop
#include "verification/native.hpp"
#include <functional>
namespace b2f {
namespace {
const J &em(const J &e) {
    return e.contains("ElementModel") ? e.at("ElementModel")
                                      : e.at("ElementBlockModel").at("ElementModel");
}
S native_type(const J &type) {
    require(type == 0 || type == 3, "unsupported FBD data type");
    return type == 0 ? "BOOL" : "UDINT";
}
J ep(size_t n, unsigned port = 0, bool inverse = false) {
    return {{"node", n}, {"port", port}, {"inverted", inverse}};
}
std::vector<J> interface_elements(const J &doc, int discriminator) {
    std::map<unsigned, J> sorted;
    for (auto &e : doc.at("DocumentModel").at("Elements"))
        if (e.at("Discriminator") == discriminator)
            require(sorted.emplace(e.at("Index"), e).second, "duplicate FBD interface index");
    std::vector<J> result;
    for (unsigned i = 0; i < sorted.size(); ++i) {
        require(sorted.count(i), "FBD interface index gap");
        result.push_back(sorted.at(i));
    }
    return result;
}
} // namespace
J read_fbd_artifact(const J &docs, const J &metadata) {
    require(docs.is_object() && !docs.empty() && docs.size() <= 256,
            "native FBD definitions required");
    S root = metadata.at("ComponentMetaData").at("UniqueId");
    require(docs.contains(root), "missing FBD root");
    require(metadata.at("MetaDataInputs") == docs.at(root).at("MetaData").at("MetaDataInputs") &&
                metadata.at("MetaDataOutputs") ==
                    docs.at(root).at("MetaData").at("MetaDataOutputs"),
            "FBD public metadata mismatch");
    J definitions = J::array();
    std::set<S> definitionNames;
    for (auto it = docs.begin(); it != docs.end(); ++it) {
        auto &doc = it.value();
        S id = it.key();
        require(doc.at("Discriminator") == 2 && doc.at("Password").is_null(),
                "unsupported FBD storage");
        auto &dm = doc.at("DocumentModel");
        require(dm.at("UniqueId") == id &&
                    doc.at("MetaData").at("ComponentMetaData").at("UniqueId") == id,
                "FBD definition id mismatch");
        require(dm.at("Variables").empty() && dm.at("VariableCatalogSections").empty(),
                "stateful FBD variables forbidden");
        S name = doc.at("MetaData").at("ComponentMetaData").at("Name");
        require(definitionNames.insert(lower(name)).second, "duplicate FBD definition name");
        auto ins = interface_elements(doc, 38), outs = interface_elements(doc, 39);
        for (auto discriminator : {38, 39}) {
            auto ports = discriminator == 38 ? ins : outs;
            S key = discriminator == 38 ? "MetaDataInputs" : "MetaDataOutputs",
              field = discriminator == 38 ? "InputName" : "OutputName";
            require(doc.at("MetaData").at(key).size() == ports.size(), "FBD metadata arity");
            std::set<S> unique;
            for (size_t i = 0; i < ports.size(); ++i)
                require(doc.at("MetaData").at(key)[i].at("Name") == ports[i].at(field) &&
                            doc.at("MetaData").at(key)[i].at("DataType") == em(ports[i]).at("Ports")[0].at("DataType") &&
                            unique.insert(lower(ports[i].at(field))).second,
                        "FBD metadata interface mismatch");
        }
        std::map<S, J> elements, ports;
        std::map<S, S> portOwner, connections;
        std::set<S> allIds;
        for (auto &e : dm.at("Elements")) {
            const auto &m = em(e);
            S uid = m.at("UniqueId");
            require(allIds.insert(uid).second, "duplicate FBD element id");
            elements[uid] = e;
            require(!m.at("IsInvertOutput").get<bool>(),
                    "unsupported native output inversion flag");
            for (auto &p : m.at("Ports")) {
                S pid = p.at("UniqueId");
                require(allIds.insert(pid).second, "duplicate FBD port id");
                ports[pid] = p;
                portOwner[pid] = uid;
                native_type(p.at("DataType"));
                require((p.at("PortType") == 0 || p.at("PortType") == 1),
                        "unsupported FBD port type/direction");
            }
        }
        for (auto &w : dm.at("ConnectorModels")) {
            require(w.at("Discriminator") == 1, "unsupported FBD connector");
            auto &c = w.at("ConnectorModel");
            require(allIds.insert(c.at("UniqueId")).second, "duplicate FBD wire id");
            S a = c.at("FromPortUId"), b = c.at("ToPortUId");
            require(ports.count(a) && ports.count(b), "missing FBD wire port");
            require(ports.at(a).at("PortType") == 1 && ports.at(b).at("PortType") == 0,
                    "FBD wire direction");
            require(ports.at(a).at("DataType") == ports.at(b).at("DataType"), "FBD wire data type");
            require(connections.emplace(b, a).second, "multiple FBD input drivers");
        }
        for (auto &p : ports)
            if (p.second.at("PortType") == 0)
                require(connections.count(p.first), "unconnected FBD input");
        J nodes = J::array();
        std::map<S, J> signals;
        std::set<S> active, finished;
        auto primitive = [&](S op, unsigned value, J args, S type) {
            J r = ep(nodes.size());
            nodes.push_back({{"kind", "primitive"},
                             {"op", op},
                             {"type", type},
                             {"value", value},
                             {"args", args}});
            return r;
        };
        std::function<void(S)> visit;
        std::function<J(S)> signal = [&](S pid) {
            require(ports.at(pid).at("PortType") == 1, "FBD signal direction");
            visit(portOwner.at(pid));
            return signals.at(pid);
        };
        visit = [&](S uid) {
            if (finished.count(uid))
                return;
            require(active.insert(uid).second, "FBD wire cycle");
            auto &e = elements.at(uid);
            auto &m = em(e);
            int discr = e.at("Discriminator"), fb = m.at("FbType");
            std::vector<S> inputPorts, outputPorts;
            for (auto &p : m.at("Ports"))
                (p.at("PortType") == 0 ? inputPorts : outputPorts).push_back(p.at("UniqueId"));
            J args = J::array();
            for (auto &pid : inputPorts)
                args.push_back(signal(connections.at(pid)));
            if (discr == 37) {
                require(fb == -1 && e.contains("ElementBlockModel"), "invalid FBD macro instance");
                S child = e.at("MacroUniqueId");
                require(docs.contains(child), "missing FBD macro dependency");
                require(e.at("LastUpdatingId") == docs.at(child).at("LastUpdatingId"),
                        "stale FBD child revision");
                auto childIns = interface_elements(docs.at(child), 38),
                     childOuts = interface_elements(docs.at(child), 39);
                require(inputPorts.size() == childIns.size() &&
                            outputPorts.size() == childOuts.size(),
                        "FBD macro arity");
                J bindings = J::array();
                std::map<S, size_t> formalIn, formalOut;
                for (size_t i = 0; i < childIns.size(); ++i)
                    formalIn[em(childIns[i]).at("UniqueId")] = i;
                for (size_t i = 0; i < childOuts.size(); ++i)
                    formalOut[em(childOuts[i]).at("UniqueId")] = i;
                std::vector<J> ordered(childIns.size());
                std::set<size_t> bound;
                for (size_t i = 0; i < inputPorts.size(); ++i) {
                    S owner = ports.at(inputPorts[i]).at("ElementOwnerUId");
                    require(formalIn.count(owner), "missing FBD formal input owner");
                    size_t index = formalIn.at(owner);
                    require(bound.insert(index).second, "duplicate FBD formal input");
                    require(ports.at(inputPorts[i]).at("DataType") == em(childIns[index]).at("Ports")[0].at("DataType"), "FBD formal input type");
                    ordered[index] = args[i];
                }
                for (auto &r : ordered)
                    bindings.push_back(r);
                size_t node = nodes.size();
                nodes.push_back(
                    {{"kind", "call"}, {"id", uid}, {"definition", child}, {"args", bindings}});
                bound.clear();
                for (auto &pid : outputPorts) {
                    S owner = ports.at(pid).at("ElementOwnerUId");
                    require(formalOut.count(owner), "missing FBD formal output owner");
                    size_t index = formalOut.at(owner);
                    require(bound.insert(index).second, "duplicate FBD formal output");
                    require(ports.at(pid).at("DataType") == em(childOuts[index]).at("Ports")[0].at("DataType"), "FBD formal output type");
                    signals[pid] = ep(node, index);
                }
            } else if (discr == 39) {
                require(fb == 13 && args.size() == 1 && outputPorts.empty(), "invalid FBD output");
            } else {
                require(outputPorts.size() == 1, "invalid FBD primitive output count");
                S type = native_type(ports.at(outputPorts[0]).at("DataType"));
                J value;
                if (discr == 38) {
                    require(fb == 12 && args.empty(), "invalid FBD input");
                    value = primitive("input", e.at("Index"), args, type);
                } else if (discr == 29) {
                    require(fb == 30 && args.empty() && e.at("Value").is_number_integer() &&
                                e.at("Value").get<int64_t>() >= 0 &&
                                e.at("Value").get<uint64_t>() <= (type == "BOOL" ? 1ULL : 0xffffffffULL),
                            "invalid FBD constant");
                    value = primitive("const", e.at("Value"), args, type);
                } else {
                    std::vector<int> types;
                    for (auto &pin : inputPorts) types.push_back(ports.at(pin).at("DataType"));
                    if (discr == 18) {
                        require(fb == 4 || fb == 5 || fb == 6 || fb == 14, "unknown FBD logic");
                        require(types == std::vector<int>(fb == 14 ? 1 : 2, type == "BOOL" ? 0 : 3),
                                "FBD logic port types");
                        require(e.at("FbType") == fb && e.at("PinDefaultValue") == unsigned(fb == 5),
                                "FBD primitive metadata mismatch");
                        if (fb == 14 && type == "BOOL") {
                            value = args[0];
                            value["inverted"] = !value.at("inverted").get<bool>();
                        } else {
                            S op = fb == 4 ? "or" : fb == 5 ? "and" : fb == 6 ? "xor" : "not";
                            value = primitive(type == "UDINT" ? "word_" + op : op, 0, args, type);
                        }
                    } else {
                        struct Signature { int discriminator; const char *op; std::vector<int> inputs; int output; };
                        static const std::map<int, Signature> signatures = {
                            {2,{23,"shl",{3,3},3}}, {3,{23,"shr",{3,3},3}},
                            {20,{21,"sel",{0,3,3},3}}, {21,{20,"extract",{3,3},0}},
                            {22,{20,"putbit",{3,3,0},3}}, {24,{21,"nonzero",{3,3},0}}, {25,{21,"word_equal",{3,3},0}},
                            {37,{20,"dc32",{3},3}}, {38,{20,"cd32",{3},3}}
                        };
                        require(signatures.count(fb), "unknown FBD primitive");
                        auto &sig = signatures.at(fb);
                        require(sig.discriminator == discr && sig.inputs == types &&
                                ports.at(outputPorts[0]).at("DataType") == sig.output, "FBD primitive signature");
                        if (fb == 24) {
                            auto &rhs = nodes.at(args[1].at("node").get<size_t>());
                            require(rhs.value("op", S()) == "const" && rhs.at("value") == 0,
                                    "expected unsigned comparison with zero");
                            args.erase(1);
                        }
                        value = primitive(sig.op, 0, args, type);
                    }
                }
                signals[outputPorts[0]] = value;
            }
            active.erase(uid);
            finished.insert(uid);
        };
        for (auto &e : elements)
            visit(e.first); // Reject unknown/dead/stateful primitives too.
        J roots = J::array();
        for (auto &e : outs) {
            S pid = em(e).at("Ports")[0].at("UniqueId");
            roots.push_back(signal(connections.at(pid)));
        }
        std::vector<S> inputTypes;
        for (auto &e : ins) inputTypes.push_back(native_type(em(e).at("Ports")[0].at("DataType")));
        definitions.push_back({{"id", id},
                               {"name", name},
                               {"language", "FBD"},
                               {"pure", true},
                               {"input_types", inputTypes},
                               {"nodes", nodes},
                               {"outputs", roots}});
    }
    return {{"schema_version", 1}, {"kind", "macro_package"},    {"language", "FBD"},
            {"root", root},        {"definitions", definitions}, {"external_levels", 0},
            {"table_bytes", 0}};
}
} // namespace b2f
