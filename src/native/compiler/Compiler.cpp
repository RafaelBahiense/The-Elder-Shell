#include "Compiler.h"
#include "shell/Syntax.h"
#include "PexMetadata.h"

#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace papyrus_console {
    namespace {
        std::filesystem::path ModuleDirectory(HMODULE module) {
            std::wstring path(32768, L'\0');
            const auto size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (!size || size == path.size())
                throw std::runtime_error("Cannot locate the console DLL");
            path.resize(size);
            return std::filesystem::path(path).parent_path();
        }

        std::filesystem::path PluginDirectory() {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&PluginDirectory), &module))
                throw std::runtime_error("Cannot locate the console DLL");
            return ModuleDirectory(module);
        }

        void Write(const std::filesystem::path& path, std::string_view text) {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

        std::string Read(const std::filesystem::path& path) {
            const auto size = std::filesystem::file_size(path);
            if (size > 32 * 1024 * 1024)
                throw std::runtime_error("Compiler input or artifact exceeds 32 MiB");
            std::ifstream stream(path, std::ios::binary);
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            std::string bytes(static_cast<std::size_t>(size), '\0');
            stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return bytes;
        }

        bool SkyrimBytecode(std::string_view bytes) {
            return bytes.size() > 16 && static_cast<unsigned char>(bytes[0]) == 0xFA &&
                   static_cast<unsigned char>(bytes[1]) == 0x57 && static_cast<unsigned char>(bytes[2]) == 0xC0 &&
                   static_cast<unsigned char>(bytes[3]) == 0xDE && bytes[4] == 3 && bytes[5] == 2;
        }

        std::string Digest(std::string_view text) {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                throw std::runtime_error("Cannot initialize SHA256");
            std::array<UCHAR, 32> bytes{};
            DWORD size = 0, returned = 0;
            auto status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                                            sizeof(size), &returned, 0);
            std::vector<UCHAR> storage(size);
            BCRYPT_HASH_HANDLE hash = nullptr;
            if (status >= 0)
                status = BCryptCreateHash(algorithm, &hash, storage.data(), size, nullptr, 0, 0);
            if (status >= 0)
                status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
                                        static_cast<ULONG>(text.size()), 0);
            if (status >= 0)
                status = BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0);
            if (hash)
                BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            if (status < 0)
                throw std::runtime_error("Cannot hash compiler input");
            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            for (const auto byte : bytes) {
                result += hex[byte >> 4];
                result += hex[byte & 15];
            }
            return result;
        }

    }

    CompilerResult CompileEvaluation(const CompilerInput& input) {
        try {
            const auto tools = PluginDirectory() / "PapyrusConsole";
            const EmbeddedCompiler compiler;
            return CompileEvaluation(input, compiler, tools / "cache");
        } catch (const std::exception& error) {
            return {{}, {}, error.what()};
        }
    }

    CompilerResult CompileEvaluation(const CompilerInput& input, const EmbeddedCompiler& compiler,
                                     const std::filesystem::path& cacheDirectory) {
        try {
            std::string identity = "Skyrim;NoExtensions;v3\n" + compiler.Identity() + "\n" + input.source;
            for (const auto& [name, declaration] : input.declarations)
                identity += "\n" + name + "\n" + declaration;
            const auto digest = Digest(identity);
            const auto script = "PapyrusConsole_Eval_" + digest;
            // Keep compiler paths short without changing Windows path settings.
            const auto workspace = cacheDirectory / digest.substr(0, 16);
            std::filesystem::create_directories(workspace / "imports");
            auto source = input.source;
            const auto marker = source.find("PapyrusConsole_Evaluation");
            if (marker == std::string::npos)
                throw std::runtime_error("Missing evaluation script marker");
            source.replace(marker, std::string_view("PapyrusConsole_Evaluation").size(), script);
            const auto sourcePath = workspace / (script + ".psc");
            const auto bytecode = workspace / (script + ".pex");
            const auto receipt = workspace / "bytecode.sha256";
            bool cached = false;
            if (std::filesystem::is_regular_file(bytecode) && std::filesystem::is_regular_file(sourcePath) &&
                std::filesystem::is_regular_file(receipt)) {
                const auto bytes = Read(bytecode);
                cached = Read(sourcePath) == source && SkyrimBytecode(bytes) && Read(receipt) == Digest(bytes);
            }
            if (!cached) {
                Write(sourcePath, source);
                Write(workspace / "Evaluation.flg", "Flag Hidden 0 { Script Property }\n");
                for (const auto& [name, declaration] : input.declarations) {
                    if (!Identifier(name))
                        throw std::runtime_error("Invalid import script name");
                    Write(workspace / "imports" / (name + ".psc"), declaration);
                }
                const auto report = compiler.Compile(
                    {sourcePath, workspace / "Evaluation.flg", workspace, {workspace, workspace / "imports"}});
                if (!report.succeeded || !std::filesystem::is_regular_file(bytecode))
                    return {
                        {},
                        {},
                        report.diagnostics.empty() ? "Caprica did not produce expected bytecode" : report.diagnostics};
                const auto bytes = NormalizePexMetadata(Read(bytecode));
                if (!SkyrimBytecode(bytes))
                    return {{}, {}, "Caprica did not produce Skyrim PEX bytecode"};
                Write(bytecode, bytes);
                Write(receipt, Digest(bytes));
            }
            return {script, bytecode, {}};
        } catch (const std::exception& error) {
            return {{}, {}, error.what()};
        }
    }

    void CopyEvaluationBytecode(const CompilerResult& result, const std::filesystem::path& scripts) {
        if (!result.error.empty() || !result.script.starts_with("PapyrusConsole_Eval_") || !Identifier(result.script))
            throw std::runtime_error("Invalid compiled evaluation result");
        const auto bytes = Read(result.bytecode);
        if (!SkyrimBytecode(bytes))
            throw std::runtime_error("Invalid evaluation bytecode");
        std::filesystem::create_directories(scripts);
        const auto destination = scripts / (result.script + ".pex");
        if (std::filesystem::exists(destination)) {
            if (Read(destination) != bytes)
                throw std::runtime_error(
                    "Existing generated script differs; refusing to replace code used by saved stacks");
            return;
        }
        std::filesystem::copy_file(result.bytecode, destination);
    }

    std::filesystem::path GameScriptDirectory() {
        return ModuleDirectory(nullptr) / "Data" / "Scripts";
    }
}
