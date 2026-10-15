#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace papyrus_console {
    struct CompilationRequest {
        std::filesystem::path source, flags, output;
        std::vector<std::filesystem::path> imports;
    };

    struct CompilationReport {
        bool succeeded = false;
        std::string diagnostics;
    };
}
