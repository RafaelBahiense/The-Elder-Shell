#pragma once

#include "Evaluation.h"

namespace papyrus_console {
    struct FunctionDeclaration {
        std::string name, result;
        std::vector<SourceParameter> parameters;
        bool global = false;
    };

    struct PropertyDeclaration {
        std::string name, type;
        bool readable = false, writable = false;
    };

    struct ScriptDeclaration {
        std::string name, parent;
        std::vector<FunctionDeclaration> functions;
        std::vector<PropertyDeclaration> properties;
    };

    inline bool DeclarationType(std::string_view type) {
        if (type.ends_with("[]"))
            type.remove_suffix(2);
        // None is a value, not a Papyrus declaration type. The VM can report
        // it when a property's or parameter's optional script type is missing.
        return Identifier(type) && !Equal(type, "none");
    }

    // These are compiler imports only. They are never compiled or installed as
    // replacements for the real scripts; calls resolve the original VM types.
    inline std::string DeclarationSource(const ScriptDeclaration& declaration) {
        std::string source = "Scriptname " + declaration.name;
        if (!declaration.parent.empty())
            source += " extends " + declaration.parent;
        source += "\n";
        for (const auto& function : declaration.functions) {
            if ((!function.result.empty() && !DeclarationType(function.result)) ||
                !std::ranges::all_of(function.parameters,
                                     [](const auto& parameter) { return DeclarationType(parameter.type); }))
                continue;
            if (!function.result.empty())
                source += function.result + " ";
            source += "Function " + function.name + "(";
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                if (i)
                    source += ", ";
                source += function.parameters[i].type + " " + function.parameters[i].name;
            }
            source += ")";
            if (function.global)
                source += " Global";
            source += " Native\n";
        }
        for (const auto& property : declaration.properties) {
            if (!DeclarationType(property.type))
                continue;
            source += property.type + " Property " + property.name + "\n";
            // A fake AutoReadOnly initializer could be constant-folded into
            // evaluation bytecode. Describe accessors instead of inventing values.
            if (property.readable)
                source += property.type + " Function Get() Native\n";
            if (property.writable)
                source += "Function Set(" + property.type + " value) Native\n";
            source += "EndProperty\n";
        }
        return source;
    }
}
