#pragma once
#include "synthesis.hpp"
namespace b2f { namespace engine {
// Only verified, target-independent logical candidates are cached. No disk cache.
Json cached_search(const Problem &,const Options &,bool enabled,Json &diagnostic,const Json &profile);
Json cache_status();
void clear_cache();
void damage_cache_for_check(const std::string &kind);
} }
