#include "transfer.hpp"
#include "../cpp/input_colors.hpp"
#include "../cpp/discrete_inputs.hpp"
#include <algorithm>

namespace unified {
using namespace t2f;
namespace {
S kind(const J &f) {
    S k = lower(trim(f.at(1)));
    if (k == u8"комментарий")
        return "comment";
    return k == "float" ? "real" : k;
}
struct Contact {
    S name, comment;
    unsigned color;
};
std::vector<Contact> contacts(Mode mode, const J &s) {
    std::vector<Contact> result;
    std::set<S> names;
    auto add = [&](S n, S comment, unsigned color) {
        n = trim(n);
        check_port_name(n, names);
        result.push_back({n, comment, color});
    };
    if (mode == Mode::Boolean) {
        for (size_t i = 0; i < s.at("inputs").size(); ++i)
            add(s.at("inputs")[i],
                s.contains("input_comments") ? s.at("input_comments").at(i).get<S>() : S(),
                s.contains("input_colors") ? s.at("input_colors").at(i).get<unsigned>() : 0);
    } else {
        for (const auto &f : s.at("fields"))
            if (kind(f) == "input")
                add(f.at(2), f.at(0).get<S>(), fieldColor(f));
    }
    require(result.size() <= 16, u8"Максимум 16 входов.");
    return result;
}
void remove_column(J &s, size_t col) {
    s["fields"].erase(col);
    for (auto &row : s["records"])
        row.erase(col);
}
void insert_column(J &s, size_t col, J field, const S &value) {
    auto &fields = s["fields"];
    fields.insert(fields.begin() + col, field);
    for (auto &r : s["records"])
        r.insert(r.begin() + col, value);
}
void into_combined(J &s, const std::vector<Contact> &incoming, bool fresh) {
    if (incoming.empty())
        return;
    auto &fs = s["fields"];
    if (fresh)
        for (size_t c = fs.size(); c--;)
            if (kind(fs[c]) == "input")
                remove_column(s, c);
    std::map<S, size_t> old;
    std::set<S> used = {"valid"};
    size_t insertAt = 0;
    for (size_t i = 0; i < fs.size(); ++i) {
        S name = lower(trim(fs[i][2]));
        if (kind(fs[i]) == "input") {
            old[name] = i;
            insertAt = i + 1;
        } else if (!name.empty())
            used.insert(name);
    }
    for (auto &contact : incoming) {
        auto key = lower(contact.name);
        require(!used.count(key), u8"Имя входа уже занято выходом: " + contact.name);
        if (old.count(key)) {
            if (!contact.comment.empty())
                fs[old[key]][0] = contact.comment;
        } else {
            require(old.size() < 16, u8"После объединения получилось бы больше 16 входов.");
            insert_column(s, insertAt, {contact.comment, "INPUT", contact.name}, fresh ? "" : "X");
            old[key] = insertAt++;
        }
        if (contact.color)
            setFieldColor(fs[old[key]], contact.color);
    }
}
void into_boolean(J &s, const std::vector<Contact> &incoming, bool fresh) {
    if (incoming.empty())
        return;
    if (fresh) {
        s["inputs"] = J::array();
        s.erase("input_comments");
        s.erase("input_colors");
    }
    auto &inputs = s["inputs"];
    size_t before = inputs.size();
    auto comments = s.value("input_comments", std::vector<S>(before));
    auto colors = s.value("input_colors", std::vector<unsigned>(before));
    std::map<S, size_t> position;
    for (size_t i = 0; i < before; ++i)
        position[lower(trim(inputs[i]))] = i;
    for (auto &c : incoming) {
        S key = lower(c.name);
        require(key != lower(trim(s.at("output"))) && key != lower(trim(s.at("inverted"))),
                u8"Имя входа совпадает с выходом Bool: " + c.name);
        if (!position.count(key)) {
            require(inputs.size() < 16, u8"После объединения получилось бы больше 16 входов.");
            position[key] = inputs.size();
            inputs.push_back(c.name);
            comments.emplace_back();
            colors.push_back(0);
        }
        if (c.color)
            colors.at(position[key]) = c.color;
        if (!c.comment.empty())
            comments.at(position[key]) = c.comment;
    }
    if (s.contains("input_comments") ||
        std::any_of(comments.begin(), comments.end(), [](const S &v) { return !v.empty(); }))
        s["input_comments"] = comments;
    if (s.contains("input_colors") ||
        std::any_of(colors.begin(), colors.end(), [](unsigned id) { return id != 0; }))
        s["input_colors"] = colors;
    if (inputs.size() == before)
        return;
    if (s.contains("truth")) {
        // Appending a contact repeats the original function for both values of the new bit.
        auto truth = s["truth"].get<S>();
        auto notes = s.value("comments", J::array()), order = s.value("row_order", J::array());
        unsigned copies = 1U << (inputs.size() - before);
        for (unsigned i = 1; i < copies; ++i) {
            s["truth"].get_ref<S &>() += truth;
            for (auto &v : notes)
                s["comments"].push_back(v);
            for (auto &v : order)
                s["row_order"].push_back(i * truth.size() + v.get<unsigned>());
        }
    } else
        for (auto &r : s.at("rules"))
            while (r["inputs"].size() < inputs.size())
                r["inputs"].push_back("X");
}
bool parameter(const S &k) { return k == "bool" || k == "udint" || k == "real" || k == "comment"; }
void parameters(const J &source, J &s, bool fresh) {
    const auto &sourceFields = source.at("fields");
    auto &fs = s.at("fields");
    if (fresh && std::any_of(sourceFields.begin(), sourceFields.end(),
                             [](const J &f) { return parameter(kind(f)); }))
        for (size_t c = fs.size(); c--;)
            if (parameter(kind(fs[c])))
                remove_column(s, c);
    std::set<S> protectedNames = {"valid"}, sourceNames;
    std::map<S, std::vector<size_t>> columns;
    auto key = [](const J &f) {
        return kind(f) == "comment" ? "#" + lower(trim(f[0])) : lower(trim(f[2]));
    };
    int id = -1;
    for (size_t c = 0; c < fs.size(); ++c) {
        if (parameter(kind(fs[c])))
            columns[key(fs[c])].push_back(c);
        else {
            protectedNames.insert(key(fs[c]));
            if (kind(fs[c]) == "id") {
                id = c;
                if (key(fs[c]).empty())
                    protectedNames.insert("id");
            }
        }
    }
    std::map<S, size_t> occurrence;
    std::vector<std::pair<size_t, size_t>> mapping;
    for (size_t i = 0; i < sourceFields.size(); ++i) {
        const auto &f = sourceFields[i];
        auto k = kind(f);
        if (!parameter(k))
            continue;
        S name = key(f);
        if (k != "comment") {
            check_port_name(trim(f[2]), sourceNames);
            require(!protectedNames.count(name), u8"Имя параметра уже занято: " + f[2].get<S>());
        }
        auto &matches = columns[name];
        size_t n = occurrence[name]++;
        if (n == matches.size()) {
            matches.push_back(fs.size());
            insert_column(s, fs.size(), f, k == "comment" ? "" : "0");
        } else {
            // An empty source description leaves an existing description in place.
            auto &dest = fs[matches[n]];
            if (!trim(f[0]).empty())
                dest[0] = f[0];
            dest[1] = f[1];
        }
        mapping.emplace_back(i, matches[n]);
    }
    if (mapping.empty())
        return;
    std::set<S> ids;
    if (id >= 0)
        for (const auto &row : s.at("records")) {
            S v = trim(row.at(id));
            try {
                ids.insert(std::to_string(std::stoull(v)));
            } catch (const std::exception &) {
            }
        }
    unsigned nextId = 0;
    for (size_t i = 0; i < source.at("records").size(); ++i) {
        auto &rows = s.at("records");
        const auto &src = source.at("records")[i];
        if (rows.size() <= i) {
            J row = J::array();
            for (auto &f : fs)
                row.push_back(kind(f) == "input" || kind(f) == "comment" ? "" : "0");
            if (id >= 0) {
                while (ids.count(std::to_string(nextId)))
                    ++nextId;
                row[id] = std::to_string(nextId++);
            }
            rows.push_back(std::move(row));
        }
        for (auto pair : mapping)
            if (!trim(src.at(pair.first)).empty())
                rows[i][pair.second] = src[pair.first];
    }
}
} // namespace
J transfer(Mode from, const J &source, Mode to, const J &target, bool freshTarget) {
    J result = target;
    if (from == to || (from != Mode::Combined && to != Mode::Combined))
        return result;
    if (from == Mode::Boolean)
        into_combined(result, contacts(from, source), freshTarget);
    else if (to == Mode::Boolean)
        into_boolean(result, contacts(from, source), freshTarget);
    else
        parameters(source, result, freshTarget);
    return result;
}
} // namespace unified
