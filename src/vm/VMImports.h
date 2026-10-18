#pragma once

#include "compiler/Compiler.h"
#include "shell/Evaluation.h"
#include "shell/ExpressionTypes.h"
#include <RE/T/TypeInfo.h>

namespace RE::BSScript::Internal {
    class VirtualMachine;
}

namespace elder_shell {
    std::optional<RE::BSScript::TypeInfo> FindValueType(RE::BSScript::Internal::VirtualMachine* vm,
                                                        std::string_view name);

    // Game-thread metadata only; the returned input contains no VM references.
    struct EvaluationImports {
        CompilerInput compiler;
        EvaluationEnvironment environment;
    };

    EvaluationImports SnapshotEvaluationImports(RE::BSScript::Internal::VirtualMachine* vm,
                                                const EvaluationStatement& statement,
                                                const std::vector<SourceParameter>& parameters);
}
