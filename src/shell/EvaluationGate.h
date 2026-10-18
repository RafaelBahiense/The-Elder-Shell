#pragma once

#include "Requests.h"

namespace elder_shell {
    // The caller owns synchronization. Only the owning job can release the gate.
    class EvaluationGate {
    public:
        bool Begin(Ticket ticket) {
            if (active_)
                return false;
            active_ = ticket;
            return true;
        }

        bool Release(Ticket ticket) {
            if (!active_ || active_->id != ticket.id || active_->generation != ticket.generation)
                return false;
            active_.reset();
            return true;
        }

        bool PreviousSession(Ticket ticket) const {
            return active_ && active_->generation != ticket.generation;
        }

    private:
        std::optional<Ticket> active_;
    };
}
