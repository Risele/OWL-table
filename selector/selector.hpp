#pragma once
#include "../cpp/core.hpp"
namespace t2f {
Table parse_discrete(const J &);
Table parse_parameter_table(const J &, const std::vector<size_t> &sourceColumns,
                            const std::vector<size_t> &sourceRows);
J selector_example();
S selector_child_name(const Table &, const S &);
S selector_local(const Table &, const S &name, S base);
J build_selector_st(const Table &, const S &, uint64_t, const S &);
J build_selector_fbd(const Table &, const S &, uint64_t);
int verify_selector_fbd(const J &, const Table &, const S &);
int verify_selector_st(const J &, const Table &, const S &);
} // namespace t2f
