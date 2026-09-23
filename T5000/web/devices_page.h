#pragma once

// The device list - the front door of the tool.
//
// Embedded rather than served from disk so the tool is a single file that
// works from wherever it is copied, for the same reason as inputs_page.h.
//
// Two things on this page are load-bearing and easy to get wrong:
//
//   1. An empty list is FOUR different situations. Not scanned yet, scanned
//      and the subnet really is empty, scanned and devices answered with
//      something unreadable, and the scan could not run. One "no devices
//      found" message for all four is the failure this project exists to stop
//      repeating - it is the same shape as a blank grid that never says the
//      firmware is too old.
//
//   2. Repairs are shown and not offered. T5000 has no write path at all yet,
//      so an "Apply" button would be a control that cannot do what it says.
//      What T3000 would have written is disclosed in full, and the page says
//      plainly that nothing will be sent.

namespace t5000::web
{
    inline constexpr const char* kDevicesPage = R"PAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Devices</title>
<style>
  :root {
    --bg:#fff; --surface:#f7f8fa; --border:#e3e6ea; --text:#1a1d21; --dim:#6b7280;
    --accent:#0b6bcb; --row-alt:#fafbfc; --ok:#0f7b3f; --ok-bg:#e6f4ec;
    --warn:#8a5300; --warn-bg:#fdf1dc; --info:#0b4ea2; --info-bg:#e8f0fe;
    --bad:#a8202e; --bad-bg:#fdeaec;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg:#16181c; --surface:#1d2025; --border:#2c3036; --text:#e6e8ea; --dim:#9aa1ab;
      --accent:#4d9bf0; --row-alt:#191c20; --ok:#5bd18b; --ok-bg:#16301f;
      --warn:#e0b060; --warn-bg:#33270f; --info:#7fb4f0; --info-bg:#16283f;
      --bad:#f08a95; --bad-bg:#3a1418;
    }
  }
  *{box-sizing:border-box}
  [hidden]{display:none !important}
  html,body{height:100%}
  body{margin:0;background:var(--bg);color:var(--text);display:flex;flex-direction:column;
       font:13px/1.45 "Segoe UI Variable Text","Segoe UI",system-ui,sans-serif}

  header{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:10px 16px;
         border-bottom:1px solid var(--border);background:var(--surface);flex:none}
  h1{font-size:14px;font-weight:600;margin:0}
  .meta{color:var(--dim);font-size:12px}
  .meta b{color:var(--text);font-weight:600;font-variant-numeric:tabular-nums}
  .spacer{flex:1}
  label.inline{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--dim)}
  select,input[type=number]{font:inherit;color:inherit;background:var(--bg);
    border:1px solid var(--border);border-radius:6px;padding:5px 8px}
  select:focus,input:focus{border-color:var(--accent);outline:none}
  button{font:inherit;color:var(--text);background:var(--bg);border:1px solid var(--border);
         border-radius:6px;padding:5px 12px;cursor:pointer}
  button:hover:not(:disabled){border-color:var(--accent);color:var(--accent)}
  button:disabled{opacity:.55;cursor:default}
  button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
  button.primary:hover:not(:disabled){filter:brightness(1.08);color:#fff}

  .banner{flex:none;padding:9px 16px;border-bottom:1px solid var(--border);font-size:12px}
  .banner b{font-weight:600}
  .banner.info{background:var(--info-bg);color:var(--info)}
  .banner.warn{background:var(--warn-bg);color:var(--warn)}
  .banner.bad{background:var(--bad-bg);color:var(--bad)}
  .banner.ok{background:var(--ok-bg);color:var(--ok)}

  .scroll{flex:1;overflow:auto}
  table{border-collapse:separate;border-spacing:0;width:100%}
  thead th{position:sticky;top:0;z-index:1;background:var(--surface);color:var(--dim);
    font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase;
    text-align:left;white-space:nowrap;padding:8px 10px;border-bottom:1px solid var(--border)}
  tbody td{padding:6px 10px;border-bottom:1px solid var(--border);white-space:nowrap}
  tbody tr.dev{cursor:pointer}
  tbody tr.dev:nth-child(even of .dev){background:var(--row-alt)}
  tbody tr.dev:hover{background:var(--info-bg)}
  tbody tr.dev.sel{background:var(--info-bg);box-shadow:inset 3px 0 0 var(--accent)}
  .num{text-align:right;font-variant-numeric:tabular-nums}
  .dim{color:var(--dim)}
  .pill{display:inline-block;padding:1px 7px;border-radius:10px;font-size:11px;font-weight:600}
  .pill-ok{background:var(--ok-bg);color:var(--ok)}
  .pill-warn{background:var(--warn-bg);color:var(--warn)}
  .pill-bad{background:var(--bad-bg);color:var(--bad)}
  .pill-info{background:var(--info-bg);color:var(--info)}

  /* A repair is disclosure, not an offer. It reads as a note attached to the
     device rather than as a row with an action on the end of it. */
  tr.rep td{background:var(--warn-bg);white-space:normal;padding:0}
  .repair{padding:7px 14px 8px 28px;border-left:3px solid var(--warn);font-size:12px}
  .repair h3{margin:0 0 3px;font-size:12px;font-weight:600;color:var(--warn)}
  /* The problem is always visible; only what WOULD be written folds away.
     At three devices the expanded form already crowded the page, and a site
     with forty controllers and a dozen duplicate ids would be unreadable -
     but a problem the reader has to click to discover is not disclosure. */
  .repair details{margin:4px 0 0}
  .repair summary{cursor:pointer;color:var(--dim);font-size:11px;
                  list-style:revert;width:max-content}
  .repair summary:hover{color:var(--warn)}
  .repair dl{margin:5px 0 0;display:grid;grid-template-columns:max-content 1fr;
             gap:2px 10px;font-size:12px}
  .repair dt{color:var(--dim)}
  .repair dd{margin:0}
  .repair .nope{margin:7px 0 0;font-style:italic;color:var(--dim)}

  .empty{display:flex;align-items:center;justify-content:center;height:100%;
         color:var(--dim);text-align:center;padding:32px}
  .empty div{max-width:460px}
  .empty h2{font-size:14px;font-weight:600;color:var(--text);margin:0 0 6px}
  .empty p{margin:0 0 8px;font-size:12px}
  .empty ul{text-align:left;margin:8px 0 0;padding-left:18px;font-size:12px}
  .empty li{margin:3px 0}

  footer{flex:none;padding:6px 16px;border-top:1px solid var(--border);
         background:var(--surface);color:var(--dim);font-size:11px;
         display:flex;gap:14px;flex-wrap:wrap}
</style>
</head>
<body>

<header>
  <h1>Devices</h1>
  <span class="meta"><b id="count">0</b> found</span>
  <span class="meta" id="problems" hidden></span>
  <span class="spacer"></span>
  <label class="inline">Scan from
    <select id="iface"><option value="">All interfaces</option></select>
  </label>
  <label class="inline">Wait
    <select id="wait">
      <option value="3000">3 s</option>
      <option value="9000" selected>9 s</option>
      <option value="20000">20 s (stragglers)</option>
    </select>
  </label>
  <button id="clear">Clear list</button>
  <button id="scan" class="primary">Scan</button>
</header>

<div class="banner info" id="banner">Loading&hellip;</div>

<div class="scroll">
  <div class="empty" id="empty">
    <div>
      <h2 id="empty-title">No devices yet</h2>
      <p id="empty-detail"></p>
      <ul id="empty-hints" hidden></ul>
    </div>
  </div>

  <table id="grid" hidden>
    <thead>
      <tr>
        <th>Serial</th>
        <th>Product</th>
        <th>Panel</th>
        <th>Address</th>
        <th class="num">Firmware</th>
        <th>Found by</th>
        <th>State</th>
      </tr>
    </thead>
    <tbody id="rows"></tbody>
  </table>
</div>

<footer>
  <span id="readonly-note">Scanning is read-only. No register is written.</span>
  <span class="spacer"></span>
  <span><a href="/inputs">Inputs</a></span>
</footer>

<script>
(function () {
  "use strict";

  var state = null;
  var scanning = false;

  function $(id) { return document.getElementById(id); }
  function text(s) { return s === null || s === undefined ? "" : String(s); }

  function el(tag, cls, content) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (content !== undefined) n.textContent = text(content);
    return n;
  }

  function setBanner(kind, html) {
    var b = $("banner");
    b.className = "banner " + kind;
    b.innerHTML = html;
    b.hidden = false;
  }

  // The four empty states. Which one is showing is decided here and nowhere
  // else, so a new case cannot be added by accident as a fifth silent one.
  function showEmpty(scan, stats) {
    $("grid").hidden = true;
    $("empty").hidden = false;
    var hints = $("empty-hints");
    hints.innerHTML = "";
    hints.hidden = true;

    function addHints(list) {
      list.forEach(function (h) { hints.appendChild(el("li", null, h)); });
      hints.hidden = false;
    }

    if (!scan.hasScanned) {
      $("empty-title").textContent = "Nothing scanned yet";
      $("empty-detail").textContent =
        "Pick the network the controllers are on and press Scan. " +
        "Nothing is written to any device.";
      return;
    }

    if (scan.error) {
      $("empty-title").textContent = "The scan could not run";
      $("empty-detail").textContent = scan.error;
      addHints([
        "Another copy of T3000 or T5000 may be holding the discovery port.",
        "A firewall rule may be blocking UDP broadcast on port 1234.",
        "The selected interface may be down."
      ]);
      return;
    }

    if (stats.malformed > 0) {
      $("empty-title").textContent = "Devices answered, but unreadably";
      $("empty-detail").textContent =
        stats.malformed + " reply" + (stats.malformed === 1 ? "" : "s") +
        " arrived that look like discovery responses but could not be read. " +
        "That is a firmware or protocol-version problem, not an empty network.";
      addHints([
        "The devices are there and powered - they are answering.",
        "Check the firmware version against what this tool expects.",
        "Worth reporting: this is the case the wire format was derived from reading source, not a capture."
      ]);
      return;
    }

    $("empty-title").textContent = "No devices answered";
    var where = scan.interfaceIp ? "on " + scan.interfaceIp : "on any interface";
    $("empty-detail").textContent =
      "Waited " + Math.round(scan.waitedMs / 1000) + " s " + where + ". " +
      stats.datagramsReceived + " datagram" +
      (stats.datagramsReceived === 1 ? "" : "s") + " arrived, none of them from a controller.";
    addHints([
      "Try a different interface - a laptop often prefers Wi-Fi or a VPN adapter over the building network.",
      "Try waiting longer; some panels answer slowly.",
      "Broadcast does not cross subnets. A controller on another subnet will never answer this."
    ]);
  }

  function repairRow(repair, columns) {
    var tr = el("tr", "rep");
    var td = document.createElement("td");
    td.colSpan = columns;

    var box = el("div", "repair");
    box.appendChild(el("h3", null, "Problem found: " + repair.kind));

    // Always visible, no interaction. What is WRONG with the device is not
    // something a reader should have to open a disclosure to find out.
    box.appendChild(el("div", null, repair.problem));

    // What T3000 would have written folds away. It is the longest part and
    // the least urgent - it matters when someone is deciding what to do, not
    // when they are scanning a list to see what is broken.
    var details = document.createElement("details");
    details.appendChild(el("summary", null, "What the old tool would have written"));

    var dl = document.createElement("dl");
    function pair(k, v) {
      dl.appendChild(el("dt", null, k));
      dl.appendChild(el("dd", null, v));
    }
    pair("T3000 would write", repair.action);
    pair("Which would mean", repair.consequence);
    pair("Reversible", repair.reversible ? "Yes - it is another write" : "No");
    details.appendChild(dl);

    // The honest part. There is no write path in this tool at all, so there is
    // no button here, and the absence is explained rather than left as a gap
    // the reader has to interpret.
    details.appendChild(el("p", "nope",
      "T5000 has not written this and cannot: it has no write path yet. " +
      "Shown so you know what the old tool would have done during a scan, " +
      "without being asked."));

    box.appendChild(details);

    td.appendChild(box);
    tr.appendChild(td);
    return tr;
  }

  function render() {
    var devices = state.devices;
    $("count").textContent = devices.length;

    var problems = $("problems");
    if (state.pendingRepairs > 0) {
      problems.hidden = false;
      problems.innerHTML = "<b>" + state.pendingRepairs + "</b> problem" +
        (state.pendingRepairs === 1 ? "" : "s") + " found";
    } else {
      problems.hidden = true;
    }

    if (!devices.length) {
      showEmpty(state.scan, state.scan.stats);
      renderBanner();
      return;
    }

    $("empty").hidden = true;
    $("grid").hidden = false;

    var rows = $("rows");
    rows.innerHTML = "";
)PAGE"
        R"PAGE(    var columns = 7;

    devices.forEach(function (d) {
      var tr = el("tr", "dev" + (d.selected ? " sel" : ""));
      tr.setAttribute("data-handle", d.handle);

      var serial = el("td", "num");
      if (d.hasStableIdentity) {
        serial.textContent = d.serialNumber;
      } else {
        serial.appendChild(el("span", "pill pill-bad", "no serial"));
      }
      tr.appendChild(serial);

      tr.appendChild(el("td", null, d.productName));

      var panel = el("td", d.panel.resolved ? null : "dim");
      panel.textContent = d.panel.resolved ? d.panel.name : "unknown";
      panel.title = d.panel.reason;
      tr.appendChild(panel);

      tr.appendChild(el("td", null, d.address));
      tr.appendChild(el("td", "num", d.firmware || ""));
      tr.appendChild(el("td", "dim", d.provenance));

      var st = document.createElement("td");
      if (d.needsAttention) {
        st.appendChild(el("span", "pill pill-warn", "needs attention"));
      } else if (d.support === "verified") {
)PAGE"
        R"PAGE(        st.appendChild(el("span", "pill pill-ok", "supported"));
      } else {
        st.appendChild(el("span", "pill pill-info", d.support));
      }
      tr.appendChild(st);

      rows.appendChild(tr);

      d.repairs.forEach(function (r) { rows.appendChild(repairRow(r, columns)); });
    });

    renderBanner();
  }

  function renderBanner() {
    var s = state.scan, st = s.stats;

    if (s.error) {
      setBanner("bad", "<b>Scan problem.</b> " + text(s.error));
      return;
    }
    if (!s.hasScanned) {
      setBanner("info",
        "Nothing has been scanned yet. Scanning sends one broadcast and listens - " +
        "it does not write to any device.");
      return;
    }

    var parts = [];
    parts.push("<b>" + st.responsesParsed + "</b> answered the last scan");
    if (st.inBootloader > 0) parts.push("<b>" + st.inBootloader + "</b> in bootloader");
    if (st.withoutSerial > 0) parts.push("<b>" + st.withoutSerial + "</b> with no serial");
    if (st.duplicateModbusIds > 0) parts.push("<b>" + st.duplicateModbusIds + "</b> with a duplicate Modbus id");
    if (st.malformed > 0) parts.push("<b>" + st.malformed + "</b> unreadable");

    // The table is cumulative and these numbers are not. A scan that finds
    // nothing used to print "0 answered" directly above a list of three
    // devices - a screen contradicting itself, which is the precise failure
    // this tool exists to stop repeating. Devices are kept across scans on
    // purpose (a controller that answered once and is quiet now is worth
    // seeing), so the gap has to be named rather than hidden.
    var carried = state.devices.length - st.responsesParsed;
    if (carried > 0) {
      parts.push("<b>" + carried + "</b> listed from an earlier scan, not seen this time");
    }

    var stale = carried > 0;
    var kind = (st.malformed > 0 || state.pendingRepairs > 0 || stale) ? "warn" : "ok";
    setBanner(kind, parts.join(" &middot; ") + " &middot; nothing was written");
  }

  function applyState(next) {
    state = next;
    $("readonly-note").textContent = state.readOnly
      ? "Scanning is read-only. No register is written."
      : "WARNING: this build reports that scanning is not read-only.";
    render();
  }

  async function loadInterfaces() {
    try {
      var res = await fetch("/api/interfaces", { cache: "no-store" });
      var data = await res.json();
      var sel = $("iface");
      (data.interfaces || []).forEach(function (n) {
        var o = document.createElement("option");
        o.value = n.ip;
        var suffix = n.isLoopback ? " - loopback, no device can answer here"
                   : !n.isUp ? " - down"
                   : n.looksVirtual ? " - probably virtual"
                   : "";
        o.textContent = n.name + " (" + n.ip + ")" + suffix;
        sel.appendChild(o);
      });
      // Default to the best real candidate rather than to all-interfaces:
      // broadcasting from whichever NIC Windows prefers is the documented way
      // this finds nothing with no error to explain it.
      var best = (data.interfaces || []).filter(function (n) {
        return n.isUp && !n.isLoopback && !n.looksVirtual;
      })[0];
      if (best) sel.value = best.ip;
    } catch (e) {
      // Not fatal. All-interfaces still works; the operator just cannot pick.
    }
  }

  async function load() {
    var res = await fetch("/api/devices", { cache: "no-store" });
    applyState(await res.json());
  }

  async function doScan() {
    if (scanning) return;
    scanning = true;

    var seconds = Math.round(parseInt($("wait").value, 10) / 1000);
    $("scan").disabled = true;
    $("scan").textContent = "Scanning…";
    setBanner("info",
      "Listening for up to " + seconds + " s. The page is waiting for the scan to " +
      "finish; nothing is being written to any device.");

    try {
      var res = await fetch("/api/scan", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          interfaceIp: $("iface").value,
          waitMs: parseInt($("wait").value, 10)
        })
      });
      applyState(await res.json());
    } catch (e) {
      setBanner("bad", "<b>The scan request failed.</b> " + text(e && e.message));
    } finally {
      scanning = false;
      $("scan").disabled = false;
      $("scan").textContent = "Scan";
    }
  }

  async function select(handle) {
    var res = await fetch("/api/devices/select", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ handle: handle })
    });
    var data = await res.json();
    applyState(data.state);
    if (!data.ok && data.message) setBanner("warn", text(data.message));
  }

  $("scan").addEventListener("click", doScan);

  $("clear").addEventListener("click", async function () {
    var res = await fetch("/api/devices/clear", { method: "POST" });
    applyState(await res.json());
  });

  $("rows").addEventListener("click", function (ev) {
    var tr = ev.target.closest("tr.dev");
    if (tr) select(tr.getAttribute("data-handle"));
  });

  loadInterfaces().then(load).catch(function (e) {
    setBanner("bad", "<b>Could not load.</b> " + text(e && e.message));
  });
})();
</script>
</body>
</html>
)PAGE";
}
