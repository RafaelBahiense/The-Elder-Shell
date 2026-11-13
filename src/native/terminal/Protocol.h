#pragma once
#include "shell/Input.h"
#include "shell/Requests.h"
#include "shell/Completion.h"
#include <charconv>
#include <algorithm>
#include <cstddef>
#include <system_error>
#include <cstdint>
#include <string>
#include <string_view>

namespace elder_shell::terminal {
    inline constexpr std::uint32_t ProtocolVersion = 1, MaximumFrame = 8 * 1024 * 1024, MaximumCommand = 65536;

    struct Input {
        ShellInput command;
        std::string error;
        bool interactive = false;
    };

    inline Input Decode(std::string_view frame) {
        Input result;
        const auto split = frame.find('\n');
        const auto operation = frame.substr(0, split);
        if (split == std::string_view::npos || (operation != "input" && operation != "execute")) {
            result.error = "Expected input or execute followed by a newline and Papyrus source.";
            return result;
        }
        const auto source = frame.substr(split + 1);
        if (source.size() > MaximumCommand || source.find('\0') != std::string_view::npos) {
            result.error = "Command exceeds 65536 UTF-8 bytes or contains a null character.";
            return result;
        }
        result.interactive = operation == "input";
        result.command = ReadShellInput(source);
        if (result.command.more && !result.interactive)
            result.error = "Incomplete Papyrus input.";
        else if (!result.command.more && result.command.source.find_first_not_of(" \t\r\n") == std::string::npos)
            result.error = "Enter a Papyrus command.";
        return result;
    }

    inline bool Pending(Status status) {
        return status == Status::Queued || status == Status::Compiling || status == Status::Submitted;
    }

    struct CompletionRequest {
        std::string source, error;
        std::size_t cursor = 0;
    };

    inline CompletionRequest DecodeCompletion(std::string_view frame) {
        CompletionRequest result;
        if (!frame.starts_with("complete\n")) {
            result.error = "Expected complete followed by a UTF-8 byte cursor and source.";
            return result;
        }
        const auto split = frame.find('\n', 9);
        if (split == std::string_view::npos) {
            result.error = "Missing completion cursor or source.";
            return result;
        }
        const auto offset = frame.substr(9, split - 9);
        const auto parsed = std::from_chars(offset.data(), offset.data() + offset.size(), result.cursor);
        result.source = frame.substr(split + 1);
        if (offset.empty() || parsed.ec != std::errc{} || parsed.ptr != offset.data() + offset.size() ||
            result.source.size() > MaximumCommand || result.source.find('\0') != std::string::npos ||
            result.cursor > result.source.size() ||
            (result.cursor < result.source.size() &&
             (static_cast<unsigned char>(result.source[result.cursor]) & 0xC0) == 0x80))
            result.error = "Invalid completion source or UTF-8 byte cursor.";
        return result;
    }

    inline std::string JsonString(std::string_view text) {
        std::string output = "\"";
        constexpr char hex[] = "0123456789abcdef";
        for (const unsigned char character : text) {
            if (character == '"' || character == '\\') {
                output += '\\';
                output += static_cast<char>(character);
            } else if (character < 0x20) {
                output += "\\u00";
                output += hex[character >> 4];
                output += hex[character & 15];
            } else
                output += static_cast<char>(character);
        }
        return output + '"';
    }

    inline std::string EncodeCompletions(std::string_view source, const CompletionResult& result) {
        std::string output =
            "{\"begin\":" + std::to_string(result.context.begin) + ",\"end\":" + std::to_string(result.context.end) +
            ",\"total\":" + std::to_string(result.items.size()) + ",\"signature\":" + JsonString(result.signature) +
            ",\"parameter\":" + JsonString(result.parameter) + ",\"items\":[";
        for (std::size_t index = 0; index < std::min<std::size_t>(result.items.size(), 128); ++index) {
            if (index)
                output += ',';
            const auto& item = result.items[index];
            const auto edit = CompletionInsertion(source, result.context, item);
            output += "{\"name\":" + JsonString(item.name) + ",\"description\":" + JsonString(item.description) +
                      ",\"text\":" + JsonString(edit.text) + ",\"cursor\":" + std::to_string(edit.cursor) + '}';
        }
        return output + "]}";
    }

    inline std::string Encode(const Entry& entry) {
        return std::string(Pending(entry.status) ? "pending\n" : "result\n") + std::to_string(entry.ticket.id) + '\n' +
               std::to_string(entry.ticket.generation) + '\n' + StatusName(entry.status) + '\n' + entry.result;
    }
}
