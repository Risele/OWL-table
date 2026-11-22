#pragma once
#include "synthesis.hpp"
namespace b2f {
namespace engine {
Graph lower_target(const Graph &, const std::string &language, const Json &profile = default_policy().at("native_fbd"));
Json target_metrics(const Graph &, const std::string &, unsigned table_bytes = 0);
unsigned m0_primitive(const Graph &, Op, const std::vector<Ref> &);
Json target_pool(const Pool &, const Json &profile = default_policy().at("native_fbd"));
} // namespace engine
} // namespace b2f
