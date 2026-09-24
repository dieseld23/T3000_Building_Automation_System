#pragma once

// The private-transfer commands T5000 is allowed to send.
//
// Every Temco private-transfer request carries a one-byte command, and reads
// and writes share that byte. T3000/CM5/ud_str.h:40-154 defines them all in one
// enum, CommandRequest, and nothing about a code says which kind it is:
//
//   - Not the range. "Reads are 1-99, writes are 100 and up" is nearly true,
//     and wrong exactly where it matters: CLEARPANEL_T3000 is 28 (:70).
//   - Not the name. READFLASHSTATUS_COMMAND is 119 and
//     READSTATUSWRITEFLASH_COMMAND is 120, both among the writes (:129-130).
//   - Not even uniqueness. PANEL_INFO1_COMMAND and WRITEMONITOR_T3000 are both
//     110; ICON_NAME_TABLE_COMMAND and WRITEUNIT_T3000 are both 114.
//
// So this is a whitelist, not a rule. It is a scoped enum rather than a uint8_t
// for the same reason Handle is: the encoder that puts a command on the wire
// takes a ReadCommand, so handing it a raw byte - a write code, a typo, a value
// read from somewhere - fails to compile instead of reaching a controller.
//
// command_guard.cpp checks every value here against the real header, and
// against every code in that header this tool must never send. A new value
// that collides with a write does not build.

#include <stddef.h>
#include <stdint.h>

namespace t5000::bacnet
{
    // The single list. The enum and kAllReadCommands are both generated from
    // it, so the guard cannot be walking a list that has fallen behind the
    // enum - which is how a whitelist check quietly stops checking.
    //
    //   X(enumerator, wire value, the CM5/ud_str.h constant it must equal)
#define T5000_READ_COMMANDS(X)                                                \
    X(Inputs, 2, READINPUT_T3000)                    /* ud_str.h:42 */        \
    X(CustomUnits, 14, READUNIT_T3000)               /* ud_str.h:54 */        \
    X(AnalogCustomTables, 34, READANALOG_CUS_TABLE_T3000) /* ud_str.h:73 */   \
    X(Settings, 98, READ_SETTING_COMMAND)            /* ud_str.h:92 */

    enum class ReadCommand : uint8_t
    {
#define T5000_ENUMERATOR(name, value, header) name = value,
        T5000_READ_COMMANDS(T5000_ENUMERATOR)
#undef T5000_ENUMERATOR
    };

    inline constexpr ReadCommand kAllReadCommands[] = {
#define T5000_LISTED(name, value, header) ReadCommand::name,
        T5000_READ_COMMANDS(T5000_LISTED)
#undef T5000_LISTED
    };

    constexpr uint8_t to_wire(ReadCommand c)
    {
        return static_cast<uint8_t>(c);
    }

    const char* to_string(ReadCommand c);
}
