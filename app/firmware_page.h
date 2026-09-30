#pragma once
// The Firmware page's checks, F1b of docs/t5000-firmware-plan.md: which of
// ISP's paths a firmware file for each device would go on, what T5000 knows
// of the device for the check, and what ISP - and T5000 - would make of a
// file for it.
//
// Nothing here sends anything, and nothing here can: it names no socket,
// serial port or file, and includes no transport
// (write_separation_guard.cpp, S7). A file reaches it as the bytes the page
// sent, and firmware/ reads it. The one request the page can send, for a
// panel's settings, is app/firmware_read.h's.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "../device/registry.h"
#include "../firmware/firmware_check.h"
#include "../firmware/firmware_file.h"

namespace t5000::app
{
    // The largest request that may carry a firmware file (http::body_limit).
    // ISP's largest buffer is 0x3FFFFF bytes. It copies a .bin of any length
    // into it, past its end (BinFileParser.cpp:82-83), where T5000 refuses a
    // .bin longer than the buffer (firmware/firmware_file.cpp). Of a
    // .hex of linear address records, it refuses a record whose address is
    // more than the buffer's length and writes any other whole, past the
    // end when it runs over (HexFileParser.cpp:566-569); T5000 refuses
    // both. A .hex of
    // sixteen-byte records that
    // fills it is about 11.5 MB of text. 16 MiB takes that and the largest
    // .bin. A larger request is refused without being read, and the page
    // says so before it sends one.
    inline constexpr size_t kLargestFirmwareRequest = 16u * 1024 * 1024;

    // Which of ISP's paths a file for this device would go on, as T3000
    // hands a device to ISP (Dowmloadfile.cpp, COM_OR_NET and Subnote;
    // ISPDlg.cpp:1124-1147): through its controller when it is on one's
    // RS485 bus, by its serial port when it is reached over one, and on the
    // network when it has an address there. False for a virtual device,
    // which has none, and for a device with no address: T3000 hands one
    // with no IP address to ISP as on a serial port (Dowmloadfile.cpp:234),
    // and it has no port either.
    bool firmware_path(const device::DeviceRecord& d, firmware::Path& path);

    // Why a device has no path, in a sentence, for the page.
    std::string no_path_text(const device::DeviceRecord& d);

    // The path as the page names it: "network", "serial", "controller".
    const char* path_key(firmware::Path path);

    // ... and in a sentence.
    std::string path_text(firmware::Path path);

    // What the check knows of a device: its product, and its bootloader's
    // version when settings read this session gave it
    // (Registry::note_bootloader).
    //
    // Not its firmware's low byte, which spares a CO2, humidity, pressure or
    // PM2.5 device on 59 or later from ISP's bootloader rule. ISP reads
    // register 4 from the device as it flashes, and the source does not
    // settle that the byte a scan gives is that register, so the rule is
    // not spared here: the refusal is the safe way round. F3 reads the
    // register, as ISP does.
    firmware::DeviceFacts device_facts(const device::DeviceRecord& d);

    // Whether the device has answered a scan, or Find, since T5000 started.
    bool reached_this_session(const device::DeviceRecord& d);

    // T5000's own reasons not to send a firmware file to this device,
    // whatever the file: the ones docs/t5000-firmware-plan.md lists under
    // "T5000's own checks before anything is sent" that can be decided now.
    // A virtual device is never flashed; a device is flashed only once it
    // has answered a scan or Find this session, and only once a scan this
    // session has reported its product (DeviceRecord::product_reported),
    // which Find does not read. Empty
    // when there are none. Kept apart from the file's check,
    // firmware::Verdict, whose reasons are ISP's and T5000's stricter ones.
    std::vector<std::string> own_refusals(const device::DeviceRecord& d);

    // Whether a device is in its bootloader, as the page shows it:
    // "bootloader", "firmware", or "unknown", with a sentence.
    const char* bootloader_state_key(const device::DeviceRecord& d);
    std::string bootloader_state_text(const device::DeviceRecord& d);

    // One device as the Firmware page lists it. `can_read` and `read_why`
    // say whether the page may read its settings, for its bootloader's
    // version (app/firmware_read.h decides, with the points pages' plan).
    std::string firmware_device_json(const device::DeviceRecord& d, bool can_read, const std::string& read_why);

    // The query of POST /api/firmware/check: handle=<n>&name=<the file's
    // name>, the name percent-encoded as encodeURIComponent writes it. The
    // file is the request's body.
    struct FirmwareCheckRequest
    {
        device::Handle handle = device::kNoHandle;
        std::string    name;
    };

    // Strict: %XX, in either case, for any byte, and nothing else changed. A
    // '+' stays a '+': encodeURIComponent writes a space as %20. False for a
    // '%' without two hex digits after it.
    bool percent_decode(const std::string& text, std::string& out);

    // Exactly the two, once each. The name is what the page's file picker
    // gave: not empty, at most 255 bytes, and no control characters.
    bool read_firmware_check_request(const std::string& query, FirmwareCheckRequest& request,
                                     std::string& message);

    // POST /api/firmware/check: the file, read as ISP reads it on the
    // device's path and checked against the device, and T5000's own
    // reasons. {"ok":true} only when ISP would take the file and T5000
    // would send it; nothing is sent either way. A device gone from the
    // list is said so.
    std::string check_firmware_json(const device::Registry& registry, const FirmwareCheckRequest& request,
                                    const std::string& file);
}
