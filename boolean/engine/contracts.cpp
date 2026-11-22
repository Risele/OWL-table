#include "contracts.hpp"
#include <set>
#include <stdexcept>
#include <functional>
namespace b2f { namespace engine {
static void check(bool ok, const std::string &s) { if (!ok) throw std::runtime_error(s); }
static void keys(const Json &j, std::initializer_list<const char *> required,
                 std::initializer_list<const char *> optional = {}) {
    check(j.is_object(), "contract: expected object");
    std::set<std::string> allowed;
    for (auto k : required) { allowed.insert(k); check(j.count(k), std::string("missing: ")+k); }
    for (auto k : optional) allowed.insert(k);
    for (auto i=j.begin(); i!=j.end(); ++i) check(allowed.count(i.key()), "unknown field: "+i.key());
}
static uint32_t integer(const Json &j, uint64_t low, uint64_t high) {
    check(j.is_number_integer() && !j.is_boolean(), "expected integer");
    check(j.get<int64_t>() >= 0 && j.get<uint64_t>() >= low && j.get<uint64_t>() <= high, "integer out of range");
    return j.get<uint32_t>();
}
bool TruthBits::get(uint32_t i) const { return (words.at(i/32) >> (i%32)) & 1U; }
void TruthBits::set(uint32_t i, bool v) {
    auto &w=words.at(i/32); uint32_t m=uint32_t(1)<<(i%32); w=v ? w|m : w&~m;
}
Json TruthBits::json() const { return {{"schema_version",1},{"kind","truth"},{"inputs",inputs},{"words",words}}; }
Support support(const TruthBits &t) {
    Support s; s.external_to_local.assign(t.inputs,-1);
    for(unsigned i=0;i<t.inputs;++i) for(uint32_t k=0;k<(1U<<t.inputs);++k)
        if(!(k&(1U<<i)) && t.get(k)!=t.get(k|(1U<<i))) {
            s.external_to_local[i]=s.local_to_external.size(); s.local_to_external.push_back(i); break;
        }
    return s;
}
Json default_policy() {
    return {{"schema_version",1},{"kind","policy"},{"mode","compact_logic"},
      {"source",{{"max_inputs",16},{"max_rules",4096}}},
      {"search",{{"max_candidates",24},{"max_ir_nodes",100000},{"work_limit",100000}}},
      {"export",{{"max_macro_levels",3},{"cross_language_calls",false},{"allow_full_lut",false},{"table_bytes",256}}},
      {"device",{{"profile","unmeasured"},{"ram_bytes",nullptr},{"flash_bytes",nullptr}}},
      {"native_fbd",{{"code32",true},{"xor",true},{"putbit",true},{"extract",true},{"sel",true}}}};
}
void validate_contract(const Json &j) {
    check(j.is_object() && j.count("schema_version") && j.count("kind"), "missing version/kind");
    integer(j["schema_version"],1,1);
    check(j["kind"].is_string(), "kind must be a string"); std::string kind=j["kind"];
    if(kind=="policy") {
        keys(j,{"schema_version","kind","mode","source","search","export","device","native_fbd"});
        check(j["mode"]=="compact_logic", "unsupported selection mode");
        auto a=j["source"]; keys(a,{"max_inputs","max_rules"}); integer(a["max_inputs"],16,16); integer(a["max_rules"],1,4096);
        a=j["search"]; keys(a,{"max_candidates","max_ir_nodes","work_limit"});
        integer(a["max_candidates"],1,1024); integer(a["max_ir_nodes"],1,1000000); integer(a["work_limit"],1,1000000000);
        a=j["export"]; keys(a,{"max_macro_levels","cross_language_calls","allow_full_lut","table_bytes"});
        integer(a["max_macro_levels"],1,3); check(a["cross_language_calls"].is_boolean() && a["cross_language_calls"]==false,"cross-language calls forbidden");
        check(a["allow_full_lut"].is_boolean(),"allow_full_lut must be bool"); integer(a["table_bytes"],0,8192);
        a=j["device"]; keys(a,{"profile","ram_bytes","flash_bytes"}); check(a["profile"]=="unmeasured","unknown device profile");
        check(a["ram_bytes"].is_null() && a["flash_bytes"].is_null(),"unmeasured device budgets must be null");
        a=j["native_fbd"]; keys(a,{"code32","xor","putbit","extract","sel"});
        for(auto &v:a) check(v.is_boolean(),"native FBD capabilities must be Boolean");
    } else if(kind=="truth") {
        keys(j,{"schema_version","kind","inputs","words"}); unsigned n=integer(j["inputs"],1,16);
        check(j["words"].is_array() && j["words"].size()==((1U<<n)+31)/32,"truth word count");
        for(auto &w:j["words"]) integer(w,0,0xffffffffULL);
        if(n<5) check((j["words"][0].get<uint32_t>() >> (1U<<n))==0,"truth padding must be zero");
    } else if(kind=="spec") {
        keys(j,{"schema_version","kind","inputs","output","inverted"},{"rules","truth","comments","row_order","input_comments","input_colors"});
        check(j["inputs"].is_array() && j["inputs"].size()>=1 && j["inputs"].size()<=16,"input count");
        if(j.count("input_colors")) {
            check(j["input_colors"].is_array() && j["input_colors"].size()==j["inputs"].size(),"input color count");
            for(auto &c:j["input_colors"]) integer(c,0,12);
        }
        if(j.count("input_comments")){check(j["input_comments"].is_array() && j["input_comments"].size()==j["inputs"].size(),"input comment count");for(auto &c:j["input_comments"])check(c.is_string(),"input comment type");}
        std::set<std::string> names;
        for(auto &v:j["inputs"]) {check(v.is_string() && !v.get<std::string>().empty(),"input name"); check(names.insert(v).second,"duplicate input");}
        check(j["output"].is_string() && j["inverted"].is_string() && j["output"]!=j["inverted"],"output names");
        check(j.count("rules")+j.count("truth")==1,"exactly one spec representation required");
        if(j.count("truth")) {
            check(j["truth"].is_string() && j["truth"].get<std::string>().size()==(1U<<j["inputs"].size()),"dense truth length");
            for(char c:j["truth"].get<std::string>()) check(c=='0'||c=='1',"dense truth bit");
            if(j.count("comments")){check(j["comments"].is_array() && j["comments"].size()==j["truth"].get<std::string>().size(),"dense comments length");for(auto &c:j["comments"])check(c.is_string(),"comment type");}
            if(j.count("row_order")) {
                check(j["row_order"].is_array(),"row order array");std::set<unsigned> seen;
                auto &bits=j["truth"].get_ref<const std::string &>();
                for(auto &v:j["row_order"]) {unsigned k=integer(v,0,bits.size()-1);check(seen.insert(k).second,"duplicate row index");}
                for(unsigned k=0;k<bits.size();++k)if(!seen.count(k))check(bits[k]=='0' && (!j.count("comments") || j["comments"][k]==""),"absent row data");
            }
        } else {
            check(!j.count("row_order") && !j.count("comments") && j["rules"].is_array() && j["rules"].size()<=65536,"rules limit");
            for(auto &r:j["rules"]) {keys(r,{"inputs","q"},{"comment"});check(r["inputs"].is_array() && r["inputs"].size()==j["inputs"].size(),"rule input count");
                for(auto &v:r["inputs"])check(v=="0"||v=="1"||(v=="X" && j["rules"].size()<=4096),"canonical rule bit");
                check(r["q"]=="0"||r["q"]=="1","canonical output bit");if(r.count("comment"))check(r["comment"].is_string(),"comment type");}
        }
    } else if(kind=="package") {
        keys(j,{"schema_version","kind","language","root","definitions"});
        check(j["language"]=="ST" || j["language"]=="FBD","package language");
        check(j["root"].is_string() && j["definitions"].is_array(),"package structure");
        std::map<std::string,int> heights;
        for(auto &d:j["definitions"]) {
            keys(d,{"name","language","dependencies","outputs","source"});
            check(d["name"].is_string() && d["language"]==j["language"],"definition name/language");
            check(d["dependencies"].is_array() && d["outputs"].is_array() && !d["outputs"].empty() && d["source"].is_string(),"definition fields");
            for(auto &o:d["outputs"])check(o.is_string(),"output name type");
            int h=1; for(auto &dep:d["dependencies"]) {check(dep.is_string() && heights.count(dep.get<std::string>()),"dependency missing, cyclic or out of order"); h=std::max(h,heights[dep.get<std::string>()]+1);}
            check(h<=3 && heights.emplace(d["name"].get<std::string>(),h).second,"depth or duplicate definition");
        }
        check(heights.count(j["root"].get<std::string>()),"missing root");
    } else if(kind=="metrics") {
        keys(j,{"schema_version","kind","structural","device_time","device_ram","device_flash"});check(j["structural"].is_object(),"structural metrics object");
        for(auto &v:j["structural"])integer(v,0,0xffffffffULL);
        for(auto k:{"device_time","device_ram","device_flash"})check(j[k].is_null(),"no measured profile yet");
    } else if(kind=="candidate") {
        keys(j,{"schema_version","kind","id","provenance","language","ir","metrics","verified"});
        check(j["id"].is_string() && j["provenance"].is_string() && (j["language"]=="ST"||j["language"]=="FBD") && j["verified"].is_boolean(),"candidate fields");
        check(j["ir"].value("kind",std::string())=="ir" && j["metrics"].value("kind",std::string())=="metrics","candidate members"); validate_contract(j["ir"]);validate_contract(j["metrics"]);
    } else if(kind=="report") {
        keys(j,{"schema_version","kind","status","checked","counterexample","warnings"});
        check(j["status"]=="passed"||j["status"]=="failed"||j["status"]=="unknown","report status"); integer(j["checked"],0,65536);
        if(!j["counterexample"].is_null())integer(j["counterexample"],0,65535);
        check(j["warnings"].is_array(),"warnings array");for(auto &v:j["warnings"])check(v.is_string(),"warning type");
    } else if(kind=="ir") {
        keys(j,{"schema_version","kind","inputs","nodes","roots"});integer(j["inputs"],1,16);
        check(j["nodes"].is_array() && j["roots"].is_array() && !j["roots"].empty(),"IR arrays");
        // Node typing and acyclicity are validated by Graph::read (P03).
    } else throw std::runtime_error("unknown contract kind: "+kind);
}
} }
