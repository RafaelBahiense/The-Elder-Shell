#include "Menu.h"
#include "VM.h"
#include "History.h"

#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <array>

namespace papyrus_console {
    namespace {
        std::array<char, 4097> input{};
        std::string error;
        History history;
        std::string transcript;
        std::vector<char> output{'\0'};
        bool focusCommand = false;
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
            ImGuiMCP::TextUnformatted("Use it in Console: call $instance Script.Function()");
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
            focusCommand = true;
        }

        int InputCallback(ImGuiMCP::ImGuiInputTextCallbackData* data) {
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
            }
            return 0;
        }

        void RenderOutput() {
            std::string text;
            for (const auto& entry : Snapshot()) {
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
            ImGuiMCP::InputTextMultiline("##Output", output.data(), output.size(), available,
                                         ImGuiMCP::ImGuiInputTextFlags_ReadOnly, nullptr, nullptr);
        }

        void __stdcall Render() {
            ImGuiMCP::TextUnformatted("Papyrus VM function console");
            ImGuiMCP::TextWrapped(
                "Calls existing compiled functions. Store returned values with let; reuse them with $name. "
                "Use eval for a single compiled Papyrus statement. Code blocks are unsupported.");
            ImGuiMCP::TextUnformatted("eval Actor player = Game.GetPlayer()   |   eval Int level = player.GetLevel()");
            ImGuiMCP::TextUnformatted("global Utility.RandomInt(1, 10)");
            ImGuiMCP::TextUnformatted("global Debug.Notification \"Hello from Papyrus\"");
            ImGuiMCP::TextUnformatted("let greeting = \"Hello\"   |   global Debug.Notification $greeting");
            ImGuiMCP::TextUnformatted("call @player Actor.GetLevel");
            ImGuiMCP::TextUnformatted("let player = global Game.GetPlayer   |   call $player Actor.GetLevel");
            ImGuiMCP::TextUnformatted("vars   |   show $name   |   unset name   |   clearvars");
            ImGuiMCP::TextUnformatted("show instance.Data   |   let data = $instance.Data");
            ImGuiMCP::TextUnformatted("let values = float[1, 0.66]   |   show $values[0]   |   show $values.Length");
            ImGuiMCP::TextUnformatted("set $instance.Property = value");
            ImGuiMCP::TextUnformatted("methods Actor   |   get <FormID> Script.Property");
            ImGuiMCP::TextWrapped(
                "Form arguments use @player or @HEXID. Supply every parameter; true, false and none are accepted where "
                "applicable. Commands can change game state.");
            if (focusCommand) {
                ImGuiMCP::SetKeyboardFocusHere();
                focusCommand = false;
            }
            if (ImGuiMCP::InputText("Command", input.data(), input.size(),
                                    ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue |
                                        ImGuiMCP::ImGuiInputTextFlags_CallbackHistory |
                                        ImGuiMCP::ImGuiInputTextFlags_CallbackEdit,
                                    InputCallback))
                Run();
            if (ImGuiMCP::Button("Execute"))
                Run();
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Clear finished"))
                ClearFinished();
            if (!error.empty())
                ImGuiMCP::TextWrapped("%s", error.c_str());
            ImGuiMCP::Separator();
            ImGuiMCP::TextWrapped(
                "Some VM calls remain pending while this menu pauses the game. Close the menu, then reopen it to see "
                "completion. Calls are never automatically repeated.");
            RenderOutput();
        }
    }

    void RegisterMenu() {
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::error("Papyrus Console requires SKSE Menu Framework for its interface");
            return;
        }
        SKSEMenuFramework::SetSection("Papyrus Console");
        SKSEMenuFramework::AddSectionItem("Console", Render);
        SKSEMenuFramework::AddSectionItem("Instances", RenderInstances);
    }
}
