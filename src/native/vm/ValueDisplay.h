#pragma once

#include "shell/Inspection.h"

namespace RE::BSScript {
    class Variable;
}

namespace elder_shell {
    std::string Display(const RE::BSScript::Variable& value);
    InspectedValue InspectValue(const RE::BSScript::Variable& value, std::string expression, InspectionBudget& budget,
                                std::size_t depth = 0);
}
