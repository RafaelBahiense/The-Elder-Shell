#include "Menu.h"
#include "vm/VM.h"
#include "History.h"
#include "Suggestions.h"
#include "CompletionControls.h"
#include "runtime/Compatibility.h"
#include "ModInfo.h"
#include "shell/Completion.h"

#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>
#include <array>

namespace elder_shell {
    namespace {
        std::array<char, 4097> input{};
        std::array<char, 4097> otherModeInput{};
        bool multiline = false;
        std::string error;
        History history;
        std::string transcript;
        std::vector<char> output{'\0'};
        bool focusCommand = false;
        bool promptActive = false;
        std::size_t commandCursor = 0;
        CompletionResult completion;
        Suggestions suggestions;
        std::optional<CompletionItem> chosenCompletion;
        std::array<char, 32> explorerForm{};
        std::array<char, 128> explorerScript{};
        std::array<char, 128> instanceVariable{'i', 'n', 's', 't', 'a', 'n', 'c', 'e', '\0'};
        std::string explorerError;

        void __stdcall RenderInstances() {
            ImGuiMCP::TextWrapped(
                "Browse existing attached scripts. Select an instance to store it as a console variable.");
            ImGuiMCP::InputText("Form/reference ID (optional)", explorerForm.data(), explorerForm.size());
            ImGuiMCP::InputText("Script name (optional)", explorerScript.data(), explorerScript.size());
            if (ImGuiMCP::Button("Search"))
                explorerError = ExploreInstances(explorerForm.data(), explorerScript.data());
            ImGuiMCP::InputText("Store selected instance as", instanceVariable.data(), instanceVariable.size());
            ImGuiMCP::TextUnformatted("Use it in Console: instance.Function() or instance.Property");
            if (!explorerError.empty())
                ImGuiMCP::TextWrapped("%s", explorerError.c_str());
            const auto snapshot = InstanceSnapshot();
            ImGuiMCP::TextWrapped("%s", snapshot.status.c_str());
            if (ImGuiMCP::BeginChild("Instances", ImGuiMCP::ImVec2{0, 0}, ImGuiMCP::ImGuiChildFlags_Border)) {
                for (const auto& row : snapshot.rows) {
                    const auto identifier = std::to_string(row.id);
                    ImGuiMCP::PushID(identifier.c_str());
                    ImGuiMCP::Text("%s | state: %s | VM handle: %s", row.script.c_str(),
                                   row.state.empty() ? "default" : row.state.c_str(), row.handle.c_str());
                    ImGuiMCP::BeginDisabled(!row.initialized);
                    if (ImGuiMCP::Button("Use instance"))
                        explorerError = UseInstance(row.id, instanceVariable.data());
                    ImGuiMCP::EndDisabled();
                    if (!row.initialized)
                        ImGuiMCP::TextUnformatted("Not initialized");
                    ImGuiMCP::PopID();
                    ImGuiMCP::Separator();
                }
            }
            ImGuiMCP::EndChild();
        }

        void Run() {
            if (!multiline)
                history.Add(input.data());
            error = Submit(input.data(), multiline);
            if (error.empty() && !multiline)
                input.fill('\0');
            completion = {};
            suggestions.Dismiss();
            commandCursor = std::char_traits<char>::length(input.data());
            focusCommand = true;
        }

        void InsertCompletion(ImGuiMCP::ImGuiInputTextCallbackData* data, const CompletionContext& context,
                              const CompletionItem& item) {
            const auto edit = CompletionInsertion(
                std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)), context, item);
            const auto& name = edit.text;
            if (context.end > static_cast<std::size_t>(data->BufTextLen) ||
                data->BufTextLen - (context.end - context.begin) + name.size() >= input.size())
                return;
            ImGuiMCP::ImGuiInputTextCallbackDataManager::DeleteChars(data, static_cast<int>(context.begin),
                                                                     static_cast<int>(context.end - context.begin));
            ImGuiMCP::ImGuiInputTextCallbackDataManager::InsertChars(data, static_cast<int>(context.begin), name.data(),
                                                                     name.data() + name.size());
            data->CursorPos =
                static_cast<int>(std::min(context.begin + edit.cursor, static_cast<std::size_t>(data->BufTextLen)));
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
            history.ResetNavigation();
            suggestions.Update(std::format("{}:{}", data->CursorPos, data->Buf), 0);
            suggestions.Dismiss();
        }

        int InputCallback(ImGuiMCP::ImGuiInputTextCallbackData* data) {
            if (chosenCompletion) {
                if (const auto environment = CompletionMetadataSnapshot()) {
                    const auto result = Complete(data->Buf, static_cast<std::size_t>(data->CursorPos), *environment);
                    InsertCompletion(data, result.context, *chosenCompletion);
                }
                chosenCompletion.reset();
            }
            commandCursor = static_cast<std::size_t>(data->CursorPos);
            if (data->EventFlag == ImGuiMCP::ImGuiInputTextFlags_CallbackEdit) {
                history.ResetNavigation();
                return 0;
            }
            if (multiline || data->EventFlag != ImGuiMCP::ImGuiInputTextFlags_CallbackHistory)
                return 0;

            const auto command =
                data->EventKey == ImGuiMCP::ImGuiKey_UpArrow
                    ? history.Previous(std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)))
                    : history.Next();
            if (command) {
                ImGuiMCP::ImGuiInputTextCallbackDataManager::DeleteChars(data, 0, data->BufTextLen);
                ImGuiMCP::ImGuiInputTextCallbackDataManager::InsertChars(data, 0, command->data(),
                                                                         command->data() + command->size());
                data->CursorPos = data->BufTextLen;
                data->SelectionStart = data->SelectionEnd = data->CursorPos;
                commandCursor = static_cast<std::size_t>(data->CursorPos);
            }
            return 0;
        }

        float PromptHeight() {
            return multiline ? ImGuiMCP::GetTextLineHeightWithSpacing() * 6 + ImGuiMCP::GetStyle()->FramePadding.y * 2
                             : ImGuiMCP::GetFrameHeight();
        }

        void RenderOutput(const std::vector<Entry>& entries, float assistanceHeight) {
            std::string text;
            for (const auto& entry : entries) {
                text += std::format("#{} - {}\n> {}\n{}\n\n", entry.ticket.id, StatusName(entry.status), entry.command,
                                    entry.result);
            }
            // Keep the buffer stable between updates so selection survives rendering.
            if (text != transcript) {
                transcript = std::move(text);
                output.assign(transcript.begin(), transcript.end());
                output.push_back('\0');
            }
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            available.y = std::max(1.0f, available.y - PromptHeight() - ImGuiMCP::GetStyle()->ItemSpacing.y -
                                             ImGuiMCP::GetStyle()->ItemSpacing.y - assistanceHeight);
            ImGuiMCP::InputTextMultiline("##Output", output.data(), output.size(), available,
                                         ImGuiMCP::ImGuiInputTextFlags_ReadOnly, nullptr, nullptr);
        }

        void RenderUsage() {
            const auto usage = EvaluationUsageSnapshot();
            ImGuiMCP::Text("Loaded types: %zu | Last evaluation: %zu inputs, %zu imports", usage.loadedTypes,
                           usage.inputVariables, usage.importedTypes);
            if (usage.loadedTypes >= EvaluationTypeWarningThreshold)
                ImGuiMCP::TextWrapped(
                    "Many evaluation types are loaded. Evaluations remain available. Retained VM memory is not "
                    "measured; restart Skyrim to release loaded types.");
            if (usage.inputVariables >= InputVariableWarningThreshold)
                ImGuiMCP::TextWrapped("Many input variables. This is advisory; evaluation is not blocked.");
            if (usage.importedTypes >= ImportedTypeWarningThreshold)
                ImGuiMCP::TextWrapped("Large import graph. Compilation may take longer; evaluation is not blocked.");
        }

        void RenderHelp() {
            if (!ImGuiMCP::CollapsingHeader("Help"))
                return;

            static char text[] =
                "Single-line: Enter runs one statement. Expressions display results.\n"
                "Multiline: Enter inserts a newline when suggestions are closed; Ctrl+Enter runs.\n"
                "Fragments support If/While blocks and comments. Commands change game state.\n\n"
                "Actor player = Game.GetPlayer()\n"
                "Int level = player.GetLevel()\n"
                "level = player.GetLevel()\n"
                "player.GetLevel()\n"
                "Debug.Notification(\"Hello from Papyrus\")\n"
                "var player = Game.GetPlayer()\n"
                "instance.Property = value\n\n"
                "vars\nunset name\nclearvars\nmethods Actor\n\n"
                "Use Game.GetFormEx(0x...) for runtime references.\n"
                "Supply every argument, including optional ones.\n"
                "Suggestions: Up/Down selects; Tab/Enter or click accepts; Escape dismisses.\n"
                "Ctrl+Space opens suggestions. Mouse wheel scrolls the list.\n"
                "With suggestions closed: single-line Up/Down recalls history.\n"
                "With suggestions closed: multiline arrows move the cursor; Tab indents.\n"
                "Ctrl+C: copy selection. Ctrl+A: select focused text.\n"
                "Variables clear on save load. Loaded types count since Skyrim started.\n"
                "Input and import counts describe the last prepared evaluation.";
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            const auto lineHeight = ImGuiMCP::GetTextLineHeightWithSpacing();
            const auto height = std::clamp(available.y * 0.4f, lineHeight * 3, lineHeight * 10);
            ImGuiMCP::InputTextMultiline("##HelpText", text, sizeof(text), ImGuiMCP::ImVec2{0, height},
                                         ImGuiMCP::ImGuiInputTextFlags_ReadOnly, nullptr, nullptr);
        }

        void RenderStatus(const std::vector<Entry>& entries) {
            std::size_t pending = 0;
            const Entry* latestPending = nullptr;
            for (const auto& entry : entries) {
                if (entry.status == Status::Queued || entry.status == Status::Compiling ||
                    entry.status == Status::Submitted) {
                    ++pending;
                    latestPending = &entry;
                }
            }

            if (latestPending) {
                ImGuiMCP::Text("%zu pending | #%llu: %s", pending,
                               static_cast<unsigned long long>(latestPending->ticket.id),
                               StatusName(latestPending->status));
                ImGuiMCP::TextWrapped(
                    "If a call waits for the paused game, close this menu to let it proceed. "
                    "Calls are never automatically repeated.");
            } else if (!entries.empty()) {
                ImGuiMCP::Text("#%llu: %s", static_cast<unsigned long long>(entries.back().ticket.id),
                               StatusName(entries.back().status));
            } else {
                ImGuiMCP::TextUnformatted("Ready");
            }
        }

        class PromptKeyMask {
        public:
            explicit PromptKeyMask(ImGuiMCP::ImGuiID owner) : owner_(owner) {}

            PromptKeyMask(const PromptKeyMask&) = delete;
            PromptKeyMask& operator=(const PromptKeyMask&) = delete;

            void Consume(ImGuiMCP::ImGuiKey key) {
                auto* state = ImGuiMCP::GetKeyData(key);
                saved_.push_back({key, state, *state});
                state->Down = false;
                state->DownDuration = state->DownDurationPrev = -1;
                state->AnalogValue = 0;
            }

            ~PromptKeyMask() {
                for (const auto& [key, state, saved] : saved_) {
                    *state = saved;
                    ImGuiMCP::SetKeyOwner(key, owner_, ImGuiMCP::ImGuiInputFlags_LockUntilRelease);
                }
            }

        private:
            // ImGui's multiline editor consumes arrows/Enter before callbacks.
            // Hide handled presses only during this widget, then restore data
            // and claim the press so it does not bubble to the surrounding UI.
            struct SavedKey {
                ImGuiMCP::ImGuiKey key;
                ImGuiMCP::ImGuiKeyData* state;
                ImGuiMCP::ImGuiKeyData value;
            };

            std::vector<SavedKey> saved_;
            ImGuiMCP::ImGuiID owner_;
        };

        bool RouteCompletionKeys(PromptKeyMask& keys) {
            const auto owner = ImGuiMCP::GetID(multiline ? "##Fragment" : "##Command");
            if (ImGuiMCP::GetActiveID() != owner)
                return false;
            const auto query = std::format("{}:{}", commandCursor, input.data());
            const auto environment = CompletionMetadataSnapshot();
            const auto result =
                environment ? Complete(input.data(), commandCursor, *environment, suggestions.ExplicitFor(query))
                            : CompletionResult{};
            suggestions.Update(query, result.items.size());
            const auto& io = *ImGuiMCP::GetIO();
            const std::pair<ImGuiMCP::ImGuiKey, CompletionKey> bindings[] = {
                {ImGuiMCP::ImGuiKey_UpArrow, CompletionKey::Up},
                {ImGuiMCP::ImGuiKey_DownArrow, CompletionKey::Down},
                {ImGuiMCP::ImGuiKey_Enter, CompletionKey::Enter},
                {ImGuiMCP::ImGuiKey_KeypadEnter, CompletionKey::Enter},
                {ImGuiMCP::ImGuiKey_Tab, CompletionKey::Tab},
                {ImGuiMCP::ImGuiKey_Escape, CompletionKey::Escape},
                {ImGuiMCP::ImGuiKey_Space, CompletionKey::Space}};
            bool submit = false;
            for (const auto& [key, logical] : bindings) {
                const bool repeat = logical == CompletionKey::Up || logical == CompletionKey::Down;
                if (!ImGuiMCP::IsKeyPressed(key, repeat ? ImGuiMCP::ImGuiInputFlags_Repeat : 0, owner))
                    continue;
                const auto action =
                    CompletionShortcut(logical, suggestions.Visible(), multiline, io.KeyCtrl, io.KeyShift, io.KeyAlt);
                if (action == CompletionAction::None)
                    continue;
                keys.Consume(key);
                switch (action) {
                    case CompletionAction::Previous:
                        suggestions.Move(false);
                        break;
                    case CompletionAction::Next:
                        suggestions.Move(true);
                        break;
                    case CompletionAction::Dismiss:
                        suggestions.Dismiss();
                        break;
                    case CompletionAction::Open:
                        suggestions.Open();
                        break;
                    case CompletionAction::Accept:
                        chosenCompletion = result.items[suggestions.Selected()];
                        break;
                    case CompletionAction::Submit:
                        submit = true;
                        break;
                    default:
                        break;
                }
            }
            return submit;
        }

        void RenderPrompt() {
            ImGuiMCP::ImVec2 rowPosition;
            ImGuiMCP::GetCursorPos(&rowPosition);
            ImGuiMCP::AlignTextToFramePadding();
            ImGuiMCP::TextUnformatted(">");
            ImGuiMCP::SameLine();
            const auto& style = *ImGuiMCP::GetStyle();
            ImGuiMCP::ImVec2 labelSize;
            ImGuiMCP::CalcTextSize(&labelSize, "Execute", nullptr, false, -1.0f);
            const auto buttonWidth = labelSize.x + style.FramePadding.x * 2;
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            ImGuiMCP::ImVec2 inputPosition;
            ImGuiMCP::GetCursorPos(&inputPosition);
            const auto buttonX = inputPosition.x + available.x - buttonWidth;
            const auto width = std::max(1.0f, available.x - buttonWidth - style.ItemSpacing.x);
            ImGuiMCP::SetNextItemWidth(width);
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg, ImGuiMCP::ImVec4{0, 0, 0, 0});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 0.0f);
            if (focusCommand) {
                ImGuiMCP::SetKeyboardFocusHere();
                focusCommand = false;
            }
            const auto flags = ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue |
                               ImGuiMCP::ImGuiInputTextFlags_CallbackEdit |
                               ImGuiMCP::ImGuiInputTextFlags_CallbackAlways;
            bool submitted = false;
            bool shortcutSubmit = false;
            {
                PromptKeyMask keys(ImGuiMCP::GetID(multiline ? "##Fragment" : "##Command"));
                shortcutSubmit = RouteCompletionKeys(keys);
                submitted = multiline ? ImGuiMCP::InputTextMultiline(
                                            "##Fragment", input.data(), input.size(), {width, PromptHeight()},
                                            flags | ImGuiMCP::ImGuiInputTextFlags_AllowTabInput, InputCallback, nullptr)
                                      : ImGuiMCP::InputText("##Command", input.data(), input.size(),
                                                            flags | ImGuiMCP::ImGuiInputTextFlags_CallbackHistory,
                                                            InputCallback);
            }
            promptActive = ImGuiMCP::IsItemActive();
            if (promptActive && suggestions.Visible()) {
                // Reserve next frame's keys before ImGui's focus navigation.
                const auto owner = ImGuiMCP::GetID(multiline ? "##Fragment" : "##Command");
                for (const auto key :
                     {ImGuiMCP::ImGuiKey_UpArrow, ImGuiMCP::ImGuiKey_DownArrow, ImGuiMCP::ImGuiKey_Enter,
                      ImGuiMCP::ImGuiKey_KeypadEnter, ImGuiMCP::ImGuiKey_Tab, ImGuiMCP::ImGuiKey_Escape})
                    ImGuiMCP::SetKeyOwner(key, owner, ImGuiMCP::ImGuiInputFlags_LockUntilRelease);
            }
            ImGuiMCP::PopStyleVar();
            ImGuiMCP::PopStyleColor();
            ImGuiMCP::SameLine();
            ImGuiMCP::SetCursorPos({buttonX, rowPosition.y});
            const auto clicked = ImGuiMCP::Button("Execute", ImGuiMCP::ImVec2{buttonWidth, 0});
            if (clicked || submitted || shortcutSubmit)
                Run();
        }

        float PrepareCompletion() {
            const auto context = CompletionAt(input.data(), commandCursor);
            auto expression = context.receiver;
            if (!context.callReceiver.empty()) {
                if (!expression.empty())
                    expression += " + ";
                expression += context.callReceiver;
            }
            RefreshCompletionMetadata(std::move(expression));
            completion = {};
            if (promptActive || focusCommand) {
                if (const auto environment = CompletionMetadataSnapshot())
                    completion = Complete(input.data(), commandCursor, *environment,
                                          suggestions.ExplicitFor(std::format("{}:{}", commandCursor, input.data())));
            }
            suggestions.Update(std::format("{}:{}", commandCursor, input.data()), completion.items.size());
            if (!suggestions.Visible())
                completion.items.clear();
            const auto rows = std::min(Suggestions::VisibleRows, completion.items.size()) +
                              (completion.signature.empty() ? 0 : 1) + (completion.parameter.empty() ? 0 : 1) +
                              (completion.items.empty() ? 0 : 1);
            return rows == 0 ? 0
                             : static_cast<float>(rows) * ImGuiMCP::GetTextLineHeightWithSpacing() +
                                   ImGuiMCP::GetStyle()->WindowPadding.y * 2 + ImGuiMCP::GetStyle()->ItemSpacing.y;
        }

        void RenderCompletion(float height) {
            if (height <= 0)
                return;
            if (ImGuiMCP::BeginChild("Completion", ImGuiMCP::ImVec2{0, height - ImGuiMCP::GetStyle()->ItemSpacing.y})) {
                if (!completion.signature.empty())
                    ImGuiMCP::TextUnformatted(completion.signature.c_str());
                if (!completion.parameter.empty())
                    ImGuiMCP::TextUnformatted(completion.parameter.c_str());
                if (!completion.items.empty())
                    ImGuiMCP::Text("%zu/%zu | Up/Down: select | Tab/Enter: accept | Esc: dismiss",
                                   suggestions.Selected() + 1, completion.items.size());
                const bool revealSelection = suggestions.TakeRevealRequest();
                for (std::size_t i = 0; i < completion.items.size(); ++i) {
                    const auto& item = completion.items[i];
                    const auto label = item.name + " : " + item.description;
                    if (ImGuiMCP::Selectable(label.c_str(), i == suggestions.Selected())) {
                        chosenCompletion = item;
                        focusCommand = true;
                    }
                    if (revealSelection && i == suggestions.Selected())
                        ImGuiMCP::SetScrollHereY();
                }
            }
            ImGuiMCP::EndChild();
        }

        void __stdcall Render() {
            if (!IsTestedRuntime())
                ImGuiMCP::TextWrapped("%s", UntestedRuntimeWarning.data());

            if (ImGuiMCP::Button("Clear finished"))
                ClearFinished();
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Checkbox("Multiline", &multiline)) {
                input.swap(otherModeInput);
                commandCursor = std::char_traits<char>::length(input.data());
                chosenCompletion.reset();
                suggestions = {};
                completion = {};
                history.ResetNavigation();
                focusCommand = true;
            }
            if (multiline)
                ImGuiMCP::TextUnformatted(
                    "Enter: accept suggestion or newline | Ctrl+Enter: run fragment | Top-level variables stay in the "
                    "session");
            if (!error.empty())
                ImGuiMCP::TextWrapped("%s", error.c_str());
            RenderHelp();
            ImGuiMCP::Separator();
            const auto entries = Snapshot();
            RenderStatus(entries);
            RenderUsage();
            const auto assistanceHeight = PrepareCompletion();
            RenderOutput(entries, assistanceHeight);
            RenderCompletion(assistanceHeight);
            RenderPrompt();
        }
    }

    void RegisterMenu() {
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::error("{} requires SKSE Menu Framework for its interface", mod::info::DisplayName);
            return;
        }
        SKSEMenuFramework::SetSection(mod::info::DisplayName);
        SKSEMenuFramework::AddSectionItem("Console", Render);
        SKSEMenuFramework::AddSectionItem("Variables", RenderVariables);
        SKSEMenuFramework::AddSectionItem("Instances", RenderInstances);
    }
}
