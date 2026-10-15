#pragma once

#include "shell/Requests.h"

namespace papyrus_console {
    struct InstanceRow {
        std::uint64_t id = 0;
        std::string script, state, handle;
        bool initialized = false;
    };

    struct InstanceExplorer {
        std::vector<InstanceRow> rows;
        std::string status = "Enter a form/reference ID or script name, then Search";
    };

    std::string ExploreInstances(std::string form, std::string script);
    InstanceExplorer InstanceSnapshot();
    std::string UseInstance(std::uint64_t id, std::string variable);

    std::string Submit(std::string command);
    std::vector<Entry> Snapshot();
    void ClearFinished();
    void SessionChanged(bool ready);
}
