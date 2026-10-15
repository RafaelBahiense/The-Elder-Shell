#include "VMImports.h"
#include "compiler/ScriptDeclarations.h"
#include "StateFunctions.h"

namespace papyrus_console {
    using VM = RE::BSScript::Internal::VirtualMachine;
    using Type = RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo>;

    std::optional<RE::BSScript::TypeInfo> FindValueType(VM* vm, std::string_view name) {
        using Raw = RE::BSScript::TypeInfo::RawType;
        if (Equal(name, "int"))
            return RE::BSScript::TypeInfo{Raw::kInt};
        if (Equal(name, "float"))
            return RE::BSScript::TypeInfo{Raw::kFloat};
        if (Equal(name, "bool"))
            return RE::BSScript::TypeInfo{Raw::kBool};
        if (Equal(name, "string"))
            return RE::BSScript::TypeInfo{Raw::kString};
        Type type;
        if (!vm || !vm->GetScriptObjectType(RE::BSFixedString(name), type) || !type)
            return std::nullopt;
        return RE::BSScript::TypeInfo{type->GetRawType()};
    }

    EvaluationImports SnapshotEvaluationImports(VM* vm, const EvaluationStatement& statement,
                                                const std::vector<SourceParameter>& parameters) {
        EvaluationImports snapshot;
        auto& input = snapshot.compiler;
        snapshot.environment.variables = parameters;
        std::vector<Type> types;
        std::set<std::string> seen;
        std::vector<std::string> required;
        const auto add = [&](const RE::BSScript::TypeInfo& info, bool directlyUsed = false) {
            if (!info.IsObject() && !info.IsObjectArray())
                return;
            auto* type = info.GetTypeInfo();
            if (type && directlyUsed)
                required.emplace_back(type->GetName());
            if (type && seen.insert(type->GetName()).second)
                types.emplace_back(type);
        };
        for (const auto& parameter : parameters) {
            auto name = parameter.type;
            if (name.ends_with("[]"))
                name.resize(name.size() - 2);
            if (const auto type = FindValueType(vm, name))
                add(*type, true);
        }
        auto resultName = statement.resultType;
        if (resultName.ends_with("[]"))
            resultName.resize(resultName.size() - 2);
        if (!resultName.empty()) {
            const auto result = FindValueType(vm, resultName);
            if (!result)
                throw std::runtime_error("Type unavailable: " + resultName +
                                         ". Check its spelling and that its script and dependencies are installed.");
            add(*result, true);
        }
        for (const auto& root : SourceRoots(statement.expression)) {
            if (Equal(root, "true") || Equal(root, "false") || Equal(root, "none") || Equal(root, "as") ||
                Equal(root, "new"))
                continue;
            if (std::ranges::any_of(parameters, [&](const auto& parameter) { return Equal(parameter.name, root); }))
                continue;
            if (const auto type = FindValueType(vm, root))
                add(*type, true);
            else
                throw std::runtime_error("Unknown variable or script: " + root +
                                         ". Declare a variable, select an instance, or check the script name.");
        }
        for (std::size_t index = 0; index < types.size(); ++index) {
            auto type = types[index];
            if (type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid) {
                // Signature references can point to a type the VM has not loaded
                // yet. Resolve it through the normal loader before reading data.
                Type loaded;
                if (vm->GetScriptObjectType(type->name, loaded) && loaded &&
                    loaded->linkedValid == RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid) {
                    type = loaded;
                    types[index] = loaded;
                }
            }
            if (!Identifier(type->GetName()))
                throw std::runtime_error("Unsupported script type name");
            ScriptDeclaration declaration;
            declaration.name = type->GetName();
            if (const auto* parent = type->GetParent()) {
                declaration.parent = parent->GetName();
                add(RE::BSScript::TypeInfo{parent->GetRawType()});
            }
            if (type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid) {
                if (std::ranges::any_of(required, [&](const auto& name) { return Equal(name, declaration.name); }))
                    throw std::runtime_error("Script type unavailable: " + declaration.name +
                                             ". Skyrim could not link its metadata; check its script dependencies.");
                // An unrelated method/property may mention an optional mod's
                // unavailable script. Its type identity is enough for imports;
                // never interpret unlinked data as function/property tables.
                // Accessing an undeclared member still fails compilation.
                input.declarations.emplace_back(declaration.name, DeclarationSource(declaration));
                snapshot.environment.issues.push_back(
                    {declaration.name, {}, "Skyrim could not link this script's metadata"});
                snapshot.environment.scripts.push_back(std::move(declaration));
                continue;
            }
            std::set<std::string> functions;
            const auto function = [&](RE::BSScript::IFunction* item) {
                if (!item || !Identifier(item->GetName().c_str()) || !functions.insert(item->GetName().c_str()).second)
                    return;
                FunctionDeclaration signature;
                signature.name = item->GetName().c_str();
                signature.global = item->GetIsStatic();
                const auto result = item->GetReturnType();
                if (result.GetRawType() != RE::BSScript::TypeInfo::RawType::kNone)
                    signature.result = result.TypeAsString();
                add(result);
                for (std::uint32_t i = 0; i < item->GetParamCount(); ++i) {
                    RE::BSFixedString name;
                    RE::BSScript::TypeInfo parameter;
                    item->GetParam(i, name, parameter);
                    add(parameter);
                    signature.parameters.push_back({parameter.TypeAsString(), Identifier(name.c_str())
                                                                                  ? name.c_str()
                                                                                  : "argument" + std::to_string(i)});
                }
                declaration.functions.push_back(std::move(signature));
            };
            const auto* globals = type->GetGlobalFuncIter();
            for (std::uint32_t i = 0; globals && i < type->GetNumGlobalFuncs(); ++i)
                function(globals[i].func.get());
            const auto* members = type->GetMemberFuncIter();
            for (std::uint32_t i = 0; members && i < type->GetNumMemberFuncs(); ++i)
                function(members[i].func.get());
            const auto* states = type->GetNamedStateIter();
            for (std::uint32_t i = 0; states && i < type->GetNumNamedStates(); ++i) {
                for (const auto& method : StateFunctions(*type, i))
                    function(method.func.get());
            }
            const auto* properties = type->GetPropertyIter();
            for (std::uint32_t i = 0; properties && i < type->GetNumProperties(); ++i) {
                const auto& property = properties[i];
                if (!Identifier(property.name.c_str()))
                    continue;
                add(property.info.type);
                const auto permission = std::to_underlying(*property.info.permissions);
                if ((permission & 3u) != 0)
                    declaration.properties.push_back({property.name.c_str(), property.info.type.TypeAsString(),
                                                      (permission & 1u) != 0, (permission & 2u) != 0});
            }
            input.declarations.emplace_back(declaration.name, DeclarationSource(declaration));
            snapshot.environment.scripts.push_back(std::move(declaration));
        }
        std::ranges::sort(input.declarations);
        return snapshot;
    }

}
