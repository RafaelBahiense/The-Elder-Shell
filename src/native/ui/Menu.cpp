#include "Menu.h"
#include "Layout.h"
#include "vm/VM.h"
#include "History.h"
#include "Suggestions.h"
#include "CompletionControls.h"
#include "runtime/Compatibility.h"
#include "runtime/CompletionInput.h"
#include "ModInfo.h"
#include "shell/Completion.h"
#include "shell/Command.h"

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
            static std::uint64_t selected = 0;
            ui::Heading("ATTACHED SCRIPT INSTANCES");
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            const bool wide = ui::Wide(available.x);
            const float filterWidth = wide ? std::min(available.x * 0.32f, ImGuiMCP::GetFontSize() * 16) : available.x;
            bool search = false;
            if (wide) {
                if (ImGuiMCP::BeginTable("Instance search", 3, ImGuiMCP::ImGuiTableFlags_SizingFixedFit)) {
                    ImGuiMCP::TableSetupColumn("Form", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, filterWidth);
                    ImGuiMCP::TableSetupColumn("Script", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, filterWidth);
                    ImGuiMCP::TableSetupColumn("Action", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                    ImGuiMCP::TableNextRow();
                    ImGuiMCP::TableSetColumnIndex(0);
                    ImGuiMCP::TextDisabled("Form/reference ID (optional)");
                    ImGuiMCP::TableSetColumnIndex(1);
                    ImGuiMCP::TextDisabled("Script name (optional)");
                    ImGuiMCP::TableNextRow();
                    ImGuiMCP::TableSetColumnIndex(0);
                    ImGuiMCP::SetNextItemWidth(filterWidth);
                    ImGuiMCP::InputText("##InstanceForm", explorerForm.data(), explorerForm.size());
                    ImGuiMCP::TableSetColumnIndex(1);
                    ImGuiMCP::SetNextItemWidth(filterWidth);
                    ImGuiMCP::InputText("##InstanceScript", explorerScript.data(), explorerScript.size());
                    ImGuiMCP::TableSetColumnIndex(2);
                    search = ImGuiMCP::Button("Search");
                    ImGuiMCP::EndTable();
                }
            } else {
                ImGuiMCP::TextDisabled("Form/reference ID (optional)");
                ImGuiMCP::SetNextItemWidth(filterWidth);
                ImGuiMCP::InputText("##InstanceForm", explorerForm.data(), explorerForm.size());
                ImGuiMCP::TextDisabled("Script name (optional)");
                ImGuiMCP::SetNextItemWidth(filterWidth);
                ImGuiMCP::InputText("##InstanceScript", explorerScript.data(), explorerScript.size());
                search = ImGuiMCP::Button("Search");
            }
            if (search) {
                explorerError = ExploreInstances(explorerForm.data(), explorerScript.data());
                selected = 0;
            }
            if (!explorerError.empty())
                ImGuiMCP::TextColored(ui::Failure, "%s", explorerError.c_str());
            const auto snapshot = InstanceSnapshot();
            ImGuiMCP::TextDisabled("%s", snapshot.status.c_str());
            ImGuiMCP::Spacing();
            auto chosen = std::ranges::find_if(snapshot.rows, [](const auto& row) { return row.id == selected; });
            if (chosen == snapshot.rows.end() && !snapshot.rows.empty()) {
                selected = snapshot.rows.front().id;
                chosen = snapshot.rows.begin();
            }
            ImGuiMCP::GetContentRegionAvail(&available);
            const auto listHeight =
                wide ? 0.0f : std::min(available.y * 0.35f, ImGuiMCP::GetTextLineHeightWithSpacing() * 7);
            if (ImGuiMCP::BeginChild("Instance results", {wide ? ui::ListWidth(available.x) : 0, listHeight},
                                     ImGuiMCP::ImGuiChildFlags_Border)) {
                ui::Heading("RESULTS");
                if (snapshot.rows.empty())
                    ImGuiMCP::TextWrapped("Search for attached scripts to inspect an instance.");
                for (const auto& row : snapshot.rows) {
                    const auto id = std::to_string(row.id);
                    ImGuiMCP::PushID(id.c_str());
                    const auto state =
                        row.initialized ? (row.state.empty() ? "Default state" : row.state) : "Not initialized";
                    const auto label = row.script + "\n" + state;
                    if (ImGuiMCP::Selectable(label.c_str(), row.id == selected, 0,
                                             {0, ImGuiMCP::GetTextLineHeightWithSpacing() * 2.5f}))
                        selected = row.id;
                    ImGuiMCP::PopID();
                }
            }
            ImGuiMCP::EndChild();
            if (wide)
                ImGuiMCP::SameLine();
            chosen = std::ranges::find_if(snapshot.rows, [](const auto& row) { return row.id == selected; });
            if (ImGuiMCP::BeginChild("Instance details", {0, 0}, ImGuiMCP::ImGuiChildFlags_Border)) {
                if (chosen == snapshot.rows.end()) {
                    ImGuiMCP::TextDisabled("Select an instance to view its details.");
                } else {
                    ui::Heading(chosen->script.c_str());
                    ImGuiMCP::TextColored(chosen->initialized ? ui::Success : ui::Pending, "%s",
                                          chosen->initialized ? "Initialized" : "Not initialized");
                    ImGuiMCP::Text("State: %s", chosen->state.empty() ? "Default" : chosen->state.c_str());
                    ImGuiMCP::TextDisabled("VM handle: %s", chosen->handle.c_str());
                    ImGuiMCP::Spacing();
                    ImGuiMCP::Separator();
                    ImGuiMCP::Spacing();
                    ImGuiMCP::TextUnformatted("Store as variable");
                    ImGuiMCP::SetNextItemWidth(
                        std::min(ImGuiMCP::GetWindowWidth() * 0.75f, ImGuiMCP::GetFontSize() * 16));
                    ImGuiMCP::InputText("##InstanceVariable", instanceVariable.data(), instanceVariable.size());
                    ImGuiMCP::BeginDisabled(!chosen->initialized);
                    if (ImGuiMCP::Button("Use instance"))
                        explorerError = UseInstance(chosen->id, instanceVariable.data());
                    ImGuiMCP::EndDisabled();
                    if (!chosen->initialized)
                        ImGuiMCP::TextWrapped("This instance must be initialized before it can be used.");
                    else
                        ImGuiMCP::TextWrapped("Use %s.Function() or %s.Property in Console.", instanceVariable.data(),
                                              instanceVariable.data());
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

        struct OutputEntry {
            Ticket owner;
            ui::TextBuffer command;
            ui::TextBuffer result;
        };

        std::vector<OutputEntry> entryOutput;

        ImGuiMCP::ImVec4 StatusColor(Status status) {
            if (status == Status::Rejected || status == Status::Abandoned)
                return ui::Failure;
            return status == Status::Completed ? ui::Success : ui::Pending;
        }

        std::string TranscriptText(const std::vector<Entry>& entries) {
            std::string text;
            for (const auto& entry : entries)
                text += std::format("#{} - {}\n> {}\n{}\n\n", entry.ticket.id, StatusName(entry.status), entry.command,
                                    entry.result);
            return text;
        }

        void RenderOutput(const std::vector<Entry>& entries, float height) {
            std::erase_if(entryOutput, [&](const auto& buffer) {
                return std::ranges::none_of(
                    entries, [&](const auto& entry) { return SameInspectionOwner(entry.ticket, buffer.owner); });
            });
            if (ImGuiMCP::BeginChild("Transcript", {0, height}, ImGuiMCP::ImGuiChildFlags_Border)) {
                const bool atEnd =
                    ImGuiMCP::GetScrollY() >= ImGuiMCP::GetScrollMaxY() - ImGuiMCP::GetTextLineHeightWithSpacing();
                bool changed = false;
                if (entries.empty()) {
                    ui::Heading("PAPYRUS CONSOLE");
                    ImGuiMCP::TextWrapped("Enter a Papyrus statement below. Results appear here.");
                    ImGuiMCP::TextDisabled("Try: var player = Game.GetPlayer()");
                }
                for (const auto& entry : entries) {
                    const auto id = std::to_string(entry.ticket.id);
                    ImGuiMCP::PushID(id.c_str());
                    ImGuiMCP::TextDisabled("#%llu", static_cast<unsigned long long>(entry.ticket.id));
                    ImGuiMCP::SameLine();
                    ImGuiMCP::TextColored(StatusColor(entry.status), "%s", StatusName(entry.status));
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::SmallButton("Copy")) {
                        const auto text = std::format("> {}\n{}", entry.command, entry.result);
                        ImGuiMCP::SetClipboardText(text.c_str());
                    }
                    ImGuiMCP::Spacing();
                    auto buffer = std::ranges::find_if(
                        entryOutput, [&](const auto& item) { return SameInspectionOwner(item.owner, entry.ticket); });
                    if (buffer == entryOutput.end()) {
                        entryOutput.push_back({entry.ticket, {}, {}});
                        buffer = std::prev(entryOutput.end());
                        changed = true;
                    }
                    changed = buffer->command.Update("> " + entry.command) || changed;
                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ui::Accent);
                    buffer->command.Draw("##CommandText", 5);
                    ImGuiMCP::PopStyleColor();
                    changed = buffer->result.Update(entry.result) || changed;
                    if (entry.status == Status::Rejected || entry.status == Status::Abandoned)
                        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ui::Failure);
                    buffer->result.Draw("##Result");
                    if (entry.status == Status::Rejected || entry.status == Status::Abandoned)
                        ImGuiMCP::PopStyleColor();
                    ImGuiMCP::Spacing();
                    ImGuiMCP::Separator();
                    ImGuiMCP::Spacing();
                    ImGuiMCP::PopID();
                }
                if (changed && atEnd)
                    ImGuiMCP::SetScrollHereY(1);
            }
            ImGuiMCP::EndChild();
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
            bool open = true;
            const auto font = ImGuiMCP::GetFontSize();
            ImGuiMCP::SetNextWindowSize({std::min(font * 30, ImGuiMCP::GetWindowWidth() * 0.9f),
                                         std::min(font * 20, ImGuiMCP::GetWindowHeight() * 0.9f)},
                                        ImGuiMCP::ImGuiCond_Appearing);
            if (!ImGuiMCP::BeginPopupModal("Console help", &open))
                return;
            if (ImGuiMCP::BeginTabBar("Help pages")) {
                if (ImGuiMCP::BeginTabItem("Basics")) {
                    ImGuiMCP::TextWrapped(
                        "Enter runs a single statement. Expressions display results; variables remain in the session "
                        "until a save load.");
                    ImGuiMCP::Spacing();
                    static ui::TextBuffer examples;
                    examples.Update(
                        "var player = "
                        "Game.GetPlayer()\nplayer.GetLevel()\nplayer.GetActorValue(\"Health\")\n\nvars\nunset "
                        "player\nclearvars\nmethods Actor");
                    examples.Draw("##Examples");
                    ImGuiMCP::TextWrapped(
                        "Enable Multiline for fragments, If/While blocks and comments. Ctrl+Enter runs the fragment.");
                    ImGuiMCP::EndTabItem();
                }
                if (ImGuiMCP::BeginTabItem("Shortcuts")) {
                    ImGuiMCP::TextWrapped(
                        "Suggestions: Up/Down selects; Tab/Enter accepts; Escape dismisses; Ctrl+Space reopens.");
                    ImGuiMCP::Spacing();
                    ImGuiMCP::TextWrapped(
                        "With suggestions closed: single-line Up/Down recalls history. Multiline Enter adds a newline "
                        "and Tab indents.");
                    ImGuiMCP::Spacing();
                    ImGuiMCP::TextWrapped(
                        "Select result text to copy it with Ctrl+C. Copy all exports the full transcript.");
                    ImGuiMCP::EndTabItem();
                }
                if (ImGuiMCP::BeginTabItem("Services")) {
                    static ui::TextBuffer examples;
                    examples.Update(
                        "var npc = "
                        "ElderShell.Selection.GetConsoleActor()\nElderShell.Forms.Inspect(npc)\nElderShell.Actors."
                        "InspectDialogue(npc)\nElderShell.Console.Execute(\"player.getavinfo health\", None)");
                    examples.Draw("##Services");
                    ImGuiMCP::TextWrapped(
                        "Check that a reference exists before using it. Installed script APIs require every argument; "
                        "packaged shell services expose defaults.");
                    ImGuiMCP::Spacing();
                    ImGuiMCP::TextWrapped(
                        "Calls run in the Papyrus VM. Getters can execute code and changes can persist in a save. "
                        "Console execution captures immediate output only.");
                    ImGuiMCP::EndTabItem();
                }
                ImGuiMCP::EndTabBar();
            }
            ImGuiMCP::EndPopup();
        }

        void RenderStatus(const std::vector<Entry>& entries) {
            const auto pending = std::ranges::count_if(entries, [](const auto& entry) {
                return entry.status == Status::Queued || entry.status == Status::Compiling ||
                       entry.status == Status::Submitted;
            });
            if (pending)
                ImGuiMCP::TextColored(ui::Pending, "%zu pending", static_cast<std::size_t>(pending));
            else if (!entries.empty())
                ImGuiMCP::TextColored(StatusColor(entries.back().status), "#%llu: %s",
                                      static_cast<unsigned long long>(entries.back().ticket.id),
                                      StatusName(entries.back().status));
            else
                ImGuiMCP::TextColored(ui::Success, "Ready");
            if (pending) {
                ImGuiMCP::SameLine();
                ImGuiMCP::TextDisabled("Close paused menus to let calls proceed.");
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

        void RenderPrompt(float height) {
            ImGuiMCP::ImVec2 rowPosition;
            ImGuiMCP::GetCursorPos(&rowPosition);
            ImGuiMCP::AlignTextToFramePadding();
            ImGuiMCP::TextColored(ui::Accent, ">");
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
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg, ImGuiMCP::ImVec4{0.06f, 0.06f, 0.06f, 1});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 1.0f);
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
                                            "##Fragment", input.data(), input.size(), {width, height},
                                            flags | ImGuiMCP::ImGuiInputTextFlags_AllowTabInput, InputCallback, nullptr)
                                      : ImGuiMCP::InputText("##Command", input.data(), input.size(),
                                                            flags | ImGuiMCP::ImGuiInputTextFlags_CallbackHistory,
                                                            InputCallback);
            }
            promptActive = ImGuiMCP::IsItemActive();
            PublishCompletionInput(promptActive && suggestions.Visible());
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
            if (TakeCompletionDismissal()) {
                suggestions.Dismiss();
                chosenCompletion.reset();
                focusCommand = true;
            }
            if (!IsTestedRuntime())
                ImGuiMCP::TextWrapped("%s", UntestedRuntimeWarning.data());
            const auto entries = Snapshot();
            RenderStatus(entries);
            ImGuiMCP::Spacing();
            ImGuiMCP::BeginDisabled(entries.empty());
            if (ImGuiMCP::Button("Copy all")) {
                const auto text = TranscriptText(entries);
                ImGuiMCP::SetClipboardText(text.c_str());
            }
            ui::NextControl("Clear finished");
            if (ImGuiMCP::Button("Clear finished"))
                ClearFinished();
            ImGuiMCP::EndDisabled();
            ui::NextControl("Multiline", true);
            if (ImGuiMCP::Checkbox("Multiline", &multiline)) {
                input.swap(otherModeInput);
                commandCursor = std::char_traits<char>::length(input.data());
                chosenCompletion.reset();
                suggestions = {};
                completion = {};
                history.ResetNavigation();
                focusCommand = true;
            }
            ui::NextControl("Help");
            if (ImGuiMCP::Button("Help"))
                ImGuiMCP::OpenPopup("Console help");
            ui::NextControl("Session details");
            if (ImGuiMCP::Button("Session details"))
                ImGuiMCP::OpenPopup("Session details##popup");
            if (ImGuiMCP::BeginPopup("Session details##popup")) {
                RenderUsage();
                ImGuiMCP::EndPopup();
            }
            ImGuiMCP::Spacing();
            if (!error.empty())
                ImGuiMCP::TextColored(ui::Failure, "%s", error.c_str());
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            const auto line = ImGuiMCP::GetTextLineHeightWithSpacing();
            const auto gap = ImGuiMCP::GetStyle()->ItemSpacing.y;
            const auto promptHeight =
                multiline ? std::min(line * 6, std::max(ImGuiMCP::GetFrameHeight(), available.y * 0.35f))
                          : ImGuiMCP::GetFrameHeight();
            const auto editorHeight = promptHeight + line + ImGuiMCP::GetStyle()->WindowPadding.y * 2 + gap;
            auto assistanceHeight =
                std::min(PrepareCompletion(), std::max(0.0f, available.y - editorHeight - line * 3));
            if (assistanceHeight < line) {
                suggestions.Dismiss();
                completion.items.clear();
                assistanceHeight = 0;
            }
            RenderOutput(entries, std::max(line * 2, available.y - editorHeight - assistanceHeight - gap * 2));
            RenderCompletion(assistanceHeight);
            if (ImGuiMCP::BeginChild("Command editor", {0, editorHeight}, ImGuiMCP::ImGuiChildFlags_Border)) {
                ImGuiMCP::TextDisabled("%s", multiline ? "FRAGMENT  |  Ctrl+Enter to run"
                                                       : "COMMAND  |  Enter to run  |  Up/Down for history");
                RenderPrompt(promptHeight);
            }
            ImGuiMCP::EndChild();
            RenderHelp();
        }

    }

    MenuSession CaptureMenuSession() {
        return {input, otherModeInput, multiline, history, explorerForm, explorerScript, instanceVariable};
    }

    void RestoreMenuSession(const MenuSession& state) {
        input = state.input;
        otherModeInput = state.otherModeInput;
        multiline = state.multiline;
        history = state.history;
        history.ResetNavigation();
        explorerForm = state.explorerForm;
        explorerScript = state.explorerScript;
        instanceVariable = state.instanceVariable;
        commandCursor = std::char_traits<char>::length(input.data());
        completion = {};
        suggestions = {};
        chosenCompletion.reset();
        focusCommand = true;
    }

    void RegisterMenu() {
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::error("{} requires SKSE Menu Framework for its interface", mod::info::DisplayName);
            return;
        }
        SKSEMenuFramework::SetSection(mod::info::DisplayName);
        RegisterCompletionInput();
        SKSEMenuFramework::AddSectionItem("Console", Render);
        SKSEMenuFramework::AddSectionItem("Variables", RenderVariables);
        SKSEMenuFramework::AddSectionItem("Instances", RenderInstances);
    }
}
