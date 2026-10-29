#include "shell/QuestStart.h"
#include <doctest/doctest.h>

using namespace elder_shell::recovery;

TEST_CASE("Startup failure requires a single matching native waiter") {
    const StartWait valid{false, true, false, 0, 1, false, true, true, true, 1, 1};
    CHECK(StartWaitError(valid).empty());
    auto candidate = valid;
    candidate.waiters = 2;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.waiters = 0;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.stageCompletion = true;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.sameVM = false;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.sameStack = false;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.needsStage = false;
    CHECK_FALSE(StartWaitError(candidate).empty());
}

TEST_CASE("Running quests and ordinary stage work cannot use startup failure recovery") {
    const StartWait valid{false, true, false, 0, 1, false, true, true, true, 1, 1};
    auto candidate = valid;
    candidate.enabled = true;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.stageWait = false;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.promoting = true;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.currentStage = 10;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.argumentStage = 10;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.argumentStage = candidate.requestedStage = 65536;
    CHECK_FALSE(StartWaitError(candidate).empty());
    candidate = valid;
    candidate.argumentStage = candidate.requestedStage = 0;
    CHECK_FALSE(StartWaitError(candidate).empty());
}
