#include "VM.h"
#include "Command.h"
#include "Variables.h"
#include "PropertyRead.h"
#include "GetterDispatch.h"
#include "ValueRules.h"
#include "Compiler.h"
#include "VMImports.h"
#include "StateFunctions.h"
#include "EvaluationGate.h"

#include <mutex>
#include <thread>

namespace papyrus_console {
    namespace {
        using VM = RE::BSScript::Internal::VirtualMachine;
        using Object = RE::BSTSmartPointer<RE::BSScript::Object>;
        using Function = RE::BSTSmartPointer<RE::BSScript::IFunction>;
        using Type = RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo>;
        using Variable = RE::BSScript::Variable;

        std::mutex mutex;
        Requests requests;
        Variables<Variable> variables;
        bool gameReady = false;
        EvaluationGate evaluationGate;
        std::jthread evaluationWorker;
        std::set<std::string> evaluationTypes;

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

        // Called only on the game thread, including getter continuations.
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

        std::string Display(const Variable& value) {
            if (value.IsArray()) {
                auto array = value.GetArray();
                if (!array)
                    return "None array";
                std::string text = std::format("{}: {} elements [", value.GetType().TypeAsString(), array->size());
                const auto count = std::min<std::size_t>(array->size(), 128);
                for (std::size_t i = 0; i < count; ++i) {
                    if (i)
                        text += ", ";
                    text += Display((*array)[static_cast<std::uint32_t>(i)]);
                    if (text.size() >= 32768) {
                        text += ", ...";
                        break;
                    }
                }
                if (array->size() > count)
                    text += ", ...";
                return text + "]";
            }
            if (value.IsInt())
                return std::to_string(value.GetSInt());
            if (value.IsFloat())
                return std::format("{}", value.GetFloat());
            if (value.IsBool())
                return value.GetBool() ? "true" : "false";
            if (value.IsString())
                return std::format("\"{}\"", value.GetString().substr(0, 2048));
            if (value.IsObject()) {
                auto object = value.GetObject();
                return object ? std::format("{} (VM handle {:016X})", object->GetTypeInfo()->GetName(),
                                            object->GetHandle())
                              : "None";
            }
            return "None";
        }

        class Completion final : public RE::BSScript::IStackCallbackFunctor {
        public:
            Completion(Ticket ticket, std::string assignment, std::string label = {}, bool evaluation = false)
                : ticket_(ticket),
                  assignment_(std::move(assignment)),
                  label_(std::move(label)),
                  evaluation_(evaluation) {}

            void operator()(Variable result) override {
                std::scoped_lock lock(mutex);
                if (evaluation_)
                    evaluationGate.Release(ticket_);
                if (!requests.Current(ticket_))
                    return;
                if (!assignment_.empty())
                    variables.Complete(assignment_, ticket_, result);
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
            bool evaluation_ = false;
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

        Object Bound(VM* vm, RE::TESForm* form, const Type& expected) {
            auto* policy = vm->GetObjectHandlePolicy();
            if (!policy || !form || !expected)
                return {};
            Object object;
            const auto nativeID = static_cast<RE::VMTypeID>(form->GetFormType());
            const auto handle = policy->GetHandleForObject(nativeID, form);
            vm->FindBoundObject(handle, expected->GetName(), object);
            if (object)
                return object->IsInitialized() ? object : Object{};
            Type nativeType;
            // A native Actor/Quest/Form wrapper may be created by normal VM
            // packing. Never create an unbound custom script to simulate Self.
            if (!vm->GetScriptObjectType(nativeID, nativeType) || !nativeType ||
                !Inherits(nativeType.get(), expected->GetName()))
                return {};
            Variable packed;
            RE::BSScript::PackHandle(&packed, form, nativeID);
            return packed.IsObject() ? packed.GetObject() : Object{};
        }

        Function FindFunction(const Type& type, std::string_view name, bool global, std::string_view state) {
            for (auto* current = type.get(); current; current = current->GetParent()) {
                if (global) {
                    const auto* functions = current->GetGlobalFuncIter();
                    for (std::uint32_t i = 0; functions && i < current->GetNumGlobalFuncs(); ++i)
                        if (functions[i].func && Equal(functions[i].func->GetName().c_str(), name))
                            return functions[i].func;
                } else {
                    const auto* states = current->GetNamedStateIter();
                    for (std::uint32_t i = 0; states && i < current->GetNumNamedStates(); ++i) {
                        if (!Equal(states[i].name.c_str(), state))
                            continue;
                        for (const auto& function : StateFunctions(*current, i))
                            if (function.func && Equal(function.func->GetName().c_str(), name))
                                return function.func;
                    }
                    const auto* functions = current->GetMemberFuncIter();
                    for (std::uint32_t i = 0; functions && i < current->GetNumMemberFuncs(); ++i)
                        if (functions[i].func && Equal(functions[i].func->GetName().c_str(), name))
                            return functions[i].func;
                }
            }
            return {};
        }

        std::string Signature(RE::BSScript::IFunction* function) {
            std::string text = function->GetIsStatic() ? "global " : "call ";
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

        std::optional<Variable> ReadVariable(std::string_view name) {
            std::scoped_lock lock(mutex);
            const auto* binding = variables.Find(name);
            return binding && !binding->pending ? binding->value : std::nullopt;
        }

        Object StoredObject(VM* vm, const Variable& value, const Type& expected) {
            if (!value.IsObject() || !value.GetObject())
                return {};
            Object cast;
            if (vm->CastObject(value.GetObject(), expected, cast) && cast && cast->IsInitialized())
                return cast;
            // A Form result may refer to a separate attached custom script.
            cast.reset();
            vm->FindBoundObject(value.GetObject()->GetHandle(), expected->GetName(), cast);
            return cast && cast->IsInitialized() ? cast : Object{};
        }

        bool ConvertStored(VM* vm, const Variable& value, const RE::BSScript::TypeInfo& expected, Variable& result) {
            if (value.GetType() == expected) {
                result = value;
                return true;
            }
            if (value.IsInt() && expected.IsFloat()) {
                result.SetFloat(static_cast<float>(value.GetSInt()));
                return true;
            }
            if (!expected.IsObject() && !expected.IsArray())
                return false;
            if (value.GetType().GetRawType() == RE::BSScript::TypeInfo::RawType::kNone ||
                (value.IsObject() && !value.GetObject())) {
                result = Variable{expected};
                return true;
            }
            if (!expected.IsObject())
                return false;
            auto object = StoredObject(vm, value, Type{expected.GetTypeInfo()});
            if (!object)
                return false;
            result.SetObject(object, expected.GetRawType());
            return true;
        }

        struct PropertyValue {
            std::string name;
            Variable value;
            Function getter, setter;
            RE::BSScript::TypeInfo type;
            bool writable = false;
        };

        bool ResolveProperty(VM* vm, Object object, std::string_view name, PropertyValue& result, std::string& error,
                             bool write = false) {
            if (!vm || !object || !object->IsInitialized()) {
                error = "Property receiver is None or uninitialized: " + std::string(name);
                return false;
            }
            for (auto* type = object->GetTypeInfo(); type; type = type->GetParent()) {
                const auto* properties = type->GetPropertyIter();
                for (std::uint32_t i = 0; properties && i < type->GetNumProperties(); ++i) {
                    if (!Equal(properties[i].name.c_str(), name))
                        continue;
                    result.name = properties[i].name.c_str();
                    result.getter = properties[i].info.getFunction;
                    result.setter = properties[i].info.setFunction;
                    result.type = properties[i].info.type;
                    result.writable = (std::to_underlying(*properties[i].info.permissions) & 2u) != 0;
                    if (write)
                        return true;
                    if ((std::to_underlying(*properties[i].info.permissions) & 1u) == 0) {
                        error = "Property is write-only: " + result.name;
                        return false;
                    }
                    if (result.getter)
                        return true;
                    // GetPropertyValue in the pinned CommonLib has a misleading signature:
                    // the engine queues a getter; it does not fill a Variable synchronously.
                    if (properties[i].info.autoVarIndex < object->GetTypeInfo()->GetTotalNumVariables() + 3) {
                        if (const auto* stored = object->GetProperty(properties[i].name)) {
                            result.value = *stored;
                            return true;
                        }
                    }
                    error = "Property is not readable: " + std::string(name);
                    return false;
                }
            }
            error = "Property unavailable: " + std::string(name);
            return false;
        }

        struct PendingPropertyRead {
            Ticket ticket;
            std::function<void(Variable)> completed;
            PropertyRead<Variable, AccessStep> cursor;
        };

        void ContinuePropertyRead(const std::shared_ptr<PendingPropertyRead>& read);

        class GetterCompletion final : public RE::BSScript::IStackCallbackFunctor {
        public:
            GetterCompletion(std::shared_ptr<PendingPropertyRead> read, std::uint64_t receipt)
                : read_(std::move(read)), receipt_(receipt) {}

            void operator()(Variable result) override {
                auto* tasks = SKSE::GetTaskInterface();
                if (!tasks) {
                    Reject(read_->ticket, "SKSE task interface unavailable; property chain cannot resume");
                    return;
                }
                tasks->AddTask([read = read_, receipt = receipt_, result = std::move(result)]() mutable {
                    {
                        std::scoped_lock lock(mutex);
                        if (!requests.Current(read->ticket))
                            return;
                    }
                    if (read->cursor.Resume(receipt, std::move(result)))
                        ContinuePropertyRead(read);
                });
            }

            void SetObject(const Object& object) override {
                object_ = object;
            }

        private:
            std::shared_ptr<PendingPropertyRead> read_;
            std::uint64_t receipt_;
            Object object_;
        };

        void ContinuePropertyRead(const std::shared_ptr<PendingPropertyRead>& read) {
            if (!CanContinue(read->ticket))
                return;
            auto* vm = VM::GetSingleton();
            while (const auto step = read->cursor.NextProperty()) {
                const auto& value = read->cursor.CurrentValue();
                if (step->kind == AccessKind::Index) {
                    const auto array = value.IsArray() ? value.GetArray() : nullptr;
                    if (!array || step->index >= array->size()) {
                        Reject(read->ticket, "Array index out of bounds or receiver is not an array");
                        return;
                    }
                    read->cursor.Advance((*array)[step->index]);
                    continue;
                }
                if (value.IsArray() && Equal(step->name, "Length")) {
                    const auto array = value.GetArray();
                    Variable length;
                    length.SetSInt(array ? static_cast<std::int32_t>(array->size()) : 0);
                    read->cursor.Advance(std::move(length));
                    continue;
                }
                if (!value.IsObject() || !value.GetObject()) {
                    Reject(read->ticket, "Cannot read " + step->name + " from " + Display(value));
                    return;
                }
                PropertyValue property;
                std::string reason;
                auto object = value.GetObject();
                if (!ResolveProperty(vm, object, step->name, property, reason)) {
                    Reject(read->ticket, std::move(reason));
                    return;
                }
                if (!property.getter) {
                    read->cursor.Advance(std::move(property.value));
                    continue;
                }
                const auto receipt = read->cursor.Suspend();
                if (!receipt) {
                    Reject(read->ticket, "Property getter is already pending");
                    return;
                }
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> completion{
                    new GetterCompletion(read, *receipt)};
                {
                    std::scoped_lock lock(mutex);
                    requests.Submitted(read->ticket);
                }
                if (auto dispatchReason = DispatchGetter(vm, object, property.name, completion);
                    !dispatchReason.empty())
                    Reject(read->ticket, std::move(dispatchReason));
                return;
            }
            read->completed(read->cursor.CurrentValue());
        }

        void StartPropertyRead(Ticket ticket, Variable value, std::vector<AccessStep> properties,
                               std::function<void(Variable)> completed) {
            auto read = std::make_shared<PendingPropertyRead>(
                PendingPropertyRead{ticket, std::move(completed),
                                    PropertyRead<Variable, AccessStep>{std::move(value), std::move(properties)}});
            ContinuePropertyRead(read);
        }

        void FinishValue(Ticket ticket, Variable value, const std::string& assignment) {
            std::scoped_lock lock(mutex);
            if (!requests.Current(ticket))
                return;
            if (!assignment.empty())
                variables.Complete(assignment, ticket, value);
            requests.Complete(ticket, Display(value));
        }

        using ValueCallback = std::function<void(Variable)>;

        void EvaluateValue(Ticket ticket, const ValueExpression& expression, ValueCallback completed,
                           std::optional<RE::BSScript::TypeInfo> preferred = std::nullopt);

        RE::BSScript::TypeInfo ArrayElementType(const RE::BSScript::TypeInfo& array) {
            using Raw = RE::BSScript::TypeInfo::RawType;
            if (array.IsObjectArray())
                return RE::BSScript::TypeInfo{array.GetTypeInfo()->GetRawType()};
            return RE::BSScript::TypeInfo{
                static_cast<Raw>(std::to_underlying(array.GetRawType()) - std::to_underlying(Raw::kNoneArray))};
        }

        ValueCategory Category(const Variable& value) {
            if (value.IsArray())
                return ValueCategory::Array;
            if (value.IsInt())
                return ValueCategory::Integer;
            if (value.IsFloat())
                return ValueCategory::Float;
            if (value.IsBool())
                return ValueCategory::Boolean;
            if (value.IsString())
                return ValueCategory::String;
            if (value.IsObject() && value.GetObject())
                return ValueCategory::Object;
            return ValueCategory::None;
        }

        std::optional<RE::BSScript::TypeInfo> InferElementType(VM* vm, const std::vector<Variable>& values) {
            std::vector<ValueCategory> categories;
            for (const auto& value : values)
                categories.push_back(Category(value));
            const auto category = InferArrayCategory(categories);
            if (!category)
                return std::nullopt;
            switch (*category) {
                case ValueCategory::Integer:
                    return FindValueType(vm, "int");
                case ValueCategory::Float:
                    return FindValueType(vm, "float");
                case ValueCategory::Boolean:
                    return FindValueType(vm, "bool");
                case ValueCategory::String:
                    return FindValueType(vm, "string");
                case ValueCategory::Object: {
                    Type common;
                    for (const auto& value : values) {
                        if (!value.IsObject() || !value.GetObject())
                            continue;
                        auto* actual = value.GetObject()->GetTypeInfo();
                        if (!common)
                            common.reset(actual);
                        while (common && !Inherits(actual, common->GetName()))
                            common.reset(common->GetParent());
                        if (!common)
                            return std::nullopt;
                    }
                    return common ? std::optional(RE::BSScript::TypeInfo{common->GetRawType()})
                                  : FindValueType(vm, "Form");
                }
                default:
                    return std::nullopt;
            }
        }

        struct PendingArray {
            Ticket ticket;
            ValueExpression expression;
            std::vector<Variable> values;
            std::size_t position = 0;
            std::optional<RE::BSScript::TypeInfo> preferred;
            ValueCallback completed;
        };

        void ContinueArray(const std::shared_ptr<PendingArray>& array) {
            if (!CanContinue(array->ticket))
                return;
            if (array->position < array->expression.elements.size()) {
                EvaluateValue(array->ticket, array->expression.elements[array->position], [array](Variable value) {
                    array->values.push_back(std::move(value));
                    ++array->position;
                    ContinueArray(array);
                });
                return;
            }
            auto* vm = VM::GetSingleton();
            std::optional<RE::BSScript::TypeInfo> element;
            if (!array->expression.elementType.empty())
                element = FindValueType(vm, array->expression.elementType);
            else if (array->preferred && array->preferred->IsArray())
                element = ArrayElementType(*array->preferred);
            else
                element = InferElementType(vm, array->values);
            if (!element) {
                Reject(array->ticket,
                       "Array element type is unknown or incompatible; use int[], float[], bool[], string[] or a "
                       "script type prefix");
                return;
            }
            std::vector<Variable> converted(array->values.size());
            for (std::size_t i = 0; i < converted.size(); ++i) {
                if (!ConvertStored(vm, array->values[i], *element, converted[i])) {
                    Reject(array->ticket, std::format("Array element {} requires {}", i, element->TypeAsString()));
                    return;
                }
            }
            RE::BSTSmartPointer<RE::BSScript::Array> created;
            if (!vm || !vm->CreateArray(*element, static_cast<std::uint32_t>(converted.size()), created) || !created) {
                Reject(array->ticket, "The VM could not allocate the array");
                return;
            }
            for (std::uint32_t i = 0; i < created->size(); ++i)
                (*created)[i] = std::move(converted[i]);
            Variable result;
            result.SetArray(created);
            array->completed(std::move(result));
        }

        void EvaluateValue(Ticket ticket, const ValueExpression& expression, ValueCallback completed,
                           std::optional<RE::BSScript::TypeInfo> preferred) {
            if (!CanContinue(ticket))
                return;
            if (expression.kind == ValueKind::Reference) {
                const auto value = ReadVariable(expression.text);
                if (!value) {
                    Reject(ticket, "Variable missing, pending or failed: $" + expression.text);
                    return;
                }
                StartPropertyRead(ticket, *value, expression.accesses, std::move(completed));
                return;
            }
            if (expression.kind == ValueKind::Array) {
                auto array = std::make_shared<PendingArray>(
                    PendingArray{ticket, expression, {}, 0, preferred, std::move(completed)});
                ContinueArray(array);
                return;
            }
            Variable value;
            switch (expression.kind) {
                case ValueKind::Integer:
                    value.SetSInt(*NumberArgument<std::int32_t>(expression.text));
                    break;
                case ValueKind::Float:
                    value.SetFloat(*NumberArgument<float>(expression.text));
                    break;
                case ValueKind::Boolean:
                    value.SetBool(Equal(expression.text, "true"));
                    break;
                case ValueKind::String:
                    value.SetString(expression.text);
                    break;
                case ValueKind::None:
                    value.SetNone();
                    break;
                case ValueKind::BareText:
                    if (!preferred || !preferred->IsString()) {
                        Reject(ticket, "Quote string literals; form arguments use @HEXID and variables use $name");
                        return;
                    }
                    value.SetString(expression.text);
                    break;
                case ValueKind::Form: {
                    const auto id = FormID(expression.text);
                    auto* form = id ? RE::TESForm::LookupByID(*id) : nullptr;
                    auto* vm = VM::GetSingleton();
                    Type native;
                    if (!form || !vm ||
                        !vm->GetScriptObjectType(static_cast<RE::VMTypeID>(form->GetFormType()), native)) {
                        Reject(ticket, "Form/reference ID not found or native type unavailable: " + expression.text);
                        return;
                    }
                    auto object = Bound(vm, form, native);
                    if (!object) {
                        Reject(ticket, "Native form wrapper unavailable: " + expression.text);
                        return;
                    }
                    value.SetObject(object);
                    break;
                }
                default:
                    return;
            }
            completed(std::move(value));
        }

        void ApplyPropertySet(Ticket ticket, Variable receiver, const AccessStep& destination, Variable source) {
            if (!CanContinue(ticket))
                return;
            if (destination.kind == AccessKind::Index) {
                Reject(ticket, "Direct array element writes are unsupported; set an object property instead");
                return;
            }
            if (!receiver.IsObject() || !receiver.GetObject()) {
                Reject(ticket, "Property writes require an object; array Length is read-only");
                return;
            }
            auto object = receiver.GetObject();
            auto* vm = VM::GetSingleton();
            PropertyValue property;
            std::string reason;
            if (!ResolveProperty(vm, object, destination.name, property, reason, true)) {
                Reject(ticket, std::move(reason));
                return;
            }
            if (!property.writable && !property.setter) {
                Reject(ticket, "Property is read-only: " + property.name);
                return;
            }
            Variable converted;
            if (!ConvertStored(vm, source, property.type, converted)) {
                Reject(ticket, "Property " + property.name + " requires " + property.type.TypeAsString());
                return;
            }
            const auto label = "Set " + property.name + " = " + Display(converted);
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> completion{new Completion(ticket, {}, label)};
            {
                std::scoped_lock lock(mutex);
                requests.Submitted(ticket);
            }
            if (auto error = DispatchSetter(vm, object, property.name, std::move(converted), completion);
                !error.empty())
                Reject(ticket, std::move(error));
        }

        void StartPropertySet(Ticket ticket, const Command& command) {
            auto receiver = command.destination;
            const auto destination = receiver.accesses.back();
            receiver.accesses.pop_back();
            EvaluateValue(ticket, receiver, [ticket, command, destination](Variable parent) {
                // Resolve the destination type before constructing an untyped array RHS.
                std::optional<RE::BSScript::TypeInfo> preferred;
                if (parent.IsObject() && parent.GetObject() && destination.kind == AccessKind::Property) {
                    PropertyValue property;
                    std::string reason;
                    if (!ResolveProperty(VM::GetSingleton(), parent.GetObject(), destination.name, property, reason,
                                         true)) {
                        Reject(ticket, std::move(reason));
                        return;
                    }
                    if (!property.writable && !property.setter) {
                        Reject(ticket, "Property is read-only: " + property.name);
                        return;
                    }
                    preferred = property.type;
                }
                EvaluateValue(
                    ticket, command.value,
                    [ticket, parent, destination](Variable source) {
                        ApplyPropertySet(ticket, parent, destination, std::move(source));
                    },
                    preferred);
            });
        }

        struct PendingArguments {
            Ticket ticket;
            Command command;
            Function function;
            Object object;
            std::vector<Variable> values;
            std::size_t position = 0;
        };

        void ContinueArguments(const std::shared_ptr<PendingArguments>& call) {
            if (!CanContinue(call->ticket))
                return;
            auto* vm = VM::GetSingleton();
            if (call->position < call->command.arguments.size()) {
                RE::BSFixedString name;
                RE::BSScript::TypeInfo type;
                call->function->GetParam(static_cast<std::uint32_t>(call->position), name, type);
                EvaluateValue(
                    call->ticket, call->command.arguments[call->position],
                    [call, type, name](Variable value) {
                        Variable converted;
                        if (!ConvertStored(VM::GetSingleton(), value, type, converted)) {
                            Reject(call->ticket, std::format("Argument {} ({}) requires {}", call->position + 1,
                                                             name.c_str(), type.TypeAsString()));
                            return;
                        }
                        call->values.push_back(std::move(converted));
                        ++call->position;
                        ContinueArguments(call);
                    },
                    type);
                return;
            }
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> completion{
                new Completion(call->ticket, call->command.assignment)};
            {
                std::scoped_lock lock(mutex);
                requests.Submitted(call->ticket);
            }
            auto* arguments = new Arguments(std::move(call->values));
            const bool accepted =
                call->command.operation == Operation::Global
                    ? vm->DispatchStaticCall(RE::BSFixedString(call->command.script),
                                             RE::BSFixedString(call->command.function), arguments, completion)
                    : vm->DispatchMethodCall(call->object, RE::BSFixedString(call->command.function), arguments,
                                             completion);
            if (!accepted)
                Reject(call->ticket, "The VM rejected dispatch");
        }

        bool VariableCommand(Ticket ticket, const Command& command) {
            if (command.operation == Operation::Value) {
                EvaluateValue(ticket, command.value, [ticket, assignment = command.assignment](Variable value) {
                    FinishValue(ticket, std::move(value), assignment);
                });
                return true;
            }
            if (command.operation == Operation::Set) {
                StartPropertySet(ticket, command);
                return true;
            }
            if (command.operation != Operation::Variables && command.operation != Operation::Unset &&
                command.operation != Operation::ClearVariables)
                return false;
            std::scoped_lock lock(mutex);
            std::string text;
            switch (command.operation) {
                case Operation::Variables:
                    for (const auto& [name, binding] : variables.Entries()) {
                        text += "$" + name + " = " +
                                (binding.pending ? "Pending"
                                 : binding.value ? Display(*binding.value)
                                                 : "Failed") +
                                '\n';
                        if (text.size() >= 32768) {
                            text += "List truncated; use show $name for individual values";
                            break;
                        }
                    }
                    if (text.empty())
                        text = "No variables";
                    break;
                case Operation::Unset:
                    if (!variables.Unset(command.target)) {
                        requests.Reject(ticket, "Unknown variable: $" + command.target);
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

        void ExecuteEvaluation(Ticket ticket, const Command& command, const std::map<std::string, Variable>& bindings) {
            if (!CanContinue(ticket))
                return;
            if (REL::Module::get().version() != REL::Version{1, 6, 1170, 0}) {
                Reject(ticket, "Compiled evaluation currently requires Skyrim 1.6.1170");
                return;
            }
            bool started = false;
            bool interrupted = false;
            {
                std::scoped_lock lock(mutex);
                started = evaluationGate.Begin(ticket);
                interrupted = evaluationGate.PreviousSession(ticket);
            }
            if (!started) {
                Reject(ticket, interrupted
                                   ? "An evaluation was interrupted by loading; restart Skyrim before using eval"
                                   : "Another compiled evaluation is pending; wait for its VM completion");
                return;
            }
            try {
                std::vector<SourceParameter> parameters;
                std::vector<Variable> arguments;
                for (const auto& root : SourceRoots(command.evaluation.expression)) {
                    const auto binding =
                        std::ranges::find_if(bindings, [&](const auto& item) { return Equal(item.first, root); });
                    if (binding == bindings.end())
                        continue;
                    const auto& value = binding->second;
                    if (value.GetType().GetRawType() == RE::BSScript::TypeInfo::RawType::kNone)
                        throw std::runtime_error("Variable " + root + " is None without a declared type");
                    parameters.push_back({value.GetType().TypeAsString(), root});
                    arguments.push_back(value);
                }
                if (parameters.size() > MaximumArguments)
                    throw std::runtime_error("Evaluation uses more than 32 console variables");
                auto input = SnapshotEvaluationImports(VM::GetSingleton(), command.evaluation, parameters);
                {
                    std::scoped_lock lock(mutex);
                    requests.Compiling(ticket);
                }
                evaluationWorker = std::jthread(
                    [ticket, command, input = std::move(input), arguments = std::move(arguments)]() mutable {
                        auto compiled = CompileEvaluation(input);
                        auto* tasks = SKSE::GetTaskInterface();
                        if (!tasks) {
                            ReleaseEvaluation(ticket);
                            Reject(ticket, "SKSE task interface unavailable");
                            return;
                        }
                        tasks->AddTask([ticket, command, compiled = std::move(compiled),
                                        arguments = std::move(arguments)]() mutable {
                            if (!CanContinue(ticket)) {
                                ReleaseEvaluation(ticket);
                                return;
                            }
                            if (!compiled.error.empty()) {
                                ReleaseEvaluation(ticket);
                                Reject(ticket, std::move(compiled.error));
                                return;
                            }
                            try {
                                if (!evaluationTypes.contains(compiled.script) && evaluationTypes.size() >= 128)
                                    throw std::runtime_error(
                                        "128 compiled statements loaded; restart Skyrim before adding more");
                                // Immutable files remain available to saved stacks. Never rewrite an existing type.
                                const auto scripts = GameScriptDirectory();
                                CopyEvaluationBytecode(compiled, scripts);
                                auto* vm = VM::GetSingleton();
                                Type type;
                                if (!vm || !vm->GetScriptObjectType(RE::BSFixedString(compiled.script), type) || !type)
                                    throw std::runtime_error("Skyrim could not load the generated script");
                                evaluationTypes.insert(compiled.script);
                                SKSE::log::info("Compiled evaluation {}: {}.Run, {} arguments", ticket.id,
                                                compiled.script, arguments.size());
                                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> completion{
                                    new Completion(ticket, command.assignment, {}, true)};
                                {
                                    std::scoped_lock lock(mutex);
                                    requests.Submitted(ticket);
                                }
                                if (!vm->DispatchStaticCall(RE::BSFixedString(compiled.script), "Run",
                                                            new Arguments(std::move(arguments)), completion)) {
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
            if (!vm || !vm->GetScriptObjectType(RE::BSFixedString(command.script), type) || !type) {
                Reject(ticket, "Script type unavailable: " + command.script);
                return;
            }
            if (command.operation == Operation::Methods) {
                const auto text = Methods(type);
                std::scoped_lock lock(mutex);
                requests.Complete(ticket, text);
                return;
            }
            const bool global = command.operation == Operation::Global;
            Object object;
            if (!global) {
                const auto id = FormID(command.target);
                if (VariableReference(command.target)) {
                    const auto value = ReadVariable(std::string_view(command.target).substr(1));
                    object = value ? StoredObject(vm, *value, type) : Object{};
                } else {
                    object = Bound(vm, id ? RE::TESForm::LookupByID(*id) : nullptr, type);
                }
                if (!object) {
                    Reject(ticket,
                           "No initialized script instance on this receiver; referenced variables must be completed "
                           "objects");
                    return;
                }
            }
            if (command.operation == Operation::Property) {
                Variable receiver;
                receiver.SetObject(object);
                StartPropertyRead(ticket, std::move(receiver), {AccessStep::Property(command.function)},
                                  [ticket, assignment = command.assignment](Variable value) {
                                      FinishValue(ticket, std::move(value), assignment);
                                  });
                return;
            }
            const auto function =
                FindFunction(type, command.function, global, object ? object->currentState.c_str() : "");
            if (!function || function->GetIsStatic() != global) {
                Reject(ticket, "Function unavailable; use methods " + command.script);
                return;
            }
            if (command.arguments.size() != function->GetParamCount()) {
                Reject(ticket, "Supply every parameter, including optional ones. " + Signature(function.get()));
                return;
            }
            if (!command.assignment.empty() &&
                function->GetReturnType().GetRawType() == RE::BSScript::TypeInfo::RawType::kNone) {
                Reject(ticket, "This function has no return value");
                return;
            }
            ContinueArguments(
                std::make_shared<PendingArguments>(PendingArguments{ticket, command, function, object, {}, 0}));
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
            const auto created = requests.Begin(std::format("Use instance {} as ${}", id, variable));
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
            variables.Complete(variable, ticket, value);
            requests.Complete(ticket, "$" + variable + " = " + Display(value));
        });
        return {};
    }

    std::string Submit(std::string line) {
        const auto parsed = Parse(line);
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
            if (const auto* error = std::get_if<std::string>(&parsed)) {
                requests.Reject(ticket, *error);
                return {};
            }
        }
        const auto& command = std::get<Command>(parsed);
        std::map<std::string, Variable> bindings;
        if (command.operation == Operation::Evaluate) {
            std::scoped_lock lock(mutex);
            for (const auto& [name, binding] : variables.Entries())
                if (!binding.pending && binding.value)
                    bindings.emplace(name, *binding.value);
        }
        if (!command.assignment.empty()) {
            std::scoped_lock lock(mutex);
            if (!requests.Current(ticket))
                return {};
            if (!variables.Reserve(command.assignment, ticket)) {
                requests.Reject(ticket, "64 variables are stored; unset one or use clearvars");
                return {};
            }
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

    std::vector<Entry> Snapshot() {
        std::scoped_lock lock(mutex);
        return requests.Entries();
    }

    void ClearFinished() {
        std::scoped_lock lock(mutex);
        requests.ClearFinished();
    }

    void SessionChanged(bool ready) {
        std::scoped_lock lock(mutex);
        requests.Loaded();
        variables.Clear();
        instances.clear();
        explorer = {};
        ++scanRevision;
        gameReady = ready;
    }
}
