#pragma once

// Reading BACnet tags from bytes that came off the network, with every length
// checked against what arrived. Shared by the read decoder
// (private_transfer.cpp) and the write-reply decoder (private_write.cpp), so
// there is one copy of the bounds checks.
//
// The DLL's own decode_tag_number_and_value has no bounds check at all
// (bacdcode.c:370), and there is a safe variant beside it that the reply path
// does not use. The input here is whatever the network hands us.

#include <stddef.h>
#include <stdint.h>

namespace t5000::bacnet::tags
{
    constexpr uint8_t kAppTagOctetString = 6;   // bacenum.h:1027
    constexpr uint8_t kAppTagEnumerated  = 9;

    inline uint32_t be(const uint8_t* p, size_t n)
    {
        uint32_t v = 0;
        for (size_t i = 0; i < n; i++)
            v = (v << 8) | p[i];
        return v;
    }

    // A bounded reader. Every decode step goes through here so that a
    // length taken from the wire can never walk past what arrived.
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
    inline bool read_context_unsigned(Cursor& c, uint8_t tag_number, uint32_t& out)
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
    inline bool read_application_length(Cursor& c, uint8_t expected_tag, uint32_t& length)
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

    inline bool read_enumerated(Cursor& c, uint32_t& out)
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
}
