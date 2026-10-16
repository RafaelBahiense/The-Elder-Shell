#include "ValueDisplay.h"
#include <set>

namespace elder_shell {
    std::string Display(const RE::BSScript::Variable& value) {
        if (value.IsArray()) {
            auto array = value.GetArray();
            if (!array)
                return "None array";
            std::string text = std::format("{}: {} elements [", value.GetType().TypeAsString(), array->size());
            const auto count = std::min<std::size_t>(array->size(), 128);
            for (std::size_t i = 0; i < count; ++i) {
                if (i)
                    text += ", ";
                text += Display((*array)[static_cast<std::uint32_t>(i)]);
                if (text.size() >= 32768) {
                    text += ", ...";
                    break;
                }
            }
            if (array->size() > count)
                text += ", ...";
            return text + "]";
        }
        if (value.IsInt())
            return std::to_string(value.GetSInt());
        if (value.IsFloat())
            return std::format("{}", value.GetFloat());
        if (value.IsBool())
            return value.GetBool() ? "true" : "false";
        if (value.IsString())
            return std::format("\"{}\"", value.GetString().substr(0, 2048));
        if (value.IsObject()) {
            auto object = value.GetObject();
            return object ? std::format("{} (VM handle {:016X})", object->GetTypeInfo()->GetName(), object->GetHandle())
                          : "None";
        }
        return "None";
    }

    InspectedValue InspectValue(const RE::BSScript::Variable& value, std::string expression, InspectionBudget& budget,
                                std::size_t depth) {
        InspectedValue result;
        result.expression = std::move(expression);
        result.type = value.GetType().TypeAsString();
        if (!budget.TakeValue(depth)) {
            result.summary = "Snapshot limit reached";
            result.truncated = true;
            return result;
        }
        if (value.IsArray()) {
            result.array = true;
            const auto array = value.GetArray();
            if (!array) {
                result.summary = "None array";
                return result;
            }
            result.length = array->size();
            result.summary = std::to_string(result.length) + " elements";
            for (std::uint32_t i = 0; i < array->size() && i < 128 && budget.values; ++i)
                result.elements.push_back(
                    InspectValue((*array)[i], result.expression + "[" + std::to_string(i) + "]", budget, depth + 1));
            result.truncated = result.elements.size() != result.length;
            return result;
        }
        result.summary = Display(value);
        if (!value.IsObject())
            return result;
        const auto object = value.GetObject();
        if (!object || !object->IsInitialized())
            return result;
        std::set<std::string> visited;
        for (auto* type = object->GetTypeInfo(); type && visited.insert(type->name.c_str()).second;
             type = type->GetParent()) {
            if (type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid)
                break;
            const auto* properties = type->GetPropertyIter();
            for (std::uint32_t i = 0; properties && i < type->GetNumProperties(); ++i) {
                const auto& property = properties[i];
                const auto name = property.name.c_str();
                if (!Identifier(name) || std::ranges::any_of(result.properties, [&](const auto& existing) {
                        return Equal(existing.name, name);
                    }))
                    continue;
                if (!budget.TakeProperty()) {
                    result.truncated = true;
                    return result;
                }
                const auto permissions = std::to_underlying(*property.info.permissions);
                result.properties.push_back({name, property.info.type.TypeAsString(), (permissions & 1u) != 0});
            }
        }
        return result;
    }
}
