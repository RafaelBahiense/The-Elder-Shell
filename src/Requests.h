#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace papyrus_console {
    enum class Status { Queued, Compiling, Submitted, Completed, Rejected, Abandoned };

    struct Ticket {
        std::uint64_t id = 0, generation = 0;
    };

    struct Entry {
        Ticket ticket;
        std::string command;
        Status status = Status::Queued;
        std::string result;
    };

    inline const char* StatusName(Status status) {
        switch (status) {
            case Status::Queued:
                return "Queued";
            case Status::Submitted:
                return "Submitted to VM";
            case Status::Compiling:
                return "Compiling";
            case Status::Completed:
                return "Completed";
            case Status::Rejected:
                return "Rejected";
            case Status::Abandoned:
                return "Abandoned after load";
        }
        return "Unknown";
    }

    // The engine/UI wrapper owns synchronization; this model is engine-independent.
    class Requests {
    public:
        std::optional<Ticket> Begin(std::string command) {
            if (std::ranges::count_if(entries_, [](const Entry& entry) { return Pending(entry.status); }) >= 32)
                return std::nullopt;
            Ticket ticket{++nextID_, generation_};
            entries_.push_back({ticket, std::move(command)});
            Trim();
            return ticket;
        }

        bool Current(Ticket ticket) const {
            return ticket.generation == generation_ && std::ranges::any_of(entries_, [ticket](const Entry& entry) {
                       return entry.ticket.id == ticket.id && Pending(entry.status);
                   });
        }

        void Submitted(Ticket ticket) {
            Update(ticket, Status::Submitted, "Execution may wait until the menu closes");
        }

        void Compiling(Ticket ticket) {
            Update(ticket, Status::Compiling, "Compiling Papyrus with Caprica");
        }

        void Complete(Ticket ticket, std::string result) {
            Update(ticket, Status::Completed, std::move(result));
        }

        void Reject(Ticket ticket, std::string reason) {
            Update(ticket, Status::Rejected, std::move(reason));
        }

        void Loaded() {
            ++generation_;
            for (auto& entry : entries_)
                if (Pending(entry.status)) {
                    entry.status = Status::Abandoned;
                    entry.result = "Game session changed; request will not be replayed";
                }
        }

        const std::vector<Entry>& Entries() const {
            return entries_;
        }

        void ClearFinished() {
            std::erase_if(entries_, [](const Entry& entry) { return !Pending(entry.status); });
        }

    private:
        static bool Pending(Status status) {
            return status == Status::Queued || status == Status::Compiling || status == Status::Submitted;
        }

        void Update(Ticket ticket, Status status, std::string result) {
            if (!Current(ticket))
                return;
            for (auto& entry : entries_)
                if (entry.ticket.id == ticket.id) {
                    entry.status = status;
                    entry.result = std::move(result);
                    break;
                }
        }

        void Trim() {
            while (entries_.size() > 128) {
                const auto finished =
                    std::ranges::find_if(entries_, [](const Entry& entry) { return !Pending(entry.status); });
                if (finished == entries_.end())
                    break;
                entries_.erase(finished);
            }
        }

        std::uint64_t generation_ = 0, nextID_ = 0;
        std::vector<Entry> entries_;
    };
}
