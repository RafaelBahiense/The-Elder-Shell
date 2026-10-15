#pragma once

#include <REL/Module.h>
#include <REL/Version.h>
#include <string_view>

namespace papyrus_console {
    inline constexpr REL::Version TestedRuntime{1, 6, 1170, 0};
    inline constexpr std::string_view UntestedRuntimeWarning =
        "Untested Skyrim version. Papyrus Console has only been tested on 1.6.1170; compatibility is unverified.";

    inline bool IsTestedRuntime() {
        return REL::Module::get().version() == TestedRuntime;
    }
}
