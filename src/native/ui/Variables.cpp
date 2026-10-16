#include "Menu.h"
#include "vm/VM.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>

namespace elder_shell {
    namespace {
        struct PropertyRead {
            PropertyInspection target;
            Ticket request;
        };

        std::vector<PropertyRead> reads;
        std::string inspectionError;

        void RenderProperty(const InspectedVariable& variable, const InspectedValue& value,
                            const InspectedProperty& property, const std::vector<Entry>& requests) {
            const auto expression = value.expression + "." + property.name;
            ImGuiMCP::PushID(expression.c_str());
            ImGuiMCP::Text("%s : %s", property.name.c_str(), property.type.c_str());
            ImGuiMCP::SameLine();
            const auto previous = std::ranges::find_if(reads, [&](const auto& read) {
                return read.target.expression == expression && SameInspectionOwner(read.target.owner, variable.owner);
            });
            const Entry* result = nullptr;
            if (previous != reads.end()) {
                const auto entry = std::ranges::find_if(requests, [&](const auto& request) {
                    return SameInspectionOwner(request.ticket, previous->request);
                });
                if (entry != requests.end())
                    result = &*entry;
            }
            const bool pending = result && (result->status == Status::Queued || result->status == Status::Compiling ||
                                            result->status == Status::Submitted);
            ImGuiMCP::BeginDisabled(!property.readable || pending);
            const auto read = ImGuiMCP::Button("Read (runs getter)");
            ImGuiMCP::EndDisabled();
            if (read) {
                PropertyInspection target{variable.name, expression, variable.owner};
                const auto submitted = ReadInspectedProperty(target);
                inspectionError = submitted.error;
                if (submitted.error.empty()) {
                    std::erase_if(reads, [&](const auto& item) { return item.target.expression == expression; });
                    if (reads.size() == 128)
                        reads.erase(reads.begin());
                    reads.push_back({std::move(target), submitted.ticket});
                }
            }
            if (!property.readable)
                ImGuiMCP::TextUnformatted("Write-only property");
            if (result)
                ImGuiMCP::TextWrapped("%s: %s", StatusName(result->status), result->result.c_str());
            ImGuiMCP::PopID();
        }

        void RenderValue(const InspectedVariable& variable, const InspectedValue& value,
                         const std::vector<Entry>& requests) {
            const auto label = value.expression + " : " + value.type + " = " + value.summary;
            if (value.array || !value.properties.empty()) {
                if (ImGuiMCP::TreeNode(value.expression.c_str(), "%s", label.c_str())) {
                    for (const auto& element : value.elements)
                        RenderValue(variable, element, requests);
                    for (const auto& property : value.properties)
                        RenderProperty(variable, value, property, requests);
                    if (value.truncated)
                        ImGuiMCP::TextWrapped(
                            "Snapshot truncated. Assign an element to a separate variable to inspect it.");
                    ImGuiMCP::TreePop();
                }
            } else {
                ImGuiMCP::TextUnformatted(label.c_str());
            }
        }
    }

    void __stdcall RenderVariables() {
        if (ImGuiMCP::Button("Refresh variables"))
            RefreshVariableInspection();
        auto snapshot = VariableInspectionSnapshot();
        if (snapshot.status == "Not refreshed")
            RefreshVariableInspection();
        ImGuiMCP::TextWrapped("%s", snapshot.refreshing ? "Refreshing variables..." : snapshot.status.c_str());
        if (!inspectionError.empty())
            ImGuiMCP::TextWrapped("%s", inspectionError.c_str());
        const auto requests = Snapshot();
        std::erase_if(reads, [&](const auto& read) {
            return std::ranges::none_of(snapshot.variables, [&](const auto& variable) {
                return variable.name == read.target.variable && SameInspectionOwner(variable.owner, read.target.owner);
            });
        });
        if (snapshot.variables.empty() && !snapshot.refreshing)
            ImGuiMCP::TextUnformatted("No variables in this snapshot. Declare one in Console, then refresh.");
        if (ImGuiMCP::BeginChild("Variable values", ImGuiMCP::ImVec2{0, 0})) {
            for (const auto& variable : snapshot.variables) {
                ImGuiMCP::PushID(variable.name.c_str());
                RenderValue(variable, variable.value, requests);
                ImGuiMCP::Separator();
                ImGuiMCP::PopID();
            }
        }
        ImGuiMCP::EndChild();
    }
}
