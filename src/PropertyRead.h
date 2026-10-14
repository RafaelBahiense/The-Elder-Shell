#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace papyrus_console {
    // A getter callback resumes exactly one step; a duplicate receipt cannot
    // advance a newer getter or complete a property chain twice.
    template <class Value, class Step = std::string>
    class PropertyRead {
    public:
        PropertyRead(Value value, std::vector<Step> properties)
            : value_(std::move(value)), properties_(std::move(properties)) {}

        const Value& CurrentValue() const {
            return value_;
        }

        std::optional<Step> NextProperty() const {
            return position_ == properties_.size() ? std::nullopt : std::optional<Step>(properties_[position_]);
        }

        bool Advance(Value value) {
            if (waiting_ || !NextProperty())
                return false;
            value_ = std::move(value);
            ++position_;
            return true;
        }

        std::optional<std::uint64_t> Suspend() {
            if (waiting_ || !NextProperty())
                return std::nullopt;
            waiting_ = true;
            return ++receipt_;
        }

        bool Resume(std::uint64_t receipt, Value value) {
            if (!waiting_ || receipt != receipt_)
                return false;
            waiting_ = false;
            return Advance(std::move(value));
        }

    private:
        Value value_;
        std::vector<Step> properties_;
        std::size_t position_ = 0;
        std::uint64_t receipt_ = 0;
        bool waiting_ = false;
    };
}
