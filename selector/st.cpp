#include <vcl.h>
#pragma hdrstop
#include "selector.hpp"
#include <algorithm>
namespace t2f {
J build_selector_st(const Table &t, const S &name, uint64_t step, const S &sourceName) {
    S child = selector_child_name(t, name), code = selector_local(t, name, "InputCode"),
      instance = selector_local(t, name, "Lookup"), key = t.input;
    auto package = build_st(t, child, step, sourceName, internal_macro_group);
    auto &docs = package["documents"];
    verify_st(docs, t, child);
    J root = docs[0]; // Reuse native metadata for all typed table outputs.
    const S description = u8"Выбор параметров по дискретным входам. Без совпадения Valid=FALSE.";
    S src = st_annotation("Description", description) +
            st_annotation("GroupName", table_macro_group) + "FUNCTION_BLOCK " + name +
            "\r\nVAR_INPUT\r\n";
    J names = J::array(), types = J::array(), indexes = J::array(), inIndexes = J::array(),
      outIndexes = J::array();
    J descriptions = J::array();
    int next = 0;
    auto variable = [&](const S &n, int type) {
        names.push_back(n);
        types.push_back(type);
        indexes.push_back(next);
        return next++;
    };
    for (auto &n : t.contacts) {
        src += "    " + st_annotation("Description", t.titles.at(n)) + "    " + n + " : BOOL;\r\n";
        inIndexes.push_back(variable(n, 6));
        descriptions.push_back(t.titles.at(n));
    }
    src += "END_VAR\r\nVAR_OUTPUT\r\n";
    for (auto &o : t.outputs) {
        int type = o.second == 0 ? 6 : o.second;
        S title = o.first == "Valid" ? u8"Строка существует" : t.titles.at(o.first);
        src += "    " + st_annotation("Description", title) + "    " + o.first + " : " +
               (type == 6   ? "BOOL"
                : type == 4 ? "REAL"
                            : "UDINT") +
               ";\r\n";
        outIndexes.push_back(variable(o.first, type));
        descriptions.push_back(title);
    }
    auto publicNames = names;
    src += "END_VAR\r\nVAR\r\n    " + code + " : UDINT;\r\n    " + key + " : UDINT;\r\n    " +
           instance + " : " + child + ";\r\nEND_VAR\r\n" + code + " := 0;\r\n";
    variable(code, 3);
    variable(key, 3);
    for (size_t i = 0; i < t.contacts.size(); ++i)
        src += code + "." + std::to_string(i) + " := " + t.contacts[i] + ";\r\n";
    src += key + " := " + code + ";\r\n";
    for (auto &r : t.rows) {
        unsigned id = r.at("key");
        if (id < t.matching_rows.size())
            continue;
        src += "IF (" + code + " AND " + r.at("mask").dump() + ") = " + r.at("bits").dump() +
               " THEN\r\n    " + key + " := " + std::to_string(id) + ";\r\nEND_IF;\r\n";
    }
    src += instance + "(" + t.input + " := " + key;
    for (auto &o : t.outputs)
        src += ", " + o.first + " => " + o.first;
    src += ");\r\nEND_FUNCTION_BLOCK\r\n";
    S uid = uuid5(ns, "DiscreteTable:ST:" + name), revision = uuid5(uid, src);
    root.update(
        {{"Id", uid},
         {"Name", name},
         {"SourceCode", src},
         {"LastModificationId", revision},
         {"ModificationLabel", revision},
         {"InputNames", t.contacts},
         {"InputDataTypes", std::vector<int>(t.contacts.size(), 6)},
         {"InputVariableIndexes", inIndexes},
         {"OutputVariableIndexes", outIndexes},
         {"VariableNames", names},
         {"VariableDataTypes", types},
         {"VariableIndexes", indexes},
         {"VariableStorageTypes", std::vector<int>(next, 2)},
         {"DefaultValues", J(J::array_t(next, nullptr))},
         {"ArrayRanges", J(J::array_t(next, {{"Start", J::array()}, {"End", J::array()}}))},
         {"FbInstanceIndexes", J::array({next})},
         {"FbInstances",
          J::array({{{"Name", instance}, {"TemplateName", child}, {"VariableDataType", 3}}})}});
    auto &meta = package["metadata"];
    meta.update({{"Name", name},
                 {"ModificationLabel", revision},
                 {"InputVariableNames", t.contacts},
                 {"InputVariableDisplayNames", t.contacts},
                 {"InputVariableTypes", root["InputDataTypes"]}});
    meta["MetaData"].update({{"FunctionBlockUniqueId", uid},
                             {"GroupName", table_macro_group},
                             {"VariableNames", publicNames},
                             {"VariableDescriptions", descriptions},
                             {"Description", description}});
    // Put the table groups before their caller in the standalone ST listing.
    if (docs.size() > 1)
        std::rotate(docs.begin(), docs.begin() + 1, docs.end());
    docs.push_back(std::move(root));
    return package;
}
} // namespace t2f
