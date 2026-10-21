#include "GameConsole.h"
#include "ConsoleRuntime.h"
#include "shell/ConsoleOutput.h"
#include <memory>

namespace elder_shell {
    namespace {
        void CompileAndRun(RE::Script* script, RE::TESObjectREFR* selected) {
            // The pinned CommonLib predates the AE 1.6.1130 entry-point move.
            // https://github.com/alandtse/CommonLibVR/blob/ng/src/RE/S/Script.cpp
            const auto version = REL::Module::get().version();
            const auto id =
                ConsoleCompileAndRunID(REL::Module::IsAE(), {version.major(), version.minor(), version.patch()});
            using Execute = void (*)(RE::Script*, RE::ScriptCompiler*, RE::COMPILER_NAME, RE::TESObjectREFR*);
            const REL::Relocation<Execute> execute{REL::ID{id}};
            RE::ScriptCompiler compiler{};
            SKSE::log::info("Console dispatch: runtime={} addressID={}", version.string(), id);
            execute(script, &compiler, RE::COMPILER_NAME::kSystemWindowCompiler, selected);
        }

        class ConsoleMode {
        public:
            ConsoleMode() : tls_(RE::GetStaticTLSData()), previous_(tls_->consoleMode) {
                tls_->consoleMode = true;
            }

            ~ConsoleMode() {
                tls_->consoleMode = previous_;
            }

        private:
            RE::TLSData* tls_;
            bool previous_;
        };
    }

    std::string RunGameConsole(std::string_view command, RE::TESObjectREFR* target) {
        auto* log = RE::ConsoleLog::GetSingleton();
        auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
        std::unique_ptr<RE::Script> script(factory ? factory->Create() : nullptr);
        if (!log || !script)
            throw std::runtime_error("Skyrim console unavailable");

        const std::string before(log->buffer.c_str());
        const std::string lastMessage(log->lastMessage);
        const auto selected = RE::Console::GetSelectedRef();
        script->SetCommand(command);
        {
            ConsoleMode mode;
            CompileAndRun(script.get(), target ? target : selected.get());
        }
        auto output = NewConsoleOutput(before, log->buffer.c_str());
        if (output.empty() && lastMessage != log->lastMessage)
            output = log->lastMessage;
        return output.empty() ? "Console command returned without new output. This does not confirm its effects."
                              : output;
    }
}
