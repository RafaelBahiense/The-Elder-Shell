#pragma once

#include "Requests.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace papyrus_console {
    template <class Value>
    class Variables {
    public:
        struct Binding {
            Ticket owner;
            std::optional<Value> value;
            bool pending = true;
        };

        bool Reserve(std::string_view name, Ticket owner) {
            const auto key = Key(name);
            if (!entries_.contains(key) && entries_.size() >= 64)
                return false;
            entries_.insert_or_assign(key, Binding{owner, {}, true});
            return true;
        }

        void Complete(std::string_view name, Ticket owner, Value value) {
            if (auto* binding = Owned(name, owner)) {
                binding->value = std::move(value);
                binding->pending = false;
            }
        }

        void Reject(Ticket owner) {
            for (auto& [name, binding] : entries_)
                if (binding.pending && Same(binding.owner, owner)) {
                    binding.value.reset();
                    binding.pending = false;
                }
        }

        const Binding* Find(std::string_view name) const {
            const auto found = entries_.find(Key(name));
            return found == entries_.end() ? nullptr : &found->second;
        }

        bool Unset(std::string_view name) {
            return entries_.erase(Key(name)) != 0;
        }

        void Clear() {
            entries_.clear();
        }

        const auto& Entries() const {
            return entries_;
        }

    private:
        static std::string Key(std::string_view name) {
            std::string key(name);
            for (auto& c : key)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            return key;
        }

        static bool Same(Ticket left, Ticket right) {
            return left.id == right.id && left.generation == right.generation;
        }

        Binding* Owned(std::string_view name, Ticket owner) {
            const auto found = entries_.find(Key(name));
            return found != entries_.end() && found->second.pending && Same(found->second.owner, owner) ? &found->second
                                                                                                        : nullptr;
        }

        std::map<std::string, Binding> entries_;
    };
}
