#include <vcl.h>
#pragma hdrstop
#include "core.hpp"
#ifdef DISCRETE_TABLE
#include "../selector/selector.hpp"
#endif
#include "icon.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
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
static int gridUp(double v) { return 5 * int(std::ceil(v / 5)); }
struct SheetBlock {
    int width, height, titleSpace;
    int column = 0, x = 0, y = 0;
};
struct Diagram {
    S name, id, revision, description, input, group = table_macro_group;
    const Table &t;
    bool booleanGrid = false;
    std::vector<SheetBlock> shapes;
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
        if (booleanGrid) {
            int pins = std::max(ins.size(),outs.size());
            w = labels ? w : gridUp(std::max(15, 5 + 3 * wide(title).Length()));
            h = (pins + 1) * (labels ? 10 : 5);
            bx = x + 5;
            shapes.push_back({int(w) + 10, int(h), labels ? 10 : 0});
        }
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
                if (booleanGrid) {
                    px = dir ? bx + w + 5 : x;
                    py = y + (labels ? 10. * (i + 1) : 5. * (i + 1) +
                              5. * int((h / 5 - ts.size() - 1) / 2));
                }
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
    S boolean_input(const S &name, int index, int type = 0) {
        auto p = block(name, 12, 38, {}, {type}, 0, 0, std::max(32, wide(name).Length() * 3),
                       {{"Index", index}, {"InputName", name}}, t.titles.count(name) && t.titles.at(name)!=name ? t.titles.at(name) : S());
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
        int fb = title == "OR" ? 4 : title == "AND" ? 5 : 6;
        int type = types.at(a);
        auto p = block(title, fb, 18, {type, type}, {type}, x, y, 24,
                       {{"PinDefaultValue", title == "AND" ? 1 : 0}, {"FbType", fb}});
        wire(a, p.ins[0]);
        wire(b, p.ins[1]);
        return p.outs[0];
    }
    std::map<S, S> instance(const Diagram &child, const S &key, double x, double y) {
        int longest = 0;
        for (auto &o : t.outputs)
            longest = std::max(longest, wide(o.first).Length());
        double w = std::max({75., longest * 3. + 45, wide(child.name).Length() * 2.5});
        int leftWidth = gridUp(5 + 3 * wide(input).Length());
        if(booleanGrid) w=gridUp(std::max(double(leftWidth+longest*3+10),5.+3*wide(child.name).Length()));
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
        m["Primitives"].push_back(text(input, x + (booleanGrid?5:4), y + 5, booleanGrid?leftWidth:35, 8, "TextPrimitive"));
        for (size_t i = 0; i < t.outputs.size(); ++i)
            m["Primitives"].push_back(text(t.outputs[i].first, x + (booleanGrid?5+leftWidth:42),
                                           y + 5 + (booleanGrid?10:12) * i, booleanGrid?w-leftWidth:w-42, 8,
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
    void arrangeBooleanSheet() {
        // Boolean elements are emitted in dependency order. Lay out a whole sheet
        // before drawing the wires, so a wide call only widens its own column.
        struct Edge {
            size_t from, to;
            int sy, ty, laneY, firstTrack = -1, lastTrack = -1;
        };
        std::map<S, size_t> owner;
        for (size_t i = 0; i < elements.size(); ++i)
            for (auto &p : model(elements[i]).at("Ports"))
                owner[p.at("UniqueId")] = i;
        std::vector<Edge> edges;
        std::vector<std::vector<size_t>> incoming(shapes.size()), outgoing(shapes.size());
        for (auto &wire : wires) {
            auto &c = wire.at("ConnectorModel");
            S a = c.at("FromPortUId"), b = c.at("ToPortUId");
            auto from = owner.at(a), to = owner.at(b);
            require(from < to, "FBD layout requires dependency order");
            incoming[to].push_back(edges.size());
            outgoing[from].push_back(edges.size());
            edges.push_back(
                {from, to, int(locations.at(a).second), int(locations.at(b).second), 0});
        }
        int last = 0;
        for (size_t i = 0; i < shapes.size(); ++i) {
            for (auto e : incoming[i])
                shapes[i].column = std::max(shapes[i].column, shapes[edges[e].from].column + 1);
            last = std::max(last, shapes[i].column);
        }
        for (auto i : outputs)
            shapes[i].column = last;
        std::vector<std::vector<size_t>> columns(last + 1);
        for (size_t i = 0; i < shapes.size(); ++i) {
            if (elements[i].at("Discriminator") == 29 && !outgoing[i].empty()) {
                int first = last;
                for (auto e : outgoing[i])
                    first = std::min(first, shapes[edges[e].to].column);
                shapes[i].column = first - 1;
            }
            columns[shapes[i].column].push_back(i);
        }

        std::vector<int> preferred(shapes.size()), widths(last + 1);
        height = 0;
        for (auto &column : columns) {
            for (auto i : column) {
                int64_t sum = 0;
                for (auto e : incoming[i]) {
                    auto &edge = edges[e];
                    sum += shapes[edge.from].y + edge.sy - edge.ty;
                }
                if (!incoming[i].empty())
                    preferred[i] = 5 * int(std::round(double(sum) / incoming[i].size() / 5));
            }
            // Keep public port order; place the other blocks near their drivers.
            if (!column.empty() && shapes[column.front()].column != last)
                std::stable_sort(column.begin(), column.end(),
                                 [&](size_t a, size_t b) { return preferred[a] < preferred[b]; });
            int bottom = 5;
            for (auto i : column) {
                auto &s = shapes[i];
                s.y = std::max(bottom + s.titleSpace, preferred[i]);
                bottom = s.y + s.height + 10;
                height = std::max(height, double(bottom));
                widths[s.column] = std::max(widths[s.column], s.width);
            }
        }

        // A row contains disjoint occupied intervals. Even coordinates denote
        // columns, odd coordinates denote their gutters. Fan-out may share a
        // passage; unrelated signals must never overlap along a horizontal run.
        struct Run {
            int end;
            size_t net;
        };
        using Row = std::map<int, Run>;
        std::map<int, Row> rows;
        auto mark = [&](int y, int position, size_t net) {
            auto result = rows[y].emplace(position, Run{position, net});
            if (!result.second && result.first->second.net != net)
                result.first->second.net = 0;
        };
        for (const auto &s : shapes)
            for (int y = s.y - s.titleSpace; y <= s.y + s.height; y += 5)
                mark(y, 2 * s.column, 0);
        std::map<S, size_t> nets;
        std::vector<size_t> netIds;
        for (size_t i = 0; i < edges.size(); ++i) {
            auto &e = edges[i];
            S pin = wires[i].at("ConnectorModel").at("FromPortUId");
            size_t net = nets.emplace(pin, nets.size() + 1).first->second;
            netIds.push_back(net);
            e.sy += shapes[e.from].y;
            e.ty += shapes[e.to].y;
            mark(e.sy, 2 * shapes[e.from].column + 1, net);
            mark(e.ty, 2 * shapes[e.to].column - 1, net);
        }

        struct Segment {
            int low, high;
            size_t edge;
            bool departure;
        };
        std::vector<std::vector<Segment>> gutters(last);
        for (size_t i = 0; i < edges.size(); ++i) {
            auto &e = edges[i];
            const int start = 2 * shapes[e.from].column + 1, end = 2 * shapes[e.to].column - 1;
            const auto net = netIds[i];
            auto firstRun = [&](Row &row) {
                auto it = row.upper_bound(start);
                if (it != row.begin() && std::prev(it)->second.end >= start)
                    --it;
                return it;
            };
            auto available = [&](int y) {
                if (y < 5)
                    return false;
                auto &row = rows[y];
                for (auto it = firstRun(row); it != row.end() && it->first <= end; ++it)
                    if (it->second.net != net)
                        return false;
                return true;
            };
            e.laneY = e.sy;
            if (!available(e.sy)) {
                if (available(e.ty))
                    e.laneY = e.ty;
                else {
                    int direction = e.sy <= e.ty ? 5 : -5;
                    for (int d = direction;; d += direction) {
                        if (available(e.sy + d)) {
                            e.laneY = e.sy + d;
                            break;
                        }
                        if (available(e.sy - d)) {
                            e.laneY = e.sy - d;
                            break;
                        }
                    }
                }
            }
            auto &row = rows[e.laneY];
            int left = start, right = end;
            auto it = firstRun(row);
            while (it != row.end() && it->first <= end) {
                left = std::min(left, it->first);
                right = std::max(right, it->second.end);
                it = row.erase(it);
            }
            row.emplace(left, Run{right, net});
            if (e.sy != e.laneY)
                gutters[shapes[e.from].column].push_back(
                    {std::min(e.sy, e.laneY), std::max(e.sy, e.laneY), i, true});
            if (e.ty != e.laneY)
                gutters[shapes[e.to].column - 1].push_back(
                    {std::min(e.ty, e.laneY), std::max(e.ty, e.laneY), i, false});
        }
        std::vector<int> x(last + 1, 5);
        for (int c = 0; c < last; ++c) {
            auto &segments = gutters[c];
            std::stable_sort(segments.begin(), segments.end(),
                             [](const Segment &a, const Segment &b) { return a.low < b.low; });
            int tracks = 0;
            // Depart before arrivals so two different nets can use the same pin
            // height on opposite sides without an apparent electrical junction.
            for (bool departure : {true, false}) {
                std::vector<int> ends;
                for (auto &s : segments) {
                    if (s.departure != departure)
                        continue;
                    size_t k = 0;
                    while (k < ends.size() && ends[k] >= s.low)
                        ++k;
                    if (k == ends.size())
                        ends.push_back(s.high);
                    else
                        ends[k] = s.high;
                    auto &edge = edges[s.edge];
                    (departure ? edge.firstTrack : edge.lastTrack) =
                        x[c] + widths[c] + 5 * (tracks + int(k) + 1);
                }
                tracks += ends.size();
            }
            x[c + 1] = x[c] + widths[c] + std::max(15, 5 * (tracks + 1));
        }
        width = x.back() + widths.back() + 5;
        for (size_t i = 0; i < shapes.size(); ++i) {
            auto &s = shapes[i];
            s.x = x[s.column];
            auto &m = model(elements[i]);
            m["Location"] = point(s.x, s.y);
            for (auto &primitive : m["Primitives"]) {
                if (primitive.contains("Points"))
                    for (auto &p : primitive["Points"])
                        p = point(p.at("X").get<double>() + s.x, p.at("Y").get<double>() + s.y);
                else if (primitive.contains("LineModel")) {
                    auto &line = primitive["LineModel"];
                    for (auto key : {"StartPointX", "EndPointX"})
                        line[key] = line[key].get<double>() + s.x;
                    for (auto key : {"StartPointY", "EndPointY"})
                        line[key] = line[key].get<double>() + s.y;
                } else {
                    primitive["X"] = primitive.at("X").get<double>() + s.x;
                    primitive["Y"] = primitive.at("Y").get<double>() + s.y;
                }
            }
        }
        for (size_t i = 0; i < edges.size(); ++i) {
            const auto &e = edges[i];
            const auto &from = shapes[e.from], &to = shapes[e.to];
            J path = J::array({point(from.x + from.width, e.sy)});
            if (e.firstTrack >= 0) {
                path.push_back(point(e.firstTrack, e.sy));
                path.push_back(point(e.firstTrack, e.laneY));
            }
            if (e.lastTrack >= 0) {
                path.push_back(point(e.lastTrack, e.laneY));
                path.push_back(point(e.lastTrack, e.ty));
            }
            path.push_back(point(to.x, e.ty));
            wires[i]["ConnectorModel"]["Points"] = std::move(path);
            height = std::max(height, double(e.laneY + 5));
        }
    }
    void finish() {
        if (booleanGrid) arrangeBooleanSheet();
        revision = uuid5(id, J::array({elements, wires, description, group}).dump());
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
              {"GroupName", group},
              {"Author", nullptr},
              {"Description", description},
              {"Tags", J::array()},
              {"ModificationDateTime", date},
              {"CreationDateTime", date}}},
            {"HasPassword", false},
            {"MetaDataInputs", J::array()},
            {"MetaDataOutputs", out}};
        for (auto index : inputs) {
            auto &e = elements[index];
            S n = e["InputName"];
            meta["MetaDataInputs"].push_back({{"Name", n}, {"DataType", model(e)["Ports"][0]["DataType"]},
                                            {"Description", t.titles.at(n)}});
        }
        auto bounds = booleanGrid ? rect(0,0,width,height) : rect(0,-20,width,height+20);
        store = {{"DocumentModel",
                  {{"UniqueId", id},
                   {"Bounds", bounds},
                   {"ApplicationVersionCreated", "3.3.378.0"},
                   {"LastChangingApplicationVersion", "3.3.378.0"},
                   {"Elements", elements},
                   {"Variables", J::array()},
                   {"VariableCatalogSections", J::array()},
                   {"ConnectorModels", wires},
                   {"CommentBlockModels", J::array()},
                   {"DevValue", "Macro"},
                   {"WorkRectangle", bounds},
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
J build_fbd(const Table &t, const S &name, uint64_t step, const S &root_group) {
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
        d->group = step ? S(internal_macro_group) : root_group;
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
    root.group = root_group;
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
#ifdef DISCRETE_TABLE
J build_selector_fbd(const Table &t,const S &name,uint64_t step) {
    S childName=selector_child_name(t,name);
    auto data=build_fbd(t,childName,step,internal_macro_group);
    auto &docs=data["documents"];
    S childId=data["root"];
    verify_fbd(docs,t,childId);
    Diagram child(childName,ns,"",t);
    const auto &stored=docs.at(childId);
    child.id=childId;child.revision=stored.at("LastUpdatingId").get<S>();
    for(const auto &e:stored.at("DocumentModel").at("Elements")) {
        int kind=e.at("Discriminator");
        if(kind!=38 && kind!=39)continue;
        (kind==38?child.inputs:child.outputs).push_back(child.elements.size());
        child.elements.push_back(e);
    }
    Diagram d(name,uuid5(ns,"DiscreteTable:"+name),u8"Дискретные входы выбирают параметры таблицы.",t);
    d.booleanGrid=true;
    std::vector<S> ports;
    for(size_t i=0;i<t.contacts.size();++i) ports.push_back(d.boolean_input(t.contacts[i],i));
    std::map<uint32_t,S> constants;
    auto literal=[&](uint32_t value) {
        if(!constants.count(value))constants[value]=d.constant(value,3,0,0);
        return constants.at(value);
    };
    S code=literal(0);
    for(size_t i=0;i<ports.size();++i) {
        S offset=literal(i);
        auto bit=d.block("PUTBIT",22,20,{3,3,0},{3},0,0);
        d.wire(code,bit.ins[0]);d.wire(offset,bit.ins[1]);d.wire(ports[i],bit.ins[2]);
        code=bit.outs[0];
    }
    S key=code;
    std::map<unsigned,S> masked;
    unsigned all=(1U<<ports.size())-1;
    masked[all]=code;
    for(const auto &row:t.rows) {
        unsigned id=row.at("key"),mask=row.at("mask"),bits=row.at("bits");
        if(id<=all)continue;
        if(!mask){key=literal(id);continue;}
        if(!masked.count(mask))masked[mask]=d.logic("AND",code,literal(mask),0,0);
        S pattern=literal(bits);
        auto eq=d.block("EQ",25,21,{3,3},{0},0,0);
        d.wire(masked[mask],eq.ins[0]);d.wire(pattern,eq.ins[1]);
        key=d.select(eq.outs[0],key,literal(id),0,0);
    }
    auto signals=d.instance(child,key,0,0);
    for(auto &o:t.outputs)d.output(o.first,o.second,signals.at(o.first),0,0);
    d.finish();docs[d.id]=std::move(d.store);data["root"]=d.id;
    return data;
}
#endif
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
J build_boolean_fbd(const std::vector<S> &names, const J &nodes, int root,
                    const S &output, const S &inverted, const S &name, const std::vector<S> &comments) {
    Table table;
    table.input = names.at(0);
    table.outputs = {{output, 0}, {inverted, 0}};
    require(comments.empty() || comments.size()==names.size(),"Input comment count");
    for (size_t i=0;i<names.size();++i) table.titles[names[i]] = comments.empty()?names[i]:comments[i];
    table.titles[output] = u8"Результат булевой функции";
    table.titles[inverted] = u8"Инверсный результат";
    Diagram d(name, uuid5(ns, "BOOLEAN:FBD:" + name), u8"Булева функция. X — вход не влияет. Без совпадения Q=FALSE.", table);
    d.group = u8"Булевы функции";
    d.booleanGrid = true;
    std::vector<S> inputs, signals;
    for (size_t i = 0; i < names.size(); ++i) inputs.push_back(d.boolean_input(names[i], i));
    if (nodes.at(root).at("op") == "mask") {
        require(names.size() <= 5, u8"UDINT-маска поддерживает не более 5 входов.");
        S address = d.constant(0, 3, 0, 0);
        for (size_t i = 0; i < inputs.size(); ++i) {
            auto position = d.constant(unsigned(i), 3, 0, 0);
            auto bit = d.block("PUTBIT", 22, 20, {3, 3, 0}, {3}, 0, 0, 40);
            d.wire(address, bit.ins[0]);
            d.wire(position, bit.ins[1]);
            d.wire(inputs[i], bit.ins[2]);
            address = bit.outs[0];
        }
        auto mask = d.constant(nodes.at(root).at("value"), 3, 0, 0);
        auto extract = d.block("EXTRACT", 21, 20, {3, 3}, {0}, 0, 0, 40);
        d.wire(mask, extract.ins[0]);
        d.wire(address, extract.ins[1]);
        auto neg = d.block("NOT", 14, 18, {0}, {0}, 0, 0, 24,
                           {{"PinDefaultValue", 0}, {"FbType", 14}});
        d.wire(extract.outs[0], neg.ins[0]);
        d.output(output, 0, extract.outs[0], 0, 0);
        d.output(inverted, 0, neg.outs[0], 0, 0);
        d.finish();
        return {{"root", d.id}, {"documents", {{d.id, d.store}}}};
    }
    // Emit only nodes reachable from the result; optimizer candidates can contain dead nodes.
    std::set<int> used;
    std::function<void(int)> visit = [&](int i) {
        if (!used.insert(i).second) return;
        S op = nodes.at(i).at("op");
        if (op == "not" || op == "and" || op == "or") visit(nodes[i]["a"]);
        if (op == "and" || op == "or") visit(nodes[i]["b"]);
    };
    visit(root);
    require(used.size() + names.size() + 3 <= 20000, u8"Схема FBD слишком большая (более 20000 элементов). Используйте ST или более общие правила с X.");
    for (size_t i = 0; i < nodes.size(); ++i) {
        signals.push_back("");
        if (!used.count(i)) continue;
        S op = nodes[i]["op"];
        if (op == "input") { signals[i] = inputs.at(nodes[i]["index"]); continue; }
        if (op == "const") signals[i] = d.constant(nodes[i]["value"], 0, 0, 0);
        else if (op == "not") {
            auto p = d.block("NOT", 14, 18, {0}, {0}, 0, 0, 24,
                             {{"PinDefaultValue", 0}, {"FbType", 14}});
            d.wire(signals.at(nodes[i]["a"]), p.ins[0]);
            signals[i] = p.outs[0];
        } else signals[i] = d.logic(op == "and" ? "AND" : "OR", signals.at(nodes[i]["a"]), signals.at(nodes[i]["b"]), 0, 0);
    }
    auto neg = d.block("NOT", 14, 18, {0}, {0}, 0, 0, 24, {{"PinDefaultValue", 0}, {"FbType", 14}});
    d.wire(signals.at(root), neg.ins[0]);
    d.output(output, 0, signals.at(root), 0, 0);
    d.output(inverted, 0, neg.outs[0], 0, 0);
    d.finish();
    return {{"root", d.id}, {"documents", {{d.id, d.store}}}};
}

// The Boolean package adapter uses the existing native drawing primitives only.
// It does not change the single-UDINT-input Table2FBD serializer above.
J build_boolean_package_fbd(const J &package, const S &name, const std::vector<S> &publicInputs,
                            const std::vector<S> &publicOutputs, const std::vector<S> &comments) {
    require(comments.empty() || comments.size()==publicInputs.size(),"Input comment count");
    S root = package.at("root"), prefix = "Logic";
    std::vector<S> reserved = publicInputs;
    reserved.insert(reserved.end(), publicOutputs.begin(), publicOutputs.end());
    reserved.push_back(name);
    for (;;) {
        bool collision = false;
        for (auto &n : reserved)
            if (lower(n).find(lower(prefix)) == 0)
                collision = true;
        if (!collision)
            break;
        prefix = "G" + prefix;
    }
    std::map<S, J> defs;
    std::map<S, S> names;
    unsigned serial = 0;
    for (auto &d : package.at("definitions")) {
        S id = d.at("id");
        defs[id] = d;
        names[id] = id == root ? name : prefix + "Definition" + std::to_string(serial++);
    }
    std::map<S, Table> tables;
    std::map<S, std::shared_ptr<Diagram>> built;
    J documents = J::object();
    std::function<void(S)> emit = [&](S id) {
        if (built.count(id))
            return;
        auto &def = defs.at(id);
        for (auto &n : def.at("nodes"))
            if (n.at("kind") == "call")
                emit(n.at("definition"));
        auto &table = tables[id];
        std::vector<S> inputs, outputs;
        for (size_t i = 0; i < def.at("input_types").size(); ++i) {
            inputs.push_back(id == root ? publicInputs.at(i) : prefix + "P" + std::to_string(i));
        }
        for (size_t i = 0; i < def.at("outputs").size(); ++i)
            outputs.push_back(id == root ? publicOutputs.at(i) : prefix + "R" + std::to_string(i));
        table.input = inputs.empty() ? "" : inputs[0];
        for (size_t i=0;i<inputs.size();++i)
            table.titles[inputs[i]] = id==root && !comments.empty()?comments[i]:inputs[i];
        for (auto &n : outputs) {
            table.titles[n] = n;
            table.outputs.push_back({n, 0});
        }
        auto holder = std::make_shared<Diagram>(names[id], uuid5(ns, "BOOLEAN:PACKAGE:FBD:" + name),
                                                "Boolean function; independent FBD package", table);
        auto &d = *holder;
        d.group = id == root ? "Boolean" : internal_macro_group;
        d.booleanGrid = true;
        std::vector<S> in;
        for (size_t i = 0; i < inputs.size(); ++i)
            in.push_back(d.boolean_input(inputs[i], i, def.at("input_types")[i] == "BOOL" ? 0 : 3));
        std::vector<std::vector<S>> signals;
        std::map<S, S> inverses;
        auto ref = [&](const J &r) {
            S v = signals.at(r.at("node").get<size_t>()).at(r.at("port").get<size_t>());
            if (!r.at("inverted").get<bool>())
                return v;
            if (inverses.count(v))
                return inverses.at(v);
            auto p = d.block("NOT", 14, 18, {0}, {0}, 0, 0, 24,
                             {{"PinDefaultValue", 0}, {"FbType", 14}});
            d.wire(v, p.ins[0]);
            inverses[v] = p.outs[0];
            return p.outs[0];
        };
        for (auto &n : def.at("nodes")) {
            std::vector<S> a, values;
            for (auto &r : n.at("args")) {
                S v = ref(r);
                a.push_back(v);
            }
            if (n.at("kind") == "call") {
                auto &child = *built.at(n.at("definition").get<S>());
                std::vector<S> io, oo;
                std::vector<int> inputTypes, outputTypes;
                for (auto i : child.inputs) {
                    const auto &port = model(child.elements[i]);
                    io.push_back(port.at("UniqueId"));
                    inputTypes.push_back(port.at("Ports")[0].at("DataType"));
                }
                for (auto i : child.outputs) {
                    const auto &port = model(child.elements[i]);
                    oo.push_back(port.at("UniqueId"));
                    outputTypes.push_back(port.at("Ports")[0].at("DataType"));
                }
                auto labelWidth = [](const std::vector<size_t> &indices, const Diagram &doc, const char *key) {
                    int width = 10;
                    for (auto i : indices)
                        width = std::max(width, 3 * wide(doc.elements[i].at(key).get<S>()).Length());
                    return gridUp(width + 5);
                };
                int left = labelWidth(child.inputs,child,"InputName");
                int right = labelWidth(child.outputs,child,"OutputName");
                int body = std::max(left + right, gridUp(5 + 3 * wide(child.name).Length()));
                auto p = d.block(child.name, -1, 37, inputTypes, outputTypes, 0, 0, body,
                                 J::object(), "", io, oo, true);
                J m = model(d.elements[p.index]);
                for (size_t i = 0; i < child.inputs.size(); ++i)
                    m["Primitives"].push_back(
                        text(child.elements[child.inputs[i]].at("InputName"), 5,
                             5 + i * 10, left, 8,
                             i == 0 ? "TextPrimitive" : "InputLabel" + std::to_string(i)));
                for (size_t i = 0; i < child.outputs.size(); ++i)
                    m["Primitives"].push_back(
                        text(child.elements[child.outputs[i]].at("OutputName"), 5 + left,
                             5 + i * 10, right, 8, "OutputLabel" + std::to_string(i)));
                auto &e = d.elements[p.index];
                e.erase("ElementModel");
                e.update(
                    {{"ElementBlockModel", {{"ElementModel", m}, {"Identificator", 1}}},
                     {"MacroUniqueId", child.id},
                     {"LastUpdatingId", child.revision},
                     {"Properties",
                      {{"UniquePropertyNames", J::array()}, {"PropertyItemModels", J::array()}}}});
                for (size_t i = 0; i < a.size(); ++i)
                    d.wire(a[i], p.ins.at(i));
                values = p.outs;
            } else {
                S op = n.at("op");
                int type = n.at("type") == "BOOL" ? 0 : 3;
                if (op == "input") {
                    values.push_back(in.at(n.at("value").get<size_t>()));
                } else {
                    if (op == "const")
                        values.push_back(d.constant(n.at("value"), type, 0, 0));
                    else if (op == "sel")
                        values.push_back(d.select(a.at(0), a.at(1), a.at(2), 0, 0));
                    else if (op == "and" || op == "or" || op == "xor" ||
                             op == "word_and" || op == "word_or" || op == "word_xor") {
                        S title = op == "and" || op == "word_and" ? "AND" :
                                  op == "or" || op == "word_or" ? "OR" : "XOR";
                        values.push_back(d.logic(title, a.at(0), a.at(1), 0, 0));
                    } else {
                        struct Block { const char *title; int fb, discriminator; };
                        static const std::map<S, Block> blocks = {
                            {"word_not", {"NOT", 14, 18}}, {"shl", {"SHL", 2, 23}},
                            {"shr", {"SHR", 3, 23}}, {"extract", {"EXTRACT", 21, 20}},
                            {"putbit", {"PUTBIT", 22, 20}}, {"nonzero", {"GT", 24, 21}}, {"word_equal", {"EQ", 25, 21}},
                            {"dc32", {"DC32", 37, 20}}, {"cd32", {"CD32", 38, 20}}
                        };
                        const auto &block = blocks.at(op);
                        if (op == "nonzero")
                            a.push_back(d.constant(0, 3, 0, 0));
                        std::vector<int> types;
                        for (auto &v : a) types.push_back(d.types.at(v));
                        J extra = J::object();
                        if (op == "word_not") extra = {{"PinDefaultValue", 0}, {"FbType", 14}};
                        auto pins = d.block(block.title, block.fb, block.discriminator, types,
                                            {type}, 0, 0, 40, extra);
                        for (size_t i = 0; i < a.size(); ++i) d.wire(a[i], pins.ins[i]);
                        values.push_back(pins.outs[0]);
                    }
                }
            }
            signals.push_back(values);
            require(d.elements.size() <= 20000, "FBD definition exceeds 20000 elements");
        }
        std::vector<S> values;
        for (auto &r : def.at("outputs")) {
            auto v = ref(r);
            values.push_back(v);
        }
        for (size_t i = 0; i < outputs.size(); ++i) {
            int type = d.types.at(values[i]);
            table.outputs[i].second = type;
            d.output(outputs[i], type, values[i], 0, 0);
        }
        d.finish();
        documents[d.id] = d.store;
        built[id] = holder;
    };
    emit(root);
    auto &d = *built.at(root);
    J metadata = d.store.at("MetaData");
    metadata["ComponentMetaData"]["Image"] = icon();
    return {{"root", d.id}, {"documents", documents}, {"metadata", metadata}};
}
} // namespace t2f
