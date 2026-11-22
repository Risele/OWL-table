#include <vcl.h>
#pragma hdrstop
#include "selector.hpp"
#include "../cpp/input_colors.hpp"
#include "../cpp/discrete_inputs.hpp"
#include <algorithm>
namespace t2f {
S selector_local(const Table &t, const S &name, S base) {
    std::set<S> used = {lower(name), lower(t.input)};
    for (const auto &p : t.contacts)
        used.insert(lower(p));
    for (const auto &p : t.outputs)
        used.insert(lower(p.first));
    while (used.count(lower(base)))
        base = "G" + base;
    return base;
}
S selector_child_name(const Table &t, const S &name) {
    // Include the project name in the private definition, also for a 64-character public name.
    S suffix = uuid5(ns, "parameter table:" + name).substr(0, 8);
    return selector_local(t, name, "Data" + suffix);
}
J selector_example() {
    return {{"fields",
             {{u8"Пуск", "INPUT", "I1"},
              {u8"Ускоренный режим", "INPUT", "I2"},
              {u8"Разрешение", "INPUT", "I3"},
              {u8"Давление", "REAL", "Pressure"},
              {u8"Режим", "UDINT", "Mode"},
              {u8"Насос", "BOOL", "Pump"}}},
            {"records",
             {{"0", "0", "X", "0", "0", "0"},
              {"1", "0", "1", "100", "1", "1"},
              {"1", "1", "1", "100.6", "2", "1"}}}};
}
Table parse_discrete(const J &state) {
    std::vector<size_t> inputs, params, sourceRows;
    std::vector<S> names;
    std::set<S> used = {"valid"};
    J fields = J::array(), records = J::array();
    const auto &source = state.at("fields");
    for (size_t c = 0; c < source.size(); ++c) {
        const auto &f = source[c];
        require(f.size() == 3 ||
                (f.size() == 4 && lower(trim(f[1])) == "input" && f[3].is_number_integer() &&
                 f[3].get<uint64_t>() < inputPalette().size()), u8"Поле должно содержать название, тип и имя.");
        S kind = lower(trim(f[1]));
        require(kind != "id", u8"Вместо ID добавьте дискретные входы типа INPUT.");
        if (kind != "comment" && kind != u8"комментарий")
            check_port_name(trim(f[2]), used);
        if (kind == "input") {
            inputs.push_back(c);
            names.push_back(identifier(f[2]));
        } else {
            params.push_back(c);
            fields.push_back(f);
        }
    }
    require(!inputs.empty() && inputs.size() <= 16,
            u8"Требуется от 1 до 16 дискретных входов INPUT.");
    S key = "RowKey";
    while (used.count(lower(key)))
        key = "G" + key;
    fields.push_back({u8"Внутренний номер строки", "ID", key});
    std::vector<unsigned> masks, bits;
    unsigned all = (1U << inputs.size()) - 1;
    for (size_t i = 0; i < state.at("records").size(); ++i) {
        const auto &r = state.at("records")[i];
        require(r.size() == source.size(), u8"Число ячеек строки не совпадает с числом полей.");
        if (std::all_of(r.begin(), r.end(), [](const J &v) { return trim(v.get<S>()).empty(); }))
            continue;
        unsigned mask = 0, value = 0;
        for (size_t b = 0; b < inputs.size(); ++b) {
            S s;
            try {
                s = discrete_bit(r[inputs[b]], true);
            } catch (const std::exception &err) {
                throw std::runtime_error(u8"Строка " + std::to_string(i + 1) + ", " + names[b] +
                                         ": " + err.what());
            }
            if (s == "X")
                continue;
            mask |= 1U << b;
            if (s == "1")
                value |= 1U << b;
        }
        J row = J::array();
        for (auto c : params)
            row.push_back(r[c]);
        row.push_back(std::to_string(records.size()));
        records.push_back(std::move(row));
        sourceRows.push_back(i + 1);
        masks.push_back(mask);
        bits.push_back(value);
    }
    auto t = parse_parameter_table({{"fields", fields}, {"records", records}}, params, sourceRows);
    t.contacts = names;
    t.matching_rows.assign(all + 1, -1);
    auto parameterFields = t.fields;
    t.fields.clear();
    size_t p = 0;
    for (size_t c = 0; c < source.size(); ++c) {
        if (lower(trim(source[c][1])) == "input") {
            S n = identifier(source[c][2]);
            t.titles[n] = trim(source[c][0]);
            t.fields.push_back({t.titles[n], "input", n, int(c + 1)});
        } else {
            t.fields.push_back(parameterFields.at(p++));
            t.fields.back().column = c + 1;
        }
    }
    std::set<unsigned> exact;
    for (size_t i = 0; i < t.rows.size(); ++i) {
        auto &r = t.rows[i];
        unsigned free = all ^ masks[i];
        r["mask"] = masks[i];
        r["bits"] = bits[i];
        r["key"] = !free && exact.insert(bits[i]).second ? bits[i] : unsigned(all + 1 + i);
        for (unsigned sub = free;; sub = (sub - 1) & free) {
            unsigned code = bits[i] | sub;
            int &old = t.matching_rows[code];
            if (old >= 0 && t.rows[old]["values"] != r["values"])
                throw std::runtime_error(u8"Строки " + std::to_string(sourceRows[old]) + u8" и " +
                                         std::to_string(sourceRows[i]) +
                                         u8" задают разные параметры для кода " +
                                         std::to_string(code) + ".");
            old = int(i);
            if (!sub)
                break;
        }
    }
    return t;
}
} // namespace t2f
