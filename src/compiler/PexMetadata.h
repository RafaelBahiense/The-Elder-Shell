#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace elder_shell {
    // Skyrim PEX 3.2 is big-endian. Preserve bytecode and debug line mappings;
    // normalize only build metadata so recompilation cannot change a saved type.
    // Layout: github.com/Orvid/Caprica/blob/v0.3.0/Caprica/pex/PexFile.cpp
    inline std::string NormalizePexMetadata(std::string_view bytes) {
        constexpr unsigned char header[]{0xFA, 0x57, 0xC0, 0xDE, 3, 2, 0, 1};
        if (bytes.size() < 16 ||
            !std::equal(std::begin(header), std::end(header), reinterpret_cast<const unsigned char*>(bytes.data())))
            throw std::runtime_error("Expected Skyrim PEX 3.2 bytecode");
        std::size_t offset = 16;
        const auto require = [&](std::size_t count) {
            if (count > bytes.size() - offset)
                throw std::runtime_error("Truncated PEX metadata");
        };
        const auto number = [&]() {
            require(2);
            const auto result = static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[offset]) << 8) |
                                                           static_cast<unsigned char>(bytes[offset + 1]));
            offset += 2;
            return result;
        };
        const auto text = [&]() {
            const auto length = number();
            require(length);
            const auto result = bytes.substr(offset, length);
            offset += length;
            return result;
        };
        auto filename = text();
        const auto separator = filename.find_last_of("/\\");
        if (separator != std::string_view::npos)
            filename.remove_prefix(separator + 1);
        if (filename.empty() || !filename.ends_with(".psc"))
            throw std::runtime_error("Invalid PEX source filename");
        text();  // Compiler user.
        text();  // Compiler machine.
        const auto bodyOffset = offset;
        const auto count = number();
        for (std::uint16_t i = 0; i < count; ++i)
            text();
        require(1);
        const auto debug = static_cast<unsigned char>(bytes[offset++]);
        if (debug > 1)
            throw std::runtime_error("Invalid PEX debug flag");
        std::string body(bytes.substr(bodyOffset));
        if (debug == 1) {
            require(8);
            std::fill_n(body.begin() + static_cast<std::ptrdiff_t>(offset - bodyOffset), 8, '\0');
        }
        std::string result(bytes.substr(0, 8));
        result.append(8, '\0');
        result += static_cast<char>(filename.size() >> 8);
        result += static_cast<char>(filename.size() & 255);
        result += filename;
        result.append(4, '\0');
        result += body;
        return result;
    }
}
