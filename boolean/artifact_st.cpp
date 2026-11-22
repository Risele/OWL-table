#include <vcl.h>
#pragma hdrstop
#include "verification/native.hpp"
#include <cctype>
namespace b2f {
namespace {
J endpoint(size_t n, unsigned port = 0, bool inverse = false) {
    return {{"node", n}, {"port", port}, {"inverted", inverse}};
}
struct Value {
    J ref;
    S type;
};
struct Parser {
    std::vector<S> tokens;
    size_t pos = 0;
    const J &document;
    const std::map<S, J> &documents;
    J nodes = J::array();
    std::map<S, Value> values;
    std::map<S, S> declared, instances;
    std::vector<S> inputs, outputs;
    std::set<S> called;
    Parser(const J &d, const std::map<S, J> &docs) : document(d), documents(docs) {
        S s = d.at("SourceCode");
        for (size_t i = 0; i < s.size();) {
            unsigned char c = s[i];
            if (std::isspace(c)) {
                ++i;
                continue;
            }
            if(c=='/' && i+1<s.size() && s[i+1]=='/') {
                i+=2;
                while(i<s.size() && s[i]!='\r' && s[i]!='\n')++i;
                continue;
            }
            size_t begin = i++;
            if (std::isalnum(c) || c == '_' || c >= 128) {
                while (i < s.size()) {
                    unsigned char b = s[i];
                    if (!std::isalnum(b) && b != '_' && b < 128)
                        break;
                    ++i;
                }
            } else if (i < s.size() && ((c == ':' && s[i] == '=') || (c == '=' && s[i] == '>') ||
                                        (c == '<' && s[i] == '>')))
                ++i;
            else
                require(S("();:,.=").find(c) != S::npos, "unsupported ST token");
            tokens.push_back(s.substr(begin, i - begin));
        }
    }
    S peek() const { return pos < tokens.size() ? tokens[pos] : ""; }
    S take() {
        require(pos < tokens.size(), "incomplete ST source");
        return tokens[pos++];
    }
    void need(const S &s) { require(take() == s, "expected ST token: " + s); }
    Value add(S op, S type, unsigned value = 0, std::vector<Value> args = {}) {
        J refs = J::array();
        for (auto &a : args)
            refs.push_back(a.ref);
        J r = endpoint(nodes.size());
        nodes.push_back(
            {{"kind", "primitive"}, {"op", op}, {"type", type}, {"value", value}, {"args", refs}});
        return {r, type};
    }
    Value bitNumber() {
        need(".");
        S n = take();
        require(n.size() <= 2 && n.find_first_not_of("0123456789") == S::npos &&
                std::stoul(n) < 32, "ST bit number must be 0..31");
        return add("const", "UDINT", unsigned(std::stoul(n)));
    }
    Value atom() {
        S t = take();
        if (t == "(") {
            auto v = expression();
            need(")");
            return v;
        }
        if (t == "NOT") {
            auto v = atom();
            if (v.type == "BOOL")
                v.ref["inverted"] = !v.ref.at("inverted").get<bool>();
            else
                v = add("word_not", "UDINT", 0, {v});
            return v;
        }
        if (t == "SHL" || t == "SHR") {
            need("(");
            auto a = expression();
            need(",");
            auto b = expression();
            need(")");
            require(a.type == "UDINT" && b.type == "UDINT", "ST shift types");
            return add(t == "SHL" ? "shl" : "shr", "UDINT", 0, {a, b});
        }
        if (t == "TRUE" || t == "FALSE")
            return add("const", "BOOL", t == "TRUE");
        if (!t.empty() && std::isdigit((unsigned char)t[0])) {
            require(t.find_first_not_of("0123456789") == S::npos && t.size() <= 10,
                    "invalid UDINT literal");
            uint64_t n = std::stoull(t);
            require(n <= 0xffffffffULL, "UDINT literal overflow");
            return add("const", "UDINT", unsigned(n));
        }
        require(values.count(t), "ST read before assignment or unknown name: " + t);
        if (peek() == ".") {
            require(values.at(t).type == "UDINT", "ST bit access requires UDINT");
            return add("extract", "BOOL", 0, {values.at(t), bitNumber()});
        }
        return values.at(t);
    }
    static int precedence(const S &s) {
        return s == "OR" ? 1 : s == "XOR" ? 2 : s == "AND" ? 3 : (s == "<>" || s == "=") ? 4 : 0;
    }
    Value expression(int minimum = 1) {
        Value a = atom();
        while (precedence(peek()) >= minimum) {
            S op = take();
            auto b = expression(precedence(op) + 1);
            require(a.type == b.type, "ST binary types differ");
            if (op == "=") {
                require(a.type == "UDINT", "ST equality requires two UDINT operands");
                a = add("word_equal", "BOOL", 0, {a,b});
            } else if (op == "<>") {
                require(a.type == "UDINT" &&
                            nodes.at(b.ref.at("node").get<size_t>()).at("op") == "const" &&
                            nodes.at(b.ref.at("node").get<size_t>()).at("value") == 0,
                        "only UDINT <> 0 supported");
                a = add("nonzero", "BOOL", 0, {a});
            } else
                a = add((a.type == "UDINT" ? "word_" : "") + lower(op), a.type, 0, {a, b});
        }
        return a;
    }
    void assign(const S &n, Value v) {
        require(declared.count(n) && declared.at(n) == v.type, "ST assignment type/name");
        require(std::find(inputs.begin(), inputs.end(), n) == inputs.end(),
                "ST input assignment forbidden");
        values[n] = v;
    }
    J parse() {
        need("FUNCTION_BLOCK");
        require(take() == document.at("Name"), "ST definition name mismatch");
        std::set<S> unique;
        while (peek() == "VAR_INPUT" || peek() == "VAR_OUTPUT" || peek() == "VAR") {
            S section = take();
            while (peek() != "END_VAR") {
                S n = take();
                need(":");
                S type = take();
                need(";");
                require(unique.insert(lower(n)).second, "duplicate ST declaration");
                if (type == "BOOL" || type == "UDINT") {
                    declared[n] = type;
                    if (section == "VAR_INPUT")
                        inputs.push_back(n);
                    if (section == "VAR_OUTPUT")
                        outputs.push_back(n);
                } else {
                    require(section == "VAR" && documents.count(type),
                            "missing ST dependency or unsupported variable type");
                    instances[n] = type;
                }
            }
            need("END_VAR");
        }
        require(J(inputs) == document.at("InputNames") && J(outputs) == document.at("OutputNames"),
                "ST interface source/metadata mismatch");
        std::map<S, S> stored;
        std::map<S, unsigned> indexes;
        std::set<unsigned> occupied;
        auto &vn = document.at("VariableNames");
        for (auto key : {"VariableDataTypes", "VariableIndexes", "VariableStorageTypes",
                         "DefaultValues", "ArrayRanges"})
            require(document.at(key).size() == vn.size(), "ST variable array length mismatch");
        for (size_t i = 0; i < vn.size(); ++i) {
            S n = vn[i];
            int t = document.at("VariableDataTypes")[i];
            require(t == 6 || t == 3, "unsupported native ST type");
            require(stored.emplace(n, t == 6 ? "BOOL" : "UDINT").second,
                    "duplicate native ST variable");
            unsigned index = document.at("VariableIndexes")[i];
            require(occupied.insert(index).second, "duplicate native ST index");
            indexes[n] = index;
            require(document.at("VariableStorageTypes")[i] == 2 &&
                        document.at("DefaultValues")[i].is_null(),
                    "stateful ST storage/default forbidden");
            require(document.at("ArrayRanges")[i].at("Start").empty() &&
                        document.at("ArrayRanges")[i].at("End").empty(),
                    "ST arrays unsupported");
        }
        require(stored == declared, "ST variable source/metadata mismatch");
        std::map<S, S> fi;
        auto &fb = document.at("FbInstances");
        require(fb.size() == document.at("FbInstanceIndexes").size(), "ST instance indexes");
        for (size_t i = 0; i < fb.size(); ++i) {
            require(fi.emplace(fb[i].at("Name"), fb[i].at("TemplateName")).second,
                    "duplicate ST instance");
            require(fb[i].at("VariableDataType") == 3 &&
                        occupied.insert(document.at("FbInstanceIndexes")[i].get<unsigned>()).second,
                    "ST instance index/type");
        }
        require(fi == instances, "ST instance source/metadata mismatch");
        for (unsigned i = 0; i < occupied.size(); ++i)
            require(occupied.count(i), "ST index gap");
        auto checkPorts = [&](const std::vector<S> &ports, S prefix) {
            require(document.at(prefix + "DataTypes").size() == ports.size() &&
                        document.at(prefix + "VariableIndexes").size() == ports.size(),
                    "ST port metadata length");
            for (size_t i = 0; i < ports.size(); ++i)
                require(document.at(prefix + "VariableIndexes")[i] == indexes.at(ports[i]) &&
                            document.at(prefix + "DataTypes")[i] ==
                                (declared.at(ports[i]) == "BOOL" ? 6 : 3),
                        "ST port index/type mismatch");
        };
        checkPorts(inputs, "Input");
        checkPorts(outputs, "Output");
        for (size_t i = 0; i < inputs.size(); ++i)
            values[inputs[i]] = add("input", declared.at(inputs[i]), i);
        while (peek() != "END_FUNCTION_BLOCK") {
            S n = take();
            if (n == "IF") {
                auto condition = expression();
                require(condition.type == "BOOL", "ST IF condition type");
                need("THEN");
                S target = take();
                need(":=");
                auto yes = expression();
                need(";");
                need("ELSE");
                require(take() == target, "ST branches must assign same variable");
                need(":=");
                auto no = expression();
                need(";");
                need("END_IF");
                need(";");
                require(yes.type == "UDINT" && no.type == "UDINT", "ST supported conditional type");
                assign(target, add("sel", "UDINT", 0, {condition, no, yes}));
            } else if (instances.count(n)) {
                require(called.insert(n).second, "duplicate ST instance execution");
                S child = instances.at(n);
                auto &d = documents.at(child);
                need("(");
                std::map<S, Value> bindings;
                std::map<S, S> results;
                if (peek() != ")")
                    for (;;) {
                        S formal = take(), op = take();
                        if (op == ":=")
                            require(bindings.emplace(formal, expression()).second,
                                    "duplicate ST input binding");
                        else {
                            require(op == "=>", "ST call binding operator");
                            require(results.emplace(formal, take()).second,
                                    "duplicate ST output binding");
                        }
                        if (peek() != ",")
                            break;
                        need(",");
                    }
                need(")");
                need(";");
                require(bindings.size() == d.at("InputNames").size() &&
                            results.size() == d.at("OutputNames").size(),
                        "incomplete ST bindings");
                J args = J::array();
                for (auto &formal : d.at("InputNames"))
                    args.push_back(bindings.at(formal.get<S>()).ref);
                size_t index = nodes.size();
                nodes.push_back(
                    {{"kind", "call"}, {"id", n}, {"definition", child}, {"args", args}});
                for (size_t i = 0; i < d.at("OutputNames").size(); ++i)
                    assign(
                        results.at(d.at("OutputNames")[i].get<S>()),
                        {endpoint(index, i), d.at("OutputDataTypes")[i] == 6 ? "BOOL" : "UDINT"});
            } else if (peek() == ".") {
                require(values.count(n) && values.at(n).type == "UDINT",
                        "ST bit write needs an initialized UDINT");
                auto index = bitNumber();
                need(":=");
                auto bit = expression();
                need(";");
                require(bit.type == "BOOL", "ST bit write requires BOOL");
                assign(n, add("putbit", "UDINT", 0, {values.at(n), index, bit}));
            } else {
                need(":=");
                auto value = expression();
                need(";");
                assign(n, value);
            }
        }
        need("END_FUNCTION_BLOCK");
        require(pos == tokens.size(), "trailing ST tokens");
        require(called.size() == instances.size(), "uncalled ST instance");
        J roots = J::array(), types = J::array();
        for (auto &n : outputs) {
            require(values.count(n), "unassigned ST output");
            roots.push_back(values.at(n).ref);
        }
        for (auto &n : inputs)
            types.push_back(declared.at(n));
        return {{"id", document.at("Name")}, {"name", document.at("Name")},
                {"language", "ST"},          {"pure", true},
                {"input_types", types},      {"nodes", nodes},
                {"outputs", roots}};
    }
};
} // namespace
J read_st_artifact(const J &docs, const J &metadata, const S &source) {
    require(docs.is_array() && !docs.empty() && docs.size() <= 256,
            "native ST definitions required");
    std::map<S, J> byName;
    std::set<S> ids, folded;
    S full, root;
    for (auto &d : docs) {
        require(d.at("Discriminator") == 2 && d.at("Password").is_null(), "unsupported ST storage");
        S name = d.at("Name"), id = d.at("Id");
        require(byName.emplace(name, d).second && ids.insert(id).second &&
                    folded.insert(lower(name)).second,
                "duplicate ST definition");
        full += d.at("SourceCode").get<S>();
        if (d.at("Id") == metadata.at("MetaData").at("FunctionBlockUniqueId"))
            root = name;
    }
    require(full == source, "standalone ST text incomplete or differs from archive");
    require(!root.empty() && metadata.at("Name") == root, "ST root metadata");
    for (auto key : {"Input", "Output"}) {
        S k = key;
        require(metadata.at(k + "VariableNames") == byName.at(root).at(k + "Names") &&
                    metadata.at(k + "VariableTypes") == byName.at(root).at(k + "DataTypes"),
                "ST public metadata mismatch");
    }
    J definitions = J::array();
    std::set<S> emitted;
    for (auto &d : docs) {
        for (auto &instance : d.at("FbInstances"))
            require(emitted.count(instance.at("TemplateName").get<S>()),
                    "ST dependencies must precede users");
        definitions.push_back(Parser(d, byName).parse());
        emitted.insert(d.at("Name"));
    }
    return {{"schema_version", 1}, {"kind", "macro_package"},    {"language", "ST"},
            {"root", root},        {"definitions", definitions}, {"external_levels", 0},
            {"table_bytes", 0}};
}
} // namespace b2f
