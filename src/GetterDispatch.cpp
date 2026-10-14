#include "GetterDispatch.h"

// The pinned CommonLib declares this destructor without defining it. The base
// owns only its reference counter and padding; our query owns and releases its
// object reference and property name through the derived destructor.
namespace RE::BSScript::Internal {
    IFuncCallQuery::~IFuncCallQuery() = default;
}

namespace papyrus_console {
    namespace {
        using VM = RE::BSScript::Internal::VirtualMachine;
        using Object = RE::BSTSmartPointer<RE::BSScript::Object>;
        using Query = RE::BSScript::Internal::IFuncCallQuery;
        using Callback = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>;
        using Stack = RE::BSTSmartPointer<RE::BSScript::Stack>;

        class AccessorQuery final : public Query {
        public:
            AccessorQuery(Object object, std::string_view property, std::optional<RE::BSScript::Variable> value)
                : object_(std::move(object)), property_(property), value_(std::move(value)) {}

            bool GetFunctionCallInfo(CallType& kind, RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo>& type,
                                     RE::BSFixedString& name, RE::BSScript::Variable& self,
                                     RE::BSScrapArray<RE::BSScript::Variable>& arguments) override {
                kind = value_ ? CallType::kSetter : CallType::kGetter;
                type.reset(object_->GetTypeInfo());
                name = property_;
                self.SetObject(object_);
                arguments.resize(value_ ? 1 : 0);
                if (value_)
                    arguments[0] = *value_;
                return true;
            }

        private:
            Object object_;
            RE::BSFixedString property_;
            std::optional<RE::BSScript::Variable> value_;
        };
    }

    static std::string DispatchAccessor(VM* vm, const Object& object, const std::string& property,
                                        std::optional<RE::BSScript::Variable> value, Callback& completion) {
        const auto version = REL::Module::get().version();
        if (version != REL::Version{1, 6, 1170, 0})
            return "Property writes and computed reads currently require Skyrim 1.6.1170; ordinary calls and auto "
                   "reads remain "
                   "available";

        // These internal ABIs were verified against 1.6.1170's DispatchStaticCall.
        // CreateStack registers a normal stack with its callback; QueueCall owns
        // the call query until the VM executes it. Do not call IFunction::Call
        // directly or treat queue acceptance as accessor completion.
        // See docs/vm-integration.md for the signatures and runtime boundary.
        using CreateStack = void (*)(VM*, RE::BSScript::Stack::StackType, Callback&, Stack&);
        using QueueCall = bool (*)(VM*, RE::BSScript::Stack*, RE::BSTSmartPointer<Query>&);
        const REL::Relocation<CreateStack> createStack{REL::ID{104870}};
        const REL::Relocation<QueueCall> queueCall{REL::ID{104848}};

        RE::BSTSmartPointer<Query> query{new AccessorQuery(object, property, std::move(value))};
        Stack stack;
        createStack(vm, RE::BSScript::Stack::StackType::kNormal, completion, stack);
        if (!stack)
            return "The VM could not allocate an accessor stack";
        if (!queueCall(vm, stack.get(), query))
            return "The VM rejected accessor dispatch: " + property;
        return {};
    }

    std::string DispatchGetter(VM* vm, const Object& object, const std::string& property, Callback& completion) {
        return DispatchAccessor(vm, object, property, std::nullopt, completion);
    }

    std::string DispatchSetter(VM* vm, const Object& object, const std::string& property, RE::BSScript::Variable value,
                               Callback& completion) {
        return DispatchAccessor(vm, object, property, std::move(value), completion);
    }
}
