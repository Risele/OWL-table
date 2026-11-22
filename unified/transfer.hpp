#pragma once
#include "../cpp/core.hpp"
namespace unified {
enum class Mode { Table, Boolean, Combined };
// Return a proposed edit. The caller commits it only after the user's confirmation.
t2f::J transfer(Mode from, const t2f::J &source, Mode to, const t2f::J &target, bool freshTarget);
} // namespace unified
