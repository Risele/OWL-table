#pragma once
#include "package.hpp"
#include "../logic.hpp"
namespace b2f {
// Native serializers consume a validated, already lowered package.
J export_st(const J &, const S &, const std::vector<S> &, const std::vector<S> &, const std::vector<S> & = {});
J export_fbd(const J &, const S &, const std::vector<S> &, const std::vector<S> &, const std::vector<S> & = {});
J export_request(const J &);
J generate_strategy(const J &,const U &,const S &,bool,bool,const J &,const U &reportPath=U());
}
