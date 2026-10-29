#pragma once

#include <cstdint>
#include <string>

namespace elder_shell::recovery {
    struct StartWait {
        bool enabled = false;
        bool stageWait = false;
        bool promoting = false;
        std::uint32_t currentStage = 0;
        std::uint32_t waiters = 0;
        bool stageCompletion = false;
        bool needsStage = false;
        bool sameVM = false;
        bool sameStack = false;
        std::uint32_t requestedStage = 0;
        std::uint32_t argumentStage = 0;
    };

    inline std::string StartWaitError(const StartWait& state) {
        if (state.enabled || !state.stageWait || state.promoting || state.currentStage != 0)
            return "Requires a disabled quest at stage 0, waiting on startup with no promotion task.";
        if (state.waiters != 1 || state.stageCompletion)
            return "Requires exactly one startup waiter and no stage-completion registration.";
        if (!state.needsStage || !state.sameVM || !state.sameStack)
            return "Startup waiter does not match the selected VM stack and stage request.";
        if (!state.requestedStage || state.requestedStage > 65535 || state.argumentStage != state.requestedStage)
            return "Native stage argument does not match the registered startup stage.";
        return {};
    }
}
