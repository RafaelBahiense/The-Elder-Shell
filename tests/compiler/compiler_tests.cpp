#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "shell/Fragment.h"

#include "compiler/Compiler.h"
#include "compiler/ScriptDeclarations.h"
#include "shell/ExpressionTypes.h"
#include "compiler/PexMetadata.h"
#include <Windows.h>
#include <fstream>
#include "compiler/EmbeddedCompiler.h"
#include <Psapi.h>

using namespace elder_shell;

namespace {
    std::filesystem::path EmbeddedWorkspace() {
        std::wstring module(32768, '\0');
        const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        REQUIRE(length > 0);
        module.resize(length);
        return std::filesystem::path(module).parent_path() / "embedded-tests";
    }

    void WriteFixture(const std::filesystem::path& path, std::string_view text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream << text;
    }

    std::size_t PrivateMemory() {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        REQUIRE(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                     sizeof(memory)));
        return memory.PrivateUsage;
    }

    TEST_CASE("Embedded sessions recover from errors and refresh reused import names") {
        const EmbeddedCompiler compiler;
        const auto workspace = EmbeddedWorkspace();
        const CompilationRequest request{
            workspace / "Evaluation.psc", workspace / "Evaluation.flg", workspace, {workspace / "imports"}};
        WriteFixture(request.flags, "Flag Hidden 0 { Script Property }\n");
        WriteFixture(request.imports.front() / "Fixture.psc", "Scriptname Fixture\nInt Function Value() Native\n");
        WriteFixture(request.source,
                     "Scriptname Evaluation Hidden\nInt Function Run(Fixture value) Global\n"
                     "Return value.Value()\nEndFunction\n");
        REQUIRE(compiler.Compile(request).succeeded);

        WriteFixture(request.imports.front() / "Fixture.psc", "Scriptname Fixture\n");
        const auto missing = compiler.Compile(request);
        CHECK_FALSE(missing.succeeded);
        CHECK(missing.diagnostics.find("Value") != std::string::npos);

        WriteFixture(request.source, "Scriptname Evaluation\nInt Function Run(\n");
        CHECK_FALSE(compiler.Compile(request).succeeded);
        WriteFixture(request.source,
                     "Scriptname Evaluation Hidden\nInt Function Run() Global\nReturn 7\nEndFunction\n");
        REQUIRE(compiler.Compile(request).succeeded);

        WriteFixture(request.imports.front() / "Fixture.psc",
                     "Scriptname Fixture\nInt Property Amount Auto\nInt Function Value() Native\n");
        const std::string validSource =
            "Scriptname Evaluation Hidden\nInt Function Run(Fixture value) Global\n"
            "Return value.Value() + value.Amount\nEndFunction\n";
        WriteFixture(request.source, validSource);
        REQUIRE(compiler.Compile(request).succeeded);
        const auto baseline = PrivateMemory();
        for (int iteration = 0; iteration < 500; ++iteration) {
            REQUIRE(compiler.Compile(request).succeeded);
            WriteFixture(request.source, "Scriptname Evaluation\nInt Function Run(\n");
            CHECK_FALSE(compiler.Compile(request).succeeded);
            WriteFixture(request.source, validSource);
        }
        const auto memory = PrivateMemory();
        INFO("Private-memory growth after 1,000 mixed sessions: ", memory > baseline ? memory - baseline : 0);
        CHECK(memory <= baseline + 8 * 1024 * 1024);
    }

    EvaluationStatement Statement(std::string_view text) {
        const auto parsed = ParseEvaluation(text);
        REQUIRE(std::holds_alternative<EvaluationStatement>(parsed));
        return std::get<EvaluationStatement>(parsed);
    }

    std::string Bytecode(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    TEST_CASE("Verified cache hits do not invoke the compiler again") {
        const auto cache = EmbeddedWorkspace() / "cache";
        CompilerInput input{EvaluationSource(Statement("Int result = 123"), {}, mod::info::EvaluationMarker), {}};
        const auto initial = CompileEvaluation(input, EmbeddedCompiler(), cache);
        REQUIRE(initial.error.empty());
        CHECK(CompileEvaluation(input, EmbeddedCompiler(std::chrono::milliseconds(0)), cache).error.empty());
        std::filesystem::remove(initial.bytecode);
        const auto expired = CompileEvaluation(input, EmbeddedCompiler(std::chrono::milliseconds(0)), cache);
        CHECK_FALSE(expired.error.empty());
        CHECK(expired.bytecode.empty());
        CHECK(CompileEvaluation(input, EmbeddedCompiler(), cache).error.empty());
    }

    TEST_CASE("Compiler cache retains thirty entries and refreshes cache hits") {
        const auto cache = EmbeddedWorkspace() / ("cache-limit-" + std::to_string(GetTickCount64()));
        CompilerInput input{EvaluationSource(Statement("Int result = 123"), {}, mod::info::EvaluationMarker), {}};
        const auto initial = CompileEvaluation(input, EmbeddedCompiler(), cache);
        REQUIRE(initial.error.empty());

        const auto oldest = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
        std::filesystem::last_write_time(initial.bytecode.parent_path(), oldest - std::chrono::hours(1));
        for (int i = 0; i < 31; ++i) {
            const auto number = std::to_string(i);
            const auto entry = cache / (std::string(16 - number.size(), '0') + number);
            WriteFixture(entry / "imports" / "Fixture.psc", "fixture");
            std::filesystem::last_write_time(entry, oldest + std::chrono::seconds(i));
        }
        WriteFixture(cache / "notes" / "keep.txt", "unrelated");

        const auto hit = CompileEvaluation(input, EmbeddedCompiler(std::chrono::milliseconds(0)), cache);
        REQUIRE(hit.error.empty());
        CHECK(std::filesystem::exists(initial.bytecode));
        CHECK_FALSE(std::filesystem::exists(cache / "0000000000000000"));
        CHECK_FALSE(std::filesystem::exists(cache / "0000000000000001"));
        CHECK(std::filesystem::exists(cache / "0000000000000002"));
        CHECK(std::filesystem::exists(cache / "notes" / "keep.txt"));
        CHECK(std::distance(std::filesystem::directory_iterator(cache), std::filesystem::directory_iterator{}) == 31);
    }

    TEST_CASE("Failed compilations trim cache without removing installed scripts") {
        const auto workspace = EmbeddedWorkspace() / ("cache-failure-" + std::to_string(GetTickCount64()));
        const auto cache = workspace / "cache";
        const auto scripts = workspace / "Scripts";
        CompilerInput input{EvaluationSource(Statement("Int result = 456"), {}, mod::info::EvaluationMarker), {}};
        const auto initial = CompileEvaluation(input, EmbeddedCompiler(), cache);
        REQUIRE(initial.error.empty());
        CopyEvaluationBytecode(initial, scripts);
        const auto installed = scripts / (initial.script + ".pex");
        const auto size = std::filesystem::file_size(installed);

        const auto oldest = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
        std::filesystem::last_write_time(initial.bytecode.parent_path(), oldest - std::chrono::hours(1));
        for (int i = 0; i < 29; ++i) {
            const auto number = std::to_string(i);
            const auto entry = cache / (std::string(16 - number.size(), '0') + number);
            WriteFixture(entry / "fixture.txt", "fixture");
            std::filesystem::last_write_time(entry, oldest + std::chrono::seconds(i));
        }

        CompilerInput invalid{std::string("Scriptname ") + mod::info::EvaluationMarker + "\ninvalid syntax", {}};
        const auto failed = CompileEvaluation(invalid, EmbeddedCompiler(), cache);
        CHECK_FALSE(failed.error.empty());
        CHECK_FALSE(std::filesystem::exists(initial.bytecode));
        CHECK(std::distance(std::filesystem::directory_iterator(cache), std::filesystem::directory_iterator{}) == 30);
        CHECK(std::filesystem::file_size(installed) == size);

        const auto rebuilt = CompileEvaluation(input, EmbeddedCompiler(), cache);
        REQUIRE(rebuilt.error.empty());
        CHECK(rebuilt.script == initial.script);
        CHECK_NOTHROW(CopyEvaluationBytecode(rebuilt, scripts));
    }

    TEST_CASE("Multiline wrappers compile control flow, typed fields and scoped locals") {
        const EvaluationEnvironment environment{
            {{"Actor", {}, {{"GetLevel", "Int", {}, false}, {"IsInCombat", "Bool", {}, false}}, {}},
             {"Debug", {}, {{"Notification", {}, {{"String", "message"}}, true}}, {}}},
            {{"Actor", "player"}, {"Int", "count"}},
            {}};
        for (const auto source :
             {"var level = player.GetLevel()\nIf "
              "player.IsInCombat()\nDebug.Notification(\"Busy!\")\nElse\nDebug.Notification(\"Safe.\")\nEndIf",
              "While count < 3\ncount += 1\nEndWhile\nvar numbers = new Int[3]\nnumbers[0] = count",
              "If True\nvar local = count + 1\nElse\nvar local = \"text\"\nEndIf",
              "Int total\nIf count > 0\ntotal = count\nEndIf\nReturn"}) {
            const auto plan = ResolveFragment(source, environment);
            CompilerInput input{FragmentSource(plan, mod::info::EvaluationMarker), {}};
            for (const auto& script : environment.scripts)
                input.declarations.emplace_back(script.name, DeclarationSource(script));
            const auto compiled = CompileEvaluation(input);
            INFO(source, ": ", compiled.error);
            CHECK(compiled.error.empty());
        }
        const auto plan = ResolveFragment("If True\nplayer.MissingMethod()\nEndIf", environment);
        CompilerInput invalid{FragmentSource(plan, mod::info::EvaluationMarker), {}};
        for (const auto& script : environment.scripts)
            invalid.declarations.emplace_back(script.name, DeclarationSource(script));
        CHECK_FALSE(CompileEvaluation(invalid).error.empty());
    }

    TEST_CASE("Inferred declarations and mutations compile with ordinary Papyrus semantics") {
        const EvaluationEnvironment environment{
            {{"Record",
              {},
              {{"Read", "Record", {}, false}},
              {{"Value", "Float", true, true}, {"WriteOnly", "Float", false, true}}}},
            {{"Record", "record"}, {"Float[]", "values"}, {"Int", "count"}},
            {}};
        for (const auto text : {"var result = record.Read()", "var result = new Float[3]",
                                "var result = record.Value + 1.5", "var result = \"Hello\"", "record.Value = 3.0",
                                "record.WriteOnly = 3.0", "record.Value += 1.0", "values[count] = 1.5", "count += 1"}) {
            auto statement = Statement(text);
            if (statement.reassignment)
                statement.resultType = "Int";
            ResolveStatement(statement, environment);
            CompilerInput input{EvaluationSource(statement, environment.variables, mod::info::EvaluationMarker), {}};
            for (const auto& script : environment.scripts)
                input.declarations.emplace_back(script.name, DeclarationSource(script));
            const auto compiled = CompileEvaluation(input);
            INFO(text, ": ", compiled.error);
            CHECK(compiled.error.empty());
        }
    }

    TEST_CASE("Compiler accepts input and import counts beyond advisory thresholds") {
        CompilerInput input;
        std::vector<SourceParameter> parameters;

        SUBCASE("Thirty-three input variables") {
            std::string expression;
            for (int i = 0; i < 33; ++i) {
                const auto name = "value" + std::to_string(i);
                parameters.push_back({"Int", name});
                if (!expression.empty())
                    expression += " + ";
                expression += name;
            }
            input.source =
                EvaluationSource(Statement("Int result = " + expression), parameters, mod::info::EvaluationMarker);
        }

        SUBCASE("Two hundred fifty-seven inherited script types") {
            for (int i = 0; i < 257; ++i) {
                const auto name = "FixtureType" + std::to_string(i);
                auto source = "Scriptname " + name;
                if (i > 0)
                    source += " extends FixtureType" + std::to_string(i - 1);
                input.declarations.emplace_back(name, source + " Hidden\n");
            }
            parameters.push_back({"FixtureType256", "value"});
            input.source =
                EvaluationSource(Statement("FixtureType256 result = value"), parameters, mod::info::EvaluationMarker);
        }

        const auto result = CompileEvaluation(input);
        CHECK_MESSAGE(result.error.empty(), result.error);
    }

    TEST_CASE("Cooperative compiler deadlines preserve the next session") {
        const auto workspace = EmbeddedWorkspace();
        const CompilationRequest request{
            workspace / "Evaluation.psc", workspace / "Evaluation.flg", workspace, {workspace / "imports"}};
        WriteFixture(request.flags, "Flag Hidden 0 { Script Property }\n");
        WriteFixture(request.source,
                     "Scriptname Evaluation Hidden\nInt Function Run() Global\nReturn 3\nEndFunction\n");
        const auto expired = EmbeddedCompiler(std::chrono::milliseconds(0)).Compile(request);
        CHECK_FALSE(expired.succeeded);
        CHECK(expired.diagnostics.find("time limit") != std::string::npos);
        CHECK(EmbeddedCompiler().Compile(request).succeeded);
    }

    TEST_CASE("Caprica compiles arithmetic and reuses immutable bytecode") {
        CompilerInput input{EvaluationSource(Statement("Float answer = amount * 2.0"), {{"Float", "amount"}},
                                             mod::info::EvaluationMarker),
                            {}};
        const auto result = CompileEvaluation(input);
        INFO(result.error);
        REQUIRE(result.error.empty());
        CHECK(result.script.starts_with(mod::info::EvaluationPrefix));
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
            EvaluationSource(Statement(statement), {{"FixtureContainer", "container"}}, mod::info::EvaluationMarker),
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

    TEST_CASE("Inferred expressions and reassignment compile as one typed execution") {
        const EvaluationEnvironment environment{
            {{"Record", {}, {{"Describe", "String", {}, false}}, {{"Value", "Float", true, true}}},
             {"Container",
              {},
              {{"GetRecord", "Record", {}, false}, {"Clear", {}, {}, false}},
              {{"Data", "Record", true, false}}}},
            {{"Container", "container"}, {"Int", "count"}},
            {}};
        for (const auto text : {"container.GetRecord().Describe()", "container.Data.Value", "count + 1.5",
                                "count = count + 1", "container.Clear()"}) {
            const auto parsed = ParseEvaluation(text);
            REQUIRE(std::holds_alternative<EvaluationStatement>(parsed));
            auto statement = std::get<EvaluationStatement>(parsed);
            if (statement.reassignment)
                statement.resultType = "Int";
            else
                statement.resultType = ExpressionTypes(statement.expression, environment).Result();
            CompilerInput input{EvaluationSource(statement, environment.variables, mod::info::EvaluationMarker), {}};
            for (const auto& script : environment.scripts)
                input.declarations.emplace_back(script.name, DeclarationSource(script));
            // Inferring a result must never generate an execution probe or
            // repeat a function call with possible side effects.
            const auto occurrence = input.source.find(statement.expression);
            REQUIRE(occurrence != std::string::npos);
            CHECK(input.source.find(statement.expression, occurrence + statement.expression.size()) ==
                  std::string::npos);
            const auto compiled = CompileEvaluation(input);
            INFO(text, ": ", compiled.error);
            CHECK(compiled.error.empty());
        }
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
            EvaluationSource(Statement("Int level = Game.GetPlayer().GetLevel()"), {}, mod::info::EvaluationMarker);
        const auto chain = CompileEvaluation(input);
        INFO(chain.error);
        CHECK(chain.error.empty());
        input.source = EvaluationSource(
            Statement("ExampleController controller = Game.GetFormFromFile(1, \"Example.esp\") as ExampleController"),
            {}, mod::info::EvaluationMarker);
        const auto cast = CompileEvaluation(input);
        INFO(cast.error);
        CHECK(cast.error.empty());
    }

    TEST_CASE("Hexadecimal runtime FormIDs preserve all 32 bits in compiled calls") {
        const ScriptDeclaration form{"Form", {}, {}, {}};
        const ScriptDeclaration container{"Actor", "Form", {}, {}};
        const ScriptDeclaration game{"Game", {}, {{"GetFormEx", "Form", {{"Int", "formId"}}, true}}, {}};
        CompilerInput input{EvaluationSource(Statement("Actor npc = Game.GetFormEx(0xFF000123) as Actor"), {},
                                             mod::info::EvaluationMarker),
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
                             mod::info::EvaluationMarker),
            {{controller.name, DeclarationSource(controller)}, {unavailable.name, DeclarationSource(unavailable)}}};
        const auto unrelated = CompileEvaluation(input);
        INFO(unrelated.error);
        CHECK(unrelated.error.empty());

        input.source = EvaluationSource(Statement("Int level = controller.OptionalLibrary.GetLevel()"),
                                        {{"OptionalController", "controller"}}, mod::info::EvaluationMarker);
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
                                             {{controller.name, "controller"}}, mod::info::EvaluationMarker),
                            {{controller.name, source}}};
        const auto valid = CompileEvaluation(input);
        INFO(valid.error);
        CHECK(valid.error.empty());
        input.source = EvaluationSource(Statement("String library = controller.MissingLibrary"),
                                        {{controller.name, "controller"}}, mod::info::EvaluationMarker);
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
        CompilerInput input{EvaluationSource(Statement("Int restored = 123"), {}, mod::info::EvaluationMarker), {}};
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
