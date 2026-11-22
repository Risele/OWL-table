#include <vcl.h>
#pragma hdrstop
#include "engine/export.hpp"
#include <algorithm>
#include <functional>
namespace b2f {
namespace {
S chain_operator(const J &n) {
    S op = n.value("op", S());
    if (op == "and" || op == "word_and")
        return " AND ";
    if (op == "or" || op == "word_or")
        return " OR ";
    if (op == "xor" || op == "word_xor")
        return " XOR ";
    return "";
}
// Flatten a single-use subtree only across identical operators and positive edges.
// Bound each line, including chains over intermediate words or child macro ports.
struct Chains {
    std::vector<std::vector<const J *>> terms;
    std::vector<bool> folded;
    std::vector<unsigned> uses;
    std::map<std::pair<size_t, size_t>, unsigned> negations;

    explicit Chains(const J &d) : terms(d.at("nodes").size()), folded(terms.size()), uses(terms.size()) {
        const auto &nodes = d.at("nodes");
        auto count = [&](const J &r) {
            size_t node = r.at("node");
            ++uses[node];
            if (r.at("inverted").get<bool>())
                ++negations[{node, r.at("port").get<size_t>()}];
        };
        for (const auto &n : nodes)
            for (const auto &r : n.at("args"))
                count(r);
        for (const auto &r : d.at("outputs"))
            count(r);
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto &n = nodes[i];
            if (chain_operator(n).empty())
                continue;
            size_t width = n.at("args").size();
            for (const auto &r : n.at("args")) {
                size_t child = r.at("node");
                if (uses[child] == 1 && !r.at("inverted").get<bool>() && !terms[child].empty() &&
                    nodes[child].at("op") == n.at("op") && width + terms[child].size() - 1 <= 16) {
                    width += terms[child].size() - 1;
                    terms[i].insert(terms[i].end(), terms[child].begin(), terms[child].end());
                    folded[child] = true;
                } else
                    terms[i].push_back(&r);
            }
        }
        auto order = [&](const J *r) {
            size_t index = r->at("node");
            const auto &node = nodes[index];
            return node.value("op", S()) == "input" ? node.at("value").get<size_t>()
                                                    : d.at("input_types").size() + index;
        };
        for (size_t i = 0; i < terms.size(); ++i)
            if (!folded[i])
                std::stable_sort(terms[i].begin(), terms[i].end(),
                                 [&](const J *a, const J *b) { return order(a) < order(b); });
    }
};
}
J export_st(const J &package, const S &name, const std::vector<S> &inputs,
            const std::vector<S> &outputs, const std::vector<S> &comments) {
    require(comments.empty() || comments.size()==inputs.size(),"Input comment count");
    engine::check_package(package);
    require(package.at("language") == "ST", "ST package required");
    require(identifier(name) == name, "invalid macro name");
    require(outputs.size() == 2, "Boolean root requires Q/QN ports");
    parse({{"inputs", inputs},
           {"output", outputs[0]},
           {"inverted", outputs[1]},
           {"rules", J::array()}},
          name);
    S root = package.at("root"), prefix = "Logic";
    std::vector<S> publicNames = inputs;
    publicNames.insert(publicNames.end(), outputs.begin(), outputs.end());
    publicNames.push_back(name);
    std::set<S> unique;
    for (auto &n : publicNames)
        require(identifier(n) == n && unique.insert(lower(n)).second,
                "invalid/duplicate public name");
    for (;;) {
        bool collision = false;
        for (auto &n : publicNames)
            if (lower(n).find(lower(prefix)) == 0)
                collision = true;
        if (!collision)
            break;
        prefix = "G" + prefix;
    }
    std::map<S, J> defs, docs;
    std::map<S, S> names;
    std::map<S, uint64_t> localBytes;
    unsigned serial = 0;
    for (auto &d : package.at("definitions")) {
        S id = d.at("id");
        defs[id] = d;
        names[id] = id == root ? name : prefix + "Definition" + std::to_string(serial++);
    }
    require(inputs.size() == defs.at(root).at("input_types").size() &&
                outputs.size() == defs.at(root).at("outputs").size(),
            "public interface arity");
    J documents = J::array();
    S allSource, created = now_iso();
    std::function<void(S)> emit = [&](S id) {
        if (docs.count(id))
            return;
        const auto &d = defs.at(id);
        for (auto &n : d.at("nodes"))
            if (n.at("kind") == "call")
                emit(n.at("definition"));
        std::vector<S> ins, outs;
        for (size_t i = 0; i < d.at("input_types").size(); ++i)
            ins.push_back(id == root ? inputs[i] : prefix + "P" + std::to_string(i));
        for (size_t i = 0; i < d.at("outputs").size(); ++i)
            outs.push_back(id == root ? outputs[i] : prefix + "R" + std::to_string(i));
        std::vector<std::vector<S>> signals, types;
        std::vector<std::pair<S, S>> locals, instances;
        std::map<S, S> inverse;
        Chains chains(d);
        std::map<size_t, S> destinations;
        for (size_t i = 0; i < outs.size(); ++i) {
            const auto &r = d.at("outputs")[i];
            if (!r.at("inverted").get<bool>() && r.at("port") == 0)
                destinations.emplace(r.at("node").get<size_t>(), outs[i]);
        }
        S body;
        auto ref = [&](const J &r) {
            S v = signals.at(r.at("node").get<size_t>()).at(r.at("port").get<size_t>());
            if (!r.at("inverted").get<bool>())
                return v;
            if (v == "TRUE")
                return S("FALSE");
            if (v == "FALSE")
                return S("TRUE");
            if (chains.negations.at({r.at("node").get<size_t>(), r.at("port").get<size_t>()}) == 1)
                return "NOT " + v;
            if (!inverse.count(v)) {
                S n = prefix + "Inverse" + std::to_string(inverse.size());
                inverse[v] = n;
                locals.push_back({n, "BOOL"});
                body += n + " := NOT " + v + ";\r\n";
            }
            return inverse.at(v);
        };
        size_t k = 0;
        for (auto &n : d.at("nodes")) {
            std::vector<S> values, ts;
            S v = prefix + "V" + std::to_string(k++);
            if (n.at("kind") == "call") {
                S child = n.at("definition"), instance = v + "Call";
                instances.push_back({instance, names.at(child)});
                S call = instance + "(";
                size_t a = 0;
                for (auto &r : n.at("args")) {
                    if (a)
                        call += ", ";
                    call += docs.at(child).at("InputNames").at(a++).get<S>() + " := " + ref(r);
                }
                size_t port = 0;
                for (auto &t : docs.at(child).at("OutputDataTypes")) {
                    S local = v + "P" + std::to_string(port), type = t == 6 ? "BOOL" : "UDINT";
                    locals.push_back({local, type});
                    values.push_back(local);
                    ts.push_back(type);
                    if (a++)
                        call += ", ";
                    call += docs.at(child).at("OutputNames").at(port++).get<S>() + " => " + local;
                }
                body += call + ");\r\n";
            } else {
                S op = n.at("op"), type = n.at("type");
                ts.push_back(type);
                if (op == "input")
                    values.push_back(ins.at(n.at("value").get<size_t>()));
                else if (op == "const")
                    values.push_back(type == "BOOL"
                                         ? (n.at("value").get<unsigned>() ? "TRUE" : "FALSE")
                                         : n.at("value").dump());
                else {
                    size_t node = k - 1;
                    if (chains.folded[node]) {
                        signals.emplace_back();
                        types.push_back(ts);
                        continue;
                    }
                    if (destinations.count(node))
                        v = destinations.at(node);
                    else if (op == "putbit" &&
                             d.at("nodes")[n.at("args")[0].at("node").get<size_t>()].value("op", S()) == "putbit" &&
                             chains.uses[n.at("args")[0].at("node").get<size_t>()] == 1)
                        v = ref(n.at("args")[0]);
                    else
                        locals.push_back({v, type});
                    values.push_back(v);
                    std::vector<S> a;
                    S expression;
                    if (!chains.terms[node].empty()) {
                        S separator = chain_operator(n);
                        for (const J *r : chains.terms[node]) {
                            if (!expression.empty())
                                expression += separator;
                            expression += ref(*r);
                        }
                    } else {
                        for (const auto &r : n.at("args"))
                            a.push_back(ref(r));
                        if (op == "word_not")
                            expression = "NOT " + a.at(0);
                        else if (op == "shl" || op == "shr")
                            expression =
                                (op == "shl" ? "SHL(" : "SHR(") + a.at(0) + ", " + a.at(1) + ")";
                        else if (op == "extract") {
                            S word = a.at(0);
                            if (d.at("nodes")[n.at("args")[0].at("node").get<size_t>()].value("op", S()) == "const") {
                                word = prefix + "Word" + std::to_string(node);
                                locals.push_back({word, "UDINT"});
                                body += word + " := " + a[0] + ";\r\n";
                            }
                            expression = word + "." + a.at(1);
                        } else if (op == "putbit") {
                            if (v != a.at(0))
                                body += v + " := " + a[0] + ";\r\n";
                            body += v + "." + a.at(1) + " := " + a.at(2) + ";\r\n";
                        } else if (op == "word_equal")
                            expression = a.at(0) + " = " + a.at(1);
                        else if (op == "nonzero")
                            expression = a.at(0) + " <> 0";
                        else if (op == "sel")
                            body += "IF " + a.at(0) + " THEN\r\n" + v + " := " + a.at(2) +
                                    ";\r\nELSE\r\n" + v + " := " + a.at(1) + ";\r\nEND_IF;\r\n";
                        else
                            require(false, "unsupported ST operation: " + op);
                    }
                    if (!expression.empty())
                        body += v + " := " + expression + ";\r\n";
                }
            }
            signals.push_back(values);
            types.push_back(ts);
        }
        J outTypes = J::array();
        for (size_t i = 0; i < outs.size(); ++i) {
            auto r = d.at("outputs").at(i);
            outTypes.push_back(
                types.at(r.at("node").get<size_t>()).at(r.at("port").get<size_t>()) == "BOOL" ? 6
                                                                                              : 3);
            S value=ref(r);
            if(outs[i]!=value)body += outs[i] + " := " + value + ";\r\n";
        }
        S source;
        if (id != root) source = st_annotation("GroupName", internal_macro_group);
        source += "FUNCTION_BLOCK " + names[id] + "\r\nVAR_INPUT\r\n";
        J inTypes = J::array();
        for (size_t i = 0; i < ins.size(); ++i) {
            S t = d.at("input_types").at(i);
            source += ins[i] + " : " + t + ";";
            if(id==root && !comments.empty() && !comments[i].empty()) { S c=comments[i];for(char &ch:c)if(ch=='\r' || ch=='\n')ch=' ';source+=" // "+c; }
            source+="\r\n";
            inTypes.push_back(t == "BOOL" ? 6 : 3);
        }
        source += "END_VAR\r\nVAR_OUTPUT\r\n";
        for (size_t i = 0; i < outs.size(); ++i)
            source += outs[i] + " : " + (outTypes[i] == 6 ? "BOOL" : "UDINT") + ";\r\n";
        source += "END_VAR\r\n";
        if (!locals.empty() || !instances.empty()) {
            source += "VAR\r\n";
            for (auto &p : instances)
                source += p.first + " : " + p.second + ";\r\n";
            for (auto &p : locals)
                source += p.first + " : " + p.second + ";\r\n";
            source += "END_VAR\r\n";
        }
        source += body + "END_FUNCTION_BLOCK\r\n";
        J vn = J::array(), vt = J::array(), vi = J::array(), storage = J::array(),
          defaults = J::array(), ranges = J::array(), ii = J::array(), oi = J::array(),
          fi = J::array(), fb = J::array();
        unsigned index = 0;
        auto variable = [&](S n, int t) {
            vn.push_back(n);
            vt.push_back(t);
            vi.push_back(index++);
            storage.push_back(2);
            defaults.push_back(nullptr);
            ranges.push_back({{"Start", J::array()}, {"End", J::array()}});
        };
        for (size_t i = 0; i < ins.size(); ++i) {
            ii.push_back(index);
            variable(ins[i], inTypes[i]);
        }
        for (auto &p : instances) {
            fi.push_back(index++);
            fb.push_back({{"Name", p.first}, {"TemplateName", p.second}, {"VariableDataType", 3}});
        }
        for (size_t i = 0; i < outs.size(); ++i) {
            oi.push_back(index);
            variable(outs[i], outTypes[i]);
        }
        for (auto &p : locals)
            variable(p.first, p.second == "BOOL" ? 6 : 3);
        S uid = uuid5(uuid5(ns, "BOOLEAN:PACKAGE:ST:" + name), names[id]),
          revision = uuid5(uid, source);
        J doc = {{"Id", uid},
                 {"Name", names[id]},
                 {"Password", nullptr},
                 {"InputNames", ins},
                 {"InputDataTypes", inTypes},
                 {"InputVariableIndexes", ii},
                 {"OutputNames", outs},
                 {"OutputDataTypes", outTypes},
                 {"OutputVariableIndexes", oi},
                 {"SourceCode", source},
                 {"LastModificationId", revision},
                 {"ModificationLabel", revision},
                 {"VariableNames", vn},
                 {"VariableDataTypes", vt},
                 {"VariableStorageTypes", storage},
                 {"VariableIndexes", vi},
                 {"DefaultValues", defaults},
                 {"ArrayRanges", ranges},
                 {"FbInstances", fb},
                 {"FbInstanceIndexes", fi},
                 {"CreationDateTime", created},
                 {"Discriminator", 2}};
        uint64_t bytes = 0;
        for (const auto &local : locals)
            bytes += local.second == "BOOL" ? 1 : 4;
        for (const auto &n : d.at("nodes"))
            if (n.at("kind") == "call")
                bytes += localBytes.at(n.at("definition").get<S>());
        localBytes[id] = bytes;
        docs[id] = doc;
        documents.push_back(doc);
        allSource += source;
    };
    emit(root);
    const auto &r = docs.at(root);
    J vars = r.at("InputNames");
    for (auto &o : r.at("OutputNames"))
        vars.push_back(o);
    J meta = {{"Screenshot", icon()},
              {"MetaData",
               {{"FunctionBlockUniqueId", r.at("Id")},
                {"Description", "Boolean function; independent ST package"},
                {"Author", ""},
                {"GroupName", "Boolean"},
                {"VariableNames", vars},
                {"VariableDescriptions", vars},
                {"Discriminator", 2}}},
              {"ModificationLabel", r.at("ModificationLabel")},
              {"Name", name},
              {"Tags", J::array()},
              {"CreationDateTime", created},
              {"ModificationDateTime", created},
              {"HasPassword", false},
              {"InputVariableNames", r.at("InputNames")},
              {"InputVariableDisplayNames", r.at("InputNames")},
              {"InputVariableTypes", r.at("InputDataTypes")},
              {"OutputVariableNames", r.at("OutputNames")},
              {"OutputVariableDisplayNames", r.at("OutputNames")},
              {"OutputVariableTypes", r.at("OutputDataTypes")}};
    if(!comments.empty())for(size_t i=0;i<comments.size();++i)meta["MetaData"]["VariableDescriptions"][i]=comments[i];
    return {{"documents", documents},
            {"metadata", meta},
            {"declared_local_bytes", localBytes.at(root)},
            {"source", allSource},
            {"root", names.at(root)}};
}
J export_request(const J &r) {
    for (auto i = r.begin(); i != r.end(); ++i)
        require(i.key() == "package" || i.key() == "name" || i.key() == "inputs" ||
                    i.key() == "outputs" || i.key()=="input_comments",
                "unknown export field");
    auto writer = r.at("package").at("language") == "ST" ? export_st : export_fbd;
    return writer(r.at("package"), r.at("name"), r.at("inputs").get<std::vector<S>>(),
                  r.at("outputs").get<std::vector<S>>(),r.value("input_comments",std::vector<S>()));
}
} // namespace b2f
