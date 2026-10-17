#include "VM.h"
#include "ValueDisplay.h"
#include "shell/Command.h"
#include "shell/Variables.h"
#include "compiler/Compiler.h"
#include "VMImports.h"
#include "shell/EvaluationGate.h"
#include "shell/Fragment.h"

#include <mutex>
#include <thread>

namespace elder_shell {
    namespace {
        using VM = RE::BSScript::Internal::VirtualMachine;
        using Object = RE::BSTSmartPointer<RE::BSScript::Object>;
        using Type = RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo>;
        using Variable = RE::BSScript::Variable;

        struct EvaluationBinding {
            Variable value;
            std::string type, error;
        };

        using EvaluationBindings = std::map<std::string, EvaluationBinding>;

        std::mutex mutex;
        Requests requests;
        Variables<Variable> variables;
        bool gameReady = false;
        EvaluationGate evaluationGate;
        std::jthread evaluationWorker;
        std::set<std::string> evaluationTypes;
        EvaluationUsage evaluationUsage;
        std::shared_ptr<const EvaluationEnvironment> completionMetadata;
        std::string completionKey;
        std::uint64_t completionRevision = 0;
        bool completionPending = false;
        VariableInspection variableInspection;
        std::uint64_t inspectionRevision = 0;

        struct CachedInstance {
            InstanceRow row;
            Object object;
        };

        std::vector<CachedInstance> instances;
        InstanceExplorer explorer;
        std::uint64_t scanRevision = 0;
        std::uint64_t nextInstanceID = 0;

        void Reject(Ticket ticket, std::string reason) {
            std::scoped_lock lock(mutex);
            if (!requests.Current(ticket))
                return;
            requests.Reject(ticket, std::move(reason));
            variables.Reject(ticket);
        }

        void ReleaseEvaluation(Ticket ticket) {
            std::scoped_lock lock(mutex);
            evaluationGate.Release(ticket);
        }

        // Called only on the game thread before compilation and dispatch.
        bool CanContinue(Ticket ticket) {
            bool ready = false;
            {
                std::scoped_lock lock(mutex);
                if (!requests.Current(ticket))
                    return false;
                ready = gameReady;
            }
            auto* ui = RE::UI::GetSingleton();
            if (!ready || (ui && ui->IsMenuOpen("Main Menu"))) {
                Reject(ticket, "Return to a loaded game before continuing");
                return false;
            }
            return true;
        }

        Variable* FragmentField(const Object& object, std::string_view name) {
            if (!object || !object->type ||
                object->type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid)
                return nullptr;
            const auto* fields = object->type->GetVariableIter();
            for (std::uint32_t i = 0; fields && i < object->type->GetNumVariables(); ++i)
                if (Equal(fields[i].name.c_str(), name))
                    return object->GetVariable(fields[i].name);
            return nullptr;
        }

        class Completion final : public RE::BSScript::IStackCallbackFunctor {
        public:
            Completion(Ticket ticket, std::string assignment, std::string label = {}, Object fragment = {},
                       std::vector<SourceParameter> outputs = {})
                : ticket_(ticket),
                  assignment_(std::move(assignment)),
                  label_(std::move(label)),
                  fragment_(std::move(fragment)),
                  outputs_(std::move(outputs)) {}

            void operator()(Variable result) override {
                std::scoped_lock lock(mutex);
                evaluationGate.Release(ticket_);
                if (!requests.Current(ticket_))
                    return;
                if (!assignment_.empty())
                    variables.Complete(assignment_, ticket_, result);
                for (const auto& output : outputs_) {
                    const auto* value = FragmentField(fragment_, output.name);
                    if (!value) {
                        variables.Reject(ticket_);
                        requests.Reject(ticket_, "Generated fragment field unavailable: " + output.name);
                        return;
                    }
                    variables.Complete(output.name, ticket_, *value);
                }
                requests.Complete(ticket_, label_.empty() ? Display(result) : label_);
            }

            void SetObject(const Object& object) override {
                object_ = object;
            }

        private:
            Ticket ticket_;
            std::string assignment_;
            std::string label_;
            Object object_;
            Object fragment_;
            std::vector<SourceParameter> outputs_;
        };

        class Arguments final : public RE::BSScript::IFunctionArguments {
        public:
            explicit Arguments(std::vector<Variable> values) : values_(std::move(values)) {}

            bool operator()(RE::BSScrapArray<Variable>& destination) const override {
                const auto count = static_cast<std::uint32_t>(values_.size());
                destination.resize(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    destination[i] = values_[i];
                return true;
            }

        private:
            std::vector<Variable> values_;
        };

        bool Inherits(const RE::BSScript::ObjectTypeInfo* type, std::string_view expected) {
            for (; type; type = type->GetParent())
                if (Equal(type->GetName(), expected))
                    return true;
            return false;
        }

        std::string Signature(RE::BSScript::IFunction* function) {
            std::string text = "Function ";
            text += std::format("{}.{}(", function->GetObjectTypeName().c_str(), function->GetName().c_str());
            for (std::uint32_t i = 0; i < function->GetParamCount(); ++i) {
                RE::BSFixedString name;
                RE::BSScript::TypeInfo type;
                function->GetParam(i, name, type);
                if (i)
                    text += ", ";
                text += std::format("{} {}", type.TypeAsString(), name.c_str());
            }
            text += ") -> " + function->GetReturnType().TypeAsString();
            return text;
        }

        std::string Methods(const Type& type) {
            std::string text;
            for (auto* current = type.get(); current; current = current->GetParent()) {
                const auto* globals = current->GetGlobalFuncIter();
                for (std::uint32_t i = 0; globals && i < current->GetNumGlobalFuncs(); ++i)
                    if (globals[i].func)
                        text += Signature(globals[i].func.get()) + '\n';
                const auto* members = current->GetMemberFuncIter();
                for (std::uint32_t i = 0; members && i < current->GetNumMemberFuncs(); ++i)
                    if (members[i].func)
                        text += Signature(members[i].func.get()) + '\n';
                if (text.size() > 32768) {
                    text += "List truncated";
                    break;
                }
            }
            return text.empty() ? "No functions in the default state" : text;
        }

        bool VariableCommand(Ticket ticket, const Command& command) {
            if (command.operation != Operation::Variables && command.operation != Operation::Unset &&
                command.operation != Operation::ClearVariables)
                return false;
            std::scoped_lock lock(mutex);
            std::string text;
            switch (command.operation) {
                case Operation::Variables:
                    for (const auto& [name, binding] : variables.Entries()) {
                        text += name + " = " +
                                (binding.pending ? "Pending"
                                 : binding.value ? Display(*binding.value)
                                                 : "Failed") +
                                '\n';
                        if (text.size() >= 32768) {
                            text += "List truncated; evaluate a variable name for individual values";
                            break;
                        }
                    }
                    if (text.empty())
                        text = "No variables";
                    break;
                case Operation::Unset:
                    if (!variables.Unset(command.target)) {
                        requests.Reject(ticket, "Unknown variable: " + command.target);
                        return true;
                    }
                    text = "Variable removed";
                    break;
                case Operation::ClearVariables:
                    variables.Clear();
                    text = "Variables cleared";
                    break;
                default:
                    break;
            }
            requests.Complete(ticket, std::move(text));
            return true;
        }

        void ExecuteEvaluation(Ticket ticket, const Command& command, const EvaluationBindings& bindings) {
            if (!CanContinue(ticket))
                return;
            bool started = false;
            bool interrupted = false;
            {
                std::scoped_lock lock(mutex);
                started = evaluationGate.Begin(ticket);
                interrupted = evaluationGate.PreviousSession(ticket);
            }
            if (!started) {
                Reject(
                    ticket,
                    interrupted
                        ? "An evaluation was interrupted by loading; restart Skyrim before evaluating another statement"
                        : "Another compiled evaluation is pending; wait for its VM completion");
                return;
            }
            try {
                auto statement = command.evaluation;
                std::vector<SourceParameter> parameters;
                std::vector<Variable> arguments;
                for (const auto& root : SourceRoots(command.evaluation.expression)) {
                    const auto binding =
                        std::ranges::find_if(bindings, [&](const auto& item) { return Equal(item.first, root); });
                    if (binding == bindings.end())
                        continue;
                    const auto& value = binding->second;
                    if (!value.error.empty())
                        throw std::runtime_error(value.error);
                    if (!DeclarationType(value.type))
                        throw std::runtime_error("Variable " + root +
                                                 " is None without a declared type; use a typed declaration.");
                    parameters.push_back({value.type, root});
                    arguments.push_back(value.value);
                }
                auto imports = SnapshotEvaluationImports(VM::GetSingleton(), statement, parameters);
                {
                    std::scoped_lock lock(mutex);
                    evaluationUsage.inputVariables = parameters.size();
                    evaluationUsage.importedTypes = imports.compiler.declarations.size();
                }
                FragmentPlan fragment;
                if (statement.multiline)
                    fragment = ResolveFragment(statement.expression, imports.environment);
                else
                    ResolveStatement(statement, imports.environment);
                auto input = std::move(imports.compiler);
                input.source = statement.multiline
                                   ? FragmentSource(fragment, mod::info::EvaluationMarker)
                                   : EvaluationSource(statement, parameters, mod::info::EvaluationMarker);
                std::string completionLabel =
                    statement.resultType.empty() ? statement.mutation ? "Statement completed" : "No return value" : "";
                {
                    std::scoped_lock lock(mutex);
                    if (!command.assignment.empty())
                        variables.SetDeclaredType(command.assignment, ticket, statement.resultType);
                    for (const auto& output : fragment.outputs)
                        variables.SetDeclaredType(output.name, ticket, output.type);
                    if (statement.multiline)
                        completionLabel = "Fragment completed";
                    requests.Compiling(ticket);
                }
                evaluationWorker =
                    std::jthread([ticket, command, input = std::move(input), arguments = std::move(arguments),
                                  completionLabel = std::move(completionLabel), parameters,
                                  fragment = std::move(fragment)]() mutable {
                        auto compiled = CompileEvaluation(input);
                        auto* tasks = SKSE::GetTaskInterface();
                        if (!tasks) {
                            ReleaseEvaluation(ticket);
                            Reject(ticket, "SKSE task interface unavailable");
                            return;
                        }
                        tasks->AddTask([ticket, command, compiled = std::move(compiled),
                                        arguments = std::move(arguments), completionLabel = std::move(completionLabel),
                                        parameters, fragment = std::move(fragment)]() mutable {
                            if (!CanContinue(ticket)) {
                                ReleaseEvaluation(ticket);
                                return;
                            }
                            if (!compiled.error.empty()) {
                                ReleaseEvaluation(ticket);
                                Reject(ticket,
                                       "Papyrus could not compile this statement. Check argument types and "
                                       "syntax.\n\nCompiler details:\n" +
                                           compiled.error);
                                return;
                            }
                            try {
                                // Immutable files remain available to saved stacks. Never rewrite an existing type.
                                const auto scripts = GameScriptDirectory();
                                CopyEvaluationBytecode(compiled, scripts);
                                auto* vm = VM::GetSingleton();
                                Type type;
                                if (!vm || !vm->GetScriptObjectType(RE::BSFixedString(compiled.script), type) || !type)
                                    throw std::runtime_error("Skyrim could not load the generated script");
                                {
                                    std::scoped_lock lock(mutex);
                                    evaluationTypes.insert(compiled.script);
                                }
                                SKSE::log::info("Compiled evaluation {}: {}.Run, {} arguments", ticket.id,
                                                compiled.script, arguments.size());
                                Object instance;
                                if (command.evaluation.multiline) {
                                    // This is our generated evaluator, not a fabricated receiver for an installed
                                    // script.
                                    if (!vm->CreateObject(RE::BSFixedString(compiled.script), instance) || !instance)
                                        throw std::runtime_error("Skyrim could not create the fragment evaluator");
                                    for (std::size_t i = 0; i < parameters.size(); ++i) {
                                        auto* field = FragmentField(instance, parameters[i].name);
                                        if (!field)
                                            throw std::runtime_error("Generated fragment input unavailable: " +
                                                                     parameters[i].name);
                                        *field = arguments[i];
                                    }
                                }
                                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> completion{
                                    new Completion(ticket, command.assignment, std::move(completionLabel), instance,
                                                   std::move(fragment.outputs))};
                                {
                                    std::scoped_lock lock(mutex);
                                    requests.Submitted(ticket);
                                }
                                const bool dispatched =
                                    instance ? vm->DispatchMethodCall(instance, "Run", new Arguments({}), completion)
                                             : vm->DispatchStaticCall(RE::BSFixedString(compiled.script), "Run",
                                                                      new Arguments(std::move(arguments)), completion);
                                if (!dispatched) {
                                    ReleaseEvaluation(ticket);
                                    Reject(ticket, "The VM rejected compiled evaluation");
                                }
                            } catch (const std::exception& error) {
                                ReleaseEvaluation(ticket);
                                Reject(ticket, error.what());
                            }
                        });
                    });
            } catch (const std::exception& error) {
                ReleaseEvaluation(ticket);
                Reject(ticket, error.what());
            }
        }

        void Execute(Ticket ticket, const Command& command) {
            {
                std::scoped_lock lock(mutex);
                if (!requests.Current(ticket))
                    return;
                if (!gameReady) {
                    requests.Reject(ticket, "Load a save or start a new game first");
                    variables.Reject(ticket);
                    return;
                }
            }
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen("Main Menu")) {
                Reject(ticket, "Load a save or start a new game first");
                return;
            }
            if (VariableCommand(ticket, command))
                return;
            auto* vm = VM::GetSingleton();
            Type type;
            if (!vm || !vm->GetScriptObjectType(RE::BSFixedString(command.script), type) || !type ||
                type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid) {
                Reject(ticket, "Script type unavailable: " + command.script);
                return;
            }
            if (command.operation == Operation::Methods) {
                const auto text = Methods(type);
                std::scoped_lock lock(mutex);
                requests.Complete(ticket, text);
                return;
            }
        }
    }

    std::string ExploreInstances(std::string form, std::string script) {
        const auto formID = form.empty() ? std::optional<std::uint32_t>{} : FormID(form);
        if ((!form.empty() && !formID) || (!script.empty() && !Identifier(script)))
            return "Use a hexadecimal form/reference ID and a script name";
        if (form.empty() && script.empty())
            return "Provide a form/reference ID or script name";
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks)
            return "SKSE task interface unavailable";
        Ticket ticket;
        std::uint64_t revision;
        {
            std::scoped_lock lock(mutex);
            const auto created = requests.Begin("Explore instances: " + form + " " + script);
            if (!created)
                return "32 requests are pending; wait for them to finish";
            ticket = *created;
            revision = ++scanRevision;
            instances.clear();
            explorer.rows.clear();
            explorer.status = "Search queued";
        }
        tasks->AddTask([ticket, revision, formID, script] {
            const auto finish = [ticket, revision](std::vector<Object> objects, std::string status) {
                std::scoped_lock lock(mutex);
                if (!requests.Current(ticket))
                    return;
                if (revision != scanRevision) {
                    requests.Reject(ticket, "Search superseded");
                    return;
                }
                for (auto& object : objects) {
                    InstanceRow row{++nextInstanceID, object->GetTypeInfo()->GetName(), object->currentState.c_str(),
                                    std::format("{:016X}", object->GetHandle()), object->IsInitialized()};
                    explorer.rows.push_back(row);
                    instances.push_back({std::move(row), std::move(object)});
                }
                explorer.status = std::move(status);
                requests.Complete(ticket, explorer.status);
            };
            {
                std::scoped_lock lock(mutex);
                if (!requests.Current(ticket))
                    return;
                auto* ui = RE::UI::GetSingleton();
                if (!gameReady || (ui && ui->IsMenuOpen("Main Menu"))) {
                    if (revision == scanRevision)
                        explorer.status = "Load a save or start a new game first";
                    requests.Reject(ticket, "Load a save or start a new game first");
                    return;
                }
            }
            auto* vm = VM::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!policy) {
                finish({}, "VM handle policy unavailable");
                return;
            }
            std::optional<RE::VMHandle> wantedHandle;
            if (formID) {
                auto* target = RE::TESForm::LookupByID(*formID);
                if (!target) {
                    finish({}, "Form/reference ID not found");
                    return;
                }
                wantedHandle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(target->GetFormType()), target);
            }
            std::vector<Object> objects;
            bool limited = false;
            std::size_t visited = 0;
            {
                // Copy references under the engine lock; perform no VM calls while holding it.
                RE::BSSpinLockGuard lock(vm->attachedScriptsLock);
                const auto collect = [&](const auto& attached) {
                    for (const auto& item : attached) {
                        if (++visited > 100000 || objects.size() == 128) {
                            limited = true;
                            return false;
                        }
                        if (item && (script.empty() || Inherits(item->GetTypeInfo(), script)))
                            objects.emplace_back(item.get());
                    }
                    return true;
                };
                if (wantedHandle) {
                    const auto found = vm->attachedScripts.find(*wantedHandle);
                    if (found != vm->attachedScripts.end())
                        collect(found->second);
                } else {
                    for (const auto& [handle, attached] : vm->attachedScripts)
                        if (!collect(attached))
                            break;
                }
            }
            const auto count = objects.size();
            finish(std::move(objects), std::format("{} attached instances{}", count,
                                                   limited ? " (search limit reached; narrow the filters)" : ""));
        });
        return {};
    }

    InstanceExplorer InstanceSnapshot() {
        std::scoped_lock lock(mutex);
        return explorer;
    }

    std::string UseInstance(std::uint64_t id, std::string variable) {
        if (!Identifier(variable))
            return "Choose a variable name using letters, digits and underscores";
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks)
            return "SKSE task interface unavailable";
        Ticket ticket;
        {
            std::scoped_lock lock(mutex);
            const auto created = requests.Begin(std::format("Use instance {} as {}", id, variable));
            if (!created)
                return "32 requests are pending; wait for them to finish";
            ticket = *created;
            if (!variables.Reserve(variable, ticket)) {
                requests.Reject(ticket, "64 variables are stored; unset one or use clearvars");
                return {};
            }
        }
        tasks->AddTask([ticket, id, variable] {
            std::scoped_lock lock(mutex);
            if (!requests.Current(ticket))
                return;
            const auto found = std::find_if(instances.begin(), instances.end(),
                                            [id](const CachedInstance& instance) { return instance.row.id == id; });
            auto* ui = RE::UI::GetSingleton();
            const auto* vm = VM::GetSingleton();
            const auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!gameReady || (ui && ui->IsMenuOpen("Main Menu")) || found == instances.end() ||
                !found->object->IsInitialized() || !policy ||
                !policy->IsHandleObjectAvailable(found->object->GetHandle())) {
                variables.Reject(ticket);
                requests.Reject(ticket, "Instance unavailable; refresh the explorer");
                return;
            }
            Variable value;
            value.SetObject(found->object);
            variables.SetDeclaredType(variable, ticket, value.GetType().TypeAsString());
            variables.Complete(variable, ticket, value);
            requests.Complete(ticket, variable + " = " + Display(value));
        });
        return {};
    }

    std::string SubmitCommand(std::string line, const PropertyInspection* inspection = nullptr,
                              Ticket* submitted = nullptr, bool multiline = false) {
        const auto parsed = Parse(line, multiline);
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks)
            return "SKSE task interface unavailable";
        Ticket ticket;
        {
            std::scoped_lock lock(mutex);
            const auto created = requests.Begin(std::move(line));
            if (!created)
                return "32 requests are pending; wait for them to finish";
            ticket = *created;
            if (submitted)
                *submitted = ticket;
            if (const auto* error = std::get_if<std::string>(&parsed)) {
                requests.Reject(ticket, *error);
                return {};
            }
        }
        auto command = std::get<Command>(parsed);
        EvaluationBindings bindings;
        if (command.operation == Operation::Evaluate) {
            std::scoped_lock lock(mutex);
            if (inspection) {
                const auto* binding = variables.Find(inspection->variable);
                if (!binding || binding->pending || !binding->value ||
                    !SameInspectionOwner(binding->owner, inspection->owner)) {
                    requests.Reject(ticket, "Variable changed; refresh the inspector before reading its properties");
                    return {};
                }
            }
            for (const auto& [name, binding] : variables.Entries()) {
                EvaluationBinding value;
                value.type = binding.declaredType;
                if (binding.pending)
                    value.error = "Variable " + name + " is still pending; wait for its command to complete.";
                else if (!binding.value)
                    value.error = "Variable " + name + " has a failed assignment; assign a value before using it.";
                else {
                    value.value = *binding.value;
                    if (value.type.empty())
                        value.type = value.value.GetType().TypeAsString();
                }
                bindings.emplace(name, std::move(value));
            }
            if (command.evaluation.reassignment) {
                const auto found = std::ranges::find_if(
                    bindings, [&](const auto& item) { return Equal(item.first, command.assignment); });
                if (found == bindings.end() || !found->second.error.empty() || !DeclarationType(found->second.type)) {
                    requests.Reject(ticket, found == bindings.end() ? "Unknown variable: " + command.assignment +
                                                                          ". Declare it with a type first."
                                            : !found->second.error.empty()
                                                ? found->second.error
                                                : "This variable has no declared type; use a typed declaration.");
                    return {};
                }
                command.evaluation.resultType = found->second.type;
            }
        }
        if (!command.assignment.empty()) {
            std::scoped_lock lock(mutex);
            if (!requests.Current(ticket))
                return {};
            if (!variables.Reserve(command.assignment, ticket, command.evaluation.resultType)) {
                requests.Reject(ticket, "64 variables are stored; unset one or use clearvars");
                return {};
            }
        }
        if (command.evaluation.multiline) {
            std::scoped_lock lock(mutex);
            std::vector<SourceParameter> outputs;
            const auto declarations = FragmentLines(command.evaluation.expression);
            for (const auto& name : FragmentWrites(command.evaluation.expression)) {
                const auto declaration = std::ranges::find_if(
                    declarations, [&](const auto& item) { return item.depth == 0 && Equal(item.name, name); });
                if (declaration != declarations.end())
                    outputs.push_back({Equal(declaration->type, "var") ? "" : declaration->type, name});
                else if (const auto* existing = variables.Find(name))
                    outputs.push_back({existing->declaredType, name});
            }
            const auto added =
                std::ranges::count_if(outputs, [&](const auto& output) { return !variables.Find(output.name); });
            if (variables.Entries().size() + added > 64) {
                requests.Reject(ticket, "64 variables are stored; unset one or use clearvars");
                return {};
            }
            for (const auto& output : outputs)
                variables.Reserve(output.name, ticket, output.type, true);
        }
        tasks->AddTask([ticket, command, bindings = std::move(bindings)] {
            try {
                if (command.operation == Operation::Evaluate)
                    ExecuteEvaluation(ticket, command, bindings);
                else
                    Execute(ticket, command);
            } catch (const std::exception& error) {
                Reject(ticket, error.what());
            }
        });
        return {};
    }

    std::string Submit(std::string line, bool multiline) {
        return SubmitCommand(std::move(line), nullptr, nullptr, multiline);
    }

    InspectionRead ReadInspectedProperty(PropertyInspection inspection) {
        InspectionRead result;
        if (!ValidPropertyInspection(inspection)) {
            result.error = "Invalid property expression";
            return result;
        }
        result.error = SubmitCommand(inspection.expression, &inspection, &result.ticket);
        return result;
    }

    void RefreshVariableInspection() {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks)
            return;
        std::uint64_t revision;
        {
            std::scoped_lock lock(mutex);
            if (variableInspection.refreshing)
                return;
            if (!gameReady) {
                variableInspection.status = "Load a save or start a new game first";
                return;
            }
            variableInspection.refreshing = true;
            revision = ++inspectionRevision;
        }
        tasks->AddTask([revision] {
            struct Captured {
                InspectedVariable row;
                std::optional<Variable> value;
            };
            std::vector<Captured> captured;
            {
                std::scoped_lock lock(mutex);
                if (revision != inspectionRevision || !gameReady)
                    return;
                for (const auto& [name, binding] : variables.Entries()) {
                    Captured item;
                    item.row.name = name;
                    item.row.owner = binding.owner;
                    item.row.value.expression = name;
                    item.row.value.type = binding.declaredType;
                    item.row.value.summary = binding.pending ? "Pending" : "Failed assignment";
                    if (!binding.pending)
                        item.value = binding.value;
                    captured.push_back(std::move(item));
                }
            }
            VariableInspection snapshot;
            InspectionBudget budget;
            try {
                for (auto& item : captured) {
                    if (item.value) {
                        const auto type = item.row.value.type;
                        item.row.value = InspectValue(*item.value, item.row.name, budget);
                        if (!type.empty())
                            item.row.value.type = type;
                    }
                    snapshot.variables.push_back(std::move(item.row));
                }
                snapshot.status = "Snapshot. Refresh to update values; property getters run only on Read.";
            } catch (const std::exception& error) {
                snapshot.status = std::string("Inspection failed: ") + error.what();
            }
            std::scoped_lock lock(mutex);
            if (revision == inspectionRevision && gameReady)
                variableInspection = std::move(snapshot);
        });
    }

    VariableInspection VariableInspectionSnapshot() {
        std::scoped_lock lock(mutex);
        return variableInspection;
    }

    void RefreshCompletionMetadata(std::string expression) {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks)
            return;
        std::vector<SourceParameter> parameters;
        auto key = expression;
        std::uint64_t revision;
        {
            std::scoped_lock lock(mutex);
            if (!gameReady || completionPending)
                return;
            for (const auto& [name, binding] : variables.Entries()) {
                if (!binding.pending && binding.value && DeclarationType(binding.declaredType)) {
                    parameters.push_back({binding.declaredType, name});
                    key += "|" + name + ":" + binding.declaredType;
                }
            }
            if (key == completionKey && completionMetadata)
                return;
            completionKey = std::move(key);
            completionPending = true;
            revision = ++completionRevision;
        }
        tasks->AddTask([expression = std::move(expression), parameters = std::move(parameters), revision] {
            {
                std::scoped_lock lock(mutex);
                if (revision != completionRevision || !gameReady)
                    return;
            }
            auto environment = std::make_shared<EvaluationEnvironment>();
            environment->variables = parameters;
            try {
                EvaluationStatement statement;
                statement.expression = expression;
                *environment = SnapshotEvaluationImports(VM::GetSingleton(), statement, parameters).environment;
            } catch (const std::exception& error) {
                // Completion is advisory. A partial snapshot must not reject a command.
                SKSE::log::debug("Completion metadata unavailable: {}", error.what());
            }
            std::scoped_lock lock(mutex);
            if (revision != completionRevision || !gameReady)
                return;
            completionMetadata = std::move(environment);
            completionPending = false;
        });
    }

    std::shared_ptr<const EvaluationEnvironment> CompletionMetadataSnapshot() {
        std::scoped_lock lock(mutex);
        return completionMetadata;
    }

    std::vector<Entry> Snapshot() {
        std::scoped_lock lock(mutex);
        return requests.Entries();
    }

    EvaluationUsage EvaluationUsageSnapshot() {
        std::scoped_lock lock(mutex);
        auto usage = evaluationUsage;
        usage.loadedTypes = evaluationTypes.size();
        return usage;
    }

    void ClearFinished() {
        std::scoped_lock lock(mutex);
        requests.ClearFinished();
    }

    void SessionChanged(bool ready) {
        std::scoped_lock lock(mutex);
        requests.Loaded();
        variables.Clear();
        variableInspection = {};
        ++inspectionRevision;
        completionMetadata.reset();
        completionKey.clear();
        completionPending = false;
        ++completionRevision;
        instances.clear();
        explorer = {};
        ++scanRevision;
        gameReady = ready;
    }
}
