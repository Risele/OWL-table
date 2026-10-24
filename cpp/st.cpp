#include <vcl.h>
#pragma hdrstop
#include "core.hpp"
#include <System.Character.hpp>
#include <algorithm>
#include <sstream>
#include <memory>
namespace t2f {
static S lit(const J &v, int type) {
    if (type == 0)
        return v.is_boolean() ? (v.get<bool>() ? "TRUE" : "FALSE")
                              : (v.get<int>() ? "TRUE" : "FALSE");
    if (type == 3)
        return std::to_string(v.get<uint32_t>());
    S s = number(v.get<double>(), 9);
    if (s.find_first_of(".eE") == S::npos)
        s += ".0";
    return s;
}
static S pad(uint64_t v) {
    S s = std::to_string(v);
    return s.size() < 2 ? "0" + s : s;
}
static S one_line(S s) {
    std::replace(s.begin(), s.end(), '\r', ' ');
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}
static J make_st(const Table &t, const S &name, const S &space, const J &rows, const J &children,
                 const std::vector<uint64_t> &groups, uint64_t step, const S &created,
                 const S &description) {
    J locals = J::array();
    std::set<S> used = {lower(name), lower(t.input)};
    for (auto &o : t.outputs)
        used.insert(lower(o.first));
    for (size_t i = 0; i < children.size(); ++i) {
        S n = "Group" + std::to_string(i);
        while (used.count(lower(n)))
            n = "G" + n;
        used.insert(lower(n));
        locals.push_back(
            {{"Name", n}, {"TemplateName", children[i]["Name"]}, {"VariableDataType", 3}});
    }
    auto doc = [](const S &tag, const S &value) {
        return "///<" + tag + ">" + xml_escape(one_line(value)) + "</" + tag + ">\r\n";
    };
    S source = doc("Description", description) + doc("GroupName", u8"Таблицы параметров") +
               "FUNCTION_BLOCK " + name + "\r\nVAR_INPUT\r\n    " +
               doc("Description", t.titles.at(t.input)) + "    " + t.input +
               " : UDINT;\r\nEND_VAR\r\nVAR_OUTPUT\r\n";
    for (auto &o : t.outputs)
        source +=
            "    " +
            doc("Description", o.first == "Valid" ? u8"Строка существует" : t.titles.at(o.first)) +
            "    " + o.first + " : " +
            (o.second == 0   ? "BOOL"
             : o.second == 3 ? "UDINT"
                             : "REAL") +
            ";\r\n";
    source += "END_VAR\r\n";
    if (!locals.empty()) {
        source += "VAR\r\n";
        for (auto &l : locals)
            source += "    " + l["Name"].get<S>() + " : " + l["TemplateName"].get<S>() + ";\r\n";
        source += "END_VAR\r\n";
    }
    source += "\r\n";
    source += "CASE " + t.input + " OF\r\n";
    if (!children.empty()) {
        for (size_t i = 0; i < children.size(); ++i) {
            uint64_t lo = groups[i] * step, hi = std::min(lo + step - 1, 4294967295ULL);
            source += "    " + std::to_string(lo) + ".." + std::to_string(hi) + ":\r\n        " +
                      locals[i]["Name"].get<S>() + "(" + t.input + " := " + t.input;
            for (auto &o : t.outputs)
                source += ", " + o.first + " => " + o.first;
            source += ");\r\n";
        }
    } else
        for (auto &r : rows) {
            S comment;
            for (auto &v : r["labels"]) {
                if (!comment.empty())
                    comment += "; ";
                comment += v.get<S>();
            }
            if (!comment.empty())
                source += "    // " + one_line(comment) + "\r\n";
            source += "    " + std::to_string(r["key"].get<uint32_t>()) + ":\r\n";
            for (auto &o : t.outputs)
                source += "        " + o.first + " := " +
                          lit(o.first == "Valid" ? J(true) : r["values"][o.first], o.second) +
                          ";\r\n";
        }
    source += "    ELSE\r\n";
    for (auto &o : t.outputs)
        source += "        " + o.first + " := " + lit(0, o.second) + ";\r\n";
    source += "END_CASE;\r\nEND_FUNCTION_BLOCK\r\n";
    S id = uuid5(space, name), revision = uuid5(id, "ST-storage-v2:" + source);
    J variables = J::array({t.input}), types = J::array({3}), outs = J::array(),
      outtypes = J::array(), outindexes = J::array(), fbindexes = J::array(), indexes = J::array(),
      storage = J::array(), defaults = J::array(), ranges = J::array();
    int index = 1;
    indexes.push_back(0);
    for (auto &l : locals) {
        fbindexes.push_back(index++);
    }
    for (auto &o : t.outputs) {
        variables.push_back(o.first);
        types.push_back(o.second == 0 ? 6 : o.second);
        outs.push_back(o.first);
        outtypes.push_back(o.second == 0 ? 6 : o.second);
        indexes.push_back(index);
        outindexes.push_back(index++);
    }
    for (size_t i = 0; i < variables.size(); ++i) {
        storage.push_back(2);
        defaults.push_back(nullptr);
        ranges.push_back({{"Start", J::array()}, {"End", J::array()}});
    }
    return {{"Id", id},
            {"Name", name},
            {"Password", nullptr},
            {"InputNames", J::array({t.input})},
            {"InputDataTypes", J::array({3})},
            {"InputVariableIndexes", J::array({0})},
            {"OutputNames", outs},
            {"OutputDataTypes", outtypes},
            {"OutputVariableIndexes", outindexes},
            {"SourceCode", source},
            {"LastModificationId", revision},
            {"VariableStorageTypes", storage},
            {"VariableDataTypes", types},
            {"VariableNames", variables},
            {"DefaultValues", defaults},
            {"VariableIndexes", indexes},
            {"FbInstances", locals},
            {"FbInstanceIndexes", fbindexes},
            {"ArrayRanges", ranges},
            {"ModificationLabel", revision},
            {"CreationDateTime", created},
            {"Discriminator", 2}};
}
J build_st(const Table &t, const S &name, uint64_t step, const S &source_name) {
    S local_time;
    S created = now_iso(&local_time);
    S description =
        u8"Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. Исходный файл: " +
        (source_name.empty() ? S(u8"таблица в редакторе (не сохранена)") : source_name) +
        u8". Время генерации (местное время ПК): " + local_time + ".";
    require(step <= 4294967296ULL, u8"Шаг ST вне диапазона.");
    S keywords =
        "ABS ACTION AND ARRAY AT BOOL BY CASE CONFIGURATION CONSTANT CONTINUE DATE DINT DO DWORD ELSE ELSIF END END_CASE END_FOR END_FUNCTION END_FUNCTION_BLOCK END_IF END_PROGRAM END_REPEAT END_STRUCT END_TYPE END_VAR END_WHILE EXIT FALSE FOR FUNCTION FUNCTION_BLOCK IF INT LINT LREAL LWORD MOD NOT OF OR PROGRAM REAL REPEAT RETAIN RETURN SINT STRING STRUCT THEN TIME TO TRUE TYPE UDINT UINT ULINT UNTIL USINT VAR VAR_INPUT VAR_OUTPUT VAR_IN_OUT VAR_TEMP VAR_GLOBAL VAR_EXTERNAL WHILE WORD XOR";
    std::set<S> reserved;
    std::istringstream input(keywords);
    S word;
    while (input >> word)
        reserved.insert(lower(word));
    std::vector<S> names = {name, t.input};
    for (auto &o : t.outputs)
        names.push_back(o.first);
    for (auto &n : names) {
        require(!reserved.count(lower(n)), u8"ST: ключевое слово в имени: " + n);
        require(System::Character::TCharacter::IsLetter(wide(n)[1]),
                u8"ST: имя должно начинаться с буквы: " + n);
    }
    for (size_t i = 1; i < names.size(); ++i)
        require(lower(name) != lower(names[i]), u8"ST: имя макроса совпадает с переменной.");
    S space = uuid5(ns, "ST:" + name);
    std::map<uint64_t, J> records;
    if (step)
        for (auto &r : t.rows) {
            uint64_t g = r["key"].get<uint64_t>() / step;
            if (!records.count(g))
                records[g] = J::array();
            records[g].push_back(r);
        }
    J children = J::array();
    std::vector<uint64_t> groups;
    for (auto &g : records) {
        groups.push_back(g.first);
        uint64_t lo = g.first * step, hi = std::min(lo + step - 1, 4294967295ULL);
        children.push_back(make_st(t, name + "_" + pad(lo) + "_" + pad(hi), space, g.second,
                                   J::array(), {}, 0, created, description));
    }
    J root = make_st(t, name, space, t.rows, children, groups, step, created, description),
      docs = J::array({root});
    for (auto &c : children)
        docs.push_back(c);
    J vars = J::array({t.input}), descriptions = J::array({t.titles.at(t.input)});
    for (auto &o : t.outputs) {
        vars.push_back(o.first);
        descriptions.push_back(o.first == "Valid" ? u8"Строка существует" : t.titles.at(o.first));
    }
    J meta = {{"Screenshot", icon()},
              {"MetaData",
               {{"FunctionBlockUniqueId", root["Id"]},
                {"Description", description},
                {"Author", ""},
                {"GroupName", u8"Таблицы параметров"},
                {"VariableNames", vars},
                {"VariableDescriptions", descriptions},
                {"Discriminator", 2}}},
              {"ModificationLabel", root["ModificationLabel"]},
              {"Name", name},
              {"Tags", J::array()},
              {"CreationDateTime", root["CreationDateTime"]},
              {"ModificationDateTime", root["CreationDateTime"]},
              {"HasPassword", false},
              {"InputVariableNames", root["InputNames"]},
              {"InputVariableDisplayNames", root["InputNames"]},
              {"InputVariableTypes", root["InputDataTypes"]},
              {"OutputVariableNames", root["OutputNames"]},
              {"OutputVariableDisplayNames", root["OutputNames"]},
              {"OutputVariableTypes", root["OutputDataTypes"]}};
    return {{"documents", docs}, {"metadata", meta}};
}
J evaluate_st(const J &docs, const S &name, uint32_t key, int depth) {
    require(depth <= (int)docs.size(), u8"Цикл ST.");
    const J *d = nullptr;
    for (auto &v : docs)
        if (v["Name"] == name)
            d = &v;
    require(d != nullptr, u8"Не найден ST-блок.");
    S source = (*d)["SourceCode"];
    size_t end = source.rfind("END_VAR");
    require(end != S::npos, u8"Нет объявления ST.");
    source = source.substr(end + 7);
    J result = J::object();
    std::map<S, int> types;
    for (size_t i = 0; i < (*d)["OutputNames"].size(); ++i)
        types[(*d)["OutputNames"][i].get<S>()] = (*d)["OutputDataTypes"][i];
    std::istringstream lines(source);
    S line;
    bool in_case = false, active = true, matched = false;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line.find("//") == 0)
            continue;
        if (line.find("CASE ") == 0) {
            require(trim(line.substr(5, line.size() - 8)) == (*d)["InputNames"][0],
                    u8"Неверный селектор ST.");
            in_case = true;
            active = false;
            continue;
        }
        if (line.find("END_CASE") == 0)
            break;
        if (in_case && line == "ELSE") {
            active = !matched;
            continue;
        }
        if (in_case && line.back() == ':') {
            S label = line.substr(0, line.size() - 1);
            auto sep = label.find("..");
            uint64_t lo = std::stoull(label),
                     hi = sep == S::npos ? lo : std::stoull(label.substr(sep + 2));
            active = lo <= key && key <= hi;
            matched = matched || active;
            continue;
        }
        if (!active)
            continue;
        auto par = line.find('(');
        if (par != S::npos) {
            S local = trim(line.substr(0, par)), child;
            for (auto &inst : (*d)["FbInstances"])
                if (inst["Name"] == local)
                    child = inst["TemplateName"].get<S>();
            require(!child.empty(), u8"Нет экземпляра ST.");
            J sub = evaluate_st(docs, child, key, depth + 1);
            auto close = line.rfind(')');
            std::istringstream bindings(line.substr(par + 1, close - par - 1));
            S binding;
            bool has_input = false;
            while (std::getline(bindings, binding, ',')) {
                auto arrow = binding.find("=>");
                if (arrow == S::npos) {
                    S id = (*d)["InputNames"][0];
                    require(trim(binding) == id + " := " + id, u8"Неверный вход ST.");
                    has_input = true;
                } else
                    result[trim(binding.substr(arrow + 2))] =
                        sub.at(trim(binding.substr(0, arrow)));
            }
            require(has_input, u8"Не передан ID ST.");
        } else {
            auto eq = line.find(":=");
            require(eq != S::npos && line.back() == ';', u8"Неверное присваивание ST.");
            S var = trim(line.substr(0, eq)),
              value = trim(line.substr(eq + 2, line.size() - eq - 3));
            require(types.count(var), u8"Неизвестный выход ST.");
            result[var] = types[var] == 6   ? J(value == "TRUE")
                          : types[var] == 4 ? J((float)std::stod(value))
                                            : J((uint32_t)std::stoull(value));
        }
    }
    require(result.size() == types.size(), u8"ST задаёт не все выходы.");
    return result;
}
int verify_st(const J &docs, const Table &t, const S &name) {
    std::map<uint32_t, J> rows;
    std::set<uint32_t> keys = {0, 1, 9, 10, 19, 20, 4294967295U};
    for (auto &r : t.rows) {
        uint32_t k = r["key"];
        rows[k] = r["values"];
        keys.insert(k);
        if (k)
            keys.insert(k - 1);
        if (k < 4294967295U)
            keys.insert(k + 1);
    }
    for (auto key : keys) {
        J expected = J::object();
        for (auto &o : t.outputs)
            expected[o.first] = o.second == 0 ? J(false) : J(0);
        if (rows.count(key)) {
            expected.update(rows[key]);
            expected["Valid"] = true;
        }
        require(evaluate_st(docs, name, key) == expected,
                u8"ST: неверное значение ID " + std::to_string(key));
    }
    return keys.size();
}
J generate(const Table &t, const U &target, const Options &o) {
    identifier(o.name);
    require(o.fbd || o.st, u8"Выберите FBD и/или ST.");
    S ext = lower(utf8(ExtractFileExt(target)));
    require(ext.empty() || ext == ".tple" || ext == ".fbst",
            u8"Результат: имя без расширения, .tple или .fbst.");
    U base = ChangeFileExt(ExpandFileName(target), "");
    ForceDirectories(ExtractFileDir(base));
    J results = J::array();
    std::vector<std::pair<U, U>> staged;
    std::map<U, S> previous;
    std::set<U> existed;
    std::vector<U> replaced;
    try {
        for (int format = 0; format < 2; ++format) {
            if ((format == 0 && !o.fbd) || (format == 1 && !o.st))
                continue;
            U path = base + (format == 0 ? ".tple" : ".fbst");
            U tmp = path + "." + IntToStr((int)GetCurrentProcessId()) + ".tmp";
            staged.push_back({tmp, path});
            J docs, meta;
            size_t groups;
            int checked = 0;
            if (format == 0) {
                J data = build_fbd(t, o.name, o.fbd_step);
                docs = data["documents"];
                S root = data["root"];
                checked = verify_fbd(docs, t, root);
                meta = docs[root]["MetaData"];
                meta["ComponentMetaData"]["Image"] = icon();
                groups = docs.size() - 1;
            } else {
                J data = build_st(t, o.name, o.st_step, o.source_name);
                docs = data["documents"];
                meta = data["metadata"];
                checked = verify_st(docs, t, o.name);
                groups = docs.size() - 1;
            }
            zip_write(tmp, {{"Template", docs.dump(2)},
                            {"MetaData", meta.dump(2)},
                            {"MinRequiredVersion", "2.0"}});
            results.push_back({{"path", utf8(path)},
                               {"name", o.name},
                               {"format", format == 0 ? "fbd" : "st"},
                               {"groups", groups},
                               {"records", t.rows.size()},
                               {"checked", checked}});
        }
        for (auto &p : staged)
            if (FileExists(p.second)) {
                existed.insert(p.second);
                previous[p.second] = read_file(p.second);
            }
        for (auto &p : staged) {
            if (!MoveFileExW(p.first.c_str(), p.second.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error(u8"Не удалось заменить файл результата.");
            replaced.push_back(p.second);
        }
    } catch (...) {
        for (auto &p : replaced) {
            if (existed.count(p))
                write_file(p, previous[p]);
            else
                DeleteFileW(p.c_str());
        }
        for (auto &p : staged)
            DeleteFileW(p.first.c_str());
        throw;
    }
    return results;
}
} // namespace t2f
