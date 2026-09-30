#include "command.h"

namespace t5000::bacnet
{
    const char* to_string(ReadCommand c)
    {
        switch (c)
        {
        case ReadCommand::Outputs:
            return "outputs";
        case ReadCommand::Inputs:
            return "inputs";
        case ReadCommand::Variables:
            return "variables";
        case ReadCommand::CustomUnits:
            return "custom digital ranges";
        case ReadCommand::AnalogCustomTables:
            return "custom analog tables";
        case ReadCommand::VariableUnits:
            return "custom variable units";
        case ReadCommand::MsvTables:
            return "multi-state tables";
        case ReadCommand::Settings:
            return "panel settings";
        }
        return "unknown read command";
    }
}
