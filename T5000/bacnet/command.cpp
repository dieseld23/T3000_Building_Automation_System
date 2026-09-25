#include "command.h"

namespace t5000::bacnet
{
    const char* to_string(ReadCommand c)
    {
        switch (c)
        {
        case ReadCommand::Inputs:
            return "inputs";
        case ReadCommand::CustomUnits:
            return "custom digital ranges";
        case ReadCommand::AnalogCustomTables:
            return "custom analog tables";
        case ReadCommand::Settings:
            return "panel settings";
        }
        return "unknown read command";
    }
}
