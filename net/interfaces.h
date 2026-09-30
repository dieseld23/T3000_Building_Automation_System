#pragma once

// Which network interface to scan from.
//
// This is not a convenience. discovery/scanner.h notes that a machine with
// several NICs will broadcast from whichever one Windows prefers, "which is
// frequently not the one the controllers are on" - and the symptom is a scan
// that finds nothing, with no error to explain it. A technician's laptop
// routinely has three or four candidates: the building network, a Wi-Fi
// connection, a VPN adapter, and a Hyper-V or WSL virtual switch.
//
// Offering the choice is the difference between "no devices found" and "no
// devices found on 192.168.1.23, and here are the other places to look".

#include <string>
#include <vector>

namespace t5000::net
{
    struct Interface
    {
        std::string ip;            // dotted quad, the value UdpTransport wants
        std::string name;          // what Windows calls it: "Ethernet", "Wi-Fi"
        std::string description;   // the adapter's own description

        bool is_up       = false;  // operationally up
        bool is_loopback = false;

        // The on-link prefix length: 24 for a 255.255.255.0 network. 0 when
        // Windows did not give one. Find uses it to refuse the network's
        // own address and its broadcast address (app/find_device.h).
        int prefix_length = 0;

        // True for adapters that are almost certainly not the building
        // network: Hyper-V switches, WSL, VPN tunnels, VirtualBox. A guess,
        // and presented as one - it sorts them down the list rather than
        // hiding them, because "almost certainly" is not "certainly" and a
        // building network behind a VPN is a real deployment.
        bool looks_virtual = false;
    };

    // Every IPv4 interface a scan could be sent from, best candidate first.
    //
    // Loopback is included. A device cannot answer over it, but leaving it out
    // of the list makes "why is 127.0.0.1 not here" a question with no answer
    // on screen; it sorts last and is labelled.
    std::vector<Interface> ipv4_interfaces(std::string& error);
}
