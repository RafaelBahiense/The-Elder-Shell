#pragma once

#include "Compilation.h"
#include <chrono>

namespace elder_shell {
    class EmbeddedCompiler final {
    public:
        explicit EmbeddedCompiler(std::chrono::milliseconds timeLimit = std::chrono::seconds(30))
            : timeLimit_(timeLimit) {}

        std::string Identity() const;
        CompilationReport Compile(const CompilationRequest& request) const;

    private:
        std::chrono::milliseconds timeLimit_;
    };
}
