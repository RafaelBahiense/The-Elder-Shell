#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <ostream>
#include <sstream>
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

    inline void WriteBacklogReport(std::ostream& output, const PapyrusBacklog& snapshot, std::size_t rows = 32,
                                   std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        output << "Papyrus backlog: " << (snapshot.full ? "full overflow report" : "quick snapshot") << '\n'
               << "Status: " << snapshot.status << '\n';
        if (snapshot.pending)
            output << "Capture pending; any counts below belong to the previous snapshot.\n";
        if (!snapshot.captured)
            return;
        const auto age = std::max(
            std::int64_t{0}, static_cast<std::int64_t>(
                                 std::chrono::duration_cast<std::chrono::seconds>(now - snapshot.capturedAt).count()));
        output << "Captured " << age << " seconds ago\n"
               << "Registered stacks: " << snapshot.registeredStacks << '\n'
               << "Latent return registrations: " << snapshot.latentReturns << '\n'
               << "Suspended overflow entries: " << snapshot.suspendedOverflow << '\n'
               << "Function-message overflow entries: " << snapshot.functionOverflow << '\n'
               << "Calls inspected: " << snapshot.sampledCalls << '\n'
               << "Stacks inspected: " << snapshot.sampledStacks << '\n'
               << "Overflow coverage complete: "
               << (snapshot.sampledCalls ==
                   static_cast<std::uint64_t>(snapshot.suspendedOverflow) + snapshot.functionOverflow)
               << '\n'
               << "Stack coverage complete: " << (snapshot.sampledStacks == snapshot.registeredStacks) << '\n';
        if (!snapshot.reportPath.empty())
            output << "Report: " << snapshot.reportPath << '\n';
        const auto writeRows = [&](const char* name, const std::vector<BacklogRow>& groups) {
            output << '\n' << name << " (" << std::min(rows, groups.size()) << " groups shown)\nCount\tName\n";
            for (std::size_t index = 0; index < std::min(rows, groups.size()); ++index)
                output << groups[index].count << '\t' << groups[index].name << '\n';
            if (groups.empty())
                output << "No entries in the inspected collections.\n";
            if (groups.size() > rows)
                output << groups.size() - rows << " additional groups omitted from this view.\n";
        };
        writeRows(snapshot.full ? "Queued calls" : "Queued call sample", snapshot.calls);
        writeRows("Stack states", snapshot.states);
        if (!snapshot.full)
            output
                << "\nQuick snapshots retain the top 32 groups from bounded samples, not a representative breakdown.\n";
        output << "\nCounts overlap; do not add them. Fixed queues are not inspected. Collections are read under "
                  "separate locks, not as one atomic VM snapshot. Unknown metadata remains counted. "
                  "Receiver names do not identify event producers. Counts are not CPU costs or wait durations. "
                  "Full scans stop at one million entries per collection. No engine work was modified.\n";
    }

    inline std::string DescribeBacklog(const PapyrusBacklog& snapshot,
                                       std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        std::ostringstream output;
        WriteBacklogReport(output, snapshot, 32, now);
        return output.str();
    }
}
