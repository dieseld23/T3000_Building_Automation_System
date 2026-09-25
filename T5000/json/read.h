#pragma once

// Reading values out of a flat JSON object.
//
// Not a JSON parser, and deliberately not on the way to becoming one. Every
// request body this tool receives is one level deep with string and number
// values, and a real parser would be the largest dependency in the project
// for no gain.
//
// What the read_* functions do NOT handle, stated so a caller can decide
// whether that is acceptable rather than discovering it: nesting, arrays,
// escape sequences inside strings, and duplicate keys (the first wins). A
// value containing a backslash or an embedded quote will be read wrong. That
// is fine for handles, addresses and port numbers, and not fine for anything
// that has been through a text box. Text someone typed - a device's name, a
// room - is read with parse_flat_object instead, which handles escapes and
// only ever takes a key for a key.
//
// This lives in its own leaf module because there used to be one copy inside
// device/connection.cpp and a second was about to be written for the scan
// routes. Two hand-rolled parsers with the same blind spots and separate bug
// fixes is worse than one.

#include <map>
#include <string>

namespace t5000::json
{
    // A flat object read properly, for bodies carrying text someone typed.
    //
    // The functions below find a key by searching for its quoted name. That
    // is fine for handles and addresses and wrong for free text: in
    // {"name":"floor","building":"B","floor":"3"} a search for "floor" finds
    // the NAME, and reads "B". parse_flat_object reads the object from the
    // start, so a key is only ever a key, and decodes every escape in a
    // string, \uXXXX and surrogate pairs included, to UTF-8.
    //
    // Values that are not strings are kept as their raw text. Nesting,
    // arrays and duplicate keys are refused rather than guessed at.
    struct FlatValue
    {
        bool        is_string = false;
        std::string text;   // decoded when is_string; the raw token otherwise
    };

    bool parse_flat_object(const std::string& text, std::map<std::string, FlatValue>& out,
                           std::string& error);

    // A non-negative whole number, all of `text` and nothing else. "-1" is
    // refused rather than wrapped round to 2^64 - 1.
    bool parse_u64(const std::string& text, unsigned long long& out);

    // The raw text of `key`, unquoted if it was a string. False when the key
    // is absent or the object is malformed.
    bool find_value(const std::string& object, const std::string& key, std::string& out);

    // Reads `key` into `target` if present and numeric, leaving it alone
    // otherwise. Returns false only when the key was present and could not be
    // read as a number - an absent key is not an error, because these are
    // patch-style bodies where an omitted field means "leave it".
    bool read_int(const std::string& object, const char* key, int& target);

    // As read_int, for the 64-bit keys that handles use.
    bool read_u64(const std::string& object, const char* key, unsigned long long& target);

    bool read_string(const std::string& object, const char* key, std::string& target);

    // Reads "true"/"false". Anything else leaves target alone and returns false.
    bool read_bool(const std::string& object, const char* key, bool& target);
}
