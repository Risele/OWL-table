#include <vcl.h>
#pragma hdrstop
#include "selector.hpp"
#include <sstream>
#include <algorithm>
namespace t2f {
namespace {
// The numeric table is independently checked by Table2FBD. Prove the remaining
// link for every input code, including codes that select no record.
template <class Select> int check_keys(const Table &t, Select select) {
    std::map<unsigned, int> byId;
    for (size_t i = 0; i < t.rows.size(); ++i)
        byId[t.rows[i].at("key")] = int(i);
    for (unsigned i = 0; i < t.matching_rows.size(); ++i) {
        auto it = byId.find(select(i));
        int ref = t.matching_rows[i];
        bool ok = ref < 0 ? it == byId.end()
                          : it != byId.end() &&
                                (ref == it->second ||
                                 t.rows[ref].at("values") == t.rows[it->second].at("values"));
        require(ok, u8"Селектор выбрал неверные параметры для кода " + std::to_string(i));
    }
    return t.matching_rows.size();
}
const J &model(const J &e) {
    return e.contains("ElementModel") ? e.at("ElementModel")
                                      : e.at("ElementBlockModel").at("ElementModel");
}
} // namespace
int verify_selector_fbd(const J &docs, const Table &t, const S &rootId) {
    const auto &sheet = docs.at(rootId).at("DocumentModel");
    std::map<S, S> wires;
    for (const auto &link : sheet.at("ConnectorModels")) {
        const auto &c = link.at("ConnectorModel");
        require(wires.emplace(c.at("ToPortUId"), c.at("FromPortUId")).second,
                "Duplicate FBD connection");
    }
    struct Op {
        int fb;
        unsigned value;
        std::vector<size_t> args;
    };
    std::vector<Op> ops;
    std::map<S, size_t> signal;
    std::map<S, S> publicOutputs;
    size_t key = 0;
    bool called = false;
    for (const auto &e : sheet.at("Elements")) {
        auto &m = model(e);
        int kind = e.at("Discriminator"), fb = m.at("FbType");
        unsigned value = 0;
        std::vector<size_t> args;
        std::vector<S> outputs;
        if (kind == 39) {
            S id = m.at("Ports")[0].at("UniqueId");
            require(publicOutputs.at(e.at("OutputName")) == wires.at(id),
                    "Incorrect table output binding");
            continue;
        }
        for (const auto &p : m.at("Ports")) {
            S id = p.at("UniqueId").get<S>();
            if (p.at("PortType") == 0)
                args.push_back(signal.at(wires.at(id)));
            else
                outputs.push_back(id);
        }
        if (kind == 37) {
            require(!called && args.size() == 1, "Expected one parameter table call");
            called = true;
            key = args[0];
            auto &child = docs.at(e.at("MacroUniqueId").get<S>());
            std::map<S, S> owners;
            for (auto &port : child.at("DocumentModel").at("Elements")) {
                if (port.at("Discriminator") == 38)
                    require(model(port).at("UniqueId") == m.at("Ports")[0].at("ElementOwnerUId"),
                            "Wrong table input");
                if (port.at("Discriminator") == 39)
                    owners[model(port).at("UniqueId")] = port.at("OutputName").get<S>();
            }
            for (auto &p : m.at("Ports"))
                if (p.at("PortType") == 1)
                    publicOutputs[owners.at(p.at("ElementOwnerUId"))] = p.at("UniqueId").get<S>();
            continue;
        }
        if (kind == 38) {
            auto it = std::find(t.contacts.begin(), t.contacts.end(), e.at("InputName").get<S>());
            require(it != t.contacts.end(), "Unknown discrete input");
            value = it - t.contacts.begin();
            require(e.at("Index") == value, "Wrong input index");
        } else if (kind == 29)
            value = e.at("Value");
        else
            require((fb == 22 && args.size() == 3) || (fb == 20 && args.size() == 3) ||
                        (fb == 25 && args.size() == 2) || (fb == 5 && args.size() == 2),
                    "Invalid selector operation");
        signal[outputs.at(0)] = ops.size();
        ops.push_back({fb, value, args});
    }
    require(called && publicOutputs.size() == t.outputs.size(), "Incomplete table call");
    std::vector<unsigned> values(ops.size());
    return check_keys(t, [&](unsigned code) {
        for (size_t i = 0; i < ops.size(); ++i) {
            auto &op = ops[i];
            auto a = [&](size_t k) { return values[op.args[k]]; };
            switch (op.fb) {
            case 12:
                values[i] = (code >> op.value) & 1;
                break;
            case 30:
                values[i] = op.value;
                break;
            case 22: {
                unsigned b = a(1);
                require(b < 32, "Invalid PUTBIT offset");
                values[i] = (a(0) & ~(1U << b)) | (a(2) << b);
                break;
            }
            case 5:
                values[i] = a(0) & a(1);
                break;
            case 25:
                values[i] = a(0) == a(1);
                break;
            case 20:
                values[i] = a(0) ? a(2) : a(1);
                break;
            default:
                throw std::runtime_error("Unknown selector operation");
            }
        }
        return values[key];
    });
}
int verify_selector_st(const J &docs, const Table &t, const S &name) {
    const auto &root = docs.back();
    require(root.at("Name") == name && root.at("InputNames") == J(t.contacts),
            "Wrong ST selector interface");
    S src = root.at("SourceCode");
    std::istringstream lines(src.substr(src.rfind("END_VAR") + 7));
    std::vector<S> body;
    S line;
    while (std::getline(lines, line))
        if (!trim(line).empty())
            body.push_back(trim(line));
    size_t at = 0;
    auto take = [&]() { return body.at(at++); };
    auto code = selector_local(t, name, "InputCode");
    require(take() == code + " := 0;", "Uninitialized selector word");
    std::vector<unsigned> positions;
    for (unsigned bit = 0; bit < t.contacts.size(); ++bit) {
        S prefix = code + "." + std::to_string(bit) + " := ", s = take();
        require(s.find(prefix) == 0 && s.back() == ';', "Invalid bit assignment");
        S input = s.substr(prefix.size(), s.size() - prefix.size() - 1);
        auto it = std::find(t.contacts.begin(), t.contacts.end(), input);
        require(it != t.contacts.end(), "Unknown input");
        positions.push_back(it - t.contacts.begin());
    }
    require(take() == t.input + " := " + code + ";", "Missing selector assignment");
    struct Rule {
        unsigned mask, bits, key;
    };
    std::vector<Rule> rules;
    while (body.at(at).find("IF ") == 0) {
        auto cond = take();
        S prefix = "IF (" + code + " AND ";
        auto sep = cond.find(") = ");
        require(cond.find(prefix) == 0 && sep != S::npos, "Invalid selector condition");
        unsigned mask = std::stoul(cond.substr(prefix.size(), sep - prefix.size())),
                 bits = std::stoul(cond.substr(sep + 4));
        auto assign = take();
        S dest = t.input + " := ";
        require(assign.find(dest) == 0 && take() == "END_IF;", "Invalid selector branch");
        rules.push_back({mask, bits, unsigned(std::stoul(assign.substr(dest.size())))});
    }
    const auto &inst = root.at("FbInstances").at(0);
    require(inst.at("TemplateName") == selector_child_name(t, name), "Incorrect table call");
    S call = inst.at("Name").get<S>() + "(" + t.input + " := " + t.input;
    for (auto &o : t.outputs)
        call += ", " + o.first + " => " + o.first;
    require(take() == call + ");" && take() == "END_FUNCTION_BLOCK" && at == body.size(),
            "Invalid ST table binding");
    return check_keys(t, [&](unsigned input) {
        unsigned packed = 0;
        for (unsigned i = 0; i < positions.size(); ++i)
            packed |= ((input >> positions[i]) & 1) << i;
        unsigned key = packed;
        for (auto &r : rules)
            if ((packed & r.mask) == r.bits)
                key = r.key;
        return key;
    });
}
} // namespace t2f
