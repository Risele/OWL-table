#pragma once
#include "target.hpp"
namespace b2f {
namespace engine {
Json flat_package(const Graph &, const std::string &language, unsigned external_levels = 0,
                  unsigned table_bytes = 0);
struct PackageResult {
    Graph expanded;
    Json report;
    PackageResult(Graph g, Json r) : expanded(std::move(g)), report(std::move(r)) {}
};
PackageResult check_package(const Json &, bool permit_depth_overflow = false);
Json inspect_package(const Json &, bool repair = false);
Json package_search(const Graph &, const std::string &, unsigned external_levels = 0,
                    unsigned table_bytes = 0);
} // namespace engine
} // namespace b2f
