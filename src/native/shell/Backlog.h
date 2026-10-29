#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace elder_shell {
    struct BacklogRow {
        std::string name;
        std::uint32_t count = 0;
    };

    inline std::vector<BacklogRow> BacklogRows(const std::map<std::string, std::uint32_t>& counts,
                                               std::size_t limit = 32) {
        std::vector<BacklogRow> rows;
        for (const auto& [name, count] : counts)
            rows.push_back({name, count});
        std::ranges::sort(rows, [](const auto& a, const auto& b) {
            return a.count != b.count ? a.count > b.count : a.name < b.name;
        });
        if (rows.size() > limit)
            rows.resize(limit);
        return rows;
    }

    inline std::string BacklogStateName(std::uint32_t state) {
        switch (state) {
            case 0:
                return "Running / scheduled";
            case 1:
                return "Finished";
            case 2:
                return "Waiting on memory";
            case 3:
                return "Waiting on latent function";
            case 4:
                return "Waiting on another stack's call";
            case 5:
                return "Waiting on another stack's return";
            case 6:
                return "Waiting on return without pop";
            case 7:
                return "Retrying return";
            case 8:
                return "Retrying call";
            default:
                return "Unknown state " + std::to_string(state);
        }
    }

    struct PapyrusBacklog {
        bool captured = false;
        bool pending = false;
        bool full = false;
        std::string status = "Refresh to capture the current Papyrus queues.";
        std::string reportPath;
        std::chrono::steady_clock::time_point capturedAt{};
        std::uint32_t registeredStacks = 0;
        std::uint32_t latentReturns = 0;
        std::uint32_t suspendedOverflow = 0;
        std::uint32_t functionOverflow = 0;
        std::uint32_t sampledStacks = 0;
        std::uint32_t sampledCalls = 0;
        std::vector<BacklogRow> states;
        std::vector<BacklogRow> calls;
    };
}
