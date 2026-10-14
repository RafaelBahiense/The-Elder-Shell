#pragma once

#include "Syntax.h"

namespace papyrus_console {
    inline constexpr std::size_t MaximumArrayElements = 128;
    inline constexpr std::size_t MaximumAccessSteps = 16;

    enum class ValueKind { Integer, Float, Boolean, String, None, Form, Reference, Array, BareText };
    enum class AccessKind { Property, Index };

    struct AccessStep {
        AccessKind kind = AccessKind::Property;
        std::string name;
        std::uint32_t index = 0;

        static AccessStep Property(std::string name) {
            return {AccessKind::Property, std::move(name), 0};
        }

        static AccessStep Index(std::uint32_t index) {
            return {AccessKind::Index, {}, index};
        }

        bool operator==(const AccessStep&) const = default;
    };

    struct ValueExpression {
        ValueKind kind = ValueKind::BareText;
        std::string text;
        std::string elementType;
        std::vector<AccessStep> accesses;
        std::vector<ValueExpression> elements;
    };

    class ValueParser {
    public:
        ValueParser(const std::vector<std::string>& tokens, const std::vector<bool>& quotes, std::size_t position)
            : tokens_(tokens), quotes_(quotes), position_(position) {}

        std::size_t Position() const {
            return position_;
        }

        const std::string& Error() const {
            return error_;
        }

        std::optional<ValueExpression> Parse(bool bareReference = false) {
            if (position_ == tokens_.size())
                return Fail("Expected a value");
            ValueExpression value;
            const auto token = tokens_[position_];
            const bool quoted = quotes_[position_++];
            value.text = token;
            if (quoted) {
                value.kind = ValueKind::String;
                return value;
            }
            if (token == "[")
                return Array({});
            if (!bareReference && Identifier(token) && At("[")) {
                ++position_;
                return Array(token);
            }
            if (Equal(token, "true") || Equal(token, "false")) {
                value.kind = ValueKind::Boolean;
                return value;
            }
            if (Equal(token, "none")) {
                value.kind = ValueKind::None;
                return value;
            }
            if (token.starts_with('@')) {
                if (!FormID(token))
                    return Fail("Invalid form/reference ID");
                value.kind = ValueKind::Form;
                return value;
            }
            if (NumberArgument<std::int32_t>(token)) {
                value.kind = ValueKind::Integer;
                return value;
            }
            const bool number = !token.empty() && ((token[0] >= '0' && token[0] <= '9') || token[0] == '-' ||
                                                   token[0] == '+' || token[0] == '.');
            if (number) {
                if (token.find_first_of(".eE") == std::string::npos || !NumberArgument<float>(token))
                    return Fail("Number must be finite and fit its declared type");
                value.kind = ValueKind::Float;
                return value;
            }
            if (token.starts_with('$') || token.find('.') != std::string::npos || bareReference) {
                value.kind = ValueKind::Reference;
                if (!Path(token, value, true))
                    return std::nullopt;
                while (position_ < tokens_.size()) {
                    if (At("[")) {
                        ++position_;
                        if (position_ == tokens_.size() || quotes_[position_])
                            return Fail("Array index must be a nonnegative integer");
                        const auto index = NumberArgument<std::uint32_t>(tokens_[position_++]);
                        if (!index || !At("]"))
                            return Fail("Use a nonnegative integer index inside []");
                        ++position_;
                        value.accesses.push_back(AccessStep::Index(*index));
                    } else if (!quotes_[position_] && tokens_[position_].starts_with('.')) {
                        const auto suffix = tokens_[position_++].substr(1);
                        if (!Path(suffix, value, false))
                            return std::nullopt;
                    } else {
                        break;
                    }
                    if (value.accesses.size() > MaximumAccessSteps)
                        return Fail("At most 16 property/index steps are supported");
                }
                return value;
            }
            if (token == "," || token == ")" || token == "(" || token == "]" || token == "=")
                return Fail("Expected a value; nested calls are unsupported");
            return value;
        }

    private:
        bool At(std::string_view token) const {
            return position_ < tokens_.size() && !quotes_[position_] && tokens_[position_] == token;
        }

        std::optional<ValueExpression> Fail(std::string message) {
            error_ = std::move(message);
            return std::nullopt;
        }

        bool Path(std::string_view path, ValueExpression& value, bool root) {
            if (root && path.starts_with('$'))
                path.remove_prefix(1);
            while (true) {
                const auto end = path.find('.');
                const auto name = path.substr(0, end);
                if (!Identifier(name)) {
                    error_ = "Invalid variable/property name";
                    return false;
                }
                if (root) {
                    value.text = name;
                    root = false;
                } else {
                    if (value.accesses.size() == MaximumAccessSteps) {
                        error_ = "At most 16 property/index steps are supported";
                        return false;
                    }
                    value.accesses.push_back(AccessStep::Property(std::string(name)));
                }
                if (end == std::string_view::npos)
                    return true;
                path.remove_prefix(end + 1);
            }
        }

        std::optional<ValueExpression> Array(std::string type) {
            ValueExpression value;
            value.kind = ValueKind::Array;
            value.elementType = std::move(type);
            if (At("]")) {
                ++position_;
                return value;
            }
            while (position_ < tokens_.size()) {
                if (value.elements.size() == MaximumArrayElements)
                    return Fail("Array literals support at most 128 elements");
                auto element = Parse();
                if (!element)
                    return std::nullopt;
                if (element->kind == ValueKind::Array)
                    return Fail("Papyrus does not support nested arrays");
                value.elements.push_back(std::move(*element));
                if (At("]")) {
                    ++position_;
                    return value;
                }
                if (!At(","))
                    return Fail("Separate array elements with commas and close with ]");
                ++position_;
                if (At("]"))
                    return Fail("A trailing comma needs an array element");
            }
            return Fail("Close the array literal with ]");
        }

        const std::vector<std::string>& tokens_;
        const std::vector<bool>& quotes_;
        std::size_t position_;
        std::string error_;
    };
}
