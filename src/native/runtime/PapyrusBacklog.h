#pragma once

#include "shell/Backlog.h"

namespace elder_shell {
    void RefreshPapyrusBacklog(bool full = false);
    PapyrusBacklog PapyrusBacklogSnapshot();
    void ResetPapyrusBacklog(bool ready);
}
