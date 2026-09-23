#pragma once

// Temco's private-data read, as bytes: encoding the request and decoding the
// reply, with no I/O.
//
// T3000 does this through BACnet_Stack_Library.dll - GetPrivateData
// (T3000/global_function.cpp:2659) builds the request and
// Send_ConfirmedPrivateTransfer (BacNetDllforVc/Handler/s_upt.c:85) sends it -
// and decodes the reply in its own handler, Bacnet_PrivateData_Handle
// (global_function.cpp:3342). That handler is T3000 code, not DLL code, so
// the decode here is a port whichever way the sending was done.
//
// The request is encoded here rather than through the DLL because the DLL's
// path runs on process-wide state: a socket, a local address and port, an
// invoke-id table, and a transfer length set by one call and read by the next.
// None of it is needed for a request that is 26 bytes long. What IS needed is
// certainty that these 26 bytes are the ones T3000 sends, so the self-test
// hands the DLL a loopback socket, captures what Send_ConfirmedPrivateTransfer
// actually emits, and compares it byte for byte with encode_read_request.
// That is a check against T3000's own encoder, not against a reading of it.
//
// What goes out, for inputs 0-9 with invoke id II:
//
//   81 0A 00 1A          BVLC: BACnet/IP, original-unicast, 26 bytes
//   01 04                NPDU: version 1, expecting a reply
//   00 05 II 12          APDU: confirmed request, no segmentation accepted,
//                              max APDU 1476, invoke id, ConfirmedPrivateTransfer
//   0A 01 04             [0] vendorID 260
//   19 01                [1] serviceNumber 1
//   2E                   [2] open
//   65 07                    octet string, 7 bytes:
//   07 00 02 00 09 2E 00       the Temco header - see TemcoHeader below
//   2F                   [2] close

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "command.h"

namespace t5000::bacnet
{
    // BACNET_VENDOR_ID as T3000 is built: BacNetDllforVc/include/config.h:71.
    //
    // NOT 148, which is Temco's registered BACnet vendor id and the number
    // anyone would reach for. What matters on the wire is what the shipping
    // tool sends, and that is 260 - every private-data request T3000 has ever
    // made carries it. Nothing in T3000's reply handler checks the vendor id a
    // device answers with, so the decoder records it and does not enforce it.
    inline constexpr uint16_t kVendorId = 260;

    // global_function.cpp:2696, hard-coded. The command byte inside the payload
    // says what is being read; this number never changes.
    inline constexpr uint8_t kServiceNumber = 1;

    // PRIVATE_HEAD_LENGTH, ud_str.h:10.
    inline constexpr size_t kTemcoHeaderLength = 7;

    // The whole request datagram. Fixed, because the request carries only a
    // header and never any point data.
    inline constexpr size_t kReadRequestLength = 26;

    // What BACnet calls the service choice for ConfirmedPrivateTransfer.
    inline constexpr uint8_t kServicePrivateTransfer = 18;

    // A read of entities [first, last], inclusive at both ends - T3000's
    // convention, and the one its reply handler loops over
    // (global_function.cpp:4202: `for (i = start; i <= end; i++)`).
    struct ReadRequest
    {
        ReadCommand command     = ReadCommand::Inputs;
        uint8_t     first       = 0;
        uint8_t     last        = 0;
        uint16_t    entity_size = 0;    // bytes per point on the wire

        int count() const { return (int)last - (int)first + 1; }
    };

    // Writes the request datagram. Returns kReadRequestLength, or 0 if the
    // buffer is too small or the range is backwards.
    //
    // Takes a ReadRequest, whose command is a ReadCommand: there is no way to
    // ask this function for a write.
    size_t encode_read_request(const ReadRequest& request, uint8_t invoke_id,
                               uint8_t* out, size_t capacity);

    // ------------------------------------------------------------------ reply

    enum class ReplyKind
    {
        ComplexAck,     // the answer; payload holds the Temco block
        Error,          // the device refused, with a class and code
        Reject,         // the device could not parse the request
        Abort,          // the transaction was abandoned, by either side
        SegmentedAck,   // an answer we said we could not accept
    };

    const char* to_string(ReplyKind kind);

    struct Reply
    {
        ReplyKind kind      = ReplyKind::Abort;
        uint8_t   invoke_id = 0;

        // ComplexAck, Error.
        uint8_t   service_choice = 0;

        // ComplexAck. The vendor id and service number the device echoed, kept
        // for diagnosis - T3000 checks neither.
        uint32_t  vendor_id      = 0;
        uint32_t  service_number = 0;

        // ComplexAck: the octet string's contents - the Temco header followed
        // by the entities, exactly as Bacnet_PrivateData_Handle sees them.
        std::vector<uint8_t> payload;

        // Error. has_error_codes is false when the PDU said "error" but its
        // class and code could not be read; the kind is still reported.
        bool     has_error_codes = false;
        uint32_t error_class = 0;
        uint32_t error_code  = 0;

        // Reject, Abort.
        uint8_t  reason = 0;
        bool     abort_from_server = false;
    };

    // Decodes one received UDP payload as a reply to a confirmed request.
    //
    // Returns false for anything that is not one - another device's broadcast,
    // an I-Am, a truncated frame - with the reason in why_not. Those are
    // expected on a BACnet port and are not errors in themselves; the caller
    // decides what to do with them. A true return says only that this IS a
    // reply to SOME confirmed request; matching the invoke id and source is
    // the caller's job.
    bool decode_reply(const uint8_t* datagram, size_t length, Reply& out,
                      std::string& why_not);

    // Checks that a ComplexAck payload answers exactly the request that was
    // sent: the same command, the same range, and precisely the number of
    // entities that range holds.
    //
    // Stricter than T3000, deliberately. Bacnet_PrivateData_Handle checks only
    // that the length is a whole number of entities
    // (global_function.cpp:4165), then loops from the start to the end index
    // THE REPLY states - so a reply claiming ten points while carrying three
    // is read past its end, and a reply for points 10-19 lands on 10-19 even
    // if 0-9 was asked for. Either is a plausible-looking wrong value on a
    // live controller's screen.
    //
    // On success, `entities` points into `payload` at the first entity.
    bool check_read_payload(const ReadRequest& request,
                            const std::vector<uint8_t>& payload,
                            const uint8_t*& entities, std::string& why_not);
}
