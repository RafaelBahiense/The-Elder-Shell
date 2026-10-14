#include "Compiler.h"
#include "Syntax.h"
#include "PexMetadata.h"

#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace papyrus_console {
    namespace {
        struct Handle {
            HANDLE value = nullptr;
            Handle() = default;

            explicit Handle(HANDLE handle) : value(handle) {}

            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;

            ~Handle() {
                if (value && value != INVALID_HANDLE_VALUE)
                    CloseHandle(value);
            }
        };

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

        std::wstring Quote(std::wstring_view argument) {
            std::wstring result = L"\"";
            std::size_t slashes = 0;
            for (const auto c : argument) {
                if (c == L'\\') {
                    ++slashes;
                    continue;
                }
                result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                result += c;
                slashes = 0;
            }
            result.append(slashes * 2, L'\\');
            return result + L'"';
        }

        std::pair<DWORD, std::string> RunCompiler(const std::filesystem::path& executable,
                                                  const std::vector<std::wstring>& arguments,
                                                  const std::filesystem::path& working) {
            SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
            Handle output, write, input;
            if (!CreatePipe(&output.value, &write.value, &security, 0) ||
                !SetHandleInformation(output.value, HANDLE_FLAG_INHERIT, 0))
                throw std::runtime_error("Cannot create compiler output pipe");
            input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (input.value == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Cannot open compiler input");
            // Inherit only these three standard handles, not unrelated Skyrim handles.
            SIZE_T attributeSize = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
            if (attributeSize == 0)
                throw std::runtime_error("Missing compiler process attribute size");
            std::vector<std::byte> storage(attributeSize);
            auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize))
                throw std::runtime_error("Cannot initialize compiler process attributes");
            HANDLE inherited[]{write.value, input.value};
            const bool configured = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                              inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            startup.StartupInfo.wShowWindow = SW_HIDE;
            startup.StartupInfo.hStdOutput = write.value;
            startup.StartupInfo.hStdError = write.value;
            startup.StartupInfo.hStdInput = input.value;
            startup.lpAttributeList = attributes;
            std::wstring command = Quote(executable.wstring());
            for (const auto& argument : arguments)
                command += L" " + Quote(argument);
            PROCESS_INFORMATION process{};
            const bool created = configured && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                                                              TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                                              nullptr, working.c_str(), &startup.StartupInfo, &process);
            const auto error = GetLastError();
            DeleteProcThreadAttributeList(attributes);
            if (!created)
                throw std::runtime_error("Cannot launch Caprica (Windows error " + std::to_string(error) + ")");
            Handle child{process.hProcess}, thread{process.hThread};
            CloseHandle(write.value);
            write.value = nullptr;
            std::string diagnostics;
            std::array<char, 4096> buffer{};
            DWORD count = 0;
            const auto started = GetTickCount64();
            bool exited = false;
            for (;;) {
                if (!exited && GetTickCount64() - started > 30000) {
                    // This terminates only our compiler process, never a VM stack.
                    if (!TerminateProcess(child.value, ERROR_TIMEOUT))
                        throw std::runtime_error("Cannot stop the timed-out compiler");
                    WaitForSingleObject(child.value, 5000);
                    throw std::runtime_error("Caprica compilation exceeded 30 seconds");
                }
                DWORD available = 0;
                if (PeekNamedPipe(output.value, nullptr, 0, nullptr, &available, nullptr) && available) {
                    if (ReadFile(output.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) &&
                        count && diagnostics.size() < 32768)
                        diagnostics.append(buffer.data(), std::min<std::size_t>(count, 32768 - diagnostics.size()));
                    continue;
                }
                if (exited)
                    break;
                if (WaitForSingleObject(child.value, 20) == WAIT_OBJECT_0) {
                    exited = true;
                    continue;
                }
            }
            DWORD exit = 1;
            if (!GetExitCodeProcess(child.value, &exit))
                throw std::runtime_error("Cannot read compiler exit status");
            return {exit, std::move(diagnostics)};
        }
    }

    CompilerResult CompileEvaluation(const CompilerInput& input) {
        try {
            const auto tools = PluginDirectory() / "PapyrusConsole";
            const auto executable = tools / "Caprica.exe";
            if (!std::filesystem::is_regular_file(executable))
                return {{}, {}, "Caprica.exe is missing; install the PapyrusConsole compiler folder beside the DLL"};
            std::string identity = "Skyrim;Caprica;NoExtensions;v2\n" + Digest(Read(executable)) + "\n" + input.source;
            for (const auto& [name, declaration] : input.declarations)
                identity += "\n" + name + "\n" + declaration;
            const auto digest = Digest(identity);
            const auto script = "PapyrusConsole_Eval_" + digest;
            // Keep compiler paths short without changing Windows path settings.
            const auto workspace = tools / "cache" / digest.substr(0, 16);
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
                const auto [exit, diagnostics] =
                    RunCompiler(executable,
                                {L"--game=skyrim", L"--ignorecwd", L"--enable-language-extensions=false", L"--quiet",
                                 L"--import=" + workspace.wstring(), L"--import=" + (workspace / "imports").wstring(),
                                 L"--flags=" + (workspace / "Evaluation.flg").wstring(),
                                 L"--output=" + workspace.wstring(), sourcePath.wstring()},
                                workspace);
                if (exit != 0 || !std::filesystem::is_regular_file(bytecode))
                    return {{},
                            {},
                            diagnostics.empty() ? "Caprica failed (exit " + std::to_string(exit) + ")" : diagnostics};
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
