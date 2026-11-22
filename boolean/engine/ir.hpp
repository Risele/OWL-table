#pragma once
#include "contracts.hpp"
#include <map>
namespace b2f { namespace engine {
enum class Type { Bool, Udint };
enum class Op { Constant, Input, And, Or, Xor, Mux, WordAnd, WordOr, WordXor, WordNot,
                Shl, Shr, Extract, Putbit, Sel, Nonzero, Encode32, Decode32, WordEqual, Subfunction };
struct Ref {
    size_t node;
    bool inverted;
    bool operator==(const Ref &r) const {return node==r.node && inverted==r.inverted;}
};
struct Node { Op op; Type type; uint32_t value; std::vector<Ref> args; };
uint32_t shl32(uint32_t value,uint32_t count);
uint32_t shr32(uint32_t value,uint32_t count);
Op parse_operation(const std::string &);
class Graph {
    unsigned inputs_;
    std::vector<Node> nodes_;
    std::map<std::string,size_t> intern_;
public:
    explicit Graph(unsigned inputs);
    Ref add(Op,Type,uint32_t value,const std::vector<Ref> &args={});
    Ref invert(Ref) const;
    const std::vector<Node> &nodes() const {return nodes_;}
    std::vector<Ref> roots;
    static Graph read(const Json &);
    static Graph legacy(unsigned inputs,const Json &nodes,int root);
    Json json() const;
    std::vector<uint32_t> evaluate(uint32_t input) const;
    Json tables() const; // BOOL bit-parallel; UDINT falls back to scalar lanes.
    std::vector<unsigned> fanout() const;
};
} }
