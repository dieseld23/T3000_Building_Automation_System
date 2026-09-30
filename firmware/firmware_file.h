#pragma once

// A firmware file, read as T3000's ISP reads one before it flashes a device
// (ISP\HexFileParser.cpp, ISP\BinFileParser.cpp, and the callers in
// ISP\ISPDlg.cpp that pick the reader, the buffer and its fill), so that the
// Firmware page can say what ISP would make of it, and later send exactly the
// image ISP would.
//
// Nothing here opens a file on disk, or anything else: it reads bytes the
// page was given. Nothing in firmware/ can send (the separation guard holds
// it to that).
//
// ISP reads a file differently on each path it can send it on:
//
//   serial (FlashByCom, ISPDlg.cpp:2472-2523)   a name ending ".BIN" is read
//       as a .bin into 0x3FFFFF bytes of 0xFF; anything else as a .hex into
//       0x1FFFFF bytes of 0x00
//   network (FlashByEthernet, :2185-2237)       ".bin" into 0x3FFFFF bytes
//       of 0xFF; ".hex" into 0x3FFFFF bytes of 0x00
//   through a controller (OnFlashSubID, :2090-2112)   a .hex only, into
//       0x1FFFFF bytes of 0xFF
//
// The bytes past the file's data are what the device is sent after it, so
// each is kept as ISP fills it.
//
// Where T5000 is stricter than ISP, it refuses rather than guess at what ISP
// would do with a file no Temco tool writes; each case is marked "Stricter"
// in firmware_file.cpp.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace t5000::firmware
{
    // How ISP would reach the device (Judge_Flash_Type, ISPDlg.cpp:1124-1147):
    // on a serial port by Modbus RTU; on the network, directly, by TFTP; or
    // through the controller it sits behind, by Modbus TCP.
    enum class Path
    {
        Serial,
        Network,
        Controller,
    };

    const char* to_string(Path path);

    enum class FileKind
    {
        Hex,
        Bin,
    };

    // What GetHexFileType makes of a .hex file's first line, from the
    // second digit of its record type (HexFileParser.cpp:104-124), and so
    // which of ISP's three readers reads it (:37-55).
    enum class HexFormat
    {
        Data          = 0,   // HEXFILE_DATA: ReadNormalHexFile
        SegmentAddress = 1,  // HEXFILE_SECADDR: ReadExtendHexFile, the same
        LinearAddress = 2,   // HEXFILE_LINERADDR: ReadExtLinearHexFile
    };

    // m_file_type (HexFileParser.h:80-82): which chip a file with linear
    // address records is for, from its first line (GetFileTypeFromLine,
    // :237-260). It decides where the file's header is.
    enum class Chip
    {
        Asix  = 0,   // or any file not read by linear address records
        Arm32K = 1,
        Arm64K = 2,
    };

    // ISP's buffers (Global_Struct.h:58, 60).
    inline constexpr size_t kHexBufferLength = 0x1FFFFF;
    inline constexpr size_t kBinBufferLength = 0x3FFFFF;

    // ReadLineFromFile's limit (HexFileParser.cpp:306-315): the 256th
    // character of a line that is not its CR is "The Hex File is broken".
    inline constexpr size_t kLongestHexLine = 255;

    // Bin_Info (Global_Struct.h:600-607): 20 bytes, no padding.
    namespace header_at
    {
        inline constexpr size_t company       = 0;    // char[5]
        inline constexpr size_t product_name  = 5;    // char[10]
        inline constexpr size_t software_low  = 15;
        inline constexpr size_t software_high = 16;
        inline constexpr size_t reserved      = 17;   // char[3]
        inline constexpr size_t size          = 20;
    }

    struct Header
    {
        uint8_t bytes[header_at::size] = {};

        // The company as ISP compares it: its 5 bytes up to the first 0
        // (HexFileParser.cpp:73-75).
        std::string company() const;

        // The product name as ISP reads it, with strlen: up to the first 0.
        // A name that fills its 10 bytes runs on into the version bytes and
        // the reserved ones; one with no 0 in the header at all runs past
        // it, into memory ISP does not own, and `ends` is false.
        std::string product_name(bool& ends) const;

        uint8_t software_low() const { return bytes[header_at::software_low]; }
        uint8_t software_high() const { return bytes[header_at::software_high]; }

        // software_high * 256 + software_low, which ISP divides by 100 for
        // some products and not others (ISPDlg.cpp:2254, 2604, 2617).
        int version() const { return software_high() * 256 + software_low(); }
    };

    struct FirmwareFile
    {
        FileKind  kind   = FileKind::Hex;
        HexFormat format = HexFormat::Data;
        Chip      chip   = Chip::Asix;
        Path      path   = Path::Serial;

        // Where the header was taken from, and what it holds.
        size_t header_at = 0;
        Header header;

        // ISP's buffer: its length for the path, filled as the path fills
        // it, with the file's data written in.
        std::vector<uint8_t> image;

        // What ISP counts as the data, its nDataSize: the highest address
        // written, less one for the Data and SegmentAddress readers
        // (HexFileParser.cpp:207-210, 402-405) and not for LinearAddress
        // (:572-573); a .bin's length.
        size_t data_size = 0;

        // ReadExtLinearHexFile's section marks (m_szFlags, :462-474, 579),
        // which ComWriter sends by. Empty for other files.
        std::vector<size_t> sections;
    };

    // Reads `data`, a file named `file_name`, as ISP would on `path`. False,
    // with `why` in a sentence, if ISP would refuse it, or T5000 does; the
    // file's product, company and version are checked in firmware_check.h.
    bool read_firmware(const std::string& file_name, const uint8_t* data, size_t length, Path path,
                       FirmwareFile& file, std::string& why);
}
