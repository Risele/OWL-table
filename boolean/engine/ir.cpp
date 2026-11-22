#include "ir.hpp"
#include "control.hpp"
#include <algorithm>
#include <stdexcept>
namespace b2f { namespace engine {
static void check(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
static const char *names[]={"const","input","and","or","xor","mux","word_and","word_or","word_xor","word_not","shl","shr","extract","putbit","sel","nonzero","cd32","dc32","word_equal","subfunction"};
Op parse_operation(const std::string &name){for(int i=0;i<=int(Op::Subfunction);++i)if(name==names[i])return Op(i);throw std::runtime_error("unknown opcode: "+name);}
static Json refjson(Ref r){return {{"node",r.node},{"inverted",r.inverted}};}
static Json nodejson(const Node &n){Json args=Json::array();for(auto r:n.args)args.push_back(refjson(r));return {{"op",names[int(n.op)]},{"type",n.type==Type::Bool?"BOOL":"UDINT"},{"value",n.value},{"args",args}};}
uint32_t shl32(uint32_t v,uint32_t c){return c>=32?0:v<<c;}
uint32_t shr32(uint32_t v,uint32_t c){return c>=32?0:v>>c;}
Graph::Graph(unsigned inputs):inputs_(inputs){check(inputs>=1 && inputs<=16,"IR input count");}
Ref Graph::invert(Ref r) const {check(r.node<nodes_.size() && nodes_[r.node].type==Type::Bool,"inverse reference requires BOOL");r.inverted=!r.inverted;return r;}
Ref Graph::add(Op op,Type type,uint32_t value,const std::vector<Ref> &arguments) {
    checkpoint();
    check(int(op)>=0 && int(op)<=int(Op::Subfunction),"unknown opcode");
    std::vector<Type> expected;
    Type result=Type::Bool;
    switch(op) {
    case Op::Constant: check(type!=Type::Bool || value<=1,"BOOL constant");result=type;break;
    case Op::Input:check(value<inputs_,"input index");break;
    case Op::And:case Op::Or:case Op::Xor:expected={Type::Bool,Type::Bool};break;
    case Op::Mux:expected={Type::Bool,Type::Bool,Type::Bool};break;
    case Op::WordAnd:case Op::WordOr:case Op::WordXor:case Op::Shl:case Op::Shr:expected={Type::Udint,Type::Udint};result=Type::Udint;break;
    case Op::WordNot:case Op::Encode32:case Op::Decode32:expected={Type::Udint};result=Type::Udint;break;
    case Op::WordEqual:case Op::Extract:expected={Type::Udint,Type::Udint};break;
    case Op::Putbit:expected={Type::Udint,Type::Udint,Type::Bool};result=Type::Udint;break;
    case Op::Sel:expected={Type::Bool,Type::Udint,Type::Udint};result=Type::Udint;break;
    case Op::Nonzero:expected={Type::Udint};break;
    case Op::Subfunction:throw std::runtime_error("unresolved subfunction placeholder");
    }
    check(type==result && arguments.size()==expected.size(),"IR result type or arity");
    check(op==Op::Constant || op==Op::Input || value==0,"unused IR value must be zero");
    for(size_t i=0;i<arguments.size();++i){auto r=arguments[i];check(r.node<nodes_.size(),"IR forward/missing/cyclic reference");check(nodes_[r.node].type==expected[i] && (!r.inverted || expected[i]==Type::Bool),"IR operand type");}
    auto args=arguments;
    if(op==Op::And || op==Op::Or || op==Op::Xor || op==Op::WordAnd || op==Op::WordOr || op==Op::WordXor) {
        if(std::make_pair(args[1].node,args[1].inverted)<std::make_pair(args[0].node,args[0].inverted))std::swap(args[0],args[1]);
        if(args[0]==args[1]) {if(op==Op::Xor || op==Op::WordXor)return add(Op::Constant,type,0);return args[0];}
    }
    if((op==Op::Mux || op==Op::Sel) && args[1]==args[2])return args[1];
    Node n{op,type,value,args};auto key=nodejson(n).dump();auto found=intern_.find(key);
    if(found!=intern_.end())return {found->second,false};
    check(nodes_.size()<100000,"IR node budget");size_t index=nodes_.size();nodes_.push_back(n);intern_[key]=index;return {index,false};
}
Graph Graph::read(const Json &j) {
    validate_contract(j);check(j["kind"]=="ir","expected IR"); Graph g(j["inputs"].get<unsigned>());std::vector<Ref> map;
    auto reference=[&](const Json &r)->Ref {
        check(r.is_object() && r.size()==2 && r.count("node") && r.count("inverted"),"IR reference fields");
        check(r["node"].is_number_integer() && r["node"].get<int64_t>()>=0 && r["node"].get<uint64_t>()<map.size() && r["inverted"].is_boolean(),"IR reference range/type");
        Ref result=map[r["node"].get<size_t>()];return r["inverted"].get<bool>()?g.invert(result):result;
    };
    check(j["nodes"].size()<=100000,"IR node budget");
    for(auto &n:j["nodes"]) {
        check(n.is_object() && n.size()==4 && n.count("op") && n.count("type") && n.count("value") && n.count("args"),"IR node fields");
        check(n["op"].is_string() && n["args"].is_array(),"IR opcode/args type");
        check(n["type"]=="BOOL" || n["type"]=="UDINT","IR type");
        check(n["value"].is_number_integer() && n["value"].get<int64_t>()>=0 && n["value"].get<uint64_t>()<=0xffffffffULL,"IR value range");
        int op=0;while(op<=int(Op::Subfunction) && n["op"]!=names[op])++op;check(op<=int(Op::Subfunction),"unknown opcode");
        std::vector<Ref> args;for(auto &r:n["args"])args.push_back(reference(r));
        map.push_back(g.add(Op(op),n["type"]=="BOOL"?Type::Bool:Type::Udint,n["value"].get<uint32_t>(),args));
    }
    for(auto &r:j["roots"])g.roots.push_back(reference(r));return g;
}
Json Graph::json() const {Json nodes=Json::array(),out=Json::array();for(auto &n:nodes_)nodes.push_back(nodejson(n));for(auto r:roots)out.push_back(refjson(r));return {{"schema_version",1},{"kind","ir"},{"inputs",inputs_},{"nodes",nodes},{"roots",out}};}
std::vector<uint32_t> Graph::evaluate(uint32_t input) const {
    std::vector<uint32_t> values;values.reserve(nodes_.size());
    auto get=[&](Ref r)->uint32_t{auto v=values.at(r.node);return r.inverted?!v:v;};
    for(auto &n:nodes_) {
        uint32_t a=n.args.size()>0?get(n.args[0]):0,b=n.args.size()>1?get(n.args[1]):0,c=n.args.size()>2?get(n.args[2]):0,v=0;
        switch(n.op){
        case Op::Constant:v=n.value;break;case Op::Input:v=(input>>n.value)&1;break;
        case Op::And:case Op::WordAnd:v=a&b;break;case Op::Or:case Op::WordOr:v=a|b;break;case Op::Xor:case Op::WordXor:v=a^b;break;
        case Op::Mux:case Op::Sel:v=a?c:b;break;case Op::WordNot:v=~a;break;
        case Op::Shl:v=shl32(a,b);break;case Op::Shr:v=shr32(a,b);break;case Op::Extract:v=shr32(a,b)&1U;break;
        case Op::Putbit:v=b>=32?a:(a&~(1U<<b))|(c<<b);break;
        case Op::WordEqual:v=a==b;break;
        case Op::Nonzero:v=a!=0;break;
        case Op::Decode32:v=1U<<(a&31);break;
        case Op::Encode32:while(a>1){a>>=1;++v;}break;
        default:throw std::runtime_error("unresolved IR operation");}
        values.push_back(v);
    }
    std::vector<uint32_t> out;for(auto r:roots)out.push_back(get(r));return out;
}
Json Graph::tables() const {
    checkpoint();
    Json tables=Json::array();for(auto r:roots)tables.push_back({{"type",nodes_.at(r.node).type==Type::Bool?"BOOL":"UDINT"},{"values",Json::array()}});
    bool boolean=true;for(auto &n:nodes_)boolean=boolean && n.type==Type::Bool;
    if(boolean) {
        for(uint32_t base=0;base<(1U<<inputs_);base+=32) {
            checkpoint();unsigned lanes=std::min(32U,(1U<<inputs_)-base);uint32_t mask=lanes==32?0xffffffffU:(1U<<lanes)-1;
            std::vector<uint32_t> values;values.reserve(nodes_.size());auto get=[&](Ref r){return values.at(r.node)^(r.inverted?mask:0);};
            for(auto &n:nodes_) {
                uint32_t v=0,a=n.args.empty()?0:get(n.args[0]),b=n.args.size()<2?0:get(n.args[1]);
                switch(n.op){case Op::Constant:v=n.value?mask:0;break;
                case Op::Input:for(unsigned lane=0;lane<lanes;++lane)if(((base+lane)>>n.value)&1)v|=1U<<lane;break;
                case Op::And:v=a&b;break;case Op::Or:v=a|b;break;case Op::Xor:v=a^b;break;
                case Op::Mux:v=((~a&b)|(a&get(n.args[2])))&mask;break;
                default:throw std::runtime_error("not a Boolean batch operation");}values.push_back(v);
            }
            for(size_t r=0;r<roots.size();++r)for(unsigned lane=0;lane<lanes;++lane)tables[r]["values"].push_back((get(roots[r])>>lane)&1);
        }
    } else for(uint32_t key=0;key<(1U<<inputs_);++key) {checkpoint();auto v=evaluate(key);for(size_t r=0;r<v.size();++r)tables[r]["values"].push_back(v[r]);}
    // Independent scalar path is checked on the same boundary/early input sets.
    for(uint32_t key=0;key<std::min(64U,1U<<inputs_);++key){checkpoint();auto v=evaluate(key);for(size_t r=0;r<v.size();++r)check(tables[r]["values"][key]==v[r],"batch/scalar disagreement");}
    return tables;
}
std::vector<unsigned> Graph::fanout() const {std::vector<unsigned> f(nodes_.size());for(auto &n:nodes_)for(auto r:n.args)++f.at(r.node);for(auto r:roots)++f.at(r.node);return f;}
Graph Graph::legacy(unsigned count,const Json &nodes,int root) {
    Graph g(count);std::vector<Ref> refs;
    for(auto &n:nodes) {
        std::string op=n.at("op");Ref r;
        if(op=="const")r=g.add(Op::Constant,Type::Bool,n.at("value").get<bool>()?1:0);
        else if(op=="input")r=g.add(Op::Input,Type::Bool,n.at("index").get<unsigned>());
        else if(op=="not")r=g.invert(refs.at(n.at("a").get<size_t>()));
        else {check(op=="and" || op=="or","unknown legacy opcode");r=g.add(op=="and"?Op::And:Op::Or,Type::Bool,0,{refs.at(n.at("a").get<size_t>()),refs.at(n.at("b").get<size_t>())});}
        refs.push_back(r);
    }
    g.roots={refs.at(root),g.invert(refs.at(root))};return g;
}
} }
