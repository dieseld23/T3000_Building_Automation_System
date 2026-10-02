// Tests for reading a firmware file as ISP reads one: which reader, what
// buffer and fill, where the header is, and what ISP counts as the data.
//
// The property most worth guarding is that the image is the one ISP would
// send. A byte in the wrong place, or the wrong fill past the data, is
// flashed into the device.

#include "firmware_file.h"

#include <stdio.h>
#include <string.h>

#include "../testing/check.h"

namespace
{
    using namespace t5000::firmware;
    using namespace t5000::testing;

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // One record, as a line of a .hex file, with its checksum.
    std::string record(int type, unsigned address, const std::vector<uint8_t>& data)
    {
        std::vector<uint8_t> b = { (uint8_t)data.size(), (uint8_t)(address >> 8), (uint8_t)address, (uint8_t)type };
        b.insert(b.end(), data.begin(), data.end());
        uint8_t sum = 0;
        for (const uint8_t x : b)
            sum = (uint8_t)(sum + x);
        b.push_back((uint8_t)(0x100 - sum));
        std::string line = ":";
        char two[3];
        for (const uint8_t x : b)
        {
            snprintf(two, sizeof two, "%02X", x);
            line += two;
        }
        return line + "\r\n";
    }

    const std::string kEnd = ":00000001FF\r\n";

    std::vector<uint8_t> bytes_of(const std::string& s)
    {
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    // A header: company, name, version.
    std::vector<uint8_t> header(const char* company, const char* name, int version)
    {
        std::vector<uint8_t> h(header_at::size, 0);
        memcpy(&h[header_at::company], company, strnlen(company, 5));
        memcpy(&h[header_at::product_name], name, strnlen(name, 10));
        h[header_at::software_low]  = (uint8_t)(version & 0xFF);
        h[header_at::software_high] = (uint8_t)(version >> 8);
        return h;
    }

    // Header records from `at`, 16 bytes to a line.
    std::string header_records(unsigned at, const std::vector<uint8_t>& h)
    {
        return record(0, at, std::vector<uint8_t>(h.begin(), h.begin() + 16)) +
               record(0, at + 16, std::vector<uint8_t>(h.begin() + 16, h.end()));
    }

    bool read(const std::string& name, const std::string& text, Path path, FirmwareFile& file, std::string& why)
    {
        const std::vector<uint8_t> b = bytes_of(text);
        return read_firmware(name, b.data(), b.size(), path, file, why);
    }

    // An ASIX-style file: data records, the header at 0x100.
    std::string asix_file()
    {
        return record(0, 0x0000, { 0x11, 0x22, 0x33 }) + header_records(0x100, header("TEMCO", "TSTAT8", 101)) +
               record(0, 0x0200, { 0xAA, 0xBB }) + kEnd;
    }

    void test_the_header()
    {
        section("firmware: the header is read as ISP reads it");

        Header h;
        memcpy(h.bytes, header("Temco", "TSTAT8", 0x1234).data(), header_at::size);
        check_streq(h.company().c_str(), "Temco", "the company is its five bytes");
        bool ends = false;
        check_streq(h.product_name(ends).c_str(), "TSTAT8", "the name is up to its 0");
        check(ends, "... and ends");
        check_eq(h.software_low(), 0x34, "software_low is byte 15");
        check_eq(h.software_high(), 0x12, "software_high is byte 16");
        check_eq(h.version(), 0x1234, "the version is high * 256 + low");

        // A name that fills its ten bytes runs on, as strlen does.
        memcpy(h.bytes, header("TEMCO", "ABCDEFGHIJ", 'K' + 'L' * 256).data(), header_at::size);
        check_streq(h.product_name(ends).c_str(), "ABCDEFGHIJKL", "a full name runs on into the version bytes");
        check(ends, "... and ends at the reserved bytes' 0");

        memset(h.bytes, 'X', header_at::size);
        const std::string all = h.product_name(ends);
        check(!ends, "a header with no 0 after the name does not end");
        check_eq((long)all.size(), 15, "... and its name is what the header holds after the company");

        memset(h.bytes, 0, header_at::size);
        memcpy(h.bytes, "CO2", 3);
        check_streq(h.company().c_str(), "CO2", "a company shorter than five bytes stops at its 0");
    }

    void test_a_hex_file_by_address()
    {
        section("firmware: a .hex file read by address");

        FirmwareFile f;
        std::string why;
        if (!require(read("a.hex", asix_file(), Path::Serial, f, why), "an ASIX-style .hex file is read"))
        {
            printf("        %s\n", why.c_str());
            return;
        }
        check(f.kind == FileKind::Hex, "it is a .hex");
        check(f.format == HexFormat::Data, "the first record is data, so it is read by address");
        check(f.chip == Chip::Asix, "and is not for an ARM chip");
        check_eq((long)f.header_at, 0x100, "the header is at 0x100");
        check_streq(f.header.company().c_str(), "TEMCO", "the header's company");
        check_eq(f.header.version(), 101, "the header's version");
        check_eq(f.image[0], 255, "the byte at address 0000 is made 255");
        check_eq(f.image[1], 0x22, "the rest of that record is as the file has it");
        check_eq(f.image[0x201], 0xBB, "a record is written at its address");
        check_eq((long)f.data_size, 0x201, "the data is the highest address written, less one");
        check_eq((long)f.image.size(), (long)kHexBufferLength, "on serial, a .hex is read into 0x1FFFFF bytes");
        check_eq(f.image[0x202], 0x00, "... filled with 0x00");
        check(f.sections.empty(), "a file read by address has no sections");

        check(read("a.hex", asix_file(), Path::Controller, f, why), "the same file through a controller");
        check_eq((long)f.image.size(), (long)kHexBufferLength, "through a controller, a .hex is read into 0x1FFFFF bytes");
        check_eq(f.image[0x202], 0xFF, "... filled with 0xFF");

        check(read("a.hex", asix_file(), Path::Network, f, why), "the same file on the network");
        check_eq((long)f.image.size(), (long)kBinBufferLength, "on the network, a .hex is read into 0x9FFFFF bytes");
        check_eq(f.image[0x202], 0x00, "... filled with 0x00");

        // A segment address record first: read the same way, its two bytes
        // written at its address as data.
        const std::string seg = record(2, 0x0000, { 0x10, 0x00 }) + record(0, 0x0010, { 0x01 }) +
                                header_records(0x100, header("TEMCO", "T", 1)) + kEnd;
        check(read("s.hex", seg, Path::Serial, f, why), "a file starting with a segment address record is read");
        check(f.format == HexFormat::SegmentAddress, "... as ISP's segment address reader");
        check_eq(f.image[0], 255, "... its record's first byte, at 0000, made 255");
        check_eq(f.image[1], 0x00, "... and its second written as data");
        check_eq(f.image[0x10], 0x01, "... and the data record at its address");

        // A record with an address lower than one before it does not lower
        // the count.
        const std::string down = header_records(0x100, header("TEMCO", "T", 1)) + record(0, 0x0300, { 1 }) +
                                 record(0, 0x0010, { 2 }) + kEnd;
        check(read("d.hex", down, Path::Serial, f, why), "records in any order are read");
        check_eq((long)f.data_size, 0x300, "the count is the highest address written, less one");

        // Lower-case hex digits and the suffix's case.
        std::string lower = asix_file();
        for (char& c : lower)
            c = (char)tolower((unsigned char)c);
        check(read("A.HEX", lower, Path::Serial, f, why), "lower-case digits and an upper-case suffix are read");
        check_eq(f.image[0x201], 0xBB, "... to the same bytes");

        // Nothing after the end-of-file record is read.
        const std::string after = asix_file() + "not a record\r\n";
        check(read("a.hex", after, Path::Serial, f, why), "text after the end-of-file record is not read");
    }

    // A file for an ARM chip: a linear address record first, whose data
    // says the chip, then the header where that chip has it.
    std::string arm_file(unsigned first_high, unsigned header_high, unsigned header_low)
    {
        return record(4, 0, { (uint8_t)(first_high >> 8), (uint8_t)first_high }) + record(0, 0x0000, { 0x11 }) +
               record(4, 0, { (uint8_t)(header_high >> 8), (uint8_t)header_high }) +
               header_records(header_low, header("Temco", "MINI_ARM", 6000)) + record(5, 0, { 0, 0, 0, 0 }) + kEnd;
    }

    void test_a_hex_file_by_linear_address()
    {
        section("firmware: a .hex file read by linear address");

        FirmwareFile f;
        std::string why;

        // 0x0800: a 32K ARM chip, its header at 0x8200.
        if (!require(read("m.hex", arm_file(0x0800, 0x0800, 0x8200), Path::Serial, f, why), "a 32K ARM file is read"))
        {
            printf("        %s\n", why.c_str());
            return;
        }
        check(f.format == HexFormat::LinearAddress, "a linear address record first is read by linear address");
        check(f.chip == Chip::Arm32K, "0x0800 first is a 32K ARM chip");
        check_eq((long)f.header_at, 0x8200, "... whose header is at 0x8200");
        check_streq(f.header.company().c_str(), "Temco", "the header is read from there");
        check_eq(f.header.version(), 6000, "... all of it");
        check_eq(f.image[0], 0x11, "the byte at 0000 is not made 255 in this reader");
        check_eq((long)f.data_size, 0x8200 + 20, "the data is the highest address written, not less one");
        if (require(f.sections.size() == 2, "a section is marked at the second linear address record, and at the end"))
        {
            check_eq((long)f.sections[0], 1, "... the first where the data had reached");
            check_eq((long)f.sections[1], 0x8200 + 20, "... the last at the end");
        }

        // 0x0801 and up: a 64K ARM chip, header at 0x10200; the high address
        // less 0x800.
        check(read("m.hex", arm_file(0x0801, 0x0801, 0x0200), Path::Serial, f, why), "a 64K ARM file is read");
        check(f.chip == Chip::Arm64K, "0x0801 first is a 64K ARM chip");
        check_eq((long)f.header_at, 0x10200, "... whose header is at 0x10200");
        check_eq(f.header.version(), 6000, "the high address 0x0801 is 0x10000 once 0x800 is taken off");
        if (require(f.sections.size() == 3, "a 64K file marks a section at every linear address record, and the end"))
        {
            check_eq((long)f.sections[0], 0x10000, "... the first 0x10000 when nothing is written yet");
            check_eq((long)f.sections[1], 0x10001, "... which the count starts from, so the next is past it");
            check_eq((long)f.sections[2], 0x10200 + 20, "... and the end");
        }

        // Below 0x0800: the ASIX chip's header at 0x100; a high address below
        // 0x800 is taken as it is.
        check(read("m.hex", arm_file(0x0001, 0x0000, 0x0100), Path::Serial, f, why), "a linear file for no ARM chip is read");
        check(f.chip == Chip::Asix, "below 0x0800 first is not an ARM chip");
        check_eq((long)f.header_at, 0x100, "... and its header is at 0x100");
        check_eq(f.header.version(), 6000, "... read from there");
        check_eq(f.image[0x10000], 0x11, "a high address below 0x800 is not lowered");

        // A record past the buffer.
        const std::string past = record(4, 0, { 0x08, 0x00 }) + record(4, 0, { 0x00, 0x1F }) +
                                 record(0, 0xFFF0, std::vector<uint8_t>(16, 1)) + kEnd;
        check(!read("p.hex", past, Path::Serial, f, why), "a record running past ISP's buffer is refused");
        check(contains(why, "past the 2097151 bytes"), "... saying where the buffer ends");
        check(read("p.hex", past, Path::Network, f, why), "... but not on the network, whose buffer is larger");

        const std::string at_end = record(4, 0, { 0x08, 0x00 }) + record(4, 0, { 0x00, 0x1F }) +
                                   record(0, 0xFFEF, std::vector<uint8_t>(16, 1)) + kEnd;
        check(read("e.hex", at_end, Path::Serial, f, why), "a record ending at the buffer's last byte is read");
    }

    void test_what_is_not_a_hex_file()
    {
        section("firmware: what is not a .hex file ISP reads");

        FirmwareFile f;
        std::string why;
        const std::string good = asix_file();

        check(!read("a.hex", good.substr(0, good.size() - kEnd.size()), Path::Serial, f, why),
              "a file without its end-of-file record is refused");
        check(contains(why, "cut short"), "... as cut short");

        std::string cut = good;
        cut.pop_back();
        check(!read("a.hex", cut, Path::Serial, f, why), "a last line without its LF is refused");
        check(contains(why, "CR LF"), "... saying so");

        std::string lf = good;
        lf.erase(lf.find("\r\n"), 1);
        check(!read("a.hex", lf, Path::Serial, f, why), "a line ended by LF alone is refused");

        check(!read("a.hex", "x" + good.substr(1), Path::Serial, f, why), "a line not starting with ':' is refused");
        check(contains(why, "line 1"), "... naming the line");

        std::string odd = good;
        odd.insert(3, "0");
        check(!read("a.hex", odd, Path::Serial, f, why), "a line with an odd number of digits is refused");

        std::string letter = good;
        letter[3] = 'G';
        check(!read("a.hex", letter, Path::Serial, f, why), "a character that is not a hex digit is refused");

        std::string sum = good;
        sum[sum.find("\r\n") - 1] = sum[sum.find("\r\n") - 1] == '0' ? '1' : '0';
        check(!read("a.hex", sum, Path::Serial, f, why), "a checksum that does not add up is refused");
        check(contains(why, "checksum"), "... saying so");

        const std::string wrong_count = ":03000000112277\r\n" + kEnd;
        check(!read("a.hex", wrong_count, Path::Serial, f, why), "a line holding fewer bytes than its count is refused");

        std::string long_line = ":" + std::string(254, '0') + "\r\n" + kEnd;
        check(!read("a.hex", long_line, Path::Serial, f, why), "a 255-character line gets past the length check");
        check(!contains(why, "255"), "... and is refused for something else");
        long_line = ":" + std::string(255, '0') + "\r\n" + kEnd;
        check(!read("a.hex", long_line, Path::Serial, f, why), "a 256-character line is refused");
        check(contains(why, "longer than 255"), "... as too long");

        check(!read("a.hex", kEnd, Path::Serial, f, why), "a file of only its end-of-file record is refused");
        check(contains(why, "no data"), "... as holding no data");

        check(!read("a.txt", good, Path::Serial, f, why), "a file named other than .hex or .bin is refused");
        check(!read("ahex", good, Path::Serial, f, why), "... a name ending hex with no dot too");
    }

    // A .bin file: its first bytes, and a header at `at`.
    std::vector<uint8_t> bin_file(const char* start, size_t length, size_t at, const char* company)
    {
        std::vector<uint8_t> b(length, 0x5A);
        memcpy(b.data(), start, strlen(start));
        const std::vector<uint8_t> h = header(company, "MINI_ARM", 6000);
        memcpy(&b[at], h.data(), h.size());
        return b;
    }

    bool read_bin(const std::vector<uint8_t>& b, Path path, FirmwareFile& f, std::string& why)
    {
        return read_firmware("m.bin", b.data(), b.size(), path, f, why);
    }

    void test_a_bin_file()
    {
        section("firmware: a .bin file");

        FirmwareFile f;
        std::string why;

        const std::vector<uint8_t> b = bin_file("ASIX", 0x400, 0x100, "Temco");
        if (!require(read_bin(b, Path::Serial, f, why), "a .bin starting ASIX is read"))
        {
            printf("        %s\n", why.c_str());
            return;
        }
        check(f.kind == FileKind::Bin, "it is a .bin");
        check_eq((long)f.header_at, 0x100, "its header is at 0x100 when the company there is TEMCO");
        check_eq(f.header.version(), 6000, "... and read from there");
        check_eq((long)f.data_size, 0x400, "the data is the file's length");
        check_eq((long)f.image.size(), (long)kBinBufferLength, "a .bin is read into 0x9FFFFF bytes");
        check_eq(f.image[0x3FF], 0x5A, "... the file copied in");
        check_eq(f.image[0x400], 0xFF, "... filled with 0xFF past it");

        check(!read_bin(b, Path::Controller, f, why), "the same .bin through a controller is refused");
        check(contains(why, "controller"), "... for going through one");
        check(read_bin(b, Path::Network, f, why), "the same .bin on the network");
        check_eq(f.image[0x400], 0xFF, "... is filled with 0xFF too");

        check(read_bin(bin_file("asix", 0x400, 0x100, "TEMCO"), Path::Serial, f, why), "ASIX in any case is read");
        check(read_bin(bin_file("ASIX", 0x400, 0x100, "co2"), Path::Serial, f, why), "a company of CO2 at 0x100");
        check_eq((long)f.header_at, 0x100, "... puts the header there");
        check(read_bin(bin_file("ASIX", 0x400, 0x100, "CO2X"), Path::Serial, f, why), "any other company at 0x100");
        check_eq((long)f.header_at, 0x200, "... puts the header at 0x200");
        check(read_bin(bin_file("ASIX", 0x400, 0x200, "TEMCO"), Path::Serial, f, why), "so a header at 0x200");
        check_eq((long)f.header_at, 0x200, "... is found there");
        check_streq(f.header.company().c_str(), "TEMCO", "... and read");

        std::vector<uint8_t> temco = bin_file("XXXX", 0x400, 0x100, "TEMCO");
        memcpy(&temco[512 + 3], "Temco", 5);
        check(read_bin(temco, Path::Serial, f, why), "a .bin saying Temco at bytes 512 to 531 is read");

        std::vector<uint8_t> late = bin_file("XXXX", 0x400, 0x100, "TEMCO");
        memcpy(&late[512 + 16], "Temco", 5);
        check(!read_bin(late, Path::Serial, f, why), "one saying it past byte 531 is refused");

        std::vector<uint8_t> after_zero = bin_file("XXXX", 0x400, 0x100, "TEMCO");
        after_zero[512] = 0;
        memcpy(&after_zero[512 + 3], "Temco", 5);
        check(!read_bin(after_zero, Path::Serial, f, why), "one saying it after a 0 is refused");

        std::vector<uint8_t> upper = bin_file("XXXX", 0x400, 0x100, "TEMCO");
        memcpy(&upper[512], "TEMCO", 5);
        check(!read_bin(upper, Path::Serial, f, why), "TEMCO in capitals at 512 is not Temco");
        check(contains(why, "neither starts with ASIX nor says Temco"), "... saying why");

        check(!read_bin(std::vector<uint8_t>(), Path::Serial, f, why), "an empty .bin is refused");
        check(!read_bin(bin_file("ASIX", kBinBufferLength + 1, 0x100, "TEMCO"), Path::Serial, f, why),
              "a .bin longer than ISP's buffer is refused");
        check(read_bin(bin_file("ASIX", kBinBufferLength, 0x100, "TEMCO"), Path::Serial, f, why),
              "one as long as the buffer is read");
        check(read_bin(bin_file("ASIX", 0x3FFFFF + 1, 0x100, "TEMCO"), Path::Serial, f, why),
              "one longer than ISP's buffer before 6.4.7, 0x3FFFFF, is read on serial");
        check(read_bin(bin_file("ASIX", 0x3FFFFF + 1, 0x100, "TEMCO"), Path::Network, f, why),
              "  and on the network");
        check(!read_bin(bin_file("ASIX", kBinBufferLength + 1, 0x100, "TEMCO"), Path::Network, f, why),
              "  where one longer than the buffer is refused too");

        const std::vector<uint8_t> small = { 'A', 'S', 'I', 'X' };
        check(read_bin(small, Path::Serial, f, why), "a .bin shorter than its header's place is read");
        check_eq((long)f.header_at, 0x200, "... its header taken from the fill at 0x200");
        check_eq(f.header.bytes[0], 0xFF, "... which is 0xFF");

        check(read_firmware("M.BIN", b.data(), b.size(), Path::Serial, f, why), "the suffix in capitals is read");
    }
}

int run_firmware_file_tests()
{
    test_the_header();
    test_a_hex_file_by_address();
    test_a_hex_file_by_linear_address();
    test_what_is_not_a_hex_file();
    test_a_bin_file();
    return 0;
}
