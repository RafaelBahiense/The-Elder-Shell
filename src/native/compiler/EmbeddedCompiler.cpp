#include "EmbeddedCompiler.h"

#include <common/CapricaConfig.h>
#include <common/CapricaReportingContext.h>
#include <common/FakeScripts.h>
#include <common/parser/CapricaUserFlagsParser.h>
#include <papyrus/PapyrusCompilationContext.h>

#include <memory>
#include <mutex>

namespace papyrus_console {
    namespace {
        using Node = caprica::papyrus::PapyrusCompilationNode;
        using Context = caprica::papyrus::PapyrusCompilationContext;
        namespace conf = caprica::conf;
        std::mutex compilerMutex;
        thread_local std::chrono::steady_clock::time_point deadline;

        void CheckDeadline() {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Embedded compilation exceeded its time limit");
        }

        void ConfigureSkyrim() {
            conf::General::quietCompile = true;
            conf::General::compileInParallel = false;
            conf::Papyrus::game = caprica::GameID::Skyrim;
            conf::Papyrus::enableLanguageExtensions = false;
            // Match the executable's default Champollion compatibility profile.
            conf::Papyrus::allowCompilerIdentifiers = true;
            conf::Papyrus::allowDecompiledStructNameRefs = true;
            conf::Papyrus::allowNegativeLiteralAsBinaryOp = true;
            conf::Papyrus::userFlagsDefinition = {};
            conf::CodeGeneration::enableCKOptimizations = true;
            conf::CodeGeneration::emitDebugInfo = true;
            conf::Performance::asyncFileRead = false;
            conf::Performance::asyncFileWrite = false;
            conf::EngineLimits::maxArrayLength = 128;
            conf::EngineLimits::maxFunctionsInEmptyStatePerObject = 2047;
            conf::EngineLimits::maxFunctionsPerState = 511;
            conf::EngineLimits::maxInitialValuesPerObject = 1023;
            conf::EngineLimits::maxNamedStatesPerObject = 127;
            conf::EngineLimits::maxParametersPerFunction = 511;
            conf::EngineLimits::maxPropertiesPerObject = 1023;
            conf::EngineLimits::maxStaticFunctionsPerObject = 511;
            conf::EngineLimits::maxUserFlags = 31;
            conf::EngineLimits::maxVariablesPerObject = 1023;
        }

        class CompilationSession {
        public:
            CompilationSession(std::string& diagnostics, std::chrono::milliseconds timeLimit) {
                ConfigureSkyrim();
                Context::resetSession();
                caprica::CapricaReportingContext::diagnosticSink = &diagnostics;
                deadline = std::chrono::steady_clock::now() + timeLimit;
                caprica::CapricaReportingContext::checkpoint = &CheckDeadline;
            }

            ~CompilationSession() {
                // Namespace keys reference node strings: clear before deleting nodes.
                Context::resetSession();
                nodes_.clear();
                caprica::CapricaReportingContext::diagnosticSink = nullptr;
                caprica::CapricaReportingContext::checkpoint = nullptr;
            }

            Node& Add(std::string path, Node::NodeType type, const std::filesystem::path& output, std::size_t size) {
                CheckDeadline();
                auto filename = std::filesystem::path(path).filename().string();
                auto node = std::make_unique<Node>(&jobs_, type, std::move(filename), output.string(), std::move(path),
                                                   0, size);
                auto& result = *node;
                nodes_.push_back(std::move(node));
                caprica::caseless_unordered_identifier_ref_map<Node*> imports;
                imports.emplace(caprica::identifier_ref(result.baseName), &result);
                Context::pushNamespaceFullContents("", std::move(imports));
                return result;
            }

            void AddImports(const CompilationRequest& request) {
                for (const std::string path :
                     {"fake://skyrim/__ScriptObject.psc", "fake://skyrim/DLC1SCWispWallScript.psc"})
                    Add(path, Node::NodeType::PapyrusImport, {},
                        caprica::FakeScripts::getSizeOfFakeScript(path, caprica::GameID::Skyrim));

                for (const auto& directory : request.imports) {
                    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".psc" &&
                            entry.path() != request.source)
                            Add(entry.path().string(), Node::NodeType::PapyrusImport, {}, entry.file_size());
                    }
                }
            }

        private:
            // No Caprica workers are started. Dependencies run through await on
            // the caller's compiler worker; the queue never owns these nodes.
            caprica::CapricaJobManager jobs_;
            std::vector<std::unique_ptr<Node>> nodes_;
        };
    }

    std::string EmbeddedCompiler::Identity() const {
        return "Caprica;embedded;" PAPYRUS_CONSOLE_CAPRICA_PROFILE;
    }

    CompilationReport EmbeddedCompiler::Compile(const CompilationRequest& request) const {
        std::lock_guard lock(compilerMutex);
        CompilationReport report;
        try {
            CompilationSession session(report.diagnostics, timeLimit_);
            caprica::CapricaReportingContext flagsContext(request.flags.string());
            caprica::parser::CapricaUserFlagsParser flags(flagsContext, request.flags.string());
            flags.parseUserFlags(conf::Papyrus::userFlagsDefinition);
            flagsContext.exitIfErrors();
            session.AddImports(request);
            auto& source = session.Add(request.source.string(), Node::NodeType::PapyrusCompile, request.output,
                                       std::filesystem::file_size(request.source));
            source.awaitWrite();
            report.succeeded = true;
        } catch (const std::exception& error) {
            if (*error.what())
                report.diagnostics += error.what();
            if (report.diagnostics.empty())
                report.diagnostics = "Embedded compilation failed";
        }
        return report;
    }
}
