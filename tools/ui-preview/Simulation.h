#pragma once
#include "Session.h"

namespace preview {
    void Reset(Scenario scenario);
    void Tick();
    void SaveSession(Session& session);
    void RestoreSession(const Session& session);
}
