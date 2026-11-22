#include <vcl.h>
#pragma hdrstop
#include "../cpp/macro_document.hpp"
#include "logic.hpp"
#include <algorithm>
namespace b2f {
static J st_data(const Spec &s, const Plan &plan, const S &name, bool lookup) {
    S prefix = "Logic";
    std::vector<S> publicNames = s.inputs;
    publicNames.push_back(s.output);
    publicNames.push_back(s.inverted);
    publicNames.push_back(name);
    for (;;) {
        bool collision = false;
        for (auto &n : publicNames)
            if (lower(n).find(lower(prefix)) == 0)
                collision = true;
        if (!collision)
            break;
        prefix = "G" + prefix;
    }
    std::set<int> used;
    std::function<void(int)> visit = [&](int i) {
        if (!used.insert(i).second)
            return;
        S op = plan.nodes[i]["op"];
        if (op == "not" || op == "and" || op == "or")
            visit(plan.nodes[i]["a"]);
        if (op == "and" || op == "or")
            visit(plan.nodes[i]["b"]);
    };
    visit(plan.root);
    std::vector<std::pair<S, int>> locals;
    if (lookup)
        locals.push_back({prefix + "Index", 3});
    else
        for (int i : used)
            if (i != plan.root && plan.nodes[i]["op"] != "input" && plan.nodes[i]["op"] != "const")
                locals.push_back({prefix + std::to_string(i), 6});
    S created = now_iso(),
      description = u8"Булева функция; входов: " + std::to_string(s.inputs.size()) +
                    u8". X — вход не влияет. Без совпадения Q=FALSE. Инверсный выход всегда NOT Q.";
    S source = "///<Description>" + xml_escape(description) +
               u8"</Description>\r\n///<GroupName>Булевы функции</GroupName>\r\nFUNCTION_BLOCK " +
               name + "\r\nVAR_INPUT\r\n";
    for (auto &n : s.inputs)
        source += "    " + n + " : BOOL;\r\n";
    source += "END_VAR\r\nVAR_OUTPUT\r\n    " + s.output + " : BOOL;\r\n    " + s.inverted +
              " : BOOL;\r\nEND_VAR\r\n";
    if (!locals.empty()) {
        source += "VAR\r\n";
        for (auto &p : locals)
            source += "    " + p.first + (p.second == 3 ? " : UDINT;\r\n" : " : BOOL;\r\n");
        source += "END_VAR\r\n";
    }
    source += "\r\n";
    if (lookup) {
        S index = locals[0].first;
        source +=
            "// I1 is bit 0. The index is recomputed on every call.\r\n" + index + " := 0;\r\n";
        for (size_t i = 0; i < s.inputs.size(); ++i)
            source += "IF " + s.inputs[i] + " THEN " + index + " := " + index + " + " +
                      std::to_string(uint32_t(1) << i) + "; END_IF;\r\n";
        uint32_t mask = 0;
        for (uint32_t key = 0; key < (uint32_t(1) << s.inputs.size()); ++key)
            if (evaluate(s, key))
                mask |= uint32_t(1) << key;
        source += s.output + " := (SHR(" + std::to_string(mask) + ", " + index +
                  ") AND 1) <> 0;\r\n";
    } else {
        auto ref = [&](int id) -> S {
            const auto &node = plan.nodes[id];
            if (node["op"] == "const")
                return node["value"].get<bool>() ? "TRUE" : "FALSE";
            if (node["op"] == "input")
                return s.inputs.at(node["index"]);
            return prefix + std::to_string(id);
        };
        for (int i : used) {
            const auto &n = plan.nodes[i];
            S op = n["op"];
            if (op == "input" || op == "const")
                continue;
            source += (i == plan.root ? s.output : ref(i)) + " := ";
            if (op == "not")
                source += "NOT " + ref(n["a"]);
            else
                source += ref(n["a"]) + (op == "and" ? " AND " : " OR ") + ref(n["b"]);
            source += ";\r\n";
        }
        if (plan.nodes[plan.root]["op"] == "input" || plan.nodes[plan.root]["op"] == "const")
            source += s.output + " := " + ref(plan.root) + ";\r\n";
    }
    source += s.inverted + " := NOT " + s.output + ";\r\nEND_FUNCTION_BLOCK\r\n";
    S id = uuid5(uuid5(ns, "BOOLEAN:ST:" + name), name), revision = uuid5(id, source);
    J names = J::array(), types = J::array(), storage = J::array(), indexes = J::array(),
      defaults = J::array(), ranges = J::array();
    J inNames = J::array(), inTypes = J::array(), inIndexes = J::array();
    auto variable = [&](const S &n, int type) {
        int index = names.size();
        names.push_back(n);
        types.push_back(type);
        indexes.push_back(index);
        storage.push_back(2);
        defaults.push_back(nullptr);
        ranges.push_back({{"Start", J::array()}, {"End", J::array()}});
    };
    for (auto &n : s.inputs) {
        inIndexes.push_back(names.size());
        inNames.push_back(n);
        inTypes.push_back(6);
        variable(n, 6);
    }
    int outIndex = names.size();
    variable(s.output, 6);
    variable(s.inverted, 6);
    for (auto &p : locals)
        variable(p.first, p.second);
    J outputs = J::array({s.output, s.inverted}), outputTypes = J::array({6, 6});
    J block = {{"Id", id},
               {"Name", name},
               {"Password", nullptr},
               {"InputNames", inNames},
               {"InputDataTypes", inTypes},
               {"InputVariableIndexes", inIndexes},
               {"OutputNames", outputs},
               {"OutputDataTypes", outputTypes},
               {"OutputVariableIndexes", J::array({outIndex, outIndex + 1})},
               {"SourceCode", source},
               {"LastModificationId", revision},
               {"ModificationLabel", revision},
               {"VariableNames", names},
               {"VariableDataTypes", types},
               {"VariableStorageTypes", storage},
               {"VariableIndexes", indexes},
               {"DefaultValues", defaults},
               {"ArrayRanges", ranges},
               {"FbInstances", J::array()},
               {"FbInstanceIndexes", J::array()},
               {"CreationDateTime", created},
               {"Discriminator", 2}};
    J publicVars = inNames;
    publicVars.push_back(s.output);
    publicVars.push_back(s.inverted);
    J meta = {{"Screenshot", icon()},
              {"MetaData",
               {{"FunctionBlockUniqueId", id},
                {"Description", description},
                {"Author", ""},
                {"GroupName", u8"Булевы функции"},
                {"VariableNames", publicVars},
                {"VariableDescriptions", publicVars},
                {"Discriminator", 2}}},
              {"ModificationLabel", revision},
              {"Name", name},
              {"Tags", J::array()},
              {"CreationDateTime", created},
              {"ModificationDateTime", created},
              {"HasPassword", false},
              {"InputVariableNames", inNames},
              {"InputVariableDisplayNames", inNames},
              {"InputVariableTypes", inTypes},
              {"OutputVariableNames", outputs},
              {"OutputVariableDisplayNames", outputs},
              {"OutputVariableTypes", outputTypes}};
    if(!s.input_comments.empty())for(size_t i=0;i<s.input_comments.size();++i)meta["MetaData"]["VariableDescriptions"][i]=s.input_comments[i];
    return {{"documents", J::array({block})}, {"metadata", meta}};
}
J generate(const J &state, const U &target, const S &name, bool fbd, bool st) {
    Spec s = parse(state, name);
    require(!target.Trim().IsEmpty(), u8"Укажите путь результата.");
    require(fbd || st, u8"Выберите FBD и/или ST.");
    S ext = lower(utf8(ExtractFileExt(target)));
    require(ext.empty() || ext == ".tple" || ext == ".fbst",
            u8"Результат: .tple, .fbst или имя без расширения.");
    Plan plan = compile(s);
    std::set<uint32_t> cases = {0, 0xffffffffU};
    if (s.inputs.size() <= 12)
        for (uint32_t k = 0; k < (uint32_t(1) << s.inputs.size()); ++k)
            cases.insert(k);
    else {
        uint32_t random = 0x12345678U;
        for (int i = 0; i < 1024; ++i) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            cases.insert(random);
        }
        for (auto &r : s.rules) {
            cases.insert(r.bits);
            cases.insert(r.bits | ~r.care);
        }
        for (int i = 0; i < 32; ++i) {
            cases.insert(uint32_t(1) << i);
            cases.insert(~(uint32_t(1) << i));
        }
    }
    for (auto key : cases)
        require(evaluate(s, key) == evaluate(plan, key),
                u8"Внутренняя проверка функции не пройдена: " + std::to_string(key));
    J report = {{"name", name},
                {"inputs", s.inputs.size()},
                {"rules", s.rules.size()},
                {"logic_method", plan.method},
                {"logic_nodes", plan.cost},
                {"checked", cases.size()},
                {"st_method", "not-requested"},
                {"files", J::array()}};
    if (s.inputs.size() <= 5) {
        uint32_t word = 0;
        for (uint32_t k = 0; k < (uint32_t(1) << s.inputs.size()); ++k)
            if (evaluate(s, k))
                word |= uint32_t(1) << k;
        report["truth_word"] = word;
    }
    struct Pending {
        U path, temporary;
        std::map<S, S> files;
        S text;
    };
    std::vector<Pending> pending;
    U base = ChangeFileExt(ExpandFileName(target), "");
    MacroDocument document{"Bool",name,state,{{"fbd",fbd},{"st",st}}};
    if (fbd) {
        bool mask = s.inputs.size() <= 5 && plan.cost > 4;
        J nodes = mask ? J::array({{{"op", "mask"}, {"value", report["truth_word"]}}}) : plan.nodes;
        auto data = build_boolean_fbd(s.inputs, nodes, mask ? 0 : plan.root, s.output, s.inverted, name, s.input_comments);
        if (mask) {
            report["logic_method"] = "truth-mask";
            const auto &body = data.at("documents").at(data.at("root").get<S>()).at("DocumentModel");
            report["logic_nodes"] = body.at("Elements").size() - s.inputs.size() - 2;
        }
        J meta = data["documents"][data["root"].get<S>()]["MetaData"];
        meta["ComponentMetaData"]["Image"] = icon();
        embedDocument(data["documents"],meta,document);
        pending.push_back({base + ".tple",
                           "",
                           {{"Template", data["documents"].dump(2)},
                            {"MetaData", meta.dump(2)},
                            {"MinRequiredVersion", "2.0"}}});
    }
    {
        auto data = st_data(s, plan, name, false);
        report["st_method"] = plan.method;
        if (s.inputs.size() <= 5) {
            auto lookup = st_data(s, plan, name, true);
            if (lookup["documents"][0]["SourceCode"].get<S>().size() <
                data["documents"][0]["SourceCode"].get<S>().size()) {
                data = std::move(lookup);
                report["st_method"] = "truth-mask";
            }
        }
        embedDocument(data["documents"],data["metadata"],document);
        if (st)
            pending.push_back({base + ".fbst",
                           "",
                           {{"Template", data["documents"].dump(2)},
                            {"MetaData", data["metadata"].dump(2)},
                            {"MinRequiredVersion", "2.0"}}});
        pending.push_back({base + ".st", "", {}, data["documents"][0]["SourceCode"].get<S>()});
    }
    ForceDirectories(ExtractFileDir(base));
    std::map<U, S> previous;
    std::vector<U> replaced;
    try {
        for (auto &p : pending) {
            p.temporary = p.path + "." + IntToStr((int)GetCurrentProcessId()) + ".tmp";
            if (p.files.empty())
                write_file(p.temporary, p.text);
            else
                zip_write(p.temporary, p.files);
            if (FileExists(p.path))
                previous[p.path] = read_file(p.path);
        }
        for (auto &p : pending) {
            require(MoveFileExW(p.temporary.c_str(), p.path.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                    u8"Не удалось заменить файл результата.");
            replaced.push_back(p.path);
            report["files"].push_back(utf8(p.path));
        }
    } catch (...) {
        for (auto &p : replaced) {
            if (previous.count(p))
                write_file(p, previous[p]);
            else
                DeleteFileW(p.c_str());
        }
        for (auto &p : pending)
            if (!p.temporary.IsEmpty())
                DeleteFileW(p.temporary.c_str());
        throw;
    }
    return report;
}
} // namespace b2f
