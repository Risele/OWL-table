#pragma once
#include "core.hpp"

namespace t2f {
struct MacroDocument {
    S mode, name;
    J state, settings;
    bool logicChanged = false;
};
void embedDocument(J &definitions, J &metadata, const MacroDocument &document);
MacroDocument readMacroDocument(const U &file);
} // namespace t2f
