#pragma once
#include "../../cpp/vendor/json.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace b2f { namespace engine {
using Json = nlohmann::json;
constexpr int schema_version = 1;
constexpr int max_inputs = 16;
constexpr int max_macro_levels = 3; // Export root is level one.
struct TruthBits {
    unsigned inputs = 0;
    std::vector<uint32_t> words;
    bool get(uint32_t index) const;
    void set(uint32_t index, bool value);
    Json json() const;
};
struct Support {
    std::vector<int> local_to_external, external_to_local;
};
Support support(const TruthBits &);
void validate_contract(const Json &);
Json default_policy();
// Unknown hardware measurements remain null, never zero.
struct Metrics {
    Json structural = Json::object();
    Json device_time = nullptr, device_ram = nullptr, device_flash = nullptr;
};
struct Candidate {
    std::string id, provenance, language;
    Json ir, metrics;
    bool verified = false;
};
struct Package {
    std::string language, root;
    Json definitions; // ordered dependencies first; each definition may have many outputs
};
} }
