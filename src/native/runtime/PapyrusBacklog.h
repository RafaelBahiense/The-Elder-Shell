#pragma once

#include "shell/Backlog.h"

namespace elder_shell {
    void RefreshPapyrusBacklog(bool full = false);
    PapyrusBacklog PapyrusBacklogSnapshot();
    // Capture immediately at a main-thread safe point and publish to the menu snapshot.
    PapyrusBacklog CapturePapyrusBacklog(bool full = false);
    void ResetPapyrusBacklog(bool ready);
}
