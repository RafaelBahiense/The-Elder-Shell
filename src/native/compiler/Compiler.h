#pragma once

#include "EmbeddedCompiler.h"

#include <filesystem>
#include <string>
#include <vector>

namespace papyrus_console {
    struct CompilerInput {
        std::string source;
        std::vector<std::pair<std::string, std::string>> declarations;
    };

    struct CompilerResult {
        std::string script;
        std::filesystem::path bytecode;
        std::string error;
    };

    // Runs on a worker. No Skyrim or VM access occurs here.
    CompilerResult CompileEvaluation(const CompilerInput& input);
    CompilerResult CompileEvaluation(const CompilerInput& input, const EmbeddedCompiler& compiler,
                                     const std::filesystem::path& cacheDirectory);
    void CopyEvaluationBytecode(const CompilerResult& result, const std::filesystem::path& scripts);
    std::filesystem::path GameScriptDirectory();
}
