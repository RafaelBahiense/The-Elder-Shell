#pragma once

#include "shell/Requests.h"
#include "shell/Inspection.h"
#include <cstddef>
#include <memory>

namespace elder_shell {
    struct EvaluationEnvironment;
    void RefreshCompletionMetadata(std::string expression);
    std::shared_ptr<const EvaluationEnvironment> CompletionMetadataSnapshot();
    void RefreshVariableInspection();
    VariableInspection VariableInspectionSnapshot();
    InspectionRead ReadInspectedProperty(PropertyInspection inspection);
    inline constexpr std::size_t EvaluationTypeWarningThreshold = 128;
    inline constexpr std::size_t InputVariableWarningThreshold = 32;
    inline constexpr std::size_t ImportedTypeWarningThreshold = 256;

    struct EvaluationUsage {
        std::size_t loadedTypes = 0;
        std::size_t inputVariables = 0;
        std::size_t importedTypes = 0;
    };

    struct InstanceRow {
        std::uint64_t id = 0;
        std::string script, state, handle;
        bool initialized = false;
    };

    struct InstanceExplorer {
        std::vector<InstanceRow> rows;
        std::string status = "Enter a form/reference ID or script name, then Search";
    };

    std::string ExploreInstances(std::string form, std::string script);
    InstanceExplorer InstanceSnapshot();
    std::string UseInstance(std::uint64_t id, std::string variable);

    std::string Submit(std::string command);
    std::vector<Entry> Snapshot();
    EvaluationUsage EvaluationUsageSnapshot();
    void ClearFinished();
    void SessionChanged(bool ready);
}
