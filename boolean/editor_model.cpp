#include <vcl.h>
#pragma hdrstop
#include "editor_model.hpp"
namespace b2f {
unsigned editor_row_count(const J &s) {
    return s.contains("truth") ? (s.contains("row_order") ? s.at("row_order").size() : s.at("truth").get_ref<const S &>().size()) : s.at("rules").size();
}
unsigned editor_index(const J &s,unsigned position) {
    require(position<editor_row_count(s),"Row outside table");
    return s.contains("row_order") ? s.at("row_order").at(position).get<unsigned>() : position;
}
static void ordered(J &s) {
    if(!s.contains("row_order")) { s["row_order"]=J::array();for(unsigned k=0;k<s.at("truth").get_ref<const S &>().size();++k)s["row_order"].push_back(k); }
}
void erase_editor_row(J &s,unsigned position) {
    unsigned k=editor_index(s,position);
    if(!s.contains("truth")) { s["rules"].erase(s["rules"].begin()+position);return; }
    ordered(s);s["truth"].get_ref<S &>()[k]='0';
    if(s.contains("comments"))s["comments"][k]="";
    s["row_order"].erase(s["row_order"].begin()+position);
}
void swap_editor_inputs(J &s, unsigned left, unsigned right) {
    unsigned count = s.at("inputs").size();
    require(left < count && right < count, "Input index outside table");
    if (left == right) return;
    for (auto field : {"inputs", "input_comments", "input_colors"})
        if (s.contains(field)) std::swap(s[field][left], s[field][right]);
    if (!s.contains("truth")) {
        for (auto &row : s.at("rules")) std::swap(row.at("inputs")[left], row.at("inputs")[right]);
        return;
    }
    // Exchanging two bits is its own inverse. Swap each affected pair exactly once.
    auto new_index = [&](unsigned k) {
        return (((k >> left) ^ (k >> right)) & 1) ? k ^ ((1U << left) | (1U << right)) : k;
    };
    auto &bits = s.at("truth").get_ref<S &>();
    for (unsigned k = 0; k < bits.size(); ++k) {
        auto other = new_index(k);
        if (other <= k) continue;
        std::swap(bits[k], bits[other]);
        if (s.contains("comments")) std::swap(s["comments"][k], s["comments"][other]);
    }
    ordered(s);
    auto &order = s.at("row_order");
    bool natural = order.size() == bits.size();
    for (unsigned position = 0; position < order.size(); ++position) {
        unsigned key = new_index(order[position].get<unsigned>());
        order[position] = key;
        natural = natural && key == position;
    }
    if (natural) s.erase("row_order");
}
void move_editor_row(J &s,unsigned position,unsigned destination) {
    editor_index(s,position);editor_index(s,destination);
    if(s.contains("truth"))ordered(s);
    auto &rows=s[s.contains("truth") ? "row_order" : "rules"];
    J row=rows[position];rows.erase(rows.begin()+position);rows.insert(rows.begin()+destination,row);
}
J rules_editor_state(const J &state) {
    if(!state.contains("truth"))return state;
    parse(state);J result=state;result.erase("truth");result.erase("comments");result.erase("row_order");result["rules"]=J::array();
    unsigned n=state.at("inputs").size();
    for(unsigned p=0;p<editor_row_count(state);++p) {
        unsigned k=editor_index(state,p);J cells=J::array();for(unsigned i=0;i<n;++i)cells.push_back((k>>i)&1?"1":"0");
        result["rules"].push_back({{"inputs",cells},{"q",S(1,state.at("truth").get_ref<const S &>()[k])},{"comment",state.contains("comments")?state.at("comments")[k]:J("")}});
    }
    return result;
}
J dense_editor_state(const J &state) {
    auto s=parse(state);if(s.dense){J result=canonical(s);if(!state.contains("truth"))result.erase("row_order");return result;}
    S bits;J comments=J::array();
    for(unsigned k=0;k<(1U<<s.inputs.size());++k) {
        bits+=s.truth.get(k)?'1':'0';S comment;
        for(auto &r:s.rules)if((k&r.care)==r.bits && !r.comment.empty()) { if(!comment.empty())comment+="; ";comment+=r.comment; }
        comments.push_back(comment);
    }
    J result=canonical(s);result.erase("rules");result["truth"]=bits;result["comments"]=comments;return result;
}
J editor_page(const J &state,unsigned page) {
    auto &bits=state.at("truth").get_ref<const S &>();unsigned n=state.at("inputs").size(),begin=page*editor_page_size;
    unsigned count=editor_row_count(state);require(begin<count || (!count && !page),"page out of range");J rows=J::array();
    for(unsigned p=begin;p<std::min(count,begin+editor_page_size);++p) {
        unsigned k=editor_index(state,p);
        J cells=J::array();for(unsigned i=0;i<n;++i)cells.push_back((k>>i)&1?"1":"0");
        rows.push_back({{"index",k},{"inputs",cells},{"q",S(1,bits[k])},{"comment",state.contains("comments")?state.at("comments")[k]:J("")}});
    }
    return rows;
}
void edit_dense_cell(J &state,unsigned index,unsigned column,const S &value) {
    unsigned n=state.at("inputs").size();auto &bits=state.at("truth").get_ref<S &>();
    require(index<bits.size() && column>=1 && column<=n+2,"dense cell outside table");
    if(state.contains("row_order"))require(std::find(state["row_order"].begin(),state["row_order"].end(),J(index))!=state["row_order"].end(),"Cannot edit an absent combination");
    if(column<=n)require(value==(((index>>(column-1))&1)?"1":"0"),"Dense input columns are fixed indices; edit Q or comment");
    else if(column==n+1) { require(value=="0" || value=="1","Q must be 0 or 1");bits[index]=value[0]; }
    else { if(!state.contains("comments"))state["comments"]=std::vector<S>(bits.size());state["comments"][index]=value; }
}
size_t editor_state_bytes(const J &state) {
    if(!state.contains("truth"))return state.dump().size()*4+1024;
    size_t size=state.at("truth").get_ref<const S &>().size()+1024;
    if(state.contains("row_order"))size+=state.at("row_order").size()*sizeof(J);
    if(state.contains("comments"))for(auto &c:state.at("comments"))size+=64+c.get_ref<const S &>().size();
    return size;
}
J editor_request(const J &r) {
    for(auto i=r.begin();i!=r.end();++i)require(i.key()=="state" || i.key()=="page" || i.key()=="edits","unknown editor field");
    J state=dense_editor_state(r.at("state"));
    for(auto &e:r.value("edits",J::array()))edit_dense_cell(state,e.at("index"),e.at("column"),e.at("value"));
    return {{"state",state},{"page",editor_page(state,r.value("page",0U))},{"estimated_bytes",editor_state_bytes(state)}};
}
}
