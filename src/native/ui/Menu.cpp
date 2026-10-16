#include "Menu.h"
#include "vm/VM.h"
#include "History.h"
#include "runtime/Compatibility.h"
#include "ModInfo.h"
#include "shell/Completion.h"

#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>
#include <array>

namespace elder_shell {
    namespace {
        std::array<char, 4097> input{};
        std::string error;
        History history;
        std::string transcript;
        std::vector<char> output{'\0'};
        bool focusCommand = false;
        bool promptActive = false;
        std::size_t commandCursor = 0;
        CompletionResult completion;
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
            history.Add(input.data());
            error = Submit(input.data());
            if (error.empty())
                input.fill('\0');
            completion = {};
            commandCursor = std::char_traits<char>::length(input.data());
            focusCommand = true;
        }

        void InsertCompletion(ImGuiMCP::ImGuiInputTextCallbackData* data, const CompletionContext& context,
                              std::string_view name) {
            if (context.end > static_cast<std::size_t>(data->BufTextLen) ||
                data->BufTextLen - (context.end - context.begin) + name.size() >= input.size())
                return;
            ImGuiMCP::ImGuiInputTextCallbackDataManager::DeleteChars(data, static_cast<int>(context.begin),
                                                                     static_cast<int>(context.end - context.begin));
            ImGuiMCP::ImGuiInputTextCallbackDataManager::InsertChars(data, static_cast<int>(context.begin), name.data(),
                                                                     name.data() + name.size());
            data->CursorPos = static_cast<int>(context.begin + name.size());
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
            history.ResetNavigation();
        }

        int InputCallback(ImGuiMCP::ImGuiInputTextCallbackData* data) {
            if (data->EventFlag == ImGuiMCP::ImGuiInputTextFlags_CallbackCompletion || chosenCompletion) {
                if (const auto environment = CompletionMetadataSnapshot()) {
                    const auto result = Complete(data->Buf, static_cast<std::size_t>(data->CursorPos), *environment);
                    if (chosenCompletion)
                        InsertCompletion(data, result.context, chosenCompletion->name);
                    else if (!result.items.empty())
                        InsertCompletion(data, result.context, result.items.front().name);
                }
                chosenCompletion.reset();
            }
            commandCursor = static_cast<std::size_t>(data->CursorPos);
            if (data->EventFlag == ImGuiMCP::ImGuiInputTextFlags_CallbackEdit) {
                history.ResetNavigation();
                return 0;
            }
            if (data->EventFlag != ImGuiMCP::ImGuiInputTextFlags_CallbackHistory)
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
            available.y = std::max(1.0f, available.y - ImGuiMCP::GetFrameHeightWithSpacing() -
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
                "Enter one Papyrus statement. Expressions display their results.\n"
                "Code blocks are unsupported. Commands can change game state.\n\n"
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
                "Up/Down: history. Ctrl+C: copy selection. Ctrl+A: select focused text.\n"
                "Tab: complete the first suggestion. Click a suggestion to choose it.\n"
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

        void RenderPrompt() {
            ImGuiMCP::AlignTextToFramePadding();
            ImGuiMCP::TextUnformatted(">");
            ImGuiMCP::SameLine();
            const auto& style = *ImGuiMCP::GetStyle();
            ImGuiMCP::ImVec2 labelSize;
            ImGuiMCP::CalcTextSize(&labelSize, "Execute", nullptr, false, -1.0f);
            const auto buttonWidth = labelSize.x + style.FramePadding.x * 2;
            const auto rightInset = std::max(style.WindowPadding.x, style.FramePadding.x);
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            ImGuiMCP::SetNextItemWidth(std::max(1.0f, available.x - buttonWidth - style.ItemSpacing.x - rightInset));
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg, ImGuiMCP::ImVec4{0, 0, 0, 0});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 0.0f);
            if (focusCommand) {
                ImGuiMCP::SetKeyboardFocusHere();
                focusCommand = false;
            }
            const auto submitted = ImGuiMCP::InputText(
                "##Command", input.data(), input.size(),
                ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue | ImGuiMCP::ImGuiInputTextFlags_CallbackHistory |
                    ImGuiMCP::ImGuiInputTextFlags_CallbackEdit | ImGuiMCP::ImGuiInputTextFlags_CallbackCompletion |
                    ImGuiMCP::ImGuiInputTextFlags_CallbackAlways,
                InputCallback);
            promptActive = ImGuiMCP::IsItemActive();
            ImGuiMCP::PopStyleVar();
            ImGuiMCP::PopStyleColor();
            ImGuiMCP::SameLine();
            const auto clicked = ImGuiMCP::Button("Execute", ImGuiMCP::ImVec2{buttonWidth, 0});
            if (submitted || clicked)
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
                    completion = Complete(input.data(), commandCursor, *environment);
            }
            const auto rows = std::min<std::size_t>(5, completion.items.size()) +
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
                    ImGuiMCP::Text("%zu matches | Tab: first match | Type to narrow", completion.items.size());
                for (std::size_t i = 0; i < std::min<std::size_t>(5, completion.items.size()); ++i) {
                    const auto& item = completion.items[i];
                    const auto label = item.name + " : " + item.description;
                    if (ImGuiMCP::Selectable(label.c_str(), false)) {
                        chosenCompletion = item;
                        focusCommand = true;
                    }
                }
            }
            ImGuiMCP::EndChild();
        }

        void __stdcall Render() {
            if (!IsTestedRuntime())
                ImGuiMCP::TextWrapped("%s", UntestedRuntimeWarning.data());

            if (ImGuiMCP::Button("Clear finished"))
                ClearFinished();
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
        SKSEMenuFramework::AddSectionItem("Instances", RenderInstances);
    }
}
