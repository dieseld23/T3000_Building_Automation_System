// Tests for the flat-object reader.
//
// This is the only place in the tool where text from a browser turns into a
// value the device layer acts on, so its refusals matter more than its
// successes. A reader that quietly returns 0 for a malformed handle hands the
// caller a plausible key for a device that is not the one asked for.

#include "read.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::json;
    using namespace t5000::testing;

    void test_it_reads_the_shapes_it_claims_to()
    {
        section("strings and numbers are read out of a flat object");

        const std::string body = "{\"ip\":\"192.168.1.23\",\"waitMs\":9000,\"longer\":true}";

        std::string ip;
        check(read_string(body, "ip", ip), "read the string");
        check(ip == "192.168.1.23", "unquoted");

        int wait = 0;
        check(read_int(body, "waitMs", wait), "read the number");
        check_eq(wait, 9000, "as a number");

        bool longer = false;
        check(read_bool(body, "longer", longer), "read the bool");
        check(longer, "true");
    }

    void test_an_absent_key_is_not_an_error()
    {
        section("an absent key leaves the target alone");

        // These are patch-style bodies: a field the page did not send means
        // "leave it", not "set it to zero". Treating absent as 0 is how a
        // scan that omits waitMs ends up with no timeout at all.
        const std::string body = "{\"other\":1}";

        int wait = 9000;
        check(read_int(body, "waitMs", wait), "not an error");
        check_eq(wait, 9000, "and the default survives");

        std::string ip = "192.168.1.1";
        check(read_string(body, "ip", ip), "not an error");
        check(ip == "192.168.1.1", "and the default survives");

        bool flag = true;
        check(read_bool(body, "flag", flag), "not an error");
        check(flag, "and the default survives");
    }

    void test_a_present_but_broken_value_is_refused()
    {
        section("a key that is present and unreadable is an error");

        // The distinction that matters. Absent means "use the default";
        // present-and-wrong means the caller asked for something specific
        // and got it wrong, which is a 400 rather than a silent default.
        int wait = 9000;
        check(!read_int("{\"waitMs\":\"banana\"}", "waitMs", wait), "not a number");
        check_eq(wait, 9000, "and nothing was assigned");

        check(!read_int("{\"waitMs\":12abc}", "waitMs", wait), "trailing rubbish");
        check_eq(wait, 9000, "still untouched");

        bool flag = false;
        check(!read_bool("{\"f\":\"yes\"}", "f", flag), "not a bool");
        check(!flag, "and nothing was assigned");
    }

    void test_a_negative_handle_is_refused_not_wrapped()
    {
        section("a negative handle is refused rather than wrapped around");

        // strtoull turns "-1" into 18446744073709551615 without complaint.
        // As a handle that is not an error value, it is a number no device
        // has - so the request would be answered "no such device" instead of
        // "that is not a handle", and a page bug would look like a missing
        // controller.
        unsigned long long handle = 7;
        check(!read_u64("{\"handle\":-1}", "handle", handle), "refused");
        check_eq((long)handle, 7, "and nothing was assigned");

        check(read_u64("{\"handle\":42}", "handle", handle), "a real one is read");
        check_eq((long)handle, 42, "correctly");

        // Handles cross as quoted strings; both spellings have to work,
        // because the page sends the string and a curl user will send the
        // number.
        unsigned long long quoted = 0;
        check(read_u64("{\"handle\":\"42\"}", "handle", quoted), "quoted is read too");
        check_eq((long)quoted, 42, "to the same value");
    }

    void test_it_admits_what_it_cannot_do()
    {
        section("the documented blind spots behave as documented");

        // Not defects - this is a flat-object reader and says so. Pinned so
        // that if someone later feeds it something nested, the behaviour is
        // known rather than discovered.
        std::string got;

        // Nesting: it finds the inner key, because it does not track depth.
        check(find_value("{\"a\":{\"b\":\"inner\"}}", "b", got), "an inner key is found");
        check(got == "inner", "as if it were top level");

        // First key wins on a duplicate.
        check(find_value("{\"k\":\"first\",\"k\":\"second\"}", "k", got), "duplicate key");
        check(got == "first", "the first one wins");

        // An escaped quote ends the string early. This is why device text
        // never comes back in through here.
        check(find_value("{\"k\":\"a\\\"b\"}", "k", got), "an escaped quote parses");
        check(got == "a\\", "but truncates at the backslash - a known limit");
    }
}

int run_json_read_tests()
{
    test_it_reads_the_shapes_it_claims_to();
    test_an_absent_key_is_not_an_error();
    test_a_present_but_broken_value_is_refused();
    test_a_negative_handle_is_refused_not_wrapped();
    test_it_admits_what_it_cannot_do();
    return 0;
}
