#include "private_transfer.h"

namespace t5000::bacnet
{
    namespace
    {
        // BVLC, Annex J. bip.c:195-216 on the way out; bip.c:304-376 on the way
        // in, which accepts the three function codes below.
        constexpr uint8_t kBvlcType             = 0x81;   // BVLL_TYPE_BACNET_IP
        constexpr uint8_t kBvlcForwardedNpdu    = 0x04;
        constexpr uint8_t kBvlcOriginalUnicast  = 0x0A;
        constexpr uint8_t kBvlcOriginalBroadcast = 0x0B;
        constexpr size_t  kBvlcHeader           = 4;
        constexpr size_t  kBvlcForwardedHeader  = 10;     // + the original source's IP and port

        // NPDU. npdu.c:177 produces 0x04 for a local destination with a reply
        // expected, and nothing else - no DNET, no SNET, normal priority.
        constexpr uint8_t kNpduVersion         = 0x01;
        constexpr uint8_t kNpduExpectingReply  = 0x04;
        constexpr uint8_t kNpduNetworkMessage  = 0x80;
        constexpr uint8_t kNpduHasDestination  = 0x20;
        constexpr uint8_t kNpduHasSource       = 0x08;

        // APDU. The high nibble is the PDU type (bacenum.h:1043-1050).
        constexpr uint8_t kPduConfirmedRequest = 0x00;
        constexpr uint8_t kPduComplexAck       = 0x30;
        constexpr uint8_t kPduError            = 0x50;
        constexpr uint8_t kPduReject           = 0x60;
        constexpr uint8_t kPduAbort            = 0x70;
        constexpr uint8_t kSegmentedMessage    = 0x08;
        constexpr uint8_t kAbortFromServer     = 0x01;

        // encode_max_segs_max_apdu(0, 1476), bacdcode.c:135: unspecified
        // segment count, 1476-octet APDUs. The SA bit in the first APDU octet
        // is left clear (ptransfer.c:90), so the device must not segment its
        // answer - and does not need to, since ten inputs is 467 bytes.
        constexpr uint8_t kMaxSegsMaxApdu = 0x05;

        constexpr uint8_t kOpeningTag2 = 0x2E;
        constexpr uint8_t kClosingTag2 = 0x2F;
        constexpr uint8_t kOpeningTag0 = 0x0E;
        constexpr uint8_t kClosingTag0 = 0x0F;

        constexpr uint8_t kAppTagOctetString = 6;   // bacenum.h:1027
        constexpr uint8_t kAppTagEnumerated  = 9;

        uint32_t be(const uint8_t* p, size_t n)
        {
            uint32_t v = 0;
            for (size_t i = 0; i < n; i++)
                v = (v << 8) | p[i];
            return v;
        }

        // A bounded reader. Every decode step goes through here so that a
        // length taken from the wire can never walk past what arrived.
        //
        // The DLL's own decode_tag_number_and_value has no bounds check at all
        // (bacdcode.c:370), and there is a safe variant beside it that the
        // reply path does not use. The input here is whatever the network
        // hands us.
        struct Cursor
        {
            const uint8_t* p;
            size_t         left;

            bool take(size_t n, const uint8_t*& out)
            {
                if (n > left)
                    return false;
                out   = p;
                p    += n;
                left -= n;
                return true;
            }

            bool byte(uint8_t& out)
            {
                const uint8_t* b;
                if (!take(1, b))
                    return false;
                out = *b;
                return true;
            }

            bool peek(uint8_t& out) const
            {
                if (left == 0)
                    return false;
                out = *p;
                return true;
            }
        };

        // A context-tagged unsigned: tag byte (number << 4 | 0x08 | length),
        // then 1-4 big-endian octets. encode_context_unsigned, bacint.c:48.
        bool read_context_unsigned(Cursor& c, uint8_t tag_number, uint32_t& out)
        {
            uint8_t tag;
            if (!c.byte(tag))
                return false;
            const uint8_t length = tag & 0x07;
            if ((tag >> 4) != tag_number || (tag & 0x08) == 0 || length < 1 || length > 4)
                return false;
            const uint8_t* v;
            if (!c.take(length, v))
                return false;
            out = be(v, length);
            return true;
        }

        // An application-tagged value's length, including the extended forms:
        // 5 means "the length follows", as one octet up to 253, or 254 and two
        // octets, or 255 and four. encode_tag, bacdcode.c:238-250.
        bool read_application_length(Cursor& c, uint8_t expected_tag, uint32_t& length)
        {
            uint8_t tag;
            if (!c.byte(tag))
                return false;
            if ((tag >> 4) != expected_tag || (tag & 0x08) != 0)
                return false;

            const uint8_t lvt = tag & 0x07;
            if (lvt <= 4)
            {
                length = lvt;
                return true;
            }
            if (lvt != 5)
                return false;

            uint8_t first;
            if (!c.byte(first))
                return false;
            if (first <= 253)
            {
                length = first;
                return true;
            }
            const size_t width = (first == 254) ? 2 : 4;
            const uint8_t* v;
            if (!c.take(width, v))
                return false;
            length = be(v, width);
            return true;
        }

        bool read_enumerated(Cursor& c, uint32_t& out)
        {
            uint32_t length;
            if (!read_application_length(c, kAppTagEnumerated, length) || length < 1 || length > 4)
                return false;
            const uint8_t* v;
            if (!c.take(length, v))
                return false;
            out = be(v, length);
            return true;
        }

        // The NPDU in front of a reply. A device on the local network sends
        // control 0x00, but one behind a router adds a source network and
        // address, and that is still a reply we asked for.
        bool skip_npdu(Cursor& c, std::string& why_not)
        {
            uint8_t version, control;
            if (!c.byte(version) || !c.byte(control))
            {
                why_not = "truncated before the NPDU header ended";
                return false;
            }
            if (version != kNpduVersion)
            {
                why_not = "NPDU version " + std::to_string(version) + ", expected 1";
                return false;
            }
            if (control & kNpduNetworkMessage)
            {
                why_not = "a network-layer message, not an application reply";
                return false;
            }

            const uint8_t* skipped;
            if (control & kNpduHasDestination)
            {
                // DNET(2) DLEN(1) DADR(DLEN); the hop count comes after SADR.
                const uint8_t* dnet;
                uint8_t dlen;
                if (!c.take(2, dnet) || !c.byte(dlen) || !c.take(dlen, skipped))
                {
                    why_not = "truncated inside the NPDU destination";
                    return false;
                }
            }
            if (control & kNpduHasSource)
            {
                const uint8_t* snet;
                uint8_t slen;
                if (!c.take(2, snet) || !c.byte(slen) || !c.take(slen, skipped))
                {
                    why_not = "truncated inside the NPDU source";
                    return false;
                }
            }
            if (control & kNpduHasDestination)
            {
                uint8_t hop_count;
                if (!c.byte(hop_count))
                {
                    why_not = "truncated at the NPDU hop count";
                    return false;
                }
            }
            return true;
        }

        bool decode_complex_ack(Cursor& c, uint8_t first, Reply& out, std::string& why_not)
        {
            if (first & kSegmentedMessage)
            {
                // Recognised so it can be named. The request said segmentation
                // is not accepted, so a device sending this has ignored that.
                uint8_t invoke = 0;
                if (!c.byte(invoke))
                {
                    why_not = "truncated segmented ComplexACK";
                    return false;
                }
                out.kind      = ReplyKind::SegmentedAck;
                out.invoke_id = invoke;
                return true;
            }

            uint8_t invoke, service;
            if (!c.byte(invoke) || !c.byte(service))
            {
                why_not = "truncated ComplexACK header";
                return false;
            }
            out.kind           = ReplyKind::ComplexAck;
            out.invoke_id      = invoke;
            out.service_choice = service;

            if (service != kServicePrivateTransfer)
                return true;    // someone's reply, not ours to unpack

            // ptransfer_decode_service_request, ptransfer.c:125-166. Unlike
            // that function, the closing tag is checked rather than assumed -
            // it takes the last byte to be the closing tag without looking
            // (ptransfer.c:151).
            if (!read_context_unsigned(c, 0, out.vendor_id))
            {
                why_not = "ComplexACK without a readable [0] vendorID";
                return false;
            }
            if (!read_context_unsigned(c, 1, out.service_number))
            {
                why_not = "ComplexACK without a readable [1] serviceNumber";
                return false;
            }
            uint8_t tag;
            if (!c.byte(tag) || tag != kOpeningTag2)
            {
                why_not = "ComplexACK without the [2] opening tag";
                return false;
            }

            // Bacnet_PrivateData_Handle requires an application octet string
            // here and returns 0 for anything else (global_function.cpp:3363).
            uint32_t length;
            if (!read_application_length(c, kAppTagOctetString, length))
            {
                why_not = "resultBlock is not an application octet string";
                return false;
            }
            const uint8_t* data;
            if (!c.take(length, data))
            {
                why_not = "octet string claims " + std::to_string(length) +
                          " bytes but only " + std::to_string(c.left) + " arrived";
                return false;
            }
            if (!c.byte(tag) || tag != kClosingTag2)
            {
                why_not = "no [2] closing tag after the octet string";
                return false;
            }
            if (c.left != 0)
            {
                why_not = std::to_string(c.left) + " unexpected bytes after the [2] closing tag";
                return false;
            }

            out.payload.assign(data, data + length);
            return true;
        }

        bool decode_error(Cursor& c, Reply& out, std::string& why_not)
        {
            uint8_t invoke, service;
            if (!c.byte(invoke) || !c.byte(service))
            {
                why_not = "truncated Error header";
                return false;
            }
            out.kind           = ReplyKind::Error;
            out.invoke_id      = invoke;
            out.service_choice = service;

            // For a private transfer the class and code sit inside [0]
            // (ptransfer_error_decode_service_request, ptransfer.c:237); for
            // every other service they are bare. Accept both - what matters is
            // telling the technician which error it was.
            uint8_t next;
            if (c.peek(next) && next == kOpeningTag0)
                c.byte(next);

            uint32_t error_class, error_code;
            if (read_enumerated(c, error_class) && read_enumerated(c, error_code))
            {
                out.has_error_codes = true;
                out.error_class     = error_class;
                out.error_code      = error_code;
            }
            return true;
        }
    }

    const char* to_string(ReplyKind kind)
    {
        switch (kind)
        {
        case ReplyKind::ComplexAck:   return "answer";
        case ReplyKind::Error:        return "error";
        case ReplyKind::Reject:       return "reject";
        case ReplyKind::Abort:        return "abort";
        case ReplyKind::SegmentedAck: return "segmented answer";
        }
        return "unknown";
    }

    size_t encode_read_request(const ReadRequest& request, uint8_t invoke_id,
                               uint8_t* out, size_t capacity)
    {
        if (out == nullptr || capacity < kReadRequestLength || request.first > request.last)
            return 0;

        uint8_t* p = out;

        // BVLC. The length is of the whole datagram, BVLC included
        // (bip.c:216: pdu_len + 4).
        *p++ = kBvlcType;
        *p++ = kBvlcOriginalUnicast;
        *p++ = (uint8_t)(kReadRequestLength >> 8);
        *p++ = (uint8_t)(kReadRequestLength & 0xFF);

        // NPDU.
        *p++ = kNpduVersion;
        *p++ = kNpduExpectingReply;

        // APDU header, ptransfer.c:90-93.
        *p++ = kPduConfirmedRequest;
        *p++ = kMaxSegsMaxApdu;
        *p++ = invoke_id;
        *p++ = kServicePrivateTransfer;

        // [0] vendorID: 260 needs two octets.
        *p++ = 0x0A;
        *p++ = (uint8_t)(kVendorId >> 8);
        *p++ = (uint8_t)(kVendorId & 0xFF);

        // [1] serviceNumber.
        *p++ = 0x19;
        *p++ = kServiceNumber;

        *p++ = kOpeningTag2;

        // The payload is an application octet string of exactly
        // kTemcoHeaderLength bytes, so its length takes the extended form
        // (65 07) - 7 does not fit in the tag's three length bits.
        *p++ = (uint8_t)((kAppTagOctetString << 4) | 5);
        *p++ = (uint8_t)kTemcoHeaderLength;

        // Str_user_data_header, ud_str.h:315-323: packed, and copied into the
        // octet string as raw memory (global_function.cpp:2713 hands the
        // struct's address to a parser that Set_transfer_length has made copy
        // bytes verbatim - bacstr.c:744-749, the hex parser above it being
        // #if 0). So its two 16-bit fields go out in the sender's byte order,
        // which on every machine T3000 runs on is little-endian. Written out
        // explicitly here rather than inherited.
        //
        // total_length is the header's own length: a read request carries no
        // data (global_function.cpp:2707).
        *p++ = (uint8_t)(kTemcoHeaderLength & 0xFF);
        *p++ = (uint8_t)(kTemcoHeaderLength >> 8);
        *p++ = to_wire(request.command);
        *p++ = request.first;
        *p++ = request.last;
        *p++ = (uint8_t)(request.entity_size & 0xFF);
        *p++ = (uint8_t)(request.entity_size >> 8);

        *p++ = kClosingTag2;

        return (size_t)(p - out);
    }

    bool decode_reply(const uint8_t* datagram, size_t length, Reply& out,
                      std::string& why_not)
    {
        out = Reply();

        if (datagram == nullptr || length < kBvlcHeader)
        {
            why_not = "shorter than a BVLC header";
            return false;
        }
        if (datagram[0] != kBvlcType)
        {
            why_not = "not BACnet/IP (BVLC type " + std::to_string(datagram[0]) + ")";
            return false;
        }

        const uint8_t function = datagram[1];
        size_t header;
        if (function == kBvlcOriginalUnicast || function == kBvlcOriginalBroadcast)
            header = kBvlcHeader;
        else if (function == kBvlcForwardedNpdu)
            header = kBvlcForwardedHeader;
        else
        {
            why_not = "BVLC function " + std::to_string(function) + " carries no NPDU";
            return false;
        }

        // The BVLC length is authoritative. Longer than what arrived means the
        // datagram was cut short; shorter means trailing bytes that are not
        // part of the frame, which are ignored rather than decoded.
        const size_t stated = be(datagram + 2, 2);
        if (stated > length)
        {
            why_not = "BVLC says " + std::to_string(stated) + " bytes, " +
                      std::to_string(length) + " arrived";
            return false;
        }
        if (stated < header)
        {
            why_not = "BVLC length " + std::to_string(stated) + " is shorter than its own header";
            return false;
        }

        Cursor c{ datagram + header, stated - header };
        if (!skip_npdu(c, why_not))
            return false;

        uint8_t first;
        if (!c.byte(first))
        {
            why_not = "no APDU";
            return false;
        }

        switch (first & 0xF0)
        {
        case kPduComplexAck:
            return decode_complex_ack(c, first, out, why_not);

        case kPduError:
            return decode_error(c, out, why_not);

        case kPduReject:
        {
            uint8_t invoke, reason;
            if (!c.byte(invoke) || !c.byte(reason))
            {
                why_not = "truncated Reject";
                return false;
            }
            out.kind      = ReplyKind::Reject;
            out.invoke_id = invoke;
            out.reason    = reason;
            return true;
        }

        case kPduAbort:
        {
            uint8_t invoke, reason;
            if (!c.byte(invoke) || !c.byte(reason))
            {
                why_not = "truncated Abort";
                return false;
            }
            out.kind              = ReplyKind::Abort;
            out.invoke_id         = invoke;
            out.reason            = reason;
            out.abort_from_server = (first & kAbortFromServer) != 0;
            return true;
        }

        default:
            why_not = "APDU type " + std::to_string(first >> 4) + " is not a reply";
            return false;
        }
    }

    bool check_read_payload(const ReadRequest& request,
                            const std::vector<uint8_t>& payload,
                            const uint8_t*& entities, std::string& why_not)
    {
        entities = nullptr;

        if (payload.size() < kTemcoHeaderLength)
        {
            why_not = "the reply is " + std::to_string(payload.size()) +
                      " bytes, shorter than the 7-byte header";
            return false;
        }

        // Header offsets as Bacnet_PrivateData_Handle reads them: the command
        // at 2 (global_function.cpp:3378), start and end at 3 and 4 (:4169-4172),
        // and the points from 7 (:4174). Bytes 0-1 and 5-6 are skipped there,
        // so they are not checked here either - enforcing a field T3000 has
        // never looked at would reject devices T3000 reads fine.
        const uint8_t command = payload[2];
        const uint8_t first   = payload[3];
        const uint8_t last    = payload[4];

        if (command != to_wire(request.command))
        {
            why_not = "the reply is for command " + std::to_string(command) +
                      ", not the " + std::to_string(to_wire(request.command)) + " that was sent";
            return false;
        }
        if (first != request.first || last != request.last)
        {
            why_not = "the reply is for points " + std::to_string(first) + "-" +
                      std::to_string(last) + ", not the " + std::to_string(request.first) +
                      "-" + std::to_string(request.last) + " that were asked for";
            return false;
        }

        const size_t body     = payload.size() - kTemcoHeaderLength;
        const size_t expected = (size_t)request.count() * request.entity_size;
        if (request.entity_size == 0 || body != expected)
        {
            why_not = "the reply carries " + std::to_string(body) + " bytes of point data; " +
                      std::to_string(request.count()) + " points of " +
                      std::to_string(request.entity_size) + " bytes is " +
                      std::to_string(expected);
            return false;
        }

        entities = payload.data() + kTemcoHeaderLength;
        return true;
    }
}
