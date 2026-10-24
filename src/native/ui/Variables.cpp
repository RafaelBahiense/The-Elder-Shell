#include "Menu.h"
#include "Layout.h"
#include "vm/VM.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>

namespace elder_shell {
    namespace {
        struct PropertyRead {
            PropertyInspection target;
            Ticket request;
            ui::TextBuffer result;
        };

        std::vector<PropertyRead> reads;
        std::string inspectionError, selectedVariable, selectedExpression;
        ui::TextBuffer selectedSummary;

        const InspectedValue* FindValue(const InspectedValue& value, std::string_view expression) {
            if (value.expression == expression)
                return &value;
            for (const auto& element : value.elements)
                if (const auto* found = FindValue(element, expression))
                    return found;
            return nullptr;
        }

        void RenderProperty(const InspectedVariable& variable, const InspectedValue& value,
                            const InspectedProperty& property, const std::vector<Entry>& requests) {
            const auto expression = value.expression + "." + property.name;
            ImGuiMCP::PushID(expression.c_str());
            ImGuiMCP::TableNextRow();
            ImGuiMCP::TableSetColumnIndex(0);
            ImGuiMCP::TextUnformatted(property.name.c_str());
            ImGuiMCP::TableSetColumnIndex(1);
            ImGuiMCP::TextDisabled("%s", property.type.c_str());
            auto previous = std::ranges::find_if(reads, [&](const auto& read) {
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
            ImGuiMCP::TableSetColumnIndex(2);
            if (!property.readable)
                ImGuiMCP::TextDisabled("Write-only");
            else if (result) {
                if (pending)
                    ImGuiMCP::TextColored(ui::Pending, "%s", StatusName(result->status));
                else {
                    previous->result.Update(result->result);
                    if (result->status == Status::Rejected || result->status == Status::Abandoned)
                        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ui::Failure);
                    previous->result.Draw("##PropertyResult", 5);
                    if (result->status == Status::Rejected || result->status == Status::Abandoned)
                        ImGuiMCP::PopStyleColor();
                }
            } else
                ImGuiMCP::TextDisabled("Not read");
            ImGuiMCP::TableSetColumnIndex(3);
            if (property.readable) {
                ImGuiMCP::BeginDisabled(pending);
                const auto read = ImGuiMCP::Button("Read");
                ImGuiMCP::EndDisabled();
                if (read) {
                    PropertyInspection target{variable.name, expression, variable.owner};
                    const auto submitted = ReadInspectedProperty(target);
                    inspectionError = submitted.error;
                    if (submitted.error.empty()) {
                        std::erase_if(reads, [&](const auto& item) { return item.target.expression == expression; });
                        if (reads.size() == 128)
                            reads.erase(reads.begin());
                        reads.push_back({std::move(target), submitted.ticket, {}});
                    }
                }
            }
            ImGuiMCP::PopID();
        }

        void RenderArray(const InspectedValue& value) {
            ui::Heading("ELEMENTS");
            if (value.elements.empty()) {
                ImGuiMCP::TextDisabled("No elements in this snapshot.");
                return;
            }
            if (ImGuiMCP::BeginTable("Array elements", 3,
                                     ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerH |
                                         ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
                ImGuiMCP::TableSetupColumn("Index", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
                                           ImGuiMCP::GetFontSize() * 3);
                ImGuiMCP::TableSetupColumn("Type", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
                                           ImGuiMCP::GetFontSize() * 5);
                ImGuiMCP::TableSetupColumn("Value", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGuiMCP::TableHeadersRow();
                for (const auto& element : value.elements) {
                    ImGuiMCP::PushID(element.expression.c_str());
                    ImGuiMCP::TableNextRow();
                    ImGuiMCP::TableSetColumnIndex(0);
                    const auto index = element.expression.starts_with(value.expression)
                                           ? element.expression.substr(value.expression.size())
                                           : element.expression;
                    if (ImGuiMCP::Selectable(index.c_str(), false))
                        selectedExpression = element.expression;
                    ImGuiMCP::TableSetColumnIndex(1);
                    ImGuiMCP::TextDisabled("%s", element.type.c_str());
                    ImGuiMCP::TableSetColumnIndex(2);
                    ImGuiMCP::TextWrapped("%s", element.summary.c_str());
                    ImGuiMCP::PopID();
                }
                ImGuiMCP::EndTable();
            }
        }

        void RenderDetails(const InspectedVariable& variable, const std::vector<Entry>& requests) {
            const auto* value = FindValue(variable.value, selectedExpression);
            if (!value) {
                selectedExpression = variable.value.expression;
                value = &variable.value;
            }
            if (value != &variable.value && ImGuiMCP::Button("Back to variable")) {
                selectedExpression = variable.value.expression;
                value = &variable.value;
            }
            ui::Heading(value->expression.c_str());
            ImGuiMCP::TextDisabled("%s", value->type.c_str());
            if (ImGuiMCP::Button("Copy value"))
                ImGuiMCP::SetClipboardText(value->summary.c_str());
            ImGuiMCP::Spacing();
            selectedSummary.Update(value->summary);
            selectedSummary.Draw("##SelectedValue", 8);
            ImGuiMCP::Spacing();
            if (value->array) {
                ImGuiMCP::TextDisabled("%zu elements", value->length);
                RenderArray(*value);
            }
            if (!value->properties.empty()) {
                ui::Heading("PROPERTIES");
                ImGuiMCP::TextDisabled("Read executes the property's getter.");
                if (ImGuiMCP::BeginTable("Properties", 4,
                                         ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerH |
                                             ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
                    ImGuiMCP::TableSetupColumn("Property", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1);
                    ImGuiMCP::TableSetupColumn("Type", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 0.7f);
                    ImGuiMCP::TableSetupColumn("Value", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.5f);
                    ImGuiMCP::TableSetupColumn("", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
                                               ImGuiMCP::GetFontSize() * 3);
                    ImGuiMCP::TableHeadersRow();
                    for (const auto& property : value->properties)
                        RenderProperty(variable, *value, property, requests);
                    ImGuiMCP::EndTable();
                }
            }
            if (value->truncated)
                ImGuiMCP::TextWrapped("Snapshot truncated. Assign an element to a separate variable to inspect it.");
        }
    }

    void __stdcall RenderVariables() {
        ui::Heading("SESSION VARIABLES");
        if (ImGuiMCP::Button("Refresh"))
            RefreshVariableInspection();
        const auto snapshot = VariableInspectionSnapshot();
        if (snapshot.status == "Not refreshed")
            RefreshVariableInspection();
        ImGuiMCP::SameLine();
        ImGuiMCP::TextDisabled("%zu variables", snapshot.variables.size());
        ImGuiMCP::TextDisabled("%s", snapshot.refreshing ? "Refreshing variables..." : snapshot.status.c_str());
        if (!inspectionError.empty())
            ImGuiMCP::TextColored(ui::Failure, "%s", inspectionError.c_str());
        const auto requests = Snapshot();
        std::erase_if(reads, [&](const auto& read) {
            return std::ranges::none_of(snapshot.variables, [&](const auto& variable) {
                return variable.name == read.target.variable && SameInspectionOwner(variable.owner, read.target.owner);
            });
        });
        if (std::ranges::none_of(snapshot.variables,
                                 [](const auto& variable) { return variable.name == selectedVariable; })) {
            selectedVariable = snapshot.variables.empty() ? "" : snapshot.variables.front().name;
            selectedExpression.clear();
        }
        ImGuiMCP::Spacing();
        ImGuiMCP::ImVec2 available;
        ImGuiMCP::GetContentRegionAvail(&available);
        const bool wide = ui::Wide(available.x);
        const auto listHeight =
            wide ? 0.0f : std::min(available.y * 0.35f, ImGuiMCP::GetTextLineHeightWithSpacing() * 7);
        if (ImGuiMCP::BeginChild("Variables list", {wide ? ui::ListWidth(available.x) : 0, listHeight},
                                 ImGuiMCP::ImGuiChildFlags_Border)) {
            ui::Heading("VARIABLES");
            if (snapshot.variables.empty())
                ImGuiMCP::TextWrapped("Declare a variable in Console, then refresh.");
            for (const auto& variable : snapshot.variables) {
                ImGuiMCP::PushID(variable.name.c_str());
                const auto label =
                    variable.name + "\n" + variable.value.type + " | " + ui::Preview(variable.value.summary);
                if (ImGuiMCP::Selectable(label.c_str(), variable.name == selectedVariable, 0,
                                         {0, ImGuiMCP::GetTextLineHeightWithSpacing() * 2.5f})) {
                    selectedVariable = variable.name;
                    selectedExpression = variable.value.expression;
                }
                ImGuiMCP::PopID();
            }
        }
        ImGuiMCP::EndChild();
        if (wide)
            ImGuiMCP::SameLine();
        if (ImGuiMCP::BeginChild("Variable details", {0, 0}, ImGuiMCP::ImGuiChildFlags_Border)) {
            const auto selected = std::ranges::find_if(
                snapshot.variables, [](const auto& variable) { return variable.name == selectedVariable; });
            if (selected != snapshot.variables.end())
                RenderDetails(*selected, requests);
            else
                ImGuiMCP::TextDisabled("Select a variable to inspect its value.");
        }
        ImGuiMCP::EndChild();
    }
}
