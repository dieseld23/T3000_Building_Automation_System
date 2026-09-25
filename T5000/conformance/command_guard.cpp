// Proves that every command T5000 may send is a read, against the header the
// shipping application uses. Nothing here runs: every check is a static_assert,
// so a whitelisted write is a build failure rather than a write to a live
// controller.
//
// Two checks, because either alone has a hole:
//
//   1. Each whitelisted value EQUALS the CM5 read constant it claims to be.
//      Catches a typo (3 for 2) and a renumbering upstream.
//   2. Each whitelisted value differs from EVERY code in that header this
//      tool must never send. Catches the case check 1 cannot: a value that
//      was deliberately set to a write code, with the X-macro's third column
//      pointing at the matching write constant so that check 1 passes.
//
// The forbidden list is written out by name rather than as "everything >= 100",
// because the header does not respect that boundary - CLEARPANEL_T3000 is 28 -
// and because naming them means a misspelled or deleted constant fails to
// compile here instead of silently dropping out of the check.

#include <stddef.h>
#include <stdint.h>

#include "cm5_header.h"
#include "../bacnet/command.h"

namespace
{
    using t5000::bacnet::ReadCommand;
    using t5000::bacnet::kAllReadCommands;
    using t5000::bacnet::to_wire;

    // --- 1. Each value is the read it says it is. --------------------------
#define T5000_MATCHES_HEADER(name, value, header)                              \
    static_assert(to_wire(ReadCommand::name) == (header),                     \
        "ReadCommand::" #name " is not " #header " in CM5/ud_str.h");
    T5000_READ_COMMANDS(T5000_MATCHES_HEADER)
#undef T5000_MATCHES_HEADER

    // --- 2. No value is anything this tool must not send. ------------------
    //
    // Every CommandRequest enumerator that is not a plain read, ud_str.h:70-153.
    // Everything from :95 on, plus the two below 100 that act on the device.
    constexpr int kNeverSend[] = {
        // Below 100, and not reads.
        CLEARPANEL_T3000,               // :70  "clear panel"
        SEND_ALARM_COMMAND,             // :71

        // The write block.
        WRITEOUTPUT_T3000,
        WRITEINPUT_T3000,
        WRITEVARIABLE_T3000,
        WRITEPID_T3000,
        WRITESCHEDULE_T3000,
        WRITEHOLIDAY_T3000,
        WRITEPROGRAM_T3000,
        WRITETABLE_T3000,
        WRITETOTALIZER_T3000,
        WRITEMONITOR_T3000,
        WRITESCREEN_T3000,
        WRITEARRAY_T3000,
        WRITEALARM_T3000,
        WRITEUNIT_T3000,
        WRITEUSER_T3000,
        WRITETIMESCHEDULE_T3000,
        WRITEANNUALSCHEDULE_T3000,
        WRITEPROGRAMCODE_T3000,
        WRITEINDIVIDUALPOINT_T3000,
        WRITETSTAT_T3000,
        WRITE_COMMAND_50,

        // Named as commands rather than writes, and inside the write range -
        // several share a value with a write above.
        PANEL_INFO1_COMMAND,
        PANEL_INFO2_COMMAND,
        MINICOMMINFO_COMMAND,
        PANELID_COMMAND,
        ICON_NAME_TABLE_COMMAND,
        WRITEDATAMINI_COMMAND,
        SENDCODEMINI_COMMAND,
        SENDDATAMINI_COMMAND,
        READFLASHSTATUS_COMMAND,        // "READ", but 119 - among the writes
        READSTATUSWRITEFLASH_COMMAND,   // and 120, which is also a write
        WRITE_TIMECOMMAND,
        WRITEPRGFLASH_COMMAND,

        WRITEANALOG_CUS_TABLE_T3000,
        WRITEVARUNIT_T3000,
        WRITEEXT_IO_T3000,
        WRITE_TSTATE_SCHEDULE_T3000,
        WRITE_REMOTE_POINT,
        WRITE_SCHEDUAL_TIME_FLAG,
        WRITE_MSV_COMMAND,
        WRITE_EMAIL_ALARM,
        WRITE_WIREGUARD_CFG,
        WRITE_JSON_SCREEN,
        WRITE_JSON_ITEM,
        WRITE_PVARIABLE_T3000,
        WRITE_AT_COMMAND,
        WRITE_GRPHIC_LABEL_COMMAND,
        WRITE_BACNET_TO_MODBUS_COMMAND,
        WRITEPIC_T3000,
        WRITE_MISC,
        WRITE_SPECIAL_COMMAND,
        WRITE_SETTING_COMMAND,
        WRITE_SUB_ID_BY_HAND,
        DELETE_MONITOR_DATABASE,
    };

    constexpr bool never_sent(uint8_t value)
    {
        for (int forbidden : kNeverSend)
        {
            if (forbidden == value)
                return true;
        }
        return false;
    }

    constexpr bool no_read_command_is_forbidden()
    {
        for (ReadCommand c : kAllReadCommands)
        {
            if (never_sent(to_wire(c)))
                return false;
        }
        return true;
    }

    static_assert(no_read_command_is_forbidden(),
        "A ReadCommand has the value of a code T5000 must never send - see "
        "kNeverSend. Reads and writes share one byte on the wire; this is a write "
        "to a live controller.");

    // And the belt to go with those braces: the header's own reads all sit
    // below 100, so a whitelisted value at or above it is wrong whatever the
    // list above says. This is the check that still holds if a new write code
    // is added upstream and nobody adds it to kNeverSend.
    constexpr bool every_read_command_is_below_100()
    {
        for (ReadCommand c : kAllReadCommands)
        {
            if (to_wire(c) >= 100)
                return false;
        }
        return true;
    }

    static_assert(every_read_command_is_below_100(),
        "A ReadCommand is 100 or above, where the header keeps its writes.");
}
