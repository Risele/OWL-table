#include <vcl.h>
#pragma hdrstop
#include "logic.hpp"
#include "../cpp/input_colors.hpp"
#include "../cpp/discrete_inputs.hpp"
#include <System.Character.hpp>
#include <algorithm>
#include <sstream>
#include <tuple>
namespace b2f {
static uint32_t all(int count) { return count == 32 ? 0xffffffffU : (uint32_t(1) << count) - 1; }
Spec parse(const J &state, const S &macro) {
    if(state.count("schema_version") || state.count("kind")) {
        engine::validate_contract(state);
        require(state.value("kind",S())=="spec", "Expected spec contract");
    }
    Spec s;
    s.inputs = state.at("inputs").get<std::vector<S>>();
    require(s.inputs.size() >= 1 && s.inputs.size() <= engine::max_inputs, u8"Число входов: от 1 до 16.");
    if (state.contains("input_colors")) {
        const auto &colors = state.at("input_colors");
        require(colors.is_array() && colors.size() == s.inputs.size(), u8"Число цветов должно совпадать с числом входов.");
        for (auto &id : colors) {
            require(id.is_number_integer() && id.get<uint64_t>() < inputPalette().size(), u8"Неизвестный цвет входа.");
            s.input_colors.push_back(id.get<unsigned>());
        }
    }
    if(state.contains("input_comments")) {
        s.input_comments=state.at("input_comments").get<std::vector<S>>();
        require(s.input_comments.size()==s.inputs.size(),"Input comment count");
        for(auto &c:s.input_comments)require(wide(c).Length()<=32767,"Input comment too long");
    }
    s.output = state.at("output").get<S>();
    s.inverted = state.at("inverted").get<S>();
    std::set<S> used;
    if (!macro.empty())
        check_port_name(macro, used);
    for (auto &n : s.inputs)
        check_port_name(n, used);
    check_port_name(s.output, used);
    check_port_name(s.inverted, used);
    uint32_t count=1U<<s.inputs.size(), mask=count-1;
    s.truth.inputs=s.inputs.size(); s.truth.words.assign((count+31)/32,0);
    require(!(state.count("rules") && state.count("truth")),"Specify rules or truth, not both");
    if(state.count("truth")) {
        require(state["truth"].is_string(),"Dense truth must be a string"); S table=state["truth"];
        require(table.size()==count,"Dense truth length must equal 2^N");
        s.dense=true; s.comments.assign(count,S());
        if(state.count("comments")) {
            s.comments=state["comments"].get<std::vector<S>>(); require(s.comments.size()==count,"Dense comment count");
        }
        for(uint32_t k=0;k<count;++k) {
            require(table[k]=='0'||table[k]=='1',"Dense truth must contain only 0/1");
            require(wide(s.comments[k]).Length()<=32767,"Comment too long");s.truth.set(k,table[k]=='1');
        }
        if(state.contains("row_order")) {
            std::set<unsigned> seen;
            require(state["row_order"].is_array(),"Row order must be an array");
            for(auto &v:state["row_order"]) {
                require(v.is_number_integer() && v.get<int64_t>()>=0 && v.get<int64_t>()<count,"Row index out of range");
                unsigned k=v.get<unsigned>();require(seen.insert(k).second,"Duplicate row index");s.row_order.push_back(k);
            }
            for(unsigned k=0;k<count;++k)if(!seen.count(k))require(!s.truth.get(k) && s.comments[k].empty(),"Absent combinations must have Q=0 and no comment");
        } else for(unsigned k=0;k<count;++k)s.row_order.push_back(k);
        return s;
    }
    require(!state.count("row_order"),"Row order requires dense truth");
    require(!state.count("comments"),"Comments array requires dense truth");
    require(state.at("rules").is_array() && state["rules"].size() <= 65536,
            u8"Допустимо до 4096 правил или до 65536 полных строк.");
    s.dense=state["rules"].size()>4096;
    if(s.dense) s.comments.assign(count,S());
    std::vector<int> owner(count,-1);
    int row = 0;
    for (auto &r : state["rules"]) {
        ++row;
        try {
            require(r.at("inputs").is_array() && r["inputs"].size() == s.inputs.size(),
                    u8"Число состояний не совпадает с числом входов.");
            Rule rule{0, 0, discrete_bit(r.at("q"), false) == "1", r.value("comment", S())};
            require(wide(rule.comment).Length() <= 32767, u8"Комментарий длиннее 32767 знаков.");
            for (size_t i = 0; i < s.inputs.size(); ++i) {
                S v = discrete_bit(r["inputs"][i], true);
                if (v != "X")
                    rule.care |= uint32_t(1) << i;
                if (v == "1")
                    rule.bits |= uint32_t(1) << i;
            }
            require(!s.dense || rule.care==mask,"More than 4096 rows requires fully specified inputs");
            uint32_t free=mask & ~rule.care, subset=free;
            for (;;) {
                uint32_t witness=rule.bits|subset;
                if (owner[witness]>=0 && s.truth.get(witness)!=rule.output) {
                    throw std::runtime_error(u8"Конфликт с правилом " + std::to_string(owner[witness]) +
                                             u8": совпадение при Dec=" + std::to_string(witness) +
                                             u8", но разные Q.");
                }
                if(s.dense && owner[witness]<0)s.row_order.push_back(witness);
                owner[witness]=row; s.truth.set(witness,rule.output);
                if(s.dense && !rule.comment.empty()) {if(!s.comments[witness].empty())s.comments[witness]+="; ";s.comments[witness]+=rule.comment;}
                if(!subset)break; subset=(subset-1)&free;
            }
            if(!s.dense)s.rules.push_back(rule);
        } catch (std::exception &e) {
            throw std::runtime_error(u8"Правило " + std::to_string(row) + ": " + e.what());
        }
    }
    return s;
}
bool evaluate(const Spec &s, uint32_t bits) {
    return s.truth.get(bits & all(s.inputs.size()));
}
J canonical(const Spec &s) {
    J j={{"schema_version",1},{"kind","spec"},{"inputs",s.inputs},{"output",s.output},{"inverted",s.inverted}};
    if(!s.input_colors.empty())j["input_colors"]=s.input_colors;
    if(!s.input_comments.empty())j["input_comments"]=s.input_comments;
    if(s.dense) {S bits; for(uint32_t k=0;k<(1U<<s.inputs.size());++k)bits+=s.truth.get(k)?'1':'0';j["truth"]=bits;j["comments"]=s.comments;}
    else {j["rules"]=J::array();for(auto &r:s.rules){J bits=J::array();for(size_t i=0;i<s.inputs.size();++i)bits.push_back(!(r.care&(1U<<i))?"X":(r.bits&(1U<<i))?"1":"0");j["rules"].push_back({{"inputs",bits},{"q",r.output?"1":"0"},{"comment",r.comment}});}}
    if(s.dense) {
        bool natural=s.row_order.size()==(1U<<s.inputs.size());
        for(unsigned p=0;natural && p<s.row_order.size();++p)natural=s.row_order[p]==p;
        if(!natural)j["row_order"]=s.row_order;
    }
    return j;
}
J fresh(int count, bool complete) {
    require(count >= 1 && count <= engine::max_inputs, u8"Число входов: от 1 до 16.");
    J s = {{"inputs", J::array()}, {"output", "Q"}, {"inverted", "NQ"}, {"rules", J::array()}};
    for (int i = 0; i < count; ++i)
        s["inputs"].push_back("I" + std::to_string(i + 1));
    return complete ? expand(s) : s;
}
J resize(const J &state, int count) {
    require(count >= 1 && count <= engine::max_inputs, u8"Число входов: от 1 до 16.");
    Spec prior=parse(state);
    require(!prior.dense || count==int(prior.inputs.size()),u8"Изменять число входов можно в таблице до 4096 правил.");
    if(prior.dense)return state;
    auto usedInputs=engine::support(prior.truth);
    for(int i:usedInputs.local_to_external)
        require(i<count,u8"Нельзя удалить вход "+prior.inputs[i]+u8": от него зависит значение Q.");
    J s = state;
    if(s.contains("input_colors")) { auto colors=prior.input_colors; colors.resize(count); s["input_colors"]=colors; }
    if(s.contains("input_comments")) { auto c=s["input_comments"].get<std::vector<S>>();c.resize(count);s["input_comments"]=c; }
    while ((int)s["inputs"].size() > count)
        s["inputs"].erase(s["inputs"].end() - 1);
    while ((int)s["inputs"].size() < count) {
        S n = "I" + std::to_string(s["inputs"].size() + 1);
        std::set<S> used = {lower(s["output"]), lower(s["inverted"])};
        for (auto &v : s["inputs"])
            used.insert(lower(v));
        while (used.count(lower(n)))
            n = "I" + n;
        s["inputs"].push_back(n);
    }
    for (auto &r : s["rules"]) {
        while ((int)r["inputs"].size() > count)
            r["inputs"].erase(r["inputs"].end() - 1);
        while ((int)r["inputs"].size() < count)
            r["inputs"].push_back("X");
    }
    parse(s);
    return s;
}
J expand(const J &state) {
    Spec spec = parse(state);
    require(
        spec.inputs.size() <= 12,
        u8"Видимая полная таблица доступна до 12 входов. Для 13–16 используйте плотный импорт или правила с X.");
    J s = state;
    s.erase("truth");s.erase("comments");s.erase("row_order");
    s["rules"] = J::array();
    for (uint32_t key = 0; key <= all(spec.inputs.size()); ++key) {
        J bits = J::array();
        for (size_t i = 0; i < spec.inputs.size(); ++i)
            bits.push_back(key & (uint32_t(1) << i) ? "1" : "0");
        S comment=spec.dense?spec.comments[key]:S();
        for (auto &r : spec.rules)
            if ((key & r.care) == r.bits && !r.comment.empty()) {
                if (!comment.empty())
                    comment += "; ";
                comment += r.comment;
            }
        s["rules"].push_back(
            {{"inputs", bits}, {"q", evaluate(spec, key) ? "1" : "0"}, {"comment", comment}});
    }
    return s;
}
using Cube = std::pair<uint32_t, uint32_t>;
static std::vector<Cube> cubes(const Spec &s) {
    std::set<Cube> current, primes;
    for (auto &r : s.rules)
        if (r.output)
            current.insert({r.care, r.bits});
    auto original = current;
    while (!current.empty()) {
        std::set<Cube> next, merged;
        for (auto p : current)
            for (int i = 0; i < 32; ++i) {
                uint32_t b = uint32_t(1) << i;
                if ((p.first & b) && !(p.second & b)) {
                    Cube other = {p.first, p.second | b};
                    if (current.count(other)) {
                        next.insert({p.first & ~b, p.second});
                        merged.insert(p);
                        merged.insert(other);
                    }
                }
            }
        for (auto p : current)
            if (!merged.count(p))
                primes.insert(p);
        if (next.size() + primes.size() > 8192) {
            primes = original;
            break;
        }
        current = std::move(next);
    }
    std::vector<Cube> result;
    for (auto p : primes) {
        bool covered = false;
        for (auto q : primes)
            if (p != q && (q.first & p.first) == q.first && (p.second & q.first) == q.second) {
                covered = true;
                break;
            }
        if (!covered)
            result.push_back(p);
    }
    return result;
}
J compact(const J &state) {
    Spec s = parse(state);
    require(!s.dense,"Dense-to-rules conversion requires an explicit search budget (P06)");
    J result = state;
    result["rules"] = J::array();
    auto append = [&](Cube p, const S &q, const std::set<S> &comments) {
        J bits = J::array();
        for (size_t i = 0; i < s.inputs.size(); ++i)
            bits.push_back(!(p.first & (uint32_t(1) << i))   ? "X"
                           : (p.second & (uint32_t(1) << i)) ? "1"
                                                             : "0");
        S comment;
        for (auto &c : comments) {
            if (!comment.empty())
                comment += "; ";
            comment += c;
        }
        result["rules"].push_back({{"inputs", bits}, {"q", q}, {"comment", comment}});
    };
    // Preserve zero-rule comments, but keep only one row per input pattern.
    std::map<Cube, std::set<S>> zeroComments;
    for (auto &r : s.rules)
        if (!r.output && !r.comment.empty())
            zeroComments[{r.care, r.bits}].insert(r.comment);
    for (auto &entry : zeroComments)
        append(entry.first, "0", entry.second);
    for (auto p : cubes(s)) {
        std::set<S> comments;
        for (auto &r : s.rules)
            if (r.output && !r.comment.empty() && !((r.bits ^ p.second) & r.care & p.first))
                comments.insert(r.comment);
        append(p, "1", comments);
    }
    parse(result);
    return result;
}
Book load(const U &path, const S &sheet) {
    if (lower(utf8(ExtractFileExt(path))) == ".json")
        return {J::parse(read_file(path)), "", {}};
    Book book = read_book(path, sheet, true);
    J &matrix = book.state["matrix"];
    require(!matrix.empty(), u8"Книга пуста.");
    auto at = [&](int r, int c) -> S {
        return r < (int)matrix.size() && c < (int)matrix[r].size() ? discrete_cell(matrix[r][c]) : S();
    };
    std::vector<int> inputs;
    int out = -1, inv = -1, comment = -1, dec = -1, start = 1;
    J state = fresh(1, false);
    state["inputs"] = J::array();
    bool canonical = false;
    for (int c = 0; c < (int)matrix[0].size(); ++c) {
        auto role = lower(at(1,c));
        if (role=="bool_in") canonical=true;
    }
    for (int c = 0; c < (int)matrix[0].size(); ++c) {
        S role = lower(at(canonical ? 1 : 0, c));
        if (canonical) {
            start = 3;
            if (role == "bool_in") {
                inputs.push_back(c);
                state["inputs"].push_back(at(2, c));
                if(!state.contains("input_comments"))state["input_comments"]=J::array();
                state["input_comments"].push_back(at(0,c));
            } else if (role == "bool_out") {
                require(out == -1, u8"Требуется один Q.");
                out = c;
                state["output"] = at(2, c);
            } else if (role == "bool_not") {
                require(inv == -1, u8"Требуется один инверсный выход.");
                inv = c;
                state["inverted"] = at(2, c);
            } else if (role == "comment" || role == u8"комментарий")
                comment = c;
            else
                require(role.empty(), u8"Неизвестный тип столбца Bool: " + role);
        } else {
            if (role == "dec")
                dec = c;
            else if (role == "q") {
                require(out == -1, u8"Повторный Q.");
                out = c;
            } else if (role == "comment" || role == u8"комментарий")
                comment = c;
            else if (!role.empty() && role[0] == 'i' && role.size() > 1 &&
                     std::all_of(role.begin() + 1, role.end(),
                                 [](char x) { return x >= '0' && x <= '9'; })) {
                inputs.push_back(c);
                state["inputs"].push_back(at(0, c));
            }
            // Ignore auxiliary calculations after Q in the original workbook.
        }
    }
    if(!canonical && (out<0 || inputs.empty())) {
        for(int c=0;c<int(matrix[0].size());++c) {
            auto role=lower(at(1,c));
            require(role!="input",u8"Откройте эту книгу на вкладке «Комбинированный».");
            require(role!="id",u8"Откройте эту книгу на вкладке «Таблицы».");
        }
    }
    require(out >= 0 && !inputs.empty(),
            u8"Нужны столбцы I1…In и Q либо трёхстрочная шапка BOOL_IN/BOOL_OUT.");
    require(inputs.size() <= engine::max_inputs, u8"Поддерживается не более 16 входов.");
    for (int r = start; r < (int)matrix.size(); ++r) {
        bool empty = at(r, out).empty();
        for (int c : inputs)
            empty = empty && at(r, c).empty();
        if (empty)
            continue;
        J bits = J::array();
        for (int c : inputs)
            bits.push_back(at(r, c));
        S q = discrete_bit(at(r, out), false);
        if (inv >= 0 && !at(r, inv).empty())
            require(discrete_bit(at(r, inv), false) != q,
                    u8"Excel: неверный инверсный выход в строке " + std::to_string(r + 1));
        state["rules"].push_back(
            {{"inputs", bits}, {"q", q}, {"comment", comment >= 0 ? at(r, comment) : S()}});
        require(state["rules"].size() <= 65536, u8"В книге более 65536 строк.");
    }
    if(state.contains("input_comments") && std::all_of(state["input_comments"].begin(),state["input_comments"].end(),[](const J &v){return v=="";}))state.erase("input_comments");
    if (book.state.contains("header_fills")) {
        std::vector<unsigned> colors;
        for (int c : inputs) {
            S rgb = book.state["header_fills"].value(std::to_string(c), S());
            colors.push_back(colorFromFill(rgb));
        }
        if (std::any_of(colors.begin(), colors.end(), [](unsigned c){ return c != 0; }))
            state["input_colors"] = colors;
    }
    Spec parsed=parse(state);
    book.state = parsed.dense ? b2f::canonical(parsed) : state;
    return book;
}
void save(const U &path, const J &state) {
    Spec s = parse(state);
    J head = J::array(), types = J::array(), names = J::array(), matrix = J::array();
    for (size_t i=0;i<s.inputs.size();++i) {
        auto &n=s.inputs[i];
        head.push_back(s.input_comments.empty()?S():s.input_comments[i]);
        types.push_back("BOOL_IN");
        names.push_back(n);
    }
    for (auto p : std::vector<std::pair<S, S>>{
             {s.output, "BOOL_OUT"}, {s.inverted, "BOOL_NOT"}, {u8"Комментарий", "COMMENT"}}) {
        head.push_back(p.first);
        types.push_back(p.second);
        names.push_back(p.second == "COMMENT" ? "" : p.first);
    }
    matrix.push_back(head);
    matrix.push_back(types);
    matrix.push_back(names);
    if(s.dense) for(uint32_t k:s.row_order) {
        J row=J::array();for(size_t i=0;i<s.inputs.size();++i)row.push_back((k&(1U<<i))?"1":"0");
        row.push_back(s.truth.get(k)?"1":"0");row.push_back(s.truth.get(k)?"0":"1");row.push_back(s.comments[k]);matrix.push_back(row);
    }
    else for (auto &r : state["rules"]) {
        J row = J::array();
        for (auto &v : r["inputs"])
            row.push_back(discrete_bit(v, true));
        S q = discrete_bit(r["q"], false);
        row.push_back(q);
        row.push_back(q == "1" ? "0" : "1");
        row.push_back(r.value("comment", S()));
        matrix.push_back(row);
    }
    std::vector<S> fills;
    for (auto id : s.input_colors) fills.push_back(inputPalette().at(id).rgb);
    write_matrix(path, matrix, fills);
}
struct Builder {
    J nodes = J::array({{{"op", "const"}, {"value", false}}, {{"op", "const"}, {"value", true}}});
    std::map<S, int> intern;
    explicit Builder(int count) {
        for (int i = 0; i < count; ++i)
            nodes.push_back({{"op", "input"}, {"index", i}});
    }
    int op(const S &kind, int a, int b = -1) {
        if (kind == "not") {
            if (a < 2)
                return 1 - a;
            if (nodes[a]["op"] == "not")
                return nodes[a]["a"];
        } else {
            if (a > b)
                std::swap(a, b);
            if (a == b)
                return a;
            if (kind == "and") {
                if (a == 0)
                    return 0;
                if (a == 1)
                    return b;
            }
            if (kind == "or") {
                if (a == 0)
                    return b;
                if (a == 1)
                    return 1;
            }
            if ((nodes[a]["op"] == "not" && nodes[a]["a"] == b) ||
                (nodes[b]["op"] == "not" && nodes[b]["a"] == a))
                return kind == "and" ? 0 : 1;
        }
        S key = kind + ":" + std::to_string(a) + ":" + std::to_string(b);
        auto it = intern.find(key);
        if (it != intern.end())
            return it->second;
        int id = nodes.size();
        nodes.push_back({{"op", kind}, {"a", a}, {"b", b}});
        intern[key] = id;
        return id;
    }
    int fold(const S &kind, std::vector<int> values) {
        if (values.empty())
            return kind == "and" ? 1 : 0;
        while (values.size() > 1) {
            std::vector<int> next;
            for (size_t i = 0; i < values.size(); i += 2)
                next.push_back(i + 1 == values.size() ? values[i]
                                                      : op(kind, values[i], values[i + 1]));
            values = std::move(next);
        }
        return values[0];
    }
    Plan finish(int root, const S &method) {
        std::set<int> used;
        std::function<void(int)> visit = [&](int i) {
            if (!used.insert(i).second)
                return;
            S op = nodes[i]["op"];
            if (op == "not" || op == "and" || op == "or")
                visit(nodes[i]["a"]);
            if (op == "and" || op == "or")
                visit(nodes[i]["b"]);
        };
        visit(root);
        size_t cost = 0;
        for (int i : used)
            if (nodes[i]["op"] != "input")
                ++cost;
        return {nodes, root, method, cost};
    }
};
Plan compile(const Spec &s) {
    if(s.dense) {
        Builder tree(s.inputs.size());
        std::function<int(int,uint32_t)> build=[&](int bit,uint32_t prefix)->int {
            if(bit<0)return s.truth.get(prefix)?1:0;
            int lo=build(bit-1,prefix),hi=build(bit-1,prefix|(1U<<bit));
            if(lo==hi)return lo;
            return tree.op("or",tree.op("and",tree.op("not",bit+2),lo),tree.op("and",bit+2,hi));
        };
        int root=build(int(s.inputs.size())-1,0);
        Plan result=tree.finish(root,"dense-decision");
        require(result.cost<=20000,"Dense function exceeds legacy export budget; reference remains available");
        return result;
    }
    auto terms = cubes(s);
    Builder rules(s.inputs.size());
    std::vector<int> sums;
    for (auto p : terms) {
        std::vector<int> factors;
        for (size_t i = 0; i < s.inputs.size(); ++i)
            if (p.first & (uint32_t(1) << i))
                factors.push_back(p.second & (uint32_t(1) << i) ? i + 2 : rules.op("not", i + 2));
        sums.push_back(rules.fold("and", factors));
    }
    Plan best = rules.finish(rules.fold("or", sums), "rules");
    // Enumerated lookup candidates are bounded to 4096 combinations. Wide functions stay symbolic.
    if (s.inputs.size() <= 12) {
        terms.clear();
        for (uint32_t k = 0; k <= all(s.inputs.size()); ++k)
            if (evaluate(s, k))
                terms.push_back({all(s.inputs.size()), k});
    }
    Builder tree(s.inputs.size());
    std::map<S, int> memo;
    size_t work = 0;
    std::function<int(std::vector<Cube>)> bdd = [&](std::vector<Cube> rows) -> int {
        work += rows.size() + 1;
        if (work > 250000 || memo.size() > 8192)
            throw std::length_error("BDD budget");
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        if (rows.empty())
            return 0;
        uint32_t mask = 0;
        S key;
        for (auto p : rows) {
            if (!p.first)
                return 1;
            mask |= p.first;
            key += std::to_string(p.first) + ":" + std::to_string(p.second) + ";";
        }
        auto found = memo.find(key);
        if (found != memo.end())
            return found->second;
        int index = 31;
        while (!(mask & (uint32_t(1) << index)))
            --index;
        uint32_t bit = uint32_t(1) << index;
        std::vector<Cube> low, high;
        for (auto p : rows) {
            Cube q = {p.first & ~bit, p.second & ~bit};
            if (!(p.first & bit) || !(p.second & bit))
                low.push_back(q);
            if (!(p.first & bit) || (p.second & bit))
                high.push_back(q);
        }
        int a = bdd(low), b = bdd(high), input = index + 2, value;
        if (a == b)
            value = a;
        else if (!a)
            value = tree.op("and", input, b);
        else if (!b)
            value = tree.op("and", tree.op("not", input), a);
        else if (a == 1)
            value = tree.op("or", tree.op("not", input), b);
        else if (b == 1)
            value = tree.op("or", input, a);
        else
            value =
                tree.op("or", tree.op("and", tree.op("not", input), a), tree.op("and", input, b));
        memo[key] = value;
        return value;
    };
    try {
        int root = bdd(terms);
        auto candidate = tree.finish(root, "decision");
        if (candidate.cost < best.cost ||
            (candidate.cost == best.cost && s.inputs.size() > 5 && s.inputs.size() <= 12))
            best = candidate;
    } catch (std::length_error &) { /* Predictable fallback to sparse rules, never expand 2^32. */
    }
    require(
        best.cost <= 20000,
        u8"Функция слишком большая: более 20000 логических узлов. Сократите правила с помощью X.");
    return best;
}
bool evaluate(const Plan &plan, uint32_t bits) {
    std::vector<bool> v;
    for (auto &n : plan.nodes) {
        S op = n["op"];
        bool x;
        if (op == "const")
            x = n["value"];
        else if (op == "input")
            x = (bits & (uint32_t(1) << n["index"].get<int>())) != 0;
        else if (op == "not")
            x = !v.at(n["a"]);
        else if (op == "and")
            x = v.at(n["a"]) && v.at(n["b"]);
        else if (op == "or")
            x = v.at(n["a"]) || v.at(n["b"]);
        else
            throw std::runtime_error("Unknown legacy opcode: " + op);
        v.push_back(x);
    }
    return v.at(plan.root);
}
} // namespace b2f
