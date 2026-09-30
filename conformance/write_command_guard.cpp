// Holds T5000's write lists (bacnet/write_command.h) to the header the
// shipping application uses.
//
// At compile time, against T3000/CM5/ud_str.h:
//
//   W1  each admitted write equals the CM5 constant it names
//   W2  no admitted write is a code T5000 already sends as a read
//   W3  each never-write number is the CM5 name beside it
//   W4  each held number is the CM5 name beside it, and each accepted alias
//   W5  each admitted write reads back through the read of the same points:
//       its CM5 read constant is the write's value less 100
//   W6  each admitted write's entity size is sizeof the CM5 struct it carries
//
// At run time, reading the header as text:
//
//   W7  the lists cover the header. Every enumerator in CommandRequest from
//       WRITEOUTPUT_T3000 to the end, and the two below it that act on a panel
//       (CLEARPANEL_T3000, SEND_ALARM_COMMAND), is on exactly one of the
//       admitted, never, held and alias lists - and nothing else is. A write
//       code added upstream that nobody has listed fails here, rather than
//       being neither held nor refused.
//
// T5000's own build checks what needs nothing of T3000's: that the lists are
// apart, and that an admitted write is 100 or more and not a read
// (bacnet/write_command.cpp).

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "cm5_header.h"
#include "source_text.h"
#include "../bacnet/write_command.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::bacnet;
    using namespace t5000::testing;

    // W1, W2.
#define T5000_MATCHES_HEADER(name, value, header, read, size)                             \
    static_assert(to_wire(WriteCommand::name) == (header),                                \
        "WriteCommand::" #name " is not " #header " in CM5/ud_str.h");                    \
    static_assert((value) == (header), #header " is not " #value " in CM5/ud_str.h");
    T5000_WRITE_COMMANDS(T5000_MATCHES_HEADER)
#undef T5000_MATCHES_HEADER

    constexpr bool is_a_read(int value)
    {
        for (ReadCommand r : kAllReadCommands)
        {
            if (to_wire(r) == value)
                return true;
        }
        return false;
    }

    // W3, W4.
#define T5000_LITERAL(value, header) \
    static_assert((value) == (header), #header " is not " #value " in CM5/ud_str.h");
    T5000_NEVER_WRITE(T5000_LITERAL)
    T5000_HELD_WRITES(T5000_LITERAL)
#undef T5000_LITERAL
#define T5000_ALIAS_LITERAL(value, header, why) \
    static_assert((value) == (header), #header " is not " #value " in CM5/ud_str.h");
    T5000_ACCEPTED_ALIASES(T5000_ALIAS_LITERAL)
#undef T5000_ALIAS_LITERAL

    // W5, W6. The CM5 read and struct for each admitted write, by hand: a
    // write admitted without a line here fails the asserts below.
    constexpr int cm5_read_for(WriteCommand c)
    {
        switch (c)
        {
        case WriteCommand::Inputs:
            return READINPUT_T3000;
        }
        return -1;
    }

    constexpr size_t cm5_struct_size(WriteCommand c)
    {
        switch (c)
        {
        case WriteCommand::Inputs:
            return sizeof(::Str_in_point);
        }
        return 0;
    }

#define T5000_READ_AND_SIZE(name, value, header, read, size)                                          \
    static_assert(!is_a_read(to_wire(WriteCommand::name)),                                            \
        "WriteCommand::" #name " has the value of a ReadCommand.");                                   \
    static_assert(cm5_read_for(WriteCommand::name) == (header) - 100,                                 \
        "WriteCommand::" #name ": its CM5 read is not " #header " - 100.");                           \
    static_assert(to_wire(read_back_for(WriteCommand::name)) == cm5_read_for(WriteCommand::name),    \
        "WriteCommand::" #name " reads back through a ReadCommand that is not its CM5 read.");        \
    static_assert(cm5_struct_size(WriteCommand::name) == entity_size(WriteCommand::name),             \
        "WriteCommand::" #name ": its entity size is not sizeof the CM5 struct it carries.");
    T5000_WRITE_COMMANDS(T5000_READ_AND_SIZE)
#undef T5000_READ_AND_SIZE

    static_assert(sizeof(::Str_in_point) == 46, "Str_in_point is not 46 bytes");

    // ------------------------------------------------------------------ W7

    // The enumerator names of CommandRequest, in the header's order.
    bool command_request_names(std::vector<std::string>& names, std::string& error)
    {
        std::string text;
        if (!t5000::conformance::read_source("T3000\\CM5\\ud_str.h", text, error))
            return false;

        // Only the enum is taken, and comments stripped from it alone: the
        // header holds GBK text elsewhere, which the stripper is not for.
        const size_t end = text.find("} CommandRequest;");
        if (end == std::string::npos || text.find("} CommandRequest;", end + 1) != std::string::npos)
        {
            error = "ud_str.h has no single \"} CommandRequest;\"";
            return false;
        }
        const size_t start = text.rfind("typedef enum", end);
        const size_t open  = start == std::string::npos ? std::string::npos : text.find('{', start);
        if (open == std::string::npos || open > end)
        {
            error = "the CommandRequest enum's opening is not found";
            return false;
        }

        const std::string body = t5000::conformance::strip_comments(text.substr(open + 1, end - open - 1));

        size_t from = 0;
        while (from <= body.size())
        {
            size_t comma = body.find(',', from);
            if (comma == std::string::npos)
                comma = body.size();
            const std::string piece = body.substr(from, comma - from);

            size_t i = 0;
            while (i < piece.size() && (piece[i] == ' ' || piece[i] == '\t' || piece[i] == '\r' || piece[i] == '\n'))
                i++;
            size_t j = i;
            while (j < piece.size() && t5000::conformance::is_identifier_char(piece[j]))
                j++;
            if (j > i)
                names.push_back(piece.substr(i, j - i));

            from = comma + 1;
        }
        return true;
    }

    void test_lists_cover_the_header()
    {
        section("the write lists name every write in CommandRequest, once");

        std::vector<std::string> names;
        std::string error;
        if (!require(command_request_names(names, error), "CommandRequest's names are read from ud_str.h"))
        {
            printf("        %s\n", error.c_str());
            return;
        }
        check(names.size() > 100, "  over a hundred of them");

        // What must be listed: the write block, WRITEOUTPUT_T3000 on, and the
        // two before it that act on a panel.
        std::set<std::string> required = { "CLEARPANEL_T3000", "SEND_ALARM_COMMAND" };
        bool in_block = false;
        for (const std::string& n : names)
        {
            in_block = in_block || n == "WRITEOUTPUT_T3000";
            if (in_block)
                required.insert(n);
        }
        check(in_block, "  the write block starts at WRITEOUTPUT_T3000");

        // What is listed, and how often.
        std::map<std::string, int> listed;
#define T5000_ADMITTED_NAME(name, value, header, read, size) listed[#header]++;
        T5000_WRITE_COMMANDS(T5000_ADMITTED_NAME)
#undef T5000_ADMITTED_NAME
#define T5000_LISTED_NAME(value, header) listed[#header]++;
        T5000_NEVER_WRITE(T5000_LISTED_NAME)
        T5000_HELD_WRITES(T5000_LISTED_NAME)
#undef T5000_LISTED_NAME
#define T5000_ALIAS_NAME(value, header, why) listed[#header]++;
        T5000_ACCEPTED_ALIASES(T5000_ALIAS_NAME)
#undef T5000_ALIAS_NAME

        int missing = 0, extra = 0, twice = 0;
        for (const std::string& n : required)
        {
            if (listed.find(n) == listed.end())
            {
                printf("        %s is in the write block but on no list in bacnet/write_command.h\n", n.c_str());
                missing++;
            }
        }
        for (const auto& [n, count] : listed)
        {
            if (required.find(n) == required.end())
            {
                printf("        %s is listed but is not in the header's write block\n", n.c_str());
                extra++;
            }
            if (count != 1)
            {
                printf("        %s is listed %d times\n", n.c_str(), count);
                twice++;
            }
        }
        check_eq(missing, 0, "every write code is admitted, held or never written");
        check_eq(extra, 0, "  and nothing listed is anything else");
        check_eq(twice, 0, "  each on one list only");
        check_eq((long)listed.size(), (long)required.size(), "  as many names as the block has");
    }
}

int run_write_command_guard_tests()
{
    test_lists_cover_the_header();
    return 0;
}
