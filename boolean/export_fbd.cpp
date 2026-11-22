#include <vcl.h>
#pragma hdrstop
#include "engine/export.hpp"
namespace b2f {
J export_fbd(const J &p, const S &name, const std::vector<S> &inputs,
             const std::vector<S> &outputs, const std::vector<S> &comments) {
    require(comments.empty() || comments.size()==inputs.size(),"Input comment count");
    engine::check_package(p);
    require(p.at("language") == "FBD", "FBD package required");
    require(outputs.size() == 2, "Boolean root requires Q/QN ports");
    parse({{"inputs", inputs},
           {"output", outputs[0]},
           {"inverted", outputs[1]},
           {"rules", J::array()}},
          name);
    for (auto &d : p.at("definitions"))
        if (d.at("id") == p.at("root"))
            require(d.at("input_types").size() == inputs.size() &&
                        d.at("outputs").size() == outputs.size(),
                    "public interface arity");
    return build_boolean_package_fbd(p, name, inputs, outputs, comments);
}
} // namespace b2f
