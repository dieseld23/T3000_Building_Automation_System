#include "firmware_file.h"

#include <ctype.h>
#include <string.h>

#include <algorithm>

namespace t5000::firmware
{
    const char* to_string(Path path)
    {
        switch (path)
        {
        case Path::Serial:     return "serial";
        case Path::Network:    return "network";
        case Path::Controller: return "through a controller";
        }
        return "unknown";
    }

    std::string Header::company() const
    {
        const char* p = reinterpret_cast<const char*>(&bytes[header_at::company]);
        return std::string(p, strnlen(p, 5));
    }

    std::string Header::product_name(bool& ends) const
    {
        // strlen from the name's first byte (ComWriter.cpp:2037, MySocket.cpp:136).
        std::string name;
        for (size_t i = header_at::product_name; i < header_at::size; i++)
        {
            if (bytes[i] == 0)
            {
                ends = true;
                return name;
            }
            name += (char)bytes[i];
        }
        ends = false;
        return name;
    }

    namespace
    {
        bool ends_with(const std::string& name, const char* suffix)
        {
            const size_t n = strlen(suffix);
            if (name.size() < n)
                return false;
            for (size_t i = 0; i < n; i++)
            {
                const char a = name[name.size() - n + i];
                if (tolower((unsigned char)a) != tolower((unsigned char)suffix[i]))
                    return false;
            }
            return true;
        }

        // turn_hex_char_to_int (ISP\global_function.cpp:122-152).
        int hex_digit(uint8_t c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        // One record, its bytes after the colon: count, address, type, the
        // data, and the checksum.
        struct Record
        {
            std::vector<uint8_t> bytes;
            int    line = 0;   // counting from 1, as a person would
            size_t count() const { return bytes[0]; }
            unsigned address() const { return bytes[1] * 256u + bytes[2]; }
            int type() const { return bytes[3]; }
            const uint8_t* data() const { return &bytes[4]; }
        };

        std::string at_line(int line)
        {
            return "at line " + std::to_string(line);
        }

        // Splits a .hex file into records, as ReadLineFromFile does
        // (HexFileParser.cpp:284-320): each line ends at a CR, and the byte
        // after it (the LF) is part of the line.
        //
        // Stricter, each where ISP would read a file no Temco tool writes:
        //   - a line of more than 255 characters: ISP says "The Hex File is
        //     broken" and then flashes what it read before it;
        //   - a line not ended by CR LF, or text after the last one: ISP
        //     drops it without a word;
        //   - a line that does not start with ':' (ISP drops its first
        //     character whatever it is), holds a character that is not a hex
        //     digit (ISP carries on with the character's own value), or
        //     whose length is not its count's: ISP writes `count` bytes from
        //     wherever they fall;
        //   - no end-of-file record (type 01): ISP reads to the end of the
        //     file, so a file cut short is flashed as far as it goes;
        //   - a checksum that does not add up on a line ISP does not check
        //     (the linear address records, :457-478).
        bool records_of(const uint8_t* data, size_t length, std::vector<Record>& records, std::string& why)
        {
            size_t at   = 0;
            int    line = 0;
            bool   ended = false;
            while (at < length)
            {
                line++;
                const size_t start = at;
                while (at < length && data[at] != 0x0d)
                {
                    if (at - start >= kLongestHexLine)
                    {
                        why = "The file is not a .hex file ISP reads: line " + std::to_string(line) +
                              " is longer than 255 characters, which ISP calls broken.";
                        return false;
                    }
                    at++;
                }
                if (at + 1 >= length || data[at + 1] != 0x0a)
                {
                    why = "The file is not a .hex file ISP reads: line " + std::to_string(line) +
                          " does not end with CR LF, and ISP would leave it out.";
                    return false;
                }
                const uint8_t* p = data + start;
                const size_t   n = at - start;
                at += 2;

                if (n == 0 || p[0] != ':')
                {
                    why = "The file is not a .hex file: line " + std::to_string(line) + " does not start with ':'.";
                    return false;
                }
                if ((n - 1) % 2 != 0 || n - 1 < 10)
                {
                    why = "The file is not a .hex file: line " + std::to_string(line) +
                          " has an odd number of digits or is too short for a record.";
                    return false;
                }
                Record r;
                r.line = line;
                for (size_t i = 1; i < n; i += 2)
                {
                    const int hi = hex_digit(p[i]);
                    const int lo = hex_digit(p[i + 1]);
                    if (hi < 0 || lo < 0)
                    {
                        why = "The file is not a .hex file: line " + std::to_string(line) +
                              " holds a character that is not a hex digit.";
                        return false;
                    }
                    r.bytes.push_back((uint8_t)(hi * 16 + lo));
                }
                if (r.bytes.size() != r.count() + 5)
                {
                    why = "The file is not a .hex file: line " + std::to_string(line) + " says it holds " +
                          std::to_string(r.count()) + " bytes and holds " + std::to_string(r.bytes.size() - 5) + ".";
                    return false;
                }
                uint8_t sum = 0;
                for (const uint8_t b : r.bytes)
                    sum = (uint8_t)(sum + b);
                if (sum != 0)
                {
                    why = "The file is damaged: the checksum " + at_line(line) + " does not add up.";
                    return false;
                }
                records.push_back(r);
                if (r.type() == 1)
                {
                    // ISP stops at the end-of-file record (:181, :376, :512)
                    // and reads nothing after it.
                    ended = true;
                    break;
                }
            }
            if (!ended)
            {
                why = "The file has no end-of-file record, so it may have been cut short.";
                return false;
            }
            return true;
        }

        // ReadNormalHexFile and ReadExtendHexFile, which are the same
        // (HexFileParser.cpp:134-219, 330-414): every record but the
        // end-of-file one is data at its 16-bit address, whatever its type,
        // a segment address record's two bytes among them. A record at
        // address 0000 has its first byte made 255 (:197-198). The count is
        // the highest address written, less one (:207-210).
        void read_by_address(const std::vector<Record>& records, FirmwareFile& file)
        {
            size_t count = 0;
            for (const Record& r : records)
            {
                if (r.type() == 1)
                    break;
                std::vector<uint8_t> bytes(r.data(), r.data() + r.count());
                if (r.address() == 0 && !bytes.empty())
                    bytes[0] = 255;
                for (size_t j = 0; j < bytes.size(); j++)
                    file.image[r.address() + j] = bytes[j];
                if (count < r.address() + r.count())
                    count = r.address() + r.count() - 1;
            }
            file.data_size = count;
        }

        // The chip, from the first line's first two data bytes, whatever
        // the line is (GetFileTypeFromLine, :237-260).
        Chip chip_of(const Record& first)
        {
            const unsigned v = first.bytes.size() > 5 ? first.bytes[4] * 256u + first.bytes[5] : 0;
            if (v == 0x0800)
                return Chip::Arm32K;
            if (v >= 0x0801)
                return Chip::Arm64K;
            return Chip::Asix;
        }

        // ReadExtLinearHexFile (:425-587). A linear address record (type 04,
        // told by its type's second digit, :457) sets the high address, less
        // 0x800 when it is 0x800 or more (GetHighAddrFromFile, :262-281),
        // and marks a section; a start address record (05) is skipped;
        // everything else is data at the high address plus its own. An
        // address past the buffer ends the read with nothing (:566-569).
        bool read_by_linear_address(const std::vector<Record>& records, FirmwareFile& file, std::string& why)
        {
            file.chip = records.empty() ? Chip::Asix : chip_of(records.front());
            size_t count = 0;
            size_t high  = 0;
            for (const Record& r : records)
            {
                const int second_digit = r.type() % 16;
                if (second_digit == 4)
                {
                    unsigned v = r.bytes.size() > 5 ? r.bytes[4] * 256u + r.bytes[5] : 0;
                    v &= 0xFFFF;
                    if (v >= 0x0800)
                        v -= 0x800;
                    high = (size_t)v << 16;
                    if (file.chip == Chip::Arm64K)
                    {
                        if (count == 0)
                            count = 0x10000;
                        file.sections.push_back(count);
                    }
                    else if (count != 0)
                    {
                        file.sections.push_back(count);
                    }
                    continue;
                }
                if (second_digit == 5)
                    continue;
                if (r.type() == 1)
                    break;

                const size_t address = r.address() + high;
                // Stricter: ISP refuses an address past its buffer, but
                // writes a record that starts inside it and runs past the
                // end.
                if (address + r.count() > file.image.size())
                {
                    why = "The file writes past the " + std::to_string(file.image.size()) + " bytes ISP reads a .hex into, " +
                          at_line(r.line) + ".";
                    return false;
                }
                for (size_t j = 0; j < r.count(); j++)
                    file.image[address + j] = r.data()[j];
                if (count < address + r.count())
                    count = address + r.count();
            }
            file.sections.push_back(count);
            file.data_size = count;
            return true;
        }

        bool read_hex(const uint8_t* data, size_t length, FirmwareFile& file, std::string& why)
        {
            // GetHexFileType reads the file's first 12 bytes and looks at the
            // ninth, the second digit of the first record's type (:104-124).
            const uint8_t ninth = length > 8 ? data[8] : 0;
            file.format = ninth == '2' ? HexFormat::SegmentAddress : ninth == '4' ? HexFormat::LinearAddress : HexFormat::Data;

            std::vector<Record> records;
            if (!records_of(data, length, records, why))
                return false;

            if (file.format == HexFormat::LinearAddress)
            {
                if (!read_by_linear_address(records, file, why))
                    return false;
            }
            else
            {
                read_by_address(records, file);
            }

            // Where the header is (:58-69).
            file.header_at = file.chip == Chip::Arm32K ? 0x8200 : file.chip == Chip::Arm64K ? 0x10200 : 0x100;
            memcpy(file.header.bytes, &file.image[file.header_at], header_at::size);

            if (file.data_size == 0)
            {
                why = "The file holds no data.";
                return false;
            }
            return true;
        }

        // CBinFileParser::GetBinFileBuffer (BinFileParser.cpp:27-135): the
        // file is copied whole. Its first 1024 bytes must start "ASIX", or
        // hold "Temco" in their bytes 512 to 531 before the first 0; the
        // header is at 0x100 if its company is TEMCO or CO2, and otherwise at
        // 0x200, whatever that holds, since the loop never refuses
        // (:96-134).
        bool read_bin(const uint8_t* data, size_t length, FirmwareFile& file, std::string& why)
        {
            // Stricter: ISP copies a file of any length into its buffer.
            if (length > file.image.size())
            {
                why = "The file is " + std::to_string(length) + " bytes, more than the " + std::to_string(file.image.size()) +
                      " ISP reads a .bin into.";
                return false;
            }
            if (length == 0)
            {
                why = "The file is empty.";
                return false;
            }

            uint8_t first[1024] = {};
            memcpy(first, data, std::min(length, sizeof first));
            bool asix = true;
            const char* word = "ASIX";
            for (int i = 0; i < 4; i++)
                asix = asix && toupper(first[i]) == word[i];
            if (!asix)
            {
                std::string identify;
                for (size_t i = 512; i < 532 && first[i] != 0; i++)
                    identify += (char)first[i];
                if (identify.find("Temco") == std::string::npos)
                {
                    why = "The file is not a .bin ISP reads: it neither starts with ASIX nor says Temco at byte 512.";
                    return false;
                }
            }

            memcpy(file.image.data(), data, length);
            file.data_size = length;

            file.header_at = 0x200;
            for (const size_t at : { (size_t)0x100, (size_t)0x200 })
            {
                Header h;
                memcpy(h.bytes, &file.image[at], header_at::size);
                std::string company = h.company();
                for (char& c : company)
                    c = (char)toupper((unsigned char)c);
                if (company == "TEMCO" || company == "CO2")
                {
                    file.header_at = at;
                    break;
                }
            }
            memcpy(file.header.bytes, &file.image[file.header_at], header_at::size);
            return true;
        }
    }

    bool read_firmware(const std::string& file_name, const uint8_t* data, size_t length, Path path,
                       FirmwareFile& file, std::string& why)
    {
        file = FirmwareFile();
        file.path = path;

        // Which reader, by the name (ISPDlg.cpp:2491-2494 on serial, 2214 and
        // 2226 on the network, a .hex always through a controller). Stricter:
        // on serial ISP reads any name not ending ".BIN" as a .hex, and on
        // the network it looks only at the last three letters.
        const bool bin = ends_with(file_name, ".bin");
        const bool hex = ends_with(file_name, ".hex");
        if (!bin && !hex)
        {
            why = "A firmware file is a .hex or a .bin.";
            return false;
        }
        if (bin && path == Path::Controller)
        {
            why = "ISP sends a .hex only to a device behind a controller.";
            return false;
        }
        file.kind = bin ? FileKind::Bin : FileKind::Hex;

        // The buffer and its fill (:2110, :2219, :2234, :2504, :2522).
        size_t  buffer = kHexBufferLength;
        uint8_t fill   = 0x00;
        if (file.kind == FileKind::Bin)
        {
            buffer = kBinBufferLength;
            fill   = 0xFF;
        }
        else if (path == Path::Network)
        {
            buffer = kBinBufferLength;
            fill   = 0x00;
        }
        else if (path == Path::Controller)
        {
            fill = 0xFF;
        }
        file.image.assign(buffer, fill);

        return file.kind == FileKind::Bin ? read_bin(data, length, file, why) : read_hex(data, length, file, why);
    }
}
