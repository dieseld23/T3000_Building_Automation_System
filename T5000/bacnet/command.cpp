#include "command.h"

namespace t5000::bacnet
{
    const char* to_string(ReadCommand c)
    {
        switch (c)
        {
        case ReadCommand::Inputs:
            return "read inputs";
        }
        return "unknown read command";
    }
}
