#pragma once
#include <string_view>

namespace elder_shell {
    inline bool previewTestedRuntime = true;
    inline constexpr std::string_view UntestedRuntimeWarning =
        "Untested Skyrim version. Tested only on 1.6.1170; compatibility is unverified.";

    inline bool IsTestedRuntime() {
        return previewTestedRuntime;
    }
}
