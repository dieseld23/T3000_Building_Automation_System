#pragma once

// The set of devices this tool knows about, and the repairs it would like to
// make to them but will not make on its own.
//
// Two ideas live here, and the second is the important one.
//
// 1. A DEVICE RECORD. Every screen needs a selected device before it can read
//    anything, so this is what the rest of the tool is blocked on. A record
//    carries both product axes (see product.h), how the device was found, and
//    how to reach it.
//
// 2. STAGED REPAIRS. T3000's scanner modifies equipment while looking at it:
//    it writes a random serial number to any device reporting serial 0, and
//    reassigns Modbus ids when two devices answer to the same one. Those are
//    reasonable things to want and unreasonable things to do without asking.
//
//    T5000 scans READ-ONLY. When it notices a problem it records a Repair -
//    what is wrong, what it would write, and what would happen - and shows it
//    to the operator. Nothing is sent until someone approves that specific
//    repair on that specific device.
//
//    This is a deliberate divergence from T3000, not an incomplete port. A
//    technician scanning a building to find out what is there should not
//    discover afterwards that the tool renumbered some of it.

#include <stdint.h>
#include <string>
#include <vector>

#include "connection.h"
#include "product.h"

namespace t5000::device
{
    // A stable, process-local key for a device record.
    //
    // Deliberately neither an index nor a serial number.
    //
    // An index is a position, and a position means a different device as soon
    // as the list changes. A serial number would be the natural key except
    // that it is missing on exactly the devices most worth looking at - the
    // ones reporting 0, which is the problem this tool exists to surface.
    //
    // A handle is assigned on insert, never reused, and survives reordering.
    // It matters because this key crosses a process boundary: a page renders
    // a device list, and the click that follows arrives later, against a
    // registry that may have changed in between.
    //
    // An `enum class` rather than a uint64_t alias, for the same reason
    // ProductClassId and MiniType are distinct types: so that passing an
    // index where a handle belongs fails to compile instead of running.
    // Every caller in this file used to pass indices, and with a plain alias
    // all of them would still build - `approve_repair(0, 0)` would quietly
    // mean handle 0, and `approve_repair(5, 0)` would authorise a write to
    // whichever device happened to hold handle 5.
    enum class Handle : uint64_t { None = 0 };
    constexpr Handle kNoHandle = Handle::None;

    // Handles cross the HTTP boundary as decimal text.
    inline uint64_t to_number(Handle h) { return static_cast<uint64_t>(h); }
    inline Handle   to_handle(uint64_t n) { return static_cast<Handle>(n); }

    // How this device came to be in the list. Worth keeping, because a device
    // someone typed in by hand and a device that answered a broadcast are
    // trusted differently - a typo produces a record that looks exactly like
    // a real discovery until you try to read it.
    enum class Provenance
    {
        ManuallyAdded,      // an operator supplied the address
        BacnetBroadcast,    // answered a Who-Is
        BacnetUnicast,      // answered a directed Who-Is at a known address
        SerialScan,         // found by walking ids on a serial line
        Restored,           // loaded from the saved device list
    };

    // What is wrong with a device, expressed as something that could be done
    // about it. Never applied automatically.
    // The registers each repair would touch were read out of
    // TStatScanner.cpp, where T3000 writes them during a scan with no
    // confirmation of any kind. They are named here so an operator can see
    // exactly what approving one would send.
    enum class RepairKind
    {
        // The device has no serial, so it cannot be told apart from any other
        // device in the same state. T3000 fixes this automatically, and does
        // it in TWO different places that do not agree with each other.
        //
        // TStatScanner.cpp:1463-1485, four writes, 16-bit words:
        //     register 16 <- 142           an init code, then Sleep(1000)
        //     register 0  <- serial low    rand() % 100000 + 200000
        //     register 2  <- serial high
        //     register 8  <- 6             hardware version, PM_TSTAT8 only,
        //                                  and only when it also reads 0 or FF
        //
        // TStatScanner.cpp:3991-3994 (ScanOldNC), four writes, single bytes:
        //     registers 0,1,2,3 <- rand() % 255 each
        //
        // The two paths differ in every respect that matters: the register
        // layout (two 16-bit words vs four bytes), the value range
        // (200000-300000 vs four independent bytes), and - see
        // is_uninitialised_serial below - whether their "is it uninitialised"
        // test is even correct. The byte-wise one at :3983 is right; the
        // arithmetic one at :1460 is not.
        //
        // Both serials are RANDOM, from srand(time(NULL)). Two devices
        // scanned within the same second can be handed the same "unique"
        // number - worth telling an operator before they agree to it.
        AssignSerialNumber,

        // Two devices on one line answer to the same Modbus id, so neither
        // can be addressed reliably. T3000 does this automatically in two
        // places inside the binary search - TStatScanner.cpp:1215 and :1657 -
        // both walking for a free id and writing register 10.
        //
        // Note the duplicate-id DIALOG path (DuplicateIdDetected.cpp) is
        // already properly user-gated in T3000 - it is the scanner that is
        // not. Only the scanner's behaviour is being changed here.
        ResolveDuplicateModbusId,

        // A gateway's sub-port and sub-baudrate are not set for the devices
        // behind it. T3000 writes registers 96 and 97 automatically during a
        // subnet scan (TStatScanner.cpp:485-486), addressed to 255 - the
        // broadcast id - so this one is not even aimed at a single device.
        ConfigureGatewaySubPort,
    };

    // A serial number that means "this device was never given one".
    //
    // Both 0 and 0xFFFFFFFF are uninitialised-flash values. T3000 tests for
    // them in two places, and only one of the two is correct.
    //
    // TStatScanner.cpp:1460 - WRONG:
    //     if ((nSerialNumber == 0) || (nSerialNumber == 255 * 255 * 255 * 255))
    //
    // 255*255*255*255 is 4,228,250,625. The all-bits-set value it is reaching
    // for is 0xFFFFFFFF = 4,294,967,295. Different numbers, so that branch is
    // dead and an all-FF serial is never detected on this path.
    //
    // TStatScanner.cpp:3983 - correct, by avoiding the arithmetic entirely:
    //     if (SerialNum[0]==255 && SerialNum[1]==255 &&
    //         SerialNum[2]==255 && SerialNum[3]==255)
    //
    // Fixed here rather than reproduced. The consequence of the bug is a
    // device that cannot be told apart from another one, which is the exact
    // problem the check exists to catch.
    bool is_uninitialised_serial(unsigned int serial);

    struct Repair
    {
        RepairKind  kind;
        std::string problem;      // what is wrong, in a technician's words
        std::string action;       // exactly what would be written, and where
        std::string consequence;  // what changes on the device afterwards
        bool        reversible = false;

        // Set only when an operator has explicitly approved THIS repair on
        // THIS device. Approving one repair never approves another, and
        // approval does not survive a rescan - a device that has moved or
        // been replaced must be looked at again.
        bool approved = false;
    };

    struct DeviceRecord
    {
        // Assigned by the Registry when the record is inserted, and 0 until
        // then. This is what the UI refers to a device by; see Handle above.
        Handle handle = kNoHandle;

        // --- Identity. ---------------------------------------------------
        // Serial number is the closest thing to a stable key, which is
        // exactly why a device reporting 0 is a problem worth surfacing
        // rather than papering over.
        //
        // UNSIGNED, deliberately. This was an int, and the all-FF
        // uninitialised value 0xFFFFFFFF became -1 on the way in from the
        // wire. -1 is not 0, so has_stable_identity() called it a real
        // identity and two unidentified devices merged into one record - the
        // exact collapse that unidentified_count() exists to prevent. A
        // technician would have been told there was one nameless device on
        // the subnet when there were two.
        uint32_t serial_number = 0;

        ProductClassId product   = ProductClassId::Unknown;   // what it reports
        int            mini_type = 0;                         // raw; resolve via resolve_panel
        int            firmware  = 0;                         // for the PTP >= 525 gate

        // The Modbus id the device SAID it has, with 0 meaning it did not say.
        //
        // Deliberately not Connection::modbus_slave_id, which is how WE would
        // address it and defaults to 1. Reading duplicates off that field
        // would treat every device that reported no id as being on id 1, and
        // accuse them all of conflicting with whatever is genuinely there.
        // An observation and a configuration are different things and only
        // one of them can be absent.
        int modbus_id_reported = 0;

        // --- Reachability. -----------------------------------------------
        Connection  connection;
        std::string address_note;   // human-readable: "192.168.1.50" or "COM3 id 12"

        // --- Provenance and state. ----------------------------------------
        Provenance          provenance = Provenance::ManuallyAdded;
        std::vector<Repair> repairs;

        // True once the tool has actually exchanged data with it. A record
        // can exist without this - a manually added device is in the list
        // before anyone has proved it is there - and the UI must not present
        // an unreached device as though its fields were read from hardware.
        bool reached = false;

        // True when this record came from a complete look at the device - a
        // scan response - rather than from a partial one, such as a serial
        // sweep that learned only an address.
        //
        // It decides whether a merge may REPLACE the repair list. A complete
        // observation that finds no problems means there are none, and the
        // old ones must go; a partial one knows nothing about problems and
        // must leave them alone.
        bool observation_complete = false;

        // A device with no usable serial cannot be keyed on one. Reported
        // rather than worked around, because every alternative key (IP,
        // Modbus id) is something a person can change.
        //
        // Both uninitialised values count as "no identity", not just zero -
        // see is_uninitialised_serial above for why that distinction has
        // teeth.
        bool has_stable_identity() const { return !is_uninitialised_serial(serial_number); }

        // True when at least one repair is outstanding.
        bool needs_attention() const;
    };

    // The known devices. Deliberately a plain list: one technician looking at
    // one building does not need an index, and a list keeps the order devices
    // were found, which is what a person scanning a site expects to see.
    class Registry
    {
    public:
        // Adds a device, or merges into an existing record when the serial
        // matches. Returns the index of the record.
        //
        // Merging on serial is the only safe merge. Merging on address would
        // collapse two devices that swapped IPs into one, and merging on
        // Modbus id would collapse exactly the duplicate-id case that most
        // needs to stay visible as two devices.
        int add_or_merge(const DeviceRecord& device);

        // Devices that report serial 0 cannot be merged and are always added
        // as separate records. How many there are is worth knowing: several
        // is a site-wide problem, not a device problem.
        int unidentified_count() const;

        const std::vector<DeviceRecord>& devices() const { return m_devices; }
        int size() const { return (int)m_devices.size(); }
        void clear();

        // --- Selection. ---------------------------------------------------
        // Every screen needs a selected device, and the selection is held as
        // a Handle rather than as an index.
        //
        // The index version of this was wrong in a way a bounds check cannot
        // catch. It cleared an out-of-range index - the easy half - and kept
        // an in-range one. So once the list changed underneath, index 1 was
        // still a perfectly valid position holding a DIFFERENT controller,
        // and the page carried on showing it under the heading of the device
        // the operator had picked. The header promised "nothing selected, not
        // somebody else's controller"; the code delivered that only for
        // indices past the end, and the test only ever checked that half.
        //
        // With a handle there is no stored position to go stale: the device
        // is either still here or it is not.
        void select(int index);                 // resolves to a handle now
        bool select_by_handle(Handle handle);   // false, and clears, if absent
        void clear_selection() { m_selected = kNoHandle; }

        Handle selected_handle() const { return m_selected; }

        // Resolved from the handle on every call. -1 when nothing is selected
        // or when the selected device is no longer in the list.
        int selected_index() const;
        const DeviceRecord* selected() const;

        // --- Repairs. -----------------------------------------------------
        // Approve one repair on one device. Keyed by handle for the same
        // reason selection is: this one authorises a WRITE to building
        // equipment, so a stale key here does not mislead a reader, it
        // reconfigures the wrong controller.
        //
        // The repair index within a device stays positional. That is bounded
        // in a way the device key was not - a stale one can only mis-target
        // inside a single device's current repair list - and approvals are
        // cleared on every merge anyway.
        //
        // Returns false if the device is gone or the index is out of range.
        bool approve_repair(Handle device, int repair_index);

        // Every repair awaiting approval, across all devices, as
        // (device handle, repair index) pairs. This is what a "N problems
        // found" banner counts.
        std::vector<std::pair<Handle, int>> pending_repairs() const;

        // Recomputes the duplicate-Modbus-id repairs across the WHOLE list,
        // and returns how many devices now carry one.
        //
        // A duplicate is a property of the set of known devices, not of one
        // scan, and detecting it inside the scan missed two cases:
        //
        //   - Two devices on id 5 that answer on DIFFERENT scans are never
        //     seen together, so neither scan finds a conflict and the list
        //     quietly holds two devices on one address.
        //
        //   - Both answer, both get the repair, and then only one answers the
        //     rescan. That one is a complete observation with no problems, so
        //     its repair list is replaced with nothing - while the other
        //     device goes on saying "id 5 is claimed by 2 devices" about a
        //     device the page is now showing as clean. The screen contradicts
        //     itself, which is the failure this tool exists to stop.
        //
        // Existing duplicate repairs are removed before the new ones are
        // worked out, so a conflict that has been resolved stops being
        // reported instead of accumulating.
        int refresh_duplicate_modbus_ids();

    private:
        int index_of(Handle handle) const;

        std::vector<DeviceRecord> m_devices;
        Handle m_selected    = kNoHandle;
        Handle m_next_handle = to_handle(1);
    };

    const char* to_string(Provenance p);
    const char* to_string(RepairKind k);
}
