#include "Menu.h"
#include "Layout.h"
#include "vm/VM.h"
#include "History.h"
#include "Suggestions.h"
#include "CompletionControls.h"
#include "ShellInput.h"
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
        ShellDocument shell;
        ShellSelection shellSelection;
        std::vector<char> shellText = std::vector<char>(4097, '\0');
        std::string nextPrefix;
        std::optional<std::string> pendingDraft;
        bool historyReplacement = false, focusTail = false;
        std::string error;
        History history;

        bool focusCommand = false;
        int lastConsoleFrame = -2;
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

        void Run(bool force) {
            const std::string draft(input.data());
            const auto command = ReadShellInput(draft);
            if (!force && command.more) {
                if (draft.size() == ShellDocument::MaximumDraft)
                    error = "Command exceeds 4096 characters";
                else
                    pendingDraft = draft + '\n';
            } else if (command.source.find_first_not_of(" \t\r\n") != std::string::npos) {
                history.Add(draft);
                error = Submit(command.source, command.fragment);
                if (error.empty())
                    pendingDraft = "";
            }
            completion = {};
            suggestions.Dismiss();
            commandCursor = std::char_traits<char>::length(input.data());
            focusCommand = true;
        }

        void InsertCompletion(const CompletionContext& context, const CompletionItem& item, ShellSelection& selection) {
            const auto edit = CompletionInsertion(shell.Draft(), context, item);
            const auto& name = edit.text;
            if (context.end > shell.Draft().size() ||
                shell.Draft().size() - (context.end - context.begin) + name.size() >= input.size())
                return;
            auto draft = shell.Draft();
            draft.replace(context.begin, context.end - context.begin, name);
            shell.SetDraft(draft, selection);
            const auto cursor = shell.Boundary() + std::min(context.begin + edit.cursor, draft.size());
            selection = {cursor, cursor, cursor};
            history.ResetNavigation();
            suggestions.Dismiss();
        }

        void SyncShell(ShellSelection& selection) {
            shell.Rebase(nextPrefix, selection);
            if (pendingDraft) {
                shell.SetDraft(*pendingDraft, selection);
                pendingDraft.reset();
            }
            if (chosenCompletion) {
                if (const auto environment = CompletionMetadataSnapshot()) {
                    const auto cursor = selection.cursor >= shell.Boundary() ? selection.cursor - shell.Boundary()
                                                                             : shell.Draft().size();
                    const auto source = ReadShellInput(shell.Draft()).completion;
                    const auto result = Complete(source, cursor, *environment);
                    InsertCompletion(result.context, *chosenCompletion, selection);
                }
                chosenCompletion.reset();
            }
            if (focusTail) {
                const auto end = shell.Boundary() + shell.Draft().size();
                selection = {end, end, end};
                focusTail = false;
            }
            input.fill('\0');
            std::ranges::copy(shell.Draft(), input.begin());
            commandCursor = selection.cursor >= shell.Boundary()
                                ? std::min(selection.cursor - shell.Boundary(), shell.Draft().size())
                                : 0;
        }

        int InputCallback(ImGuiMCP::ImGuiInputTextCallbackData* data) {
            if (data->EventFlag == ImGuiMCP::ImGuiInputTextFlags_CallbackResize) {
                shellText.resize(static_cast<std::size_t>(data->BufTextLen) + input.size());
                data->Buf = shellText.data();
                return 0;
            }
            ShellSelection selection{static_cast<std::size_t>(data->CursorPos),
                                     static_cast<std::size_t>(data->SelectionStart),
                                     static_cast<std::size_t>(data->SelectionEnd)};
            const auto previous = shell.Draft();
            if (!shell.Edit(std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)), selection))
                error = "Command exceeds 4096 characters";
            if (shell.Draft() != previous && !historyReplacement)
                history.ResetNavigation();
            historyReplacement = false;
            SyncShell(selection);
            const auto text = shell.Text();
            if (std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen)) != text) {
                ImGuiMCP::ImGuiInputTextCallbackDataManager::DeleteChars(data, 0, data->BufTextLen);
                ImGuiMCP::ImGuiInputTextCallbackDataManager::InsertChars(data, 0, text.data(),
                                                                         text.data() + text.size());
            }
            data->CursorPos = static_cast<int>(std::min(selection.cursor, text.size()));
            data->SelectionStart = static_cast<int>(std::min(selection.start, text.size()));
            data->SelectionEnd = static_cast<int>(std::min(selection.end, text.size()));
            shellSelection = selection;
            return 0;
        }

        ImGuiMCP::ImVec4 StatusColor(Status status) {
            if (status == Status::Rejected || status == Status::Abandoned)
                return ui::Failure;
            return status == Status::Completed ? ui::Success : ui::Pending;
        }

        std::string TranscriptText(const std::vector<Entry>& entries) {
            std::string text;
            for (const auto& entry : entries)
                text += std::format("> {}\n{}\n\n", entry.command, entry.result);
            return text;
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
                        "Enter runs a complete command. Unfinished blocks, parentheses and quotes continue on the "
                        "next line. A trailing backslash joins lines. Variables remain until a save load.");
                    ImGuiMCP::Spacing();
                    static ui::TextBuffer examples;
                    examples.Update(
                        "var player = "
                        "Game.GetPlayer()\nplayer.GetLevel()\nplayer.GetActorValue(\"Health\")\n\nvars\nunset "
                        "player\nclearvars\nmethods Actor");
                    examples.Draw("##Examples");
                    ImGuiMCP::TextWrapped(
                        "Paste several statements or use Shift+Enter to insert a line. Ctrl+Enter submits without "
                        "waiting for an unfinished construct to close.");
                    ImGuiMCP::EndTabItem();
                }
                if (ImGuiMCP::BeginTabItem("Shortcuts")) {
                    ImGuiMCP::TextWrapped(
                        "Suggestions: Up/Down selects; Tab accepts; Escape dismisses; Ctrl+Space reopens.");
                    ImGuiMCP::Spacing();
                    ImGuiMCP::TextWrapped(
                        "Up/Down recalls history at the first or last draft line when suggestions are closed. "
                        "Enter runs the command. Shift+Enter inserts a line.");
                    ImGuiMCP::Spacing();
                    ImGuiMCP::TextWrapped(
                        "Select across commands and results, then copy with Ctrl+C. Ctrl+A selects the focused "
                        "transcript.");
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
                ImGuiMCP::TextColored(StatusColor(entries.back().status), "%s", StatusName(entries.back().status));
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

        bool RouteCompletionKeys(PromptKeyMask& keys, bool& force) {
            const auto owner = ImGuiMCP::GetID("##Shell");
            if (ImGuiMCP::GetActiveID() != owner)
                return false;
            const auto query = std::format("{}:{}", commandCursor, input.data());
            const auto environment = CompletionMetadataSnapshot();
            const auto source = ReadShellInput(input.data()).completion;
            const auto result = environment
                                    ? Complete(source, commandCursor, *environment, suggestions.ExplicitFor(query))
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
                if (shellSelection.cursor < shell.Boundary() || shellSelection.start < shell.Boundary()) {
                    if (logical == CompletionKey::Enter) {
                        keys.Consume(key);
                        focusTail = true;
                    }
                    continue;
                }
                if (!suggestions.Visible() && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
                    (logical == CompletionKey::Up || logical == CompletionKey::Down)) {
                    const auto draft = std::string_view(input.data());
                    const bool edge = logical == CompletionKey::Up
                                          ? draft.substr(0, commandCursor).find('\n') == std::string_view::npos
                                          : draft.substr(commandCursor).find('\n') == std::string_view::npos;
                    if (edge) {
                        const auto command = logical == CompletionKey::Up ? history.Previous(draft) : history.Next();
                        if (command) {
                            pendingDraft = std::string(*command);
                            historyReplacement = true;
                        }
                        keys.Consume(key);
                        continue;
                    }
                }
                const auto action =
                    CompletionShortcut(logical, suggestions.Visible(), io.KeyCtrl, io.KeyShift, io.KeyAlt);
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
                        force = io.KeyCtrl;
                        submit = true;
                        break;
                    default:
                        break;
                }
            }
            return submit;
        }

        void RenderShell(const std::vector<Entry>& entries, float height) {
            nextPrefix = TranscriptText(entries) + "> ";
            const auto owner = ImGuiMCP::GetID("##Shell");
            if (ImGuiMCP::GetActiveID() != owner) {
                SyncShell(shellSelection);
                const auto text = shell.Text();
                shellText.resize(std::max(shellText.size(), text.size() + input.size()));
                std::ranges::copy(text, shellText.begin());
                shellText[text.size()] = '\0';
            } else
                shellText.resize(std::max(shellText.size(), nextPrefix.size() + input.size()));
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg, {0, 0, 0, 0});
            if (focusCommand) {
                ImGuiMCP::SetKeyboardFocusHere();
                focusCommand = false;
                focusTail = true;
            }
            const auto flags = ImGuiMCP::ImGuiInputTextFlags_CallbackEdit |
                               ImGuiMCP::ImGuiInputTextFlags_CallbackAlways |
                               ImGuiMCP::ImGuiInputTextFlags_CallbackResize | ImGuiMCP::ImGuiInputTextFlags_NoUndoRedo;
            bool shortcutSubmit = false;
            bool force = false;
            {
                PromptKeyMask keys(owner);
                shortcutSubmit = RouteCompletionKeys(keys, force);
                ImGuiMCP::InputTextMultiline("##Shell", shellText.data(), shellText.size(), {-1, height}, flags,
                                             InputCallback, nullptr);
            }
            promptActive = ImGuiMCP::IsItemActive() && shellSelection.cursor >= shell.Boundary() &&
                           shellSelection.start >= shell.Boundary();
            PublishCompletionInput(promptActive && suggestions.Visible());
            if (promptActive) {
                // Reserve next frame's keys before ImGui's focus navigation.
                for (const auto key :
                     {ImGuiMCP::ImGuiKey_UpArrow, ImGuiMCP::ImGuiKey_DownArrow, ImGuiMCP::ImGuiKey_Enter,
                      ImGuiMCP::ImGuiKey_KeypadEnter, ImGuiMCP::ImGuiKey_Tab})
                    ImGuiMCP::SetKeyOwner(key, owner, ImGuiMCP::ImGuiInputFlags_LockUntilRelease);
                if (suggestions.Visible())
                    ImGuiMCP::SetKeyOwner(ImGuiMCP::ImGuiKey_Escape, owner, ImGuiMCP::ImGuiInputFlags_LockUntilRelease);
            }
            ImGuiMCP::PopStyleColor();
            if (shortcutSubmit)
                Run(force);
        }

        float PrepareCompletion() {
            const auto source = ReadShellInput(input.data()).completion;
            const auto context = CompletionAt(source, commandCursor);
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
                    completion = Complete(source, commandCursor, *environment,
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
                    ImGuiMCP::Text("%zu/%zu | Up/Down: select | Tab: accept | Enter: run | Esc: dismiss",
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
            const auto frame = ImGuiMCP::GetFrameCount();
            if (frame != lastConsoleFrame + 1)
                focusCommand = true;
            lastConsoleFrame = frame;
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
            if (ImGuiMCP::Button("Clear finished"))
                ClearFinished();
            ImGuiMCP::EndDisabled();
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
            auto assistanceHeight = std::min(PrepareCompletion(), std::max(0.0f, available.y - line * 3));
            if (assistanceHeight < line) {
                suggestions.Dismiss();
                completion.items.clear();
                assistanceHeight = 0;
            }
            RenderShell(entries, std::max(line * 2, available.y - assistanceHeight - gap));
            RenderCompletion(assistanceHeight);
            RenderHelp();
        }

    }

    MenuSession CaptureMenuSession() {
        auto draft = input;
        if (pendingDraft) {
            draft.fill('\0');
            std::ranges::copy(*pendingDraft, draft.begin());
        }
        return {draft, history, explorerForm, explorerScript, instanceVariable};
    }

    void RestoreMenuSession(const MenuSession& state) {
        input = state.input;
        pendingDraft = input.data();
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
