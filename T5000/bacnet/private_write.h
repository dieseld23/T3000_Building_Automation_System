#pragma once

// Temco's private-data write, as bytes: encoding the request and classifying
// the reply, with no I/O. Nothing here sends anything. The only thing that
// could send one is a write transport, which does not exist yet;
// conformance/write_separation_guard.cpp fails the build if anything outside
// bacnet/ includes this header.
//
// T3000 writes through WritePrivateData (T3000/global_function.cpp:1636), which
// fills Str_user_data_header with total_length = 7 + n * entitysize
// (:1786-1790), copies each point's raw struct after it (:1859-1871 for
// inputs), and hands the buffer to the same DLL path a read takes
// (:2161-2172). So a write is a read request with a different command and the
// points appended. One input, index 2, invoke id II:
//
//   81 0A 00 48          BVLC: original-unicast, 72 bytes. 0A always: bip.c
//                              sends 0B only for a destination with no MAC
//   01 04                NPDU: version 1, expecting a reply, never routed
//   00 05 II 12          APDU: confirmed request, max APDU 1476, invoke id,
//                              ConfirmedPrivateTransfer
//   0A 01 04             [0] vendorID 260
//   19 01                [1] serviceNumber 1
//   2E                   [2] open
//   65 35                    octet string, 53 bytes
//   35 00 66 02 02 2E 00       the Temco header: total_length 53 (LE),
//                              command 102, points 2-2, entitysize 46 (LE)
//   <46 bytes>                 the Str_in_point
//   2F                   [2] close
//
// conformance/private_transfer_oracle.cpp builds the same writes through
// T3000's own stack and compares them byte for byte.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "write_command.h"

namespace t5000::bacnet
{
    // A write of points [first, last], inclusive at both ends, as a read's
    // range is. `entities` is count() points of entity_size(command) bytes
    // each: the raw ud_str.h structs, in point order.
    struct WriteRequest
    {
        WriteCommand         command = WriteCommand::Inputs;
        uint8_t              first   = 0;
        uint8_t              last    = 0;
        std::vector<uint8_t> entities;

        int count() const { return (int)last - (int)first + 1; }
    };

    // The largest Temco payload - header and points - that T3000 can send.
    // It encodes the octet string into uint8_t test_value[480]
    // (global_function.cpp:1644), and the DLL checks only against MAX_APDU,
    // 1476 (bacdcode.c:909), so anything longer overruns that buffer. The
    // long length form takes 4 bytes of the 480. Ten inputs (467 bytes) fit;
    // eleven (513) do not.
    inline constexpr size_t kMaxWritePayload = 476;

    // Writes the request datagram. Returns its length, or 0 with nothing
    // written when the range is backwards, `entities` is not count() points
    // of the command's size, the payload is over kMaxWritePayload, or the
    // buffer is too small.
    //
    // Takes a WriteRequest, whose command is a WriteCommand: only the writes
    // write_command.h admits can be encoded.
    size_t encode_write_request(const WriteRequest& request, uint8_t invoke_id,
                                uint8_t* out, size_t capacity);

    // The octet string's tag and length as encode_tag writes them
    // (bacdcode.c:238-250): 65 LL for 5 to 253 bytes, 65 FE HH LL from 254 to
    // 65535. A write's payload is never under 7 bytes, so the short forms for
    // 0-4 are not needed. Returns the bytes written, 2 or 4, or 0 for a length
    // outside 5-65535.
    namespace detail
    {
        size_t put_octet_string_header(size_t length, uint8_t* out);
    }

    // ------------------------------------------------------------------ reply

    // What a panel can plausibly send back to a write. Which of these a Temco
    // panel does send is not known: its firmware is not in the repository, and
    // T3000 treats every one of them alike. Its only test is whether the
    // invoke id was freed in time, and the DLL frees it for a ComplexACK
    // whatever it holds, and for Error, Reject and Abort (apdu.c:563-642) - so
    // T3000 reports a refused write as written. A SimpleACK for a private
    // transfer does not free it (apdu.c:500-535), so T3000 would report a
    // timeout.
    //
    // Here the reply is recorded and never decides: whether a write took is
    // decided by reading the point back.
    enum class WriteAck
    {
        ComplexAck,           // 30 II 12 [0] [1] [2]{ ... } - body kept
        ComplexAckNoResult,   // 30 II 12 [0] [1], and no [2] block
        SimpleAck,            // 20 II 12
        Error,
        Reject,
        Abort,
        Segmented,            // a segmented ComplexACK, which was not allowed
    };

    const char* to_string(WriteAck kind);

    struct WriteReply
    {
        WriteAck kind      = WriteAck::Abort;
        uint8_t  invoke_id = 0;

        // ComplexAck: the vendor id and service number the panel echoed.
        uint32_t vendor_id      = 0;
        uint32_t service_number = 0;

        // ComplexAck: what the [2] block held - the octet string's contents
        // when it held one octet string, which is how a read's answer comes,
        // or its raw bytes when it held anything else. Empty for an empty [2].
        std::vector<uint8_t> body;
        bool body_is_octet_string = false;

        // Error.
        bool     has_error_codes = false;
        uint32_t error_class = 0;
        uint32_t error_code  = 0;

        // Reject, Abort.
        uint8_t reason = 0;
        bool    abort_from_server = false;
    };

    // Classifies one received UDP payload as a reply to a private-transfer
    // write. False, with the reason in why_not, for anything that is not a
    // reply to a confirmed request for service 18: another service's ACK or
    // Error, an I-Am, a frame cut short, a ComplexACK whose [2] block does not
    // close. Those are other traffic, not answers. Matching the invoke id and
    // the source is the caller's job, as for reads.
    //
    // Deliberately broader than decode_reply, which a read keeps: a read needs
    // the answer's contents, and a write needs only to know that one came.
    bool decode_write_reply(const uint8_t* datagram, size_t length, WriteReply& out,
                            std::string& why_not);
}
