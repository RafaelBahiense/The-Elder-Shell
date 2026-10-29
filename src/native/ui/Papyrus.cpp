#include "Layout.h"
#include "runtime/PapyrusBacklog.h"
#include "runtime/Compatibility.h"

namespace elder_shell {
    namespace {
        void Rows(const char* id, const char* label, const std::vector<BacklogRow>& rows) {
            if (rows.empty()) {
                ImGuiMCP::TextDisabled("No entries in this sample.");
                return;
            }
            if (ImGuiMCP::BeginTable(id, 2, ImGuiMCP::ImGuiTableFlags_SizingFixedFit)) {
                ImGuiMCP::TableSetupColumn("Count", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed,
                                           ImGuiMCP::GetFontSize() * 5);
                ImGuiMCP::TableSetupColumn(label, ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGuiMCP::TableHeadersRow();
                for (std::size_t index = 0; index < std::min<std::size_t>(rows.size(), 32); ++index) {
                    const auto& row = rows[index];
                    ImGuiMCP::TableNextRow();
                    ImGuiMCP::TableSetColumnIndex(0);
                    ImGuiMCP::Text("%u", row.count);
                    ImGuiMCP::TableSetColumnIndex(1);
                    ImGuiMCP::TextWrapped("%s", row.name.c_str());
                }
                ImGuiMCP::EndTable();
            }
        }
    }

    void __stdcall RenderPapyrus() {
        ui::Heading("PAPYRUS BACKLOG");
        if (!IsTestedRuntime())
            ImGuiMCP::TextWrapped("%s", UntestedRuntimeWarning.data());
        const auto snapshot = PapyrusBacklogSnapshot();
        ImGuiMCP::BeginDisabled(snapshot.pending);
        if (ImGuiMCP::Button("Refresh snapshot", {0, 0}))
            RefreshPapyrusBacklog();
        ui::NextControl("Full backlog report");
        if (ImGuiMCP::Button("Full backlog report", {0, 0}))
            RefreshPapyrusBacklog(true);
        ImGuiMCP::EndDisabled();
        ImGuiMCP::TextDisabled("A full scan can briefly pause the game; reports are written beside the SKSE logs.");
        ImGuiMCP::TextWrapped("%s", snapshot.status.c_str());
        if (!snapshot.captured)
            return;
        const auto age =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - snapshot.capturedAt);
        ImGuiMCP::TextDisabled("Captured %lld seconds ago; refresh is manual.", static_cast<long long>(age.count()));
        ImGuiMCP::Spacing();
        ui::Heading("QUEUE SIZES");
        ImGuiMCP::Text("Registered stacks: %u", snapshot.registeredStacks);
        ImGuiMCP::Text("Latent return registrations: %u", snapshot.latentReturns);
        ImGuiMCP::Text("Suspended overflow entries: %u", snapshot.suspendedOverflow);
        ImGuiMCP::Text("Function-message overflow entries: %u", snapshot.functionOverflow);
        if (!snapshot.reportPath.empty())
            ImGuiMCP::TextWrapped("Report: %s", snapshot.reportPath.c_str());
        ImGuiMCP::TextWrapped("Counts overlap and must not be added. Overflow counts exclude the VM's fixed queues.");
        ImGuiMCP::Spacing();
        ui::Heading(snapshot.full ? "QUEUED CALL COUNTS" : "QUEUED CALL SAMPLE");
        ImGuiMCP::TextDisabled("%u entries inspected; up to 32 groups shown.", snapshot.sampledCalls);
        if (snapshot.full &&
            snapshot.sampledCalls != static_cast<std::uint64_t>(snapshot.suspendedOverflow) + snapshot.functionOverflow)
            ImGuiMCP::TextWrapped("Scan limit reached. The report does not cover every overflow entry.");
        Rows("Backlog calls", "Receiver / function", snapshot.calls);
        ImGuiMCP::Spacing();
        ui::Heading(snapshot.full ? "STACK STATES" : "STACK STATE SAMPLE");
        ImGuiMCP::TextDisabled("%u of %u registered stacks inspected.", snapshot.sampledStacks,
                               snapshot.registeredStacks);
        Rows("Backlog states", "State", snapshot.states);
        ImGuiMCP::Spacing();
        ImGuiMCP::TextWrapped(
            "Quick snapshots sample the first bounded entries, not a representative breakdown. "
            "Queued calls identify their receiver, not necessarily the event producer. "
            "These counts do not measure CPU cost, request age or prove a deadlock. "
            "No stacks are resumed, cancelled or removed.");
    }
}
