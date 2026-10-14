#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Compiler.h"
#include "ScriptDeclarations.h"
#include "PexMetadata.h"
#include <fstream>

using namespace papyrus_console;

namespace {
    EvaluationStatement Statement(std::string_view text) {
        const auto parsed = ParseEvaluation(text);
        REQUIRE(std::holds_alternative<EvaluationStatement>(parsed));
        return std::get<EvaluationStatement>(parsed);
    }

    std::string Bytecode(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    TEST_CASE("Caprica compiles arithmetic and reuses immutable bytecode") {
        CompilerInput input{EvaluationSource(Statement("Float answer = amount * 2.0"), {{"Float", "amount"}},
                                             "PapyrusConsole_Evaluation"),
                            {}};
        const auto result = CompileEvaluation(input);
        INFO(result.error);
        REQUIRE(result.error.empty());
        CHECK(result.script.starts_with("PapyrusConsole_Eval_"));
        CHECK(std::filesystem::file_size(result.bytecode) > 40);
        const auto timestamp = std::filesystem::last_write_time(result.bytecode);
        const auto repeated = CompileEvaluation(input);
        CHECK(repeated.error.empty());
        CHECK(repeated.script == result.script);
        CHECK(std::filesystem::last_write_time(result.bytecode) == timestamp);
    }

    CompilerInput ObjectInput(std::string_view statement) {
        ScriptDeclaration record{
            "FixtureRecord", {}, {{"Describe", "String", {}, false}}, {{"Value", "Float", true, true}}};
        ScriptDeclaration container{"FixtureContainer",
                                    {},
                                    {{"GetRecord", "FixtureRecord", {}, false}},
                                    {{"record", "FixtureRecord", true, false}}};
        return {
            EvaluationSource(Statement(statement), {{"FixtureContainer", "container"}}, "PapyrusConsole_Evaluation"),
            {{record.name, DeclarationSource(record)}, {container.name, DeclarationSource(container)}}};
    }

    TEST_CASE("Signature imports compile chained calls and custom script property access") {
        const auto chain = CompileEvaluation(ObjectInput("String description = container.GetRecord().Describe()"));
        INFO(chain.error);
        CHECK(chain.error.empty());
        const auto property = CompileEvaluation(ObjectInput("Float amount = container.record.Value"));
        INFO(property.error);
        CHECK(property.error.empty());
    }

    TEST_CASE("Compiler failures preserve diagnostics and produce no runnable result") {
        const auto result = CompileEvaluation(ObjectInput("String description = container.MissingFunction()"));
        CHECK_FALSE(result.error.empty());
        CHECK(result.script.empty());
        CHECK(result.bytecode.empty());
        CHECK(result.error.find("MissingFunction") != std::string::npos);
    }

    TEST_CASE("Native inheritance imports support global call chains and custom script casts") {
        const std::vector<ScriptDeclaration> declarations{
            {"Form", {}, {{"GetName", "String", {}, false}}, {}},
            {"ObjectReference", "Form", {}, {}},
            {"Actor", "ObjectReference", {{"GetLevel", "Int", {}, false}}, {}},
            {"Quest", "Form", {}, {}},
            {"ExampleController", "Quest", {{"GetActor", "Actor", {}, false}}, {}},
            {"Game",
             {},
             {{"GetPlayer", "Actor", {}, true},
              {"GetFormFromFile", "Form", {{"Int", "id"}, {"String", "plugin"}}, true}},
             {}}};
        CompilerInput input;
        for (const auto& declaration : declarations)
            input.declarations.emplace_back(declaration.name, DeclarationSource(declaration));
        input.source =
            EvaluationSource(Statement("Int level = Game.GetPlayer().GetLevel()"), {}, "PapyrusConsole_Evaluation");
        const auto chain = CompileEvaluation(input);
        INFO(chain.error);
        CHECK(chain.error.empty());
        input.source = EvaluationSource(
            Statement("ExampleController controller = Game.GetFormFromFile(1, \"Example.esp\") as ExampleController"),
            {}, "PapyrusConsole_Evaluation");
        const auto cast = CompileEvaluation(input);
        INFO(cast.error);
        CHECK(cast.error.empty());
    }

    TEST_CASE("Hexadecimal runtime FormIDs preserve all 32 bits in compiled calls") {
        const ScriptDeclaration form{"Form", {}, {}, {}};
        const ScriptDeclaration container{"Actor", "Form", {}, {}};
        const ScriptDeclaration game{"Game", {}, {{"GetFormEx", "Form", {{"Int", "formId"}}, true}}, {}};
        CompilerInput input{EvaluationSource(Statement("Actor npc = Game.GetFormEx(0xFF000123) as Actor"), {},
                                             "PapyrusConsole_Evaluation"),
                            {{form.name, DeclarationSource(form)},
                             {container.name, DeclarationSource(container)},
                             {game.name, DeclarationSource(game)}}};
        const auto result = CompileEvaluation(input);
        INFO(result.error);
        REQUIRE(result.error.empty());
        CHECK(Bytecode(result.bytecode).find(std::string("\xFF\x00\x01\x23", 4)) != std::string::npos);
    }

    TEST_CASE("Unavailable signature dependencies do not block unrelated expressions") {
        const ScriptDeclaration unavailable{"UnavailableDependency", {}, {}, {}};
        const ScriptDeclaration controller{"OptionalController",
                                           {},
                                           {{"GetLevel", "Int", {}, false}},
                                           {{"OptionalLibrary", "UnavailableDependency", true, false}}};
        CompilerInput input{
            EvaluationSource(Statement("Int level = controller.GetLevel()"), {{"OptionalController", "controller"}},
                             "PapyrusConsole_Evaluation"),
            {{controller.name, DeclarationSource(controller)}, {unavailable.name, DeclarationSource(unavailable)}}};
        const auto unrelated = CompileEvaluation(input);
        INFO(unrelated.error);
        CHECK(unrelated.error.empty());

        input.source = EvaluationSource(Statement("Int level = controller.OptionalLibrary.GetLevel()"),
                                        {{"OptionalController", "controller"}}, "PapyrusConsole_Evaluation");
        const auto missingMember = CompileEvaluation(input);
        CHECK_FALSE(missingMember.error.empty());
        CHECK(missingMember.bytecode.empty());
        CHECK(missingMember.error.find("GetLevel") != std::string::npos);
    }

    TEST_CASE("Missing VM types are omitted rather than emitted as None declarations") {
        const ScriptDeclaration controller{
            "MissingTypesController",
            {},
            {{"GetLevel", "Int", {}, false},
             {"MissingArgument", {}, {{"none", "dependency"}}, false},
             {"MissingReturn", "none", {}, false}},
            {{"MissingLibrary", "none", true, true}, {"MissingArray", "none[]", true, false}}};
        const auto source = DeclarationSource(controller);
        CHECK(source.find("MissingLibrary") == std::string::npos);
        CHECK(source.find("MissingArray") == std::string::npos);
        CHECK(source.find("MissingArgument") == std::string::npos);
        CHECK(source.find("MissingReturn") == std::string::npos);

        CompilerInput input{EvaluationSource(Statement("Int level = controller.GetLevel()"),
                                             {{controller.name, "controller"}}, "PapyrusConsole_Evaluation"),
                            {{controller.name, source}}};
        const auto valid = CompileEvaluation(input);
        INFO(valid.error);
        CHECK(valid.error.empty());
        input.source = EvaluationSource(Statement("String library = controller.MissingLibrary"),
                                        {{controller.name, "controller"}}, "PapyrusConsole_Evaluation");
        const auto missing = CompileEvaluation(input);
        CHECK_FALSE(missing.error.empty());
        CHECK(missing.bytecode.empty());
    }

    TEST_CASE("Generated files are immutable and collision failures preserve existing bytecode") {
        const auto result = CompileEvaluation(ObjectInput("String immutable = container.GetRecord().Describe()"));
        REQUIRE(result.error.empty());
        const auto scripts = result.bytecode.parent_path() / "test-scripts";
        CHECK_NOTHROW(CopyEvaluationBytecode(result, scripts));
        CHECK_NOTHROW(CopyEvaluationBytecode(result, scripts));
        const auto destination = scripts / (result.script + ".pex");
        {
            std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
            stream << "collision";
        }
        CHECK_THROWS_AS(CopyEvaluationBytecode(result, scripts), std::runtime_error);
        CHECK(std::filesystem::file_size(destination) == 9);
        // Restore only this test's output, so rerunning the test starts cleanly.
        std::filesystem::remove(destination);
    }

    TEST_CASE("Corrupt cached bytecode is recompiled instead of executed") {
        CompilerInput input{EvaluationSource(Statement("Int restored = 123"), {}, "PapyrusConsole_Evaluation"), {}};
        const auto first = CompileEvaluation(input);
        REQUIRE(first.error.empty());
        const auto original = Bytecode(first.bytecode);
        {
            std::ofstream stream(first.bytecode, std::ios::binary | std::ios::trunc);
            stream << "broken";
        }
        const auto rebuilt = CompileEvaluation(input);
        INFO(rebuilt.error);
        CHECK(rebuilt.error.empty());
        CHECK(rebuilt.script == first.script);
        CHECK(std::filesystem::file_size(rebuilt.bytecode) > 40);
        CHECK(Bytecode(rebuilt.bytecode) == original);
        CHECK(NormalizePexMetadata(original) == original);
        CHECK_THROWS_AS(NormalizePexMetadata("truncated"), std::runtime_error);
    }
}
