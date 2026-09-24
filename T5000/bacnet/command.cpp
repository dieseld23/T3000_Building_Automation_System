#include "command.h"

namespace t5000::bacnet
{
    const char* to_string(ReadCommand c)
    {
        switch (c)
        {
        case ReadCommand::Inputs:
            return "read inputs";
        case ReadCommand::CustomUnits:
            return "read custom digital ranges";
        case ReadCommand::AnalogCustomTables:
            return "read custom analog tables";
        case ReadCommand::Settings:
            return "read panel settings";
        }
        return "unknown read command";
    }
}
