#pragma once

#include "Compiler.h"
#include "Evaluation.h"
#include <RE/T/TypeInfo.h>

namespace RE::BSScript::Internal {
    class VirtualMachine;
}

namespace papyrus_console {
    std::optional<RE::BSScript::TypeInfo> FindValueType(RE::BSScript::Internal::VirtualMachine* vm,
                                                        std::string_view name);
    // Game-thread metadata only; the returned input contains no VM references.
    CompilerInput SnapshotEvaluationImports(RE::BSScript::Internal::VirtualMachine* vm,
                                            const EvaluationStatement& statement,
                                            const std::vector<SourceParameter>& parameters);
}
