#pragma once

// Includes T3000/CM5/ud_str.h - the live wire format - into a translation unit
// that is not MFC.
//
// FOR COMPILE-TIME GUARDS ONLY. Include this from a file whose job is to
// static_assert that something T5000 defines still agrees with the header the
// shipping application uses. Nothing that runs should depend on it.
//
// The header does not compile on its own:
//
//   - `byte` is used for several schedule fields and is only defined once the
//     Windows headers are in scope. Defining it here is what lets the real
//     header be included without dragging in windows.h and MFC.
//   - Point_T3000 contains a `public:` access specifier, so the header has to be
//     compiled as C++ regardless.
//
// Both are shallow, and fixing them upstream would mean editing a GBK-encoded
// header that scripted edits have corrupted in this repo before. Shimming is
// the lower-risk option.
//
// The shims exist ONLY so the header parses. None of them appears in anything
// a guard asserts about - the point structs and the command codes - so getting
// a shim's size wrong cannot make a wrong layout pass; it would fail the
// offsetof checks instead. A translation unit including this must never
// include real MFC.
//
// Shared rather than repeated in each guard, so there is one copy of the shims
// to keep right. Two guards were about to need them.

typedef unsigned char  byte;    // ud_str.h:419-422, 445-446, 456, 497-498
typedef unsigned short WORD;    // ud_str.h:499, normally from windef.h

// ud_str.h:1224 puts an MFC CString inside register_point - a struct whose own
// comment says "(size = 6 bytes)". It is not a wire struct despite reading like
// one, and nothing here depends on it; the placeholder just lets the file parse.
struct CString { void* opaque; };

#include "../../T3000/CM5/ud_str.h"
