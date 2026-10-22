#include "shell/Services.h"
#include "compiler/EmbeddedCompiler.h"
#include "compiler/PexMetadata.h"
#include <fstream>
#include <iostream>

namespace {
    std::string Read(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        stream.exceptions(std::ios::badbit);
        if (!stream)
            throw std::runtime_error("Cannot read " + path.string());
        return {std::istreambuf_iterator<char>(stream), {}};
    }

    void Write(const std::filesystem::path& path, std::string_view data) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << data;
    }
}

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: compile_papyrus <Papyrus source directory> <output directory>");
        const auto source = std::filesystem::absolute(argv[1]);
        const auto output = std::filesystem::absolute(argv[2]);
        const auto imports = output / "imports";
        const auto scripts = output / "Scripts";
        std::filesystem::create_directories(scripts);
        // Signature-only compiler inputs. These are never packaged as scripts.
        Write(imports / "Form.psc", "Scriptname Form\n");
        Write(imports / "Quest.psc", "Scriptname Quest extends Form\nBool Function Start() Native\n");
        Write(imports / "ObjectReference.psc", "Scriptname ObjectReference extends Form\n");
        Write(imports / "Actor.psc", "Scriptname Actor extends ObjectReference\n");
        Write(imports / "Package.psc", "Scriptname Package extends Form\n");
        const auto flags = imports / "Services.flg";
        Write(flags, "Flag Hidden 0 { Script Property }\n");
        const elder_shell::EmbeddedCompiler compiler;
        for (const auto script : {elder_shell::services::RootScript, elder_shell::services::ConsoleScript,
                                  elder_shell::services::DiagnosticsScript}) {
            const auto filename = std::string(script);
            const auto result = compiler.Compile({source / (filename + ".psc"), flags, scripts, {imports, source}});
            if (!result.succeeded)
                throw std::runtime_error(filename + ": " + result.diagnostics);
            const auto pex = scripts / (filename + ".pex");
            Write(pex, elder_shell::NormalizePexMetadata(Read(pex)));
        }
        std::cout << "Compiled Elder Shell service scripts.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
