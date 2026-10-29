#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <sstream>

namespace elder_shell::recovery {
    // Session-only, single-use permissions. IDs are never reused after a load.
    class Plans {
    public:
        explicit Plans(std::string scope = "test") : scope_(std::move(scope)) {}

        void SetScope(std::string scope) {
            if (scope_.empty() && plans_.empty() && !next_)
                scope_ = std::move(scope);
        }

        struct Plan {
            std::string operation;
            std::string identity;
            std::string expected;
            std::string description;
        };

        std::string Add(Plan plan) {
            if (scope_.empty() || plans_.size() >= 32 || next_ == std::numeric_limits<std::int32_t>::max())
                return {};
            const auto id = scope_ + ":" + std::to_string(++next_);
            plans_.emplace(id, std::move(plan));
            return id;
        }

        const Plan* Find(std::string_view id) const {
            const auto found = plans_.find(std::string(id));
            return found == plans_.end() ? nullptr : &found->second;
        }

        std::string Consume(std::string_view id, std::string_view operation, std::string_view identity,
                            std::string_view current) {
            const auto found = plans_.find(std::string(id));
            if (found == plans_.end())
                return "Plan missing, consumed, or invalidated by a save load.";
            const auto plan = std::move(found->second);
            plans_.erase(found);
            if (plan.operation != operation || plan.identity != identity)
                return "Plan belongs to another operation or target; nothing changed.";
            if (plan.expected != current)
                return "Live state changed since preview; nothing changed. Preview again.";
            return {};
        }

        void Clear() {
            plans_.clear();
        }

    private:
        std::int32_t next_ = 0;
        std::string scope_;
        std::map<std::string, Plan> plans_;
    };

    inline std::string Lifecycle(bool enabled, bool stageWait, bool promoting) {
        if (promoting)
            return enabled ? "Starting" : "Transition pending";
        if (stageWait)
            return enabled ? "Running; stage work pending" : "Stopping; stage work pending";
        return enabled ? "Running" : "Stopped";
    }

    inline std::string LatentRecoveryError(bool waiting, bool native, bool engineOperationCancelled) {
        if (!waiting || !native)
            return "Stack is not waiting on an identified native latent call.";
        if (!engineOperationCancelled)
            return "Recovery unavailable: cancellation and completion ownership of the native operation are "
                   "unverified. "
                   "Returning a result would resume the script without cancelling engine work.";
        return {};
    }

    inline std::string CompareReports(std::string_view before, std::string_view after) {
        std::map<std::string, int> changes;
        const auto count = [&](std::string_view report, int direction) {
            std::istringstream stream{std::string(report)};
            for (std::string line; std::getline(stream, line);)
                changes[line] += direction;
        };
        count(before, -1);
        count(after, 1);
        std::string text;
        std::size_t rows = 0;
        for (const auto& [line, difference] : changes) {
            if (!difference)
                continue;
            if (++rows > 128)
                return text + "\n[Difference listing truncated]";
            text += std::string(difference < 0 ? "\n- " : "\n+ ") + line;
        }
        return text.empty() ? "No changes in captured fields." : text;
    }

    inline std::string PropertyRepairSource(std::string_view plan, std::uint32_t owner, std::string_view script,
                                            std::string_view property, std::string_view expected,
                                            std::string_view replacement) {
        return std::format(
            "; Plan {}; review these expressions, then execute this block in Multiline.\n"
            "; Getters/setters and supplied expressions execute normally; side effects are not rolled back.\n"
            "{} repairTarget = Game.GetFormEx(0x{:08X}) as {}\n"
            "String propertyRepairReport = \"Expected value or live instance changed; nothing assigned.\"\n"
            "If repairTarget != None\n"
            "    If repairTarget.{} == ({})\n"
            "        If ElderShell.Scripts.CheckPropertyRestore(Game.GetFormEx(0x{:08X}), \"{}\", \"{}\", \"{}\")\n"
            "            repairTarget.{} = ({})\n"
            "            propertyRepairReport = \"Assignment returned; verify the property and its effects.\"\n"
            "        EndIf\n    EndIf\nEndIf\n",
            plan, script, owner, script, property, expected, owner, script, property, plan, property, replacement);
    }
}
