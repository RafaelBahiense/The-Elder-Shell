#pragma once

#include "ExpressionTokens.h"
#include "Requests.h"

namespace elder_shell {
    struct InspectedProperty {
        std::string name, type;
        bool readable = false;
    };

    struct InspectedValue {
        std::string expression, type, summary;
        std::size_t length = 0;
        bool array = false, truncated = false;
        std::vector<InspectedValue> elements;
        std::vector<InspectedProperty> properties;
    };

    struct InspectedVariable {
        std::string name;
        Ticket owner;
        InspectedValue value;
    };

    struct VariableInspection {
        std::vector<InspectedVariable> variables;
        std::string status = "Not refreshed";
        bool refreshing = false;
    };

    struct PropertyInspection {
        std::string variable, expression;
        Ticket owner;
    };

    struct InspectionRead {
        Ticket ticket;
        std::string error;
    };

    inline bool SameInspectionOwner(Ticket a, Ticket b) {
        return a.id == b.id && a.generation == b.generation;
    }

    // UI snapshots are bounded independently of the size of a retained VM value.
    struct InspectionBudget {
        std::size_t values = 1024, properties = 2048;

        bool TakeValue(std::size_t depth) {
            if (values == 0 || depth > 4)
                return false;
            --values;
            return true;
        }

        bool TakeProperty() {
            if (properties == 0)
                return false;
            --properties;
            return true;
        }
    };

    inline bool ValidPropertyInspection(const PropertyInspection& inspection) {
        if (!Identifier(inspection.variable) || inspection.expression.size() > MaximumCommandLength)
            return false;
        try {
            const auto tokens = ExpressionTokens(inspection.expression);
            if (tokens.empty() || !Equal(tokens.front().text, inspection.variable))
                return false;
            std::size_t i = 1;
            while (i + 2 < tokens.size() && tokens[i].text == "[") {
                const auto& index = tokens[i + 1].text;
                if (index.empty() || index.find_first_not_of("0123456789") != std::string::npos ||
                    tokens[i + 2].text != "]")
                    return false;
                i += 3;
            }
            return i + 2 == tokens.size() && tokens[i].text == "." && !tokens[i + 1].quoted &&
                   Identifier(tokens[i + 1].text);
        } catch (const std::exception&) {
            return false;
        }
    }
}
