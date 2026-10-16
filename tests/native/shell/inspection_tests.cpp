#include <doctest/doctest.h>
#include "shell/Inspection.h"
#include "shell/Variables.h"

using namespace elder_shell;

namespace {
    TEST_CASE("Property inspection accepts only a bound variable, array indices and a property") {
        for (const auto expression : {"record.Value", "record[0].Value", "record[12].Enabled"})
            CHECK(ValidPropertyInspection({"record", expression, {1, 2}}));
        for (const auto expression :
             {"other.Value", "record.Reset()", "record.Value = 1", "record[-1].Value", "record[GetIndex()].Value",
              "record.Value.Other", "record[0]", "record.\"Value\"", "record.Value\nDebug.Notification(\"x\")"})
            CHECK_FALSE(ValidPropertyInspection({"record", expression, {1, 2}}));
    }

    TEST_CASE("A refreshed variable binding invalidates the previous property's receiver") {
        Variables<int> variables;
        const Ticket first{1, 2}, next{2, 2};
        REQUIRE(variables.Reserve("record", first, "Record"));
        variables.Complete("record", first, 10);
        CHECK(SameInspectionOwner(variables.Find("record")->owner, first));
        REQUIRE(variables.Reserve("record", next, "Record"));
        CHECK_FALSE(SameInspectionOwner(variables.Find("record")->owner, first));
        CHECK_FALSE(SameInspectionOwner(first, Ticket{1, 3}));
    }

    TEST_CASE("Inspection snapshot budgets bound values, nesting and property metadata") {
        InspectionBudget budget{2, 1};
        CHECK_FALSE(budget.TakeValue(5));
        CHECK(budget.values == 2);
        CHECK(budget.TakeValue(0));
        CHECK(budget.TakeValue(4));
        CHECK_FALSE(budget.TakeValue(0));
        CHECK(budget.TakeProperty());
        CHECK_FALSE(budget.TakeProperty());
    }
}
