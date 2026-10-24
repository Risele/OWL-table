#include <vcl.h>
#pragma hdrstop
#include "core.hpp"
#include "icon.hpp"
#include <algorithm>
#include <deque>
namespace t2f {
S icon() { return block_icon; }
static int color(int t) { return t == 0 ? -16777216 : t == 3 ? -47872 : -7722014; }
static J point(double x, double y) { return {{"IsEmpty", x == 0 && y == 0}, {"X", x}, {"Y", y}}; }
static J rect(double x, double y, double w, double h) {
    return {{"Location", point(x, y)},
            {"Size", {{"IsEmpty", false}, {"Width", w}, {"Height", h}}},
            {"X", x},
            {"Y", y},
            {"Width", w},
            {"Height", h},
            {"Left", x},
            {"Top", y},
            {"Right", x + w},
            {"Bottom", y + h},
            {"IsEmpty", false}};
}
static J text(const S &s, double x, double y, double w, double h = 8, const S &name = "textTitle") {
    return {{"Text", s},
            {"Name", name},
            {"X", x},
            {"Y", y},
            {"Width", w},
            {"Height", h},
            {"TextStyleModel",
             {{"HorzAlign", 0},
              {"VertAlign", 0},
              {"TextOffset", point(0, 0)},
              {"FontName", "Consolas"},
              {"FontSize", 8},
              {"FontStyle", 0},
              {"TextColor", -16777216}}},
            {"Discriminator", 2}};
}
static J &model(J &e) {
    return e.contains("ElementModel") ? e["ElementModel"] : e["ElementBlockModel"]["ElementModel"];
}
static const J &model(const J &e) {
    return e.contains("ElementModel") ? e["ElementModel"] : e["ElementBlockModel"]["ElementModel"];
}
struct Pins {
    size_t index;
    std::vector<S> ins, outs;
};
struct Diagram {
    S name, id, revision, description, input;
    const Table &t;
    int counter = 0;
    double width = 100, height = 100;
    J elements = J::array(), wires = J::array(), store;
    std::vector<size_t> inputs, outputs;
    std::map<S, std::pair<double, double>> locations;
    std::map<S, int> types;
    Diagram(const S &n, const S &space, const S &desc, const Table &table)
        : name(n), id(uuid5(space, n)), description(desc), input(table.input), t(table) {}
    S next() { return uuid5(id, std::to_string(++counter)); }
    Pins block(const S &title, int fb, int discr, const std::vector<int> &ins,
               const std::vector<int> &outs, double x, double y, double w = 24,
               J extra = J::object(), const S &comment = "", std::vector<S> inowners = {},
               std::vector<S> outowners = {}, bool labels = false) {
        S uid = next();
        double h = std::max(10.0, (double)std::max(ins.size(), outs.size()) * (labels ? 12 : 5) +
                                      (labels ? 12 : 0)),
               bx = x + 2.5;
        J primitives = J::array({{{"Points", J::array({point(bx, y), point(bx + w, y),
                                                       point(bx + w, y + h), point(bx, y + h)})},
                                  {"PointTypes", "AAEBgQ=="},
                                  {"StyleSheetName", discr == 29 ? "WHITE" : "QUESTION"},
                                  {"Discriminator", 1}},
                                 text(title, bx, labels ? y - 10 : y, w, labels ? 8 : h,
                                      labels ? "textIdentificator" : "textTitle")});
        J ports = J::array();
        Pins pins;
        pins.index = elements.size();
        for (int dir = 0; dir < 2; ++dir) {
            auto &ts = dir ? outs : ins;
            auto &owners = dir ? outowners : inowners;
            for (size_t i = 0; i < ts.size(); ++i) {
                S pn = (dir ? "Output" : "Input") +
                       std::to_string(labels ? i : (dir ? ins.size() + i : i));
                double py = y + (labels ? 9 + 12 * i : (i + .5) * h / ts.size()),
                       px = dir ? bx + w + 2.5 : x;
                S anchor = next(), pid = next();
                primitives.push_back({{"LineModel",
                                       {{"StartPointX", dir ? bx + w : px},
                                        {"StartPointY", py},
                                        {"EndPointX", dir ? px : bx},
                                        {"EndPointY", py},
                                        {"Discriminator", 4}}},
                                      {"UniqueId", anchor},
                                      {"Name", pn},
                                      {"Color", color(ts[i])},
                                      {"Discriminator", 3}});
                ports.push_back({{"UniqueId", pid},
                                 {"AnchorUniqueId", anchor},
                                 {"ElementOwnerUId", owners.empty() ? zero : owners[i]},
                                 {"PortType", dir},
                                 {"DataType", ts[i]},
                                 {"IsFloating", discr == 18},
                                 {"Name", pn},
                                 {"PointIndexMode", dir},
                                 {"ArrayType", nullptr}});
                locations[pid] = {px, py};
                types[pid] = ts[i];
                (dir ? pins.outs : pins.ins).push_back(pid);
            }
        }
        J m = {{"Version", 0},
               {"UniqueId", uid},
               {"Title", title},
               {"Descriptor", comment},
               {"Location", point(x, y)},
               {"IsInvertOutput", false},
               {"FontSize", 8},
               {"Comment", comment},
               {"PropertyNames", J::array()},
               {"OldDisplayNames", J::array()},
               {"NewDisplayNames", J::array()},
               {"FbType", fb},
               {"Primitives", primitives},
               {"Ports", ports},
               {"IsAddToMacroProperty", false},
               {"ZOrder", elements.size() + wires.size()}};
        J e = {{"ElementModel", m}, {"Discriminator", discr}};
        e.update(extra);
        elements.push_back(e);
        width = std::max(width, x + w + 20);
        height = std::max(height, y + h + 20);
        return pins;
    }
    void wire(const S &a, const S &b) {
        require(types.at(a) == types.at(b), u8"Типы портов FBD различаются.");
        auto p = locations.at(a), q = locations.at(b);
        double mid = q.first - 7.5;
        wires.push_back({{"ConnectorModel",
                          {{"UniqueId", next()},
                           {"Color", color(types.at(a))},
                           {"Points", J::array({point(p.first, p.second), point(mid, p.second),
                                                point(mid, q.second), point(q.first, q.second)})},
                           {"FromPortUId", a},
                           {"ToPortUId", b},
                           {"ZOrder", elements.size() + wires.size()}}},
                         {"Discriminator", 1}});
    }
    S add_input() {
        auto p = block(input, 12, 38, {}, {3}, 0, 0, std::max(32, wide(input).Length() * 3),
                       {{"Index", 0}, {"InputName", input}});
        inputs.push_back(p.index);
        return p.outs[0];
    }
    void output(const S &n, int type, const S &source, double x, double y) {
        auto p = block(n, 13, 39, {type}, {}, x, y, std::max(32, wide(n).Length() * 3),
                       {{"Index", outputs.size()}, {"OutputName", n}},
                       t.titles.count(n) ? t.titles.at(n) : n);
        outputs.push_back(p.index);
        wire(source, p.ins[0]);
    }
    S constant(const J &v, int type, double x, double y, const S &comment = "") {
        double value = v.is_boolean() ? (v.get<bool>() ? 1 : 0) : v.get<double>();
        S label = type == 4 ? number(value, 9) : std::to_string((uint32_t)value);
        if (type == 4)
            std::replace(label.begin(), label.end(), '.', ',');
        auto p =
            block(label, 30, 29, {}, {type}, x, y, std::max(24., wide(label).Length() * 2.5),
                  {{"Value", type == 4 ? J(value) : J((uint32_t)value)}, {"Version", 4}}, comment);
        auto &m = model(elements[p.index]);
        m["PropertyNames"] = J::array({"Value"});
        m["OldDisplayNames"] = m["NewDisplayNames"] = J::array({u8"Значение константы"});
        return p.outs[0];
    }
    S compare(const S &source, uint32_t value, double x, double y, const S &label) {
        S c = constant(value, 3, x, y + 15, label);
        auto p = block("EQ", 25, 21, {3, 3}, {0}, x + 55, y + 10, 24, J::object(), label);
        model(elements[p.index])["Primitives"].push_back(text(label, x, y - 8, 125));
        wire(source, p.ins[0]);
        wire(c, p.ins[1]);
        return p.outs[0];
    }
    S select(const S &cond, const S &no, const S &yes, double x, double y) {
        int type = types.at(yes);
        auto p = block(type == 4 ? "fSEL" : "SEL", 20, type == 4 ? 22 : 21, {0, type, type}, {type},
                       x, y);
        wire(cond, p.ins[0]);
        wire(no, p.ins[1]);
        wire(yes, p.ins[2]);
        return p.outs[0];
    }
    S logic(const S &title, const S &a, const S &b, double x, double y) {
        int fb = title == "OR" ? 4 : 5;
        auto p = block(title, fb, 18, {0, 0}, {0}, x, y, 24,
                       {{"PinDefaultValue", title == "OR" ? 0 : 1}, {"FbType", fb}});
        wire(a, p.ins[0]);
        wire(b, p.ins[1]);
        return p.outs[0];
    }
    std::map<S, S> instance(const Diagram &child, const S &key, double x, double y) {
        int longest = 0;
        for (auto &o : t.outputs)
            longest = std::max(longest, wide(o.first).Length());
        double w = std::max({75., longest * 3. + 45, wide(child.name).Length() * 2.5});
        std::vector<S> inowners, outowners;
        std::vector<int> outtypes;
        for (auto i : child.inputs)
            inowners.push_back(model(child.elements[i])["UniqueId"]);
        for (auto i : child.outputs)
            outowners.push_back(model(child.elements[i])["UniqueId"]);
        for (auto &o : t.outputs)
            outtypes.push_back(o.second);
        auto p = block(child.name, -1, 37, {3}, outtypes, x, y, w, J::object(), "", inowners,
                       outowners, true);
        J m = model(elements[p.index]);
        // Это служебное имя используется OwenLogic при сохранении экземпляра макроса.
        m["Primitives"].push_back(text(input, x + 4, y + 5, 35, 8, "TextPrimitive"));
        for (size_t i = 0; i < t.outputs.size(); ++i)
            m["Primitives"].push_back(text(t.outputs[i].first, x + 42, y + 5 + 12 * i, w - 42, 8,
                                           "OutputLabel" + std::to_string(i)));
        auto &e = elements[p.index];
        e.erase("ElementModel");
        e.update({{"ElementBlockModel", {{"ElementModel", m}, {"Identificator", 1}}},
                  {"MacroUniqueId", child.id},
                  {"LastUpdatingId", child.revision},
                  {"Properties",
                   {{"UniquePropertyNames", J::array()}, {"PropertyItemModels", J::array()}}}});
        wire(key, p.ins[0]);
        std::map<S, S> r;
        for (size_t i = 0; i < t.outputs.size(); ++i)
            r[t.outputs[i].first] = p.outs[i];
        return r;
    }
    void finish() {
        revision = uuid5(id, J::array({elements, wires, description}).dump());
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        uint64_t date =
            (((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) + 504911232000000000ULL;
        date |= 1ULL << 62;
        J out = J::array();
        for (auto &o : t.outputs)
            out.push_back({{"Name", o.first},
                           {"DataType", o.second},
                           {"Description",
                            o.first == "Valid" ? u8"Строка существует" : t.titles.at(o.first)}});
        J meta = {
            {"ComponentMetaData",
             {{"Name", name},
              {"UniqueId", id},
              {"ModificationId", revision},
              {"GroupName", u8"Таблицы параметров"},
              {"Author", nullptr},
              {"Description", description},
              {"Tags", J::array()},
              {"ModificationDateTime", date},
              {"CreationDateTime", date}}},
            {"HasPassword", false},
            {"MetaDataInputs",
             J::array({{{"Name", input}, {"DataType", 3}, {"Description", t.titles.at(input)}}})},
            {"MetaDataOutputs", out}};
        store = {{"DocumentModel",
                  {{"UniqueId", id},
                   {"Bounds", rect(0, -20, width, height + 20)},
                   {"ApplicationVersionCreated", "3.3.378.0"},
                   {"LastChangingApplicationVersion", "3.3.378.0"},
                   {"Elements", elements},
                   {"Variables", J::array()},
                   {"VariableCatalogSections", J::array()},
                   {"ConnectorModels", wires},
                   {"CommentBlockModels", J::array()},
                   {"DevValue", "Macro"},
                   {"WorkRectangle", rect(0, -20, width, height + 20)},
                   {"KeyFileName", nullptr}}},
                 {"Password", nullptr},
                 {"LastUpdatingId", revision},
                 {"MetaData", meta},
                 {"Discriminator", 2}};
    }
};
static S label(const J &r) {
    S s;
    for (auto &v : r["labels"]) {
        if (!s.empty())
            s += "; ";
        s += v.get<S>();
    }
    return s;
}
static S padded(uint64_t x) {
    S s = std::to_string(x);
    return s.size() < 2 ? "0" + s : s;
}
J build_fbd(const Table &t, const S &name, uint64_t step) {
    require(step <= 4294967296ULL, u8"Шаг FBD вне диапазона.");
    S space = uuid5(ns, name);
    std::map<uint64_t, J> groups;
    for (auto &r : t.rows) {
        uint64_t g = step ? r["key"].get<uint64_t>() / step : 0;
        if (!groups.count(g))
            groups[g] = J::array();
        groups[g].push_back(r);
    }
    std::vector<std::shared_ptr<Diagram>> children;
    for (auto &g : groups) {
        S desc;
        for (auto &r : g.second) {
            if (!desc.empty())
                desc += '\n';
            desc += std::to_string(r["key"].get<uint32_t>()) + ": " + label(r);
        }
        S childname = step ? name + "_" + padded(g.first * step) + "_" +
                                 padded(std::min(g.first * step + step - 1, 4294967295ULL))
                           : name;
        auto d = std::make_shared<Diagram>(childname, space, desc, t);
        S key = d->add_input();
        std::map<S, S> signals;
        int i = 0;
        for (auto &o : t.outputs)
            signals[o.first] = d->constant(0, o.second, 0, 70 + i++ * 30);
        int col = 0;
        for (auto &r : g.second) {
            double x = 60 + col++ * 155;
            S matched =
                d->compare(key, r["key"], x, 10,
                           u8"№ " + std::to_string(r["key"].get<uint32_t>()) + " " + label(r));
            i = 0;
            for (auto &o : t.outputs) {
                double y = 70 + i++ * 30;
                S n = o.first;
                int type = o.second;
                J value = n == "Valid" ? J(true) : r["values"][n];
                S c = d->constant(value, type, x, y + 12, t.titles.count(n) ? t.titles.at(n) : n);
                if (type == 3 || type == 4)
                    signals[n] = d->select(matched, signals[n], c, x + 75, y);
                else {
                    S v = d->logic("AND", matched, c, x + 50, y);
                    signals[n] = d->logic("OR", signals[n], v, x + 100, y);
                }
            }
        }
        i = 0;
        for (auto &o : t.outputs)
            d->output(o.first, o.second, signals[o.first], 70 + g.second.size() * 155,
                      70 + i++ * 30);
        d->finish();
        children.push_back(d);
    }
    J docs = J::object();
    if (!step) {
        docs[children[0]->id] = children[0]->store;
        return {{"root", children[0]->id}, {"documents", docs}};
    }
    Diagram root(name, space,
                 u8"Сводный макрос таблицы. Автоматический выбор группы. " + t.input +
                     u8" — ID. При отсутствии строки Valid=FALSE, параметры равны нулю.",
                 t);
    S key = root.add_input();
    double base = std::max(170., 15 + t.outputs.size() * 12. + 35);
    int longest = 0;
    for (auto &o : t.outputs)
        longest = std::max(longest, wide(o.first).Length());
    double spacing = std::max({155., longest * 3. + 90, wide(name).Length() * 2.5 + 120});
    std::map<S, S> signals;
    int i = 0;
    for (auto &o : t.outputs)
        signals[o.first] = root.constant(0, o.second, 0, base + i++ * 30);
    int col = 0;
    for (auto &child : children) {
        double x = 65 + col++ * spacing;
        auto outs = root.instance(*child, key, x, 15);
        i = 0;
        for (auto &o : t.outputs) {
            double y = base + i++ * 30;
            S n = o.first;
            signals[n] = (o.second == 3 || o.second == 4)
                             ? root.select(outs["Valid"], signals[n], outs[n], x + spacing - 65, y)
                             : root.logic("OR", signals[n], outs[n], x + spacing - 65, y);
        }
    }
    i = 0;
    for (auto &o : t.outputs)
        root.output(o.first, o.second, signals[o.first], 75 + children.size() * spacing,
                    base + i++ * 30);
    root.finish();
    docs[root.id] = root.store;
    for (auto &c : children)
        docs[c->id] = c->store;
    return {{"root", root.id}, {"documents", docs}};
}
// Вычисление по соединениям готовой схемы, независимо от алгоритма построения.
J evaluate_fbd(const J &docs, const S &id, uint32_t key) {
    const auto &doc = docs.at(id).at("DocumentModel");
    std::map<S, S> incoming;
    for (auto &w : doc["ConnectorModels"]) {
        auto &c = w["ConnectorModel"];
        S dest = c["ToPortUId"];
        require(!incoming.count(dest), u8"Два источника порта.");
        incoming[dest] = c["FromPortUId"].get<S>();
    }
    std::map<S, J> values;
    std::set<size_t> pending;
    for (size_t i = 0; i < doc["Elements"].size(); ++i)
        pending.insert(i);
    J result = J::object();
    while (!pending.empty()) {
        bool progressed = false;
        for (auto it = pending.begin(); it != pending.end();) {
            auto &e = doc["Elements"][*it];
            auto &m = model(e);
            std::vector<J> args;
            std::vector<J> outputs;
            bool ready = true;
            for (auto &p : m["Ports"]) {
                if (p["PortType"] == 1)
                    outputs.push_back(p);
                else {
                    S pid = p["UniqueId"];
                    require(incoming.count(pid), u8"Нет источника порта.");
                    S source = incoming.at(pid);
                    if (!values.count(source)) {
                        ready = false;
                        break;
                    }
                    args.push_back(values.at(source));
                }
            }
            if (!ready) {
                ++it;
                continue;
            }
            int kind = e["Discriminator"], fb = m["FbType"];
            std::vector<J> out;
            if (kind == 38)
                out = {key};
            else if (kind == 39)
                result[e["OutputName"].get<S>()] = args[0];
            else if (kind == 29)
                out = {e["Value"]};
            else if (kind == 21 && fb == 25)
                out = {args[0] == args[1]};
            else if ((kind == 21 || kind == 22) && fb == 20)
                out = {(args[0].is_boolean() ? args[0].get<bool>() : args[0].get<double>() != 0)
                           ? args[2]
                           : args[1]};
            else if (kind == 18 && (fb == 4 || fb == 5)) {
                auto truth = [](const J &v) {
                    return v.is_boolean() ? v.get<bool>() : v.get<double>() != 0;
                };
                out = {fb == 4 ? (truth(args[0]) || truth(args[1]))
                               : (truth(args[0]) && truth(args[1]))};
            } else if (kind == 37) {
                S cid = e["MacroUniqueId"];
                J sub = evaluate_fbd(docs, cid, key);
                for (auto &p : outputs) {
                    S owner = p["ElementOwnerUId"];
                    bool found = false;
                    for (auto &ce : docs.at(cid)["DocumentModel"]["Elements"])
                        if (model(ce)["UniqueId"] == owner) {
                            out.push_back(sub.at(ce["OutputName"].get<S>()));
                            found = true;
                            break;
                        }
                    require(found, u8"Нет выхода дочернего макроса.");
                }
            } else
                throw std::runtime_error(u8"Неизвестный элемент FBD.");
            for (size_t i = 0; i < outputs.size(); ++i)
                values[outputs[i]["UniqueId"].get<S>()] = out.at(i);
            it = pending.erase(it);
            progressed = true;
        }
        require(progressed, u8"Циклическая связь FBD.");
    }
    return result;
}
int verify_fbd(const J &docs, const Table &t, const S &root) {
    std::map<uint32_t, J> rows;
    std::set<uint32_t> keys = {0, 1, 9, 10, 19, 20, 49, 50, 4294967295U};
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
        auto got = evaluate_fbd(docs, root, key);
        for (auto &o : t.outputs) {
            auto a = got.at(o.first), b = expected.at(o.first);
            double av = a.is_boolean() ? (a.get<bool>() ? 1 : 0) : a.get<double>(),
                   bv = b.is_boolean() ? (b.get<bool>() ? 1 : 0) : b.get<double>();
            require(av == bv, u8"FBD: неверное значение ID " + std::to_string(key));
        }
    }
    return keys.size();
}
} // namespace t2f
