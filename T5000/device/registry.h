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

#include <string>
#include <vector>

#include "connection.h"
#include "product.h"

namespace t5000::device
{
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
        // device in the same state. T3000 does this automatically at
        // TStatScanner.cpp:1463-1485 and it is FOUR writes, not one:
        //
        //     register 16 <- 142           an init code, then Sleep(1000)
        //     register 0  <- serial low    rand() % 100000 + 200000
        //     register 2  <- serial high
        //     register 8  <- 6             hardware version, PM_TSTAT8 only,
        //                                  and only when it also reads 0 or FF
        //
        // The serial is RANDOM, from srand(time(NULL)). Two devices scanned
        // within the same second can therefore be given the same "unique"
        // number - which is worth telling the operator before they agree.
        AssignSerialNumber,

        // Two devices on one line answer to the same Modbus id, so neither
        // can be addressed reliably. T3000 does this automatically at
        // TStatScanner.cpp:1657, walking j from 254 downwards for a free id
        // and writing register 10.
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
    // them at TStatScanner.cpp:1460 as:
    //
    //     if ((nSerialNumber == 0) || (nSerialNumber == 255 * 255 * 255 * 255))
    //
    // ...and that second constant is wrong. 255*255*255*255 is 4,228,250,625;
    // the all-bits-set value it is reaching for is 0xFFFFFFFF = 4,294,967,295.
    // So T3000 never detects an all-FF serial, and a device in that state
    // keeps reporting it. Fixed here rather than reproduced, because the
    // consequence of the bug is a device that cannot be told apart from
    // another one - which is the exact problem the check exists to catch.
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
        // --- Identity. ---------------------------------------------------
        // Serial number is the closest thing to a stable key, which is
        // exactly why a device reporting 0 is a problem worth surfacing
        // rather than papering over.
        int serial_number = 0;

        ProductClassId product   = ProductClassId::Unknown;   // what it reports
        int            mini_type = 0;                         // raw; resolve via resolve_panel
        int            firmware  = 0;                         // for the PTP >= 525 gate

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

        // A device with no usable serial cannot be keyed on one. Reported
        // rather than worked around, because every alternative key (IP,
        // Modbus id) is something a person can change.
        bool has_stable_identity() const { return serial_number != 0; }

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
        // Every screen needs a selected device. Selecting an out-of-range
        // index clears the selection rather than throwing or clamping: a
        // stale index from a page that was open across a rescan should show
        // "nothing selected", not somebody else's controller.
        void select(int index);
        void clear_selection() { m_selected = -1; }
        int  selected_index() const { return m_selected; }
        const DeviceRecord* selected() const;

        // --- Repairs. -----------------------------------------------------
        // Approve one repair on one device. Returns false if either index is
        // out of range, which is the case where a page has gone stale.
        bool approve_repair(int device_index, int repair_index);

        // Every repair awaiting approval, across all devices, as
        // (device index, repair index) pairs. This is what a "N problems
        // found" banner counts.
        std::vector<std::pair<int, int>> pending_repairs() const;

    private:
        std::vector<DeviceRecord> m_devices;
        int m_selected = -1;
    };

    const char* to_string(Provenance p);
    const char* to_string(RepairKind k);
}
