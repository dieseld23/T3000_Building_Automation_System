#pragma once

// The private-transfer writes T5000 is allowed to encode, and the ones it is
// not.
//
// A sibling of command.h, not an extension of it. ReadCommand is the proof
// that everything the read path sends is a read; adding a write to it would
// end that proof. A write is its own type, so no read-path function can be
// handed one: encode_read_request takes a ReadCommand, and nothing converts.
//
// Three lists, together covering every code at or above 100 in
// T3000/CM5/ud_str.h's CommandRequest enum, and the two below it that act on a
// panel:
//
//   T5000_WRITE_COMMANDS  admitted: the only writes that can be encoded
//   T5000_NEVER_WRITE     never admitted, whatever is built later
//   T5000_HELD_WRITES     not admitted yet. A PR that admits one deletes its
//                         line here, so the decision shows in review as a
//                         change to this file as well as the one using it.
//
// conformance/write_command_guard.cpp ties every number here to its CM5 name
// and checks that the three lists, and the aliases, name every enumerator in
// that block exactly once. The static_asserts in write_command.cpp hold the
// lists apart in T5000's own build, which runs without T3000.
//
// Admitting a code also needs its read: every write is read back, so every
// admitted write names the ReadCommand that reads the same points. That rules
// out the codes with no read at all - WRITEPRGFLASH_COMMAND (122),
// WRITE_SPECIAL_COMMAND (197), WRITE_SUB_ID_BY_HAND (199),
// DELETE_MONITOR_DATABASE (200) - by construction, as well as by listing.

#include <stddef.h>
#include <stdint.h>

#include "command.h"
#include "../wire/decode.h"

namespace t5000::bacnet
{
    //   X(enumerator, wire value, the CM5/ud_str.h constant it must equal,
    //     the ReadCommand that reads it back, bytes per point on the wire)
    //
    // Inputs first, alone: Str_in_point's layout is compiler-checked
    // (conformance/wire_guard.cpp), its read is whitelisted, and 102 shares its
    // value with no other name in the header.
#define T5000_WRITE_COMMANDS(X)                                                        \
    X(Inputs, 102, WRITEINPUT_T3000, Inputs, wire::kInputPointWireSize) /* ud_str.h:96 */

    //   X(wire value, CM5 name). Codes that erase, commit, reset or reach past
    // the panel, and that share their value with no write T5000 might one day
    // admit.
#define T5000_NEVER_WRITE(X)                                                           \
    X(28, CLEARPANEL_T3000)               /* clears the panel */                       \
    X(32, SEND_ALARM_COMMAND)                                                          \
    X(119, READFLASHSTATUS_COMMAND)       /* "READ", but among the writes */           \
    X(122, WRITEPRGFLASH_COMMAND)         /* commits to flash; never answered */       \
    X(194, WRITE_BACNET_TO_MODBUS_COMMAND) /* writes any Modbus register */            \
    X(197, WRITE_SPECIAL_COMMAND)         /* clears health counters */                 \
    X(199, WRITE_SUB_ID_BY_HAND)          /* rewrites the subnet database */           \
    X(200, DELETE_MONITOR_DATABASE)       /* erases trend data */

    //   X(wire value, CM5 name). Every other name in the write block, in the
    // header's order. Several values appear twice: the header gives 110-114,
    // 116-118 and 120 a second name each (see command.h), and admitting one of
    // those values means deciding about both names.
#define T5000_HELD_WRITES(X)                                                           \
    X(101, WRITEOUTPUT_T3000)                                                          \
    X(103, WRITEVARIABLE_T3000)                                                        \
    X(104, WRITEPID_T3000)                                                             \
    X(105, WRITESCHEDULE_T3000)                                                        \
    X(106, WRITEHOLIDAY_T3000)                                                         \
    X(107, WRITEPROGRAM_T3000)                                                         \
    X(108, WRITETABLE_T3000)              /* T3000 has no encoder for it */            \
    X(109, WRITETOTALIZER_T3000)          /* nor for this */                           \
    X(110, WRITEMONITOR_T3000)                                                         \
    X(111, WRITESCREEN_T3000)             /* graphics */                               \
    X(112, WRITEARRAY_T3000)                                                           \
    X(113, WRITEALARM_T3000)                                                           \
    X(114, WRITEUNIT_T3000)                                                            \
    X(115, WRITEUSER_T3000)               /* passwords in clear */                     \
    X(117, WRITETIMESCHEDULE_T3000)                                                    \
    X(118, WRITEANNUALSCHEDULE_T3000)                                                  \
    X(116, WRITEPROGRAMCODE_T3000)        /* replaces control logic */                 \
    X(120, WRITEINDIVIDUALPOINT_T3000)    /* no encoder in T3000 */                    \
    X(133, WRITETSTAT_T3000)                                                           \
    X(150, WRITE_COMMAND_50)              /* no encoder in T3000 */                    \
    X(110, PANEL_INFO1_COMMAND)                                                        \
    X(111, PANEL_INFO2_COMMAND)                                                        \
    X(112, MINICOMMINFO_COMMAND)                                                       \
    X(113, PANELID_COMMAND)                                                            \
    X(114, ICON_NAME_TABLE_COMMAND)                                                    \
    X(116, WRITEDATAMINI_COMMAND)                                                      \
    X(117, SENDCODEMINI_COMMAND)                                                       \
    X(118, SENDDATAMINI_COMMAND)                                                       \
    X(120, READSTATUSWRITEFLASH_COMMAND)                                               \
    X(121, WRITE_TIMECOMMAND)             /* sets the panel clock */                   \
    X(134, WRITEANALOG_CUS_TABLE_T3000)                                                \
    X(136, WRITEVARUNIT_T3000)                                                         \
    X(137, WRITEEXT_IO_T3000)                                                          \
    X(138, WRITE_TSTATE_SCHEDULE_T3000)                                                \
    X(140, WRITE_REMOTE_POINT)            /* no encoder in T3000 */                    \
    X(141, WRITE_SCHEDUAL_TIME_FLAG)                                                   \
    X(142, WRITE_MSV_COMMAND)                                                          \
    X(143, WRITE_EMAIL_ALARM)             /* SMTP password in clear */                 \
    X(144, WRITE_WIREGUARD_CFG)           /* keys; can cut the panel off */            \
    X(186, WRITE_JSON_SCREEN)             /* graphics */                               \
    X(187, WRITE_JSON_ITEM)               /* graphics */                               \
    X(188, WRITE_PVARIABLE_T3000)         /* 188 means another thing in the DLL's copy */ \
    X(190, WRITE_AT_COMMAND)              /* unreachable in T3000 */                   \
    X(191, WRITE_GRPHIC_LABEL_COMMAND)    /* graphics */                               \
    X(195, WRITEPIC_T3000)                /* graphics */                               \
    X(196, WRITE_MISC)                    /* zeroes trend-log indexes */               \
    X(198, WRITE_SETTING_COMMAND)         /* carries reset and reboot as a byte */

    //   X(wire value, CM5 name, why). A name that shares its value with an
    // admitted write, and has been looked at and accepted. Empty while no
    // shared value is admitted.
#define T5000_ACCEPTED_ALIASES(X)

    enum class WriteCommand : uint8_t
    {
#define T5000_ENUMERATOR(name, value, header, read, size) name = value,
        T5000_WRITE_COMMANDS(T5000_ENUMERATOR)
#undef T5000_ENUMERATOR
    };

    inline constexpr WriteCommand kAllWriteCommands[] = {
#define T5000_LISTED(name, value, header, read, size) WriteCommand::name,
        T5000_WRITE_COMMANDS(T5000_LISTED)
#undef T5000_LISTED
    };

    constexpr uint8_t to_wire(WriteCommand c)
    {
        return static_cast<uint8_t>(c);
    }

    // The read that fetches the points this write changes, for reading them
    // back afterwards.
    constexpr ReadCommand read_back_for(WriteCommand c)
    {
        switch (c)
        {
#define T5000_READ_BACK(name, value, header, read, size) \
        case WriteCommand::name:                         \
            return ReadCommand::read;
            T5000_WRITE_COMMANDS(T5000_READ_BACK)
#undef T5000_READ_BACK
        }
        return ReadCommand::Settings;   // not reached: every enumerator is listed
    }

    // Bytes per point on the wire: what the Temco header's entitysize says,
    // and what each point in the payload must be.
    constexpr size_t entity_size(WriteCommand c)
    {
        switch (c)
        {
#define T5000_SIZE(name, value, header, read, size) \
        case WriteCommand::name:                    \
            return size;
            T5000_WRITE_COMMANDS(T5000_SIZE)
#undef T5000_SIZE
        }
        return 0;
    }

    const char* to_string(WriteCommand c);
}
