#include "serial_scan.h"

#include <algorithm>

#include "scanner.h"

namespace t5000::discovery
{
    namespace
    {
        using namespace t5000::device;
        using serial::RangeAnswer;
        using serial::ScanFrame;

        // Room for more than any one reply, so that several replies arrive
        // as more bytes than one, rather than being cut to fit.
        constexpr int kReplyCapacity = 64;

        struct Search
        {
            SerialScanTransport& line;
            const SerialScanSettings& settings;
            SerialScanResult& result;
            bool stop = false;

            // Sends one frame and collects the reply. False, with the scan
            // stopped, when the line fails.
            bool ask(const ScanFrame& frame, std::vector<uint8_t>& reply)
            {
                reply.clear();
                std::string error;
                if (!line.send(frame, error))
                {
                    result.error = error.empty() ? "a frame could not be sent" : error;
                    stop = true;
                    return false;
                }
                result.stats.frames_sent++;

                uint8_t buffer[kReplyCapacity] = {};
                const int n = line.receive(buffer, kReplyCapacity, settings.reply_ms, error);
                if (n < 0)
                {
                    result.error = error.empty() ? "the line failed" : error;
                    stop = true;
                    return false;
                }
                reply.assign(buffer, buffer + n);
                return true;
            }

            void identify(uint8_t id)
            {
                const ScanFrame read = ScanFrame::identity_read(id);
                for (int attempt = 0; attempt < settings.attempts && !stop; attempt++)
                {
                    std::vector<uint8_t> reply;
                    if (!ask(read, reply))
                        return;

                    serial::DeviceIdentity identity;
                    std::string why;
                    if (serial::decode_identity_reply(reply.data(), reply.size(), id, identity, why))
                    {
                        result.devices.push_back(serial_record(identity, id, settings));
                        return;
                    }
                }
                result.stats.unreadable_ids.push_back(id);
            }

            // Every device answers a query whose range holds its id, so one
            // clean answer means one device, and more than one garbles the
            // reply. A garbled range is halved until each id stands alone,
            // as binarySearchforComDevice does (TStatScanner.cpp:1586-1606).
            void search(uint8_t lo, uint8_t hi)
            {
                const ScanFrame query = ScanFrame::range_query(lo, hi);
                for (int attempt = 0; attempt < settings.attempts && !stop; attempt++)
                {
                    std::vector<uint8_t> reply;
                    if (!ask(query, reply))
                        return;

                    const serial::RangeReply r = serial::decode_range_reply(reply.data(), reply.size(), query);
                    switch (r.answer)
                    {
                    case RangeAnswer::Nobody:
                        return;

                    case RangeAnswer::Mstp:
                        result.runs_mstp = true;
                        result.line_busy = "BACnet MS/TP frames came back in answer to a Modbus query, so this "
                                           "line runs MS/TP. The scan stopped.";
                        stop = true;
                        return;

                    case RangeAnswer::One:
                        identify(r.id);
                        return;

                    case RangeAnswer::Several:
                    case RangeAnswer::Garbled:
                        if (r.answer == RangeAnswer::Garbled)
                            result.stats.garbled++;
                        if (lo < hi)
                        {
                            const uint8_t mid = (uint8_t)((lo + hi) / 2);
                            search(lo, mid);
                            if (!stop)
                                search((uint8_t)(mid + 1), hi);
                            return;
                        }
                        // One id, and still more than one reply: two devices
                        // share it. T3000 moves one by writing register 10
                        // (TStatScanner.cpp:1605-1657); this only says so.
                        if (r.answer == RangeAnswer::Several)
                        {
                            result.stats.shared_ids.push_back(lo);
                            return;
                        }
                        // Garbled at one id: asked again, a set number of times.
                        break;
                    }
                }
                if (!stop)
                    result.stats.unreadable_ids.push_back(lo);
            }
        };
    }

    SerialScanResult scan_serial(SerialScanTransport& line, const SerialScanSettings& settings)
    {
        SerialScanResult result;

        if (settings.lowest < serial::kLowestId || settings.highest > serial::kHighestId ||
            settings.lowest > settings.highest)
        {
            result.error = "the ids to scan must lie within 1-254";
            return result;
        }
        // With no attempts, nothing would be asked, and the first id would
        // be reported as unreadable when it was never tried.
        if (settings.attempts < 1)
        {
            result.error = "each question must be asked at least once";
            return result;
        }

        // Listening first, with nothing sent. A line that is already talking
        // is someone else's: MS/TP, or another master polling it.
        uint8_t heard[256] = {};
        std::string error;
        const int n = line.receive(heard, (int)sizeof(heard), settings.listen_ms, error);
        if (n < 0)
        {
            result.error = error.empty() ? "the line failed" : error;
            return result;
        }
        if (n > 0)
        {
            if (serial::has_mstp_preambles(heard, (size_t)n))
            {
                result.runs_mstp = true;
                result.line_busy = "BACnet MS/TP frames were heard on this line, so it runs MS/TP, not "
                                   "Modbus. Nothing was sent.";
            }
            else
            {
                result.line_busy = std::to_string(n) + " bytes arrived while listening, before anything was "
                                   "sent, so something else is already talking on this line - another "
                                   "Modbus master, perhaps. A scan would collide with it, so nothing was "
                                   "sent.";
            }
            return result;
        }

        Search s{ line, settings, result };
        s.search(settings.lowest, settings.highest);

        std::sort(result.stats.shared_ids.begin(), result.stats.shared_ids.end());
        std::sort(result.stats.unreadable_ids.begin(), result.stats.unreadable_ids.end());
        return result;
    }

    int PortScanResult::frames_sent() const
    {
        int n = 0;
        for (const RateScan& r : rates)
            n += r.result.stats.frames_sent;
        return n;
    }

    int PortScanResult::busy_rates() const
    {
        int n = 0;
        for (const RateScan& r : rates)
            if (!r.result.line_busy.empty() && !r.result.runs_mstp)
                n++;
        return n;
    }

    PortScanResult scan_serial_port(SerialLine& line, const std::vector<int>& rates,
                                    const SerialScanSettings& settings)
    {
        PortScanResult out;
        out.port = settings.port_name;

        for (const int baud : rates)
        {
            std::string error;
            if (!line.set_rate(baud, error))
            {
                out.error = "the port would not take " + std::to_string(baud) + " baud" +
                            (error.empty() ? std::string() : ": " + error);
                return out;
            }

            SerialScanSettings at = settings;
            at.baud = baud;

            RateScan r;
            r.baud   = baud;
            r.result = scan_serial(line, at);
            out.rates.push_back(r);

            for (const DeviceRecord& d : r.result.devices)
            {
                const bool repeat = std::any_of(out.devices.begin(), out.devices.end(), [&](const DeviceRecord& e) {
                    return d.has_stable_identity() ? e.serial_number == d.serial_number
                                                   : !e.has_stable_identity() &&
                                                         e.connection.modbus_slave_id == d.connection.modbus_slave_id;
                });
                if (repeat)
                    out.repeats++;
                else
                    out.devices.push_back(d);
            }

            // The line failed, or the settings were refused: the rates after
            // would fail the same way.
            if (!r.result.error.empty())
            {
                out.error = r.result.error;
                return out;
            }

            // MS/TP heard at this rate. A line that was merely busy at a
            // rate goes on to the next: MS/TP at another rate sounds like
            // noise, and is heard as MS/TP only at its own.
            if (r.result.runs_mstp)
            {
                out.runs_mstp = true;
                out.mstp_baud = baud;
                return out;
            }
        }
        return out;
    }

    device::DeviceRecord serial_record(const serial::DeviceIdentity& identity, uint8_t answered_id,
                                       const SerialScanSettings& settings)
    {
        DeviceRecord d;
        d.serial_number = identity.serial;
        d.product       = static_cast<ProductClassId>(identity.product);
        d.product_reported = d.product != ProductClassId::Unknown;
        d.firmware      = identity.firmware;
        d.provenance    = Provenance::SerialScan;

        // It answered, and its registers 0-9 are a complete look at it, as a
        // scan response is on the network.
        d.reached              = true;
        d.observation_complete = true;

        // Register 6 is the id the device says it has. It is addressed on
        // the id that answered, which is the same on any device that follows
        // its own register.
        d.modbus_id_reported = identity.modbus_id;

        const std::string port = !settings.port_name.empty() ? settings.port_name
                                                             : "COM" + std::to_string(settings.com_port);

        d.connection.transport       = Transport::ModbusRtu;
        d.connection.serial_port     = port;
        d.connection.com_port        = settings.com_port;
        d.connection.baud            = settings.baud;
        d.connection.modbus_slave_id = answered_id;
        d.address_note = port + " id " + std::to_string(answered_id) + ", " + std::to_string(settings.baud) + " baud";

        if (is_uninitialised_serial(identity.serial))
            d.repairs.push_back(no_serial_repair(identity.serial));
        return d;
    }
}
