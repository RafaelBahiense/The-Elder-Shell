#pragma once
#include "ui/Menu.h"
#include "vm/VM.h"
#include "shell/ExpressionTypes.h"

namespace preview {
    enum class Scenario { Ready, Empty, Pending, Rejected, Large, Untested };

    struct Session {
        bool valid = false, ready = true;
        int scenario = 0;
        float scale = 1;
        std::string reloadStatus = "Watching UI sources";
        elder_shell::MenuSession menu;
        elder_shell::Requests requests;
        elder_shell::VariableInspection variables;
        elder_shell::InstanceExplorer instances;
        elder_shell::EvaluationUsage usage;
        elder_shell::EvaluationEnvironment metadata;
    };
}
