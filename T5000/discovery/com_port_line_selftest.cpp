// Tests for the COM port line.
//
// Nothing here opens a real port. The echo and the messages are worked out
// from bytes and Windows error codes. open() is only handed names it refuses
// before asking Windows, and one no machine has, so a port wired to
// something is never touched - and if the refusal were broken, those names
// would still open nothing.

#include "com_port_line.h"

#include <windows.h>

#include <string>
#include <vector>

#include "../testing/check.h"

namespace
{
    using namespace t5000::discovery;
    using namespace t5000::serial;
    using namespace t5000::testing;

    bool has(const std::string& s, const char* part)
    {
        return s.find(part) != std::string::npos;
    }

    std::vector<uint8_t> with_crc(std::vector<uint8_t> b)
    {
        const uint16_t crc = crc16(b.data(), b.size());
        b.push_back((uint8_t)(crc & 0xFF));
        b.push_back((uint8_t)(crc >> 8));
        return b;
    }

    void test_an_echo_is_found()
    {
        section("an adapter's echo of the frame is found in front of the reply");

        const std::vector<uint8_t> sent  = ScanFrame::range_query(1, 254).bytes();
        const std::vector<uint8_t> reply = with_crc({ 0xFF, 0x19, 0x05, 0x11, 0x22, 0x33, 0x44 });

        check_eq((long)leading_echo(sent.data(), sent.size(), sent), (long)sent.size(), "the echo alone is all echo");
        check_eq((long)leading_echo(sent.data(), 3, sent), 3, "its first bytes, still arriving, are echo");

        std::vector<uint8_t> both = sent;
        both.insert(both.end(), reply.begin(), reply.end());
        check_eq((long)leading_echo(both.data(), both.size(), sent), (long)sent.size(),
                 "in front of a reply, the echo is taken and the reply left");

        check_eq((long)leading_echo(reply.data(), reply.size(), sent), 0, "a reply with no echo has none");
        check_eq((long)leading_echo(reply.data(), reply.size(), {}), 0, "nothing is echo when nothing was sent");
        check_eq((long)leading_echo(reply.data(), 0, sent), 0, "and nothing received is none");
    }

    void test_a_reply_like_the_frame_is_kept()
    {
        section("a reply that only begins like the frame is not taken for an echo");

        // A query for id 5 alone, and the reply from a device at 5 whose
        // serial begins with the query's own last three bytes.
        const std::vector<uint8_t> sent = ScanFrame::range_query(5, 5).bytes();
        if (!require(sent.size() == 6, "the query is six bytes"))
            return;
        const std::vector<uint8_t> reply = with_crc({ 0xFF, 0x19, 0x05, sent[3], sent[4], sent[5], 0x00 });

        check_eq((long)leading_echo(reply.data(), reply.size(), sent), 0, "kept whole: its CRC checks out");

        std::vector<uint8_t> both = sent;
        both.insert(both.end(), reply.begin(), reply.end());
        check_eq((long)leading_echo(both.data(), both.size(), sent), (long)sent.size(),
                 "and behind a real echo, the echo is still taken");
    }

    void test_open_failures_are_explained()
    {
        section("a port that will not open is explained by why");

        const std::string held = open_error_text("COM4", ERROR_ACCESS_DENIED);
        check(has(held, "COM4 is open in another program"), "access denied: another program has it");
        check(has(open_error_text("COM4", ERROR_SHARING_VIOLATION), "open in another program"),
              "a sharing violation says the same");

        check(has(open_error_text("COM4", ERROR_FILE_NOT_FOUND), "COM4 is not there"), "not found: gone");
        check(has(open_error_text("COM4", ERROR_PATH_NOT_FOUND), "unplugged"), "and perhaps unplugged");

        check(has(open_error_text("COM4", ERROR_GEN_FAILURE), "did not respond"), "a failed device");
        check(has(open_error_text("COM4", ERROR_DEVICE_NOT_CONNECTED), "did not respond"), "one not connected");

        const std::string other = open_error_text("COM4", ERROR_INVALID_PARAMETER);
        check(has(other, "COM4 could not be opened"), "anything else names the port");
        check(has(other, "(Windows error 87)"), "and gives the code");
    }

    void test_only_plain_names_are_opened()
    {
        section("a name that is not plain is refused before Windows is asked");

        const char* const names[] = { "", "T5000\\NOSUCH", "T5000.NOSUCH", "T5000:NOSUCH", "T5000 NOSUCH",
                                      "T5000NOSUCHPORTNAMEISMORETHAN32CH" };
        for (const char* name : names)
        {
            ComPortLine line;
            std::string error;
            const bool opened = line.open(name, 9600, error);
            check(!opened && !line.is_open(), (std::string("\"") + name + "\" is not opened").c_str());
            check(has(error, "is not a serial port name T5000 opens"), "refused by its name, not by Windows");
        }
    }

    void test_a_port_that_is_not_there()
    {
        section("a plain name no machine has is not there");

        ComPortLine line;
        std::string error;
        check(!line.open("T5000NOSUCHPORT0", 9600, error), "it does not open");
        check(!line.is_open(), "and the line is not open");
        check(has(error, "T5000NOSUCHPORT0 is not there"), "and is said to be not there");
    }

    void test_a_line_that_is_not_open_does_nothing()
    {
        section("a line that is not open sends, reads and changes rate never");

        ComPortLine line;
        std::string error;
        check(!line.set_rate(9600, error), "no rate");
        check(has(error, "not open"), "because it is not open");

        error.clear();
        check(!line.send(ScanFrame::range_query(1, 254), error), "no frame");
        check(has(error, "not open"), "because it is not open");

        error.clear();
        uint8_t buffer[16] = {};
        check_eq(line.receive(buffer, (int)sizeof(buffer), 10, error), -1, "no reply");
        check(has(error, "not open"), "because it is not open");

        line.close();
        check(!line.is_open(), "and closing it again is harmless");
    }
}

int run_com_port_line_tests()
{
    test_an_echo_is_found();
    test_a_reply_like_the_frame_is_kept();
    test_open_failures_are_explained();
    test_only_plain_names_are_opened();
    test_a_port_that_is_not_there();
    test_a_line_that_is_not_open_does_nothing();
    return 0;
}
