#pragma once

#include <optional>
#include <span>

namespace papyrus_console {
    enum class ValueCategory { None, Integer, Float, Boolean, String, Object, Array };

    inline std::optional<ValueCategory> InferArrayCategory(std::span<const ValueCategory> elements) {
        if (elements.empty())
            return std::nullopt;
        auto category = elements.front();
        for (const auto element : elements) {
            if (element == ValueCategory::Array)
                return std::nullopt;
            if (element == category)
                continue;
            const bool numeric = (category == ValueCategory::Integer || category == ValueCategory::Float) &&
                                 (element == ValueCategory::Integer || element == ValueCategory::Float);
            const bool objects = (category == ValueCategory::Object || category == ValueCategory::None) &&
                                 (element == ValueCategory::Object || element == ValueCategory::None);
            if (numeric)
                category = ValueCategory::Float;
            else if (objects)
                category = ValueCategory::Object;
            else
                return std::nullopt;
        }
        return category == ValueCategory::None ? ValueCategory::Object : category;
    }
}
