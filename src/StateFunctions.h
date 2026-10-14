#pragma once

#include "StateFunctionLayout.h"
#include <RE/O/ObjectTypeInfo.h>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace papyrus_console {
    inline std::vector<RE::BSScript::ObjectTypeInfo::NamedStateInfo::Func> StateFunctions(
        const RE::BSScript::ObjectTypeInfo& owner, std::uint32_t index) {
        // The pinned CommonLib declares the offset as a 9-bit field. The packed
        // word actually uses 9 bits for count and all remaining bits for the
        // byte offset relative to this state record. Its GetFuncIter truncates
        // offsets >= 512 and can read another state record as a function pointer.
        // Keep this workaround local; do not modify the shared dependency.
        if (owner.linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid || !owner.data ||
            index >= owner.GetNumNamedStates())
            throw std::runtime_error("Named-state metadata is unavailable or unlinked");
        const auto* states = owner.GetNamedStateIter();
        const auto& state = states[index];
        std::uint32_t packed = 0;
        const auto* base = reinterpret_cast<const std::byte*>(&state);
        std::memcpy(&packed, base + sizeof(RE::BSFixedString), sizeof(packed));
        const auto layout = StateFunctionLayout::Decode(packed);
        if (layout.count == 0)
            return {};
        std::size_t total = 0;
        std::optional<std::size_t> functionStart;
        for (std::uint32_t i = 0; i < owner.GetNumNamedStates(); ++i) {
            std::uint32_t word = 0;
            std::memcpy(&word, reinterpret_cast<const std::byte*>(&states[i]) + sizeof(RE::BSFixedString),
                        sizeof(word));
            const auto entry = StateFunctionLayout::Decode(word);
            if (entry.count != 0) {
                const auto start = i * sizeof(*states) + entry.offset;
                if (!functionStart)
                    functionStart = start;
                // Extra metadata may separate the headers and function array.
                // Function blocks themselves remain consecutive, even when
                // their byte offsets are not multiples of eight.
                if (start != *functionStart + total * sizeof(RE::BSScript::ObjectTypeInfo::NamedStateInfo::Func))
                    throw std::runtime_error("Nonconsecutive named-state function blocks: " +
                                             std::string(owner.GetName()));
            }
            total += entry.count;
        }
        const auto position = layout.Position(index, owner.GetNumNamedStates(), total, functionStart);
        if (!position)
            throw std::runtime_error(
                "Named-state function table lies outside its metadata region: " + std::string(owner.GetName()) +
                ", state " + std::to_string(index) + ", count " + std::to_string(layout.count) + ", offset " +
                std::to_string(layout.offset) + ", states " + std::to_string(owner.GetNumNamedStates()) +
                ", functions " + std::to_string(total));
        using Function = RE::BSScript::ObjectTypeInfo::NamedStateInfo::Func;
        const auto* table = reinterpret_cast<const std::byte*>(states);
        std::vector<Function> functions;
        functions.reserve(layout.count);
        for (std::uint32_t i = 0; i < layout.count; ++i) {
            // The engine can place these pointers at a four-byte boundary.
            // Copy their bytes rather than forming unaligned C++ objects.
            RE::BSScript::IFunction* function = nullptr;
            std::memcpy(&function, table + *position + i * sizeof(Function), sizeof(function));
            functions.push_back({RE::BSTSmartPointer<RE::BSScript::IFunction>{function}});
        }
        return functions;
    }
}
