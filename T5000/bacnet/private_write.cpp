#include "private_write.h"

#include "private_transfer.h"
#include "tag_reader.h"

namespace t5000::bacnet
{
    namespace
    {
        // The framing a read request has (private_transfer.cpp), written again
        // here rather than shared, so that the read encoder's file stays the
        // only one that writes a read and this the only one that writes a
        // write. The oracle holds both to T3000's stack.
        constexpr uint8_t kBvlcType            = 0x81;
        constexpr uint8_t kBvlcOriginalUnicast = 0x0A;
        constexpr uint8_t kNpduVersion         = 0x01;
        constexpr uint8_t kNpduExpectingReply  = 0x04;
        constexpr uint8_t kPduConfirmedRequest = 0x00;
        constexpr uint8_t kMaxSegsMaxApdu      = 0x05;
        constexpr uint8_t kOpeningTag2         = 0x2E;
        constexpr uint8_t kClosingTag2         = 0x2F;

        constexpr uint8_t kPduSimpleAck     = 0x20;
        constexpr uint8_t kPduComplexAck    = 0x30;
        constexpr uint8_t kPduError         = 0x50;
        constexpr uint8_t kPduReject        = 0x60;
        constexpr uint8_t kPduAbort         = 0x70;
        constexpr uint8_t kSegmentedMessage = 0x08;

        // BVLC 4, NPDU 2, APDU header 4, [0] vendorID 3, [1] serviceNumber 2,
        // [2] open 1: where the octet string's tag starts.
        constexpr size_t kBeforeOctetString = 16;
    }

    size_t detail::put_octet_string_header(size_t length, uint8_t* out)
    {
        if (length < 5 || length > 65535)
            return 0;

        out[0] = (uint8_t)((tags::kAppTagOctetString << 4) | 5);
        if (length <= 253)
        {
            out[1] = (uint8_t)length;
            return 2;
        }
        out[1] = 254;
        out[2] = (uint8_t)(length >> 8);
        out[3] = (uint8_t)(length & 0xFF);
        return 4;
    }

    size_t encode_write_request(const WriteRequest& request, uint8_t invoke_id,
                                uint8_t* out, size_t capacity)
    {
        const size_t size = entity_size(request.command);
        if (out == nullptr || size == 0 || request.first > request.last ||
            request.entities.size() != (size_t)request.count() * size)
            return 0;

        const size_t payload = kTemcoHeaderLength + request.entities.size();
        if (payload > kMaxWritePayload)
            return 0;

        uint8_t tag[4];
        const size_t tag_length = detail::put_octet_string_header(payload, tag);
        const size_t total      = kBeforeOctetString + tag_length + payload + 1;
        if (tag_length == 0 || capacity < total)
            return 0;

        uint8_t* p = out;

        // BVLC, whole datagram. Unicast always: bip.c:197-202 broadcasts only
        // when the destination has no MAC, and this has one address to go to.
        *p++ = kBvlcType;
        *p++ = kBvlcOriginalUnicast;
        *p++ = (uint8_t)(total >> 8);
        *p++ = (uint8_t)(total & 0xFF);

        // NPDU: local, reply expected. No DNET, so it is never routed.
        *p++ = kNpduVersion;
        *p++ = kNpduExpectingReply;

        // APDU header, ptransfer.c:90-93.
        *p++ = kPduConfirmedRequest;
        *p++ = kMaxSegsMaxApdu;
        *p++ = invoke_id;
        *p++ = kServicePrivateTransfer;

        // [0] vendorID 260, [1] serviceNumber 1 (global_function.cpp:1652-1653).
        *p++ = 0x0A;
        *p++ = (uint8_t)(kVendorId >> 8);
        *p++ = (uint8_t)(kVendorId & 0xFF);
        *p++ = 0x19;
        *p++ = kServiceNumber;

        *p++ = kOpeningTag2;

        for (size_t i = 0; i < tag_length; i++)
            *p++ = tag[i];

        // Str_user_data_header as global_function.cpp:1786-1792 fills it and
        // copies it: packed, raw memory, so little-endian. total_length counts
        // the header and every point after it.
        *p++ = (uint8_t)(payload & 0xFF);
        *p++ = (uint8_t)(payload >> 8);
        *p++ = to_wire(request.command);
        *p++ = request.first;
        *p++ = request.last;
        *p++ = (uint8_t)(size & 0xFF);
        *p++ = (uint8_t)(size >> 8);

        // The points, as the caller gives them: T3000 copies each raw struct
        // after the header (:1859-1871).
        for (uint8_t b : request.entities)
            *p++ = b;

        *p++ = kClosingTag2;

        return (size_t)(p - out);
    }

    const char* to_string(WriteAck kind)
    {
        switch (kind)
        {
        case WriteAck::ComplexAck:         return "complexAck";
        case WriteAck::ComplexAckNoResult: return "complexAckNoResult";
        case WriteAck::SimpleAck:          return "simpleAck";
        case WriteAck::Error:              return "error";
        case WriteAck::Reject:             return "reject";
        case WriteAck::Abort:              return "abort";
        case WriteAck::Segmented:          return "segmented";
        }
        return "unknown";
    }

    bool decode_write_reply(const uint8_t* datagram, size_t length, WriteReply& out,
                            std::string& why_not)
    {
        out = WriteReply();

        size_t apdu_at = 0, apdu_end = 0;
        if (!locate_apdu(datagram, length, apdu_at, apdu_end, why_not))
            return false;

        tags::Cursor c{ datagram + apdu_at, apdu_end - apdu_at };
        uint8_t first;
        if (!c.byte(first))
        {
            why_not = "no APDU";
            return false;
        }

        switch (first & 0xF0)
        {
        case kPduSimpleAck:
        {
            uint8_t invoke, service;
            if (!c.byte(invoke) || !c.byte(service))
            {
                why_not = "truncated SimpleACK";
                return false;
            }
            if (service != kServicePrivateTransfer)
            {
                why_not = "a SimpleACK for service " + std::to_string(service) + ", not a private transfer";
                return false;
            }
            out.kind      = WriteAck::SimpleAck;
            out.invoke_id = invoke;
            return true;
        }

        case kPduComplexAck:
        {
            if (first & kSegmentedMessage)
            {
                uint8_t invoke;
                if (!c.byte(invoke))
                {
                    why_not = "truncated segmented ComplexACK";
                    return false;
                }
                out.kind      = WriteAck::Segmented;
                out.invoke_id = invoke;
                return true;
            }

            uint8_t invoke, service;
            if (!c.byte(invoke) || !c.byte(service))
            {
                why_not = "truncated ComplexACK header";
                return false;
            }
            if (service != kServicePrivateTransfer)
            {
                why_not = "a ComplexACK for service " + std::to_string(service) + ", not a private transfer";
                return false;
            }
            out.invoke_id = invoke;

            if (!tags::read_context_unsigned(c, 0, out.vendor_id))
            {
                why_not = "ComplexACK without a readable [0] vendorID";
                return false;
            }
            if (!tags::read_context_unsigned(c, 1, out.service_number))
            {
                why_not = "ComplexACK without a readable [1] serviceNumber";
                return false;
            }

            // The result block is optional in a private-transfer ACK.
            if (c.left == 0)
            {
                out.kind = WriteAck::ComplexAckNoResult;
                return true;
            }

            uint8_t tag;
            if (!c.byte(tag) || tag != kOpeningTag2)
            {
                why_not = "after [1] serviceNumber, neither the end nor a [2] block";
                return false;
            }
            if (c.left == 0 || c.p[c.left - 1] != kClosingTag2)
            {
                why_not = "the [2] block does not close with the frame";
                return false;
            }

            // Inside the block: one octet string that fills it, as a read's
            // answer has; anything else is kept as it came.
            tags::Cursor inner{ c.p, c.left - 1 };
            tags::Cursor probe = inner;
            uint32_t octets = 0;
            const uint8_t* data = nullptr;
            if (inner.left > 0 && tags::read_application_length(probe, tags::kAppTagOctetString, octets) &&
                probe.left == octets && probe.take(octets, data))
            {
                out.body.assign(data, data + octets);
                out.body_is_octet_string = true;
            }
            else
            {
                out.body.assign(inner.p, inner.p + inner.left);
            }
            out.kind = WriteAck::ComplexAck;
            return true;
        }

        case kPduError:
        case kPduReject:
        case kPduAbort:
        {
            // The read decoder already reads these exactly; a write's are the
            // same PDUs.
            Reply reply;
            if (!decode_reply(datagram, length, reply, why_not))
                return false;

            out.invoke_id = reply.invoke_id;
            if (reply.kind == ReplyKind::Error)
            {
                if (reply.service_choice != kServicePrivateTransfer)
                {
                    why_not = "an Error for service " + std::to_string(reply.service_choice) +
                              ", not a private transfer";
                    return false;
                }
                out.kind            = WriteAck::Error;
                out.has_error_codes = reply.has_error_codes;
                out.error_class     = reply.error_class;
                out.error_code      = reply.error_code;
            }
            else if (reply.kind == ReplyKind::Reject)
            {
                out.kind   = WriteAck::Reject;
                out.reason = reply.reason;
            }
            else
            {
                out.kind              = WriteAck::Abort;
                out.reason            = reply.reason;
                out.abort_from_server = reply.abort_from_server;
            }
            return true;
        }

        default:
            why_not = "APDU type " + std::to_string(first >> 4) + " is not a reply";
            return false;
        }
    }
}
