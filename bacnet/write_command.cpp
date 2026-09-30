#include "write_command.h"

// The checks on the write lists that need nothing of T3000's, so they hold in
// T5000's own build and in CI's selftest job, which checks out T5000/ alone.
// conformance/write_command_guard.cpp adds the ones that need the header: that
// each number is the CM5 name beside it, and that the lists cover the header.

namespace t5000::bacnet
{
    namespace
    {
#define T5000_VALUE(value, header) value,
        constexpr int kNeverWrite[] = { T5000_NEVER_WRITE(T5000_VALUE) };
        constexpr int kHeldWrites[] = { T5000_HELD_WRITES(T5000_VALUE) };
#undef T5000_VALUE

        template <size_t N>
        constexpr bool listed(const int (&list)[N], int value)
        {
            for (int v : list)
            {
                if (v == value)
                    return true;
            }
            return false;
        }

        constexpr bool is_read(int value)
        {
            for (ReadCommand r : kAllReadCommands)
            {
                if (to_wire(r) == value)
                    return true;
            }
            return false;
        }

        constexpr bool never_and_held_are_apart()
        {
            for (int v : kNeverWrite)
            {
                if (listed(kHeldWrites, v))
                    return false;
            }
            return true;
        }

        static_assert(never_and_held_are_apart(),
            "A value is both never written and held. Held means a later PR may admit it; "
            "never means none may.");

        // Each admitted write, one assert per rule, so a failure names both.
#define T5000_ADMITTED_RULES(name, value, header, read, size)                                          \
        static_assert(to_wire(WriteCommand::name) >= 100,                                              \
            "WriteCommand::" #name " is below 100, where the header keeps its reads.");                \
        static_assert(!is_read(to_wire(WriteCommand::name)),                                           \
            "WriteCommand::" #name " has the value of a ReadCommand. One byte on the wire means both."); \
        static_assert(!listed(kNeverWrite, to_wire(WriteCommand::name)),                               \
            "WriteCommand::" #name " is on T5000_NEVER_WRITE.");                                        \
        static_assert(!listed(kHeldWrites, to_wire(WriteCommand::name)),                               \
            "WriteCommand::" #name " is still on T5000_HELD_WRITES. Admitting a write removes its "     \
            "line there, and the line of any other name with its value.");                            \
        static_assert(to_wire(WriteCommand::name) == to_wire(read_back_for(WriteCommand::name)) + 100, \
            "WriteCommand::" #name " is not its read-back plus 100, as every write/read pair in "      \
            "the header is.");                                                                         \
        static_assert(entity_size(WriteCommand::name) == (size) && (size) > 0,                         \
            "WriteCommand::" #name " has no entity size.");
        T5000_WRITE_COMMANDS(T5000_ADMITTED_RULES)
#undef T5000_ADMITTED_RULES

        static_assert(entity_size(WriteCommand::Inputs) == sizeof(wire::InputPoint),
            "An input write carries Str_in_point as wire::InputPoint lays it out.");
    }

    const char* to_string(WriteCommand c)
    {
        switch (c)
        {
        case WriteCommand::Inputs:
            return "inputs";
        }
        return "unknown";
    }
}
