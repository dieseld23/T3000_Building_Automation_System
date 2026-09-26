#pragma once

// Find: looking for one device in the list at an address the operator gives.
//
// For a device no scan reaches. A scan is a broadcast, and a broadcast stays
// on this computer's own subnet, so a controller across a router - or one
// added by hand before the laptop was on its network - is never found by
// one. The operator knows its address; Find checks that the device is there.
//
// ONE REQUEST, and only when the operator clicks Find: the panel's settings
// (READ_SETTING_COMMAND, by BACnet private transfer), the same read every
// points page starts with, to the address and port given, and sent once more
// if nothing answers, as every read is. It is a read, from the same read-only
// transport (bacnet/point_read.h). The device is found when its settings give
// the serial the entry has. Nothing else is sent, and nothing is written.
//
// T3000's nearest equivalent is Add Remote Device
// (BacnetAddRemoteDevice.cpp:146-300): a Who-Is sent to the address up to
// five times, then Modbus registers 0-49 read through BACnet
// (PROTOCOL_BIP_T0_MSTP_TO_MODBUS, :195-204), the serial from the first four.
// T5000 does not read Modbus registers yet, and sends the one read its pages
// rely on instead: it gives the serial by the path they read by.
//
// What a Find does NOT establish, and so what the list does not claim:
//   - the product. The settings do not give one, so the entry's stays.
//   - the firmware. The settings give one, but on a scale not yet checked
//     against the scan's, so it is left for a scan to fill in.
//   - that the device will be there next time. It is held to the stricter
//     rule on every read (Identity::FoundAtAddress): each read checks the
//     serial again before anything else is read.

#include <stdint.h>

#include <string>

#include <vector>

#include "../bacnet/point_read.h"
#include "../device/registry.h"
#include "../net/interfaces.h"
#include "../store/device_db.h"
#include "scan_json.h"

namespace t5000::app
{
    // BACNETIP_PORT, global_define.h:242: where a BACnet/IP device listens,
    // and what the Find dialog offers.
    inline constexpr int kBacnetIpPort = 47808;

    // What the page sends: {"handle":"12","host":"192.168.1.50","port":"47808"}.
    // The port may come as a number or a string of digits, and left out or
    // empty is 47808.
    struct FindRequest
    {
        device::Handle handle = device::kNoHandle;
        std::string    host;
        int            port = kBacnetIpPort;
    };

    bool read_find_request(const std::string& body, FindRequest& request, std::string& message);

    // Why Find does not apply to this device, in a sentence for the page, or
    // empty when it does. The page offers Find only when this is empty, and
    // the route asks again.
    //
    // It applies to a device with a serial to match, that has not answered a
    // scan since T5000 started, whose settings T5000 reads by private
    // transfer, and that is not on another controller's RS485 bus. That is an
    // entry added by hand, a device from the saved list, and one found by Find
    // before.
    std::string why_not_findable(const device::DeviceRecord& d);

    // The address the operator typed, as somewhere to send one request: an
    // IPv4 address as four numbers from 0 to 255, and a port from 1 to 65535.
    // Refused, with the reason in `message`, for anything else - a host name,
    // which T5000 does not look up - and for an address that is not one
    // device's: 0.0.0.0, the broadcast address, multicast, and the reserved
    // 240.0.0.0 range.
    //
    // Refused too: the broadcast address and the network's own address of
    // each network in `local`, this computer's interfaces. A typo such as
    // 192.168.1.255 for .25 would otherwise go to every device on that
    // network, and Windows does not reliably stop it: the socket is not
    // allowed to broadcast, but a send from it to 127.255.255.255 went out.
    // The broadcast address of a network this computer is not on cannot be
    // told from a device's, and a router drops a packet sent to one unless it
    // has been set to forward them.
    bool find_endpoint(const std::string& host, int port, const std::vector<net::Interface>& local,
                       bacnet::Endpoint& at, std::string& message);

    // Everything decided before anything is sent: the device the request
    // names, whether Find applies to it, and the address, checked against
    // `local` as find_endpoint says. False, with the reason in `message`,
    // when nothing is to be sent. `device` is a copy of the entry as it is
    // now.
    bool plan_find(const device::Registry& registry, const FindRequest& request,
                   const std::vector<net::Interface>& local, device::DeviceRecord& device, bacnet::Endpoint& at,
                   std::string& message);

    // What one settings read at `at` says about `device`.
    struct FindOutcome
    {
        bool found = false;

        // When found: the device as the read shows it, to merge into the
        // list. The entry's handle, serial and product; BacnetUnicast,
        // reached, seen `now`, at `at`, with the panel type, Modbus id,
        // instance and name from its settings.
        device::DeviceRecord record;

        // What the operator is told either way: in the Find dialog, or after
        // "Found." or "Not found." in the page's banner.
        std::string message;

        int requests_sent = 0;
    };

    // Sends the one request and matches the serial. Found only when the
    // settings give exactly the entry's serial: another serial is another
    // device, and serial 0, a refusal, silence or settings that cannot be
    // decoded confirm nothing. `registry` is only looked at, to name the
    // entry another serial belongs to when it is in the list.
    FindOutcome find_at(bacnet::ReadTransport& transport, const bacnet::Endpoint& at,
                        const device::DeviceRecord& device, const device::Registry& registry,
                        const bacnet::ReadSettings& settings, uint8_t& next_invoke_id, int64_t now);

    // Merges a device Find has found into the list, re-derives the duplicate
    // Modbus ids, and saves it.
    //
    // As for every change the operator makes (device_list.h), the list takes
    // the change only once the file has: the merge is made on a copy, the
    // copy's record is saved, and only then does the copy become the list.
    // A failed save leaves both as they were, and says why. When the list is
    // not being saved, the device is merged for this session and the message
    // says so.
    //
    // The entry keeps its name, its location and any inputs changed offline,
    // which its Inputs page then lists as not written. A device added by
    // hand stops being configured offline: it has an address now, and is
    // read from it.
    //
    // `found` carries the handle of the entry it was found for, and is
    // recorded only if it merges into that entry.
    bool record_found(device::Registry& registry, store::DeviceDb& db, const device::DeviceRecord& found,
                      ScanSummary& summary, std::string& message);

    // A planned Find carried out over a transport already open to `at`:
    // find_at, then record_found when it is found. False, with `message`
    // saying why, when it was not found or could not be recorded; the list
    // and the file are then as they were.
    bool find_device(device::Registry& registry, store::DeviceDb& db, const device::DeviceRecord& device,
                     const bacnet::Endpoint& at, bacnet::ReadTransport& transport,
                     const bacnet::ReadSettings& settings, uint8_t& next_invoke_id, int64_t now,
                     ScanSummary& summary, std::string& message);
}
