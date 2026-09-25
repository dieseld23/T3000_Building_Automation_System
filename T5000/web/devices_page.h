#pragma once

// The device list - the front door of the tool.
//
// Embedded rather than served from disk so the tool is a single file that
// works from wherever it is copied, for the same reason as inputs_page.h.
//
// Three things on this page are load-bearing and easy to get wrong:
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
//
//   3. The list is saved, so a device on it is not necessarily there now.
//      Whether each one answered the last scan comes from the server, per
//      device. The page used to subtract the number of responses from the
//      length of the list, which stopped meaning anything once the list held
//      devices from other buildings and other days.
//
// Text from a device or typed by the operator goes onto the page through
// textContent, or through esc() where a banner is built as HTML.

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
  select,input[type=number],input[type=text]{font:inherit;color:inherit;background:var(--bg);
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

  /* A building, floor and room, over the devices in it. Only drawn once at
     least one device has been given a location; until then the list is flat. */
  tr.grp td{background:var(--surface);color:var(--dim);font-size:11px;font-weight:600;
            letter-spacing:.03em;padding:12px 10px 4px;cursor:default}
  tr.grp td b{color:var(--text);font-size:12px;letter-spacing:0}

  td.act{text-align:right;padding:3px 8px}
  td.act button{padding:2px 9px;font-size:11px;margin-left:4px}

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

  dialog{border:1px solid var(--border);border-radius:10px;background:var(--bg);color:var(--text);
         padding:0;width:min(420px,calc(100vw - 32px))}
  dialog::backdrop{background:rgba(0,0,0,.35)}
  dialog form{padding:16px 18px}
  dialog h2{font-size:14px;font-weight:600;margin:0 0 2px}
  dialog label{display:block;font-size:12px;color:var(--dim);margin:10px 0 0}
  dialog input[type=text],dialog select{display:block;width:100%;margin-top:3px}
  dialog .lead{font-size:12px;color:var(--dim);margin:4px 0 0}
  dialog .note{font-size:11px;color:var(--dim);margin:14px 0 0}
  dialog .err{font-size:12px;color:var(--bad);margin:8px 0 0}
  dialog .buttons{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}

  footer{flex:none;padding:6px 16px;border-top:1px solid var(--border);
         background:var(--surface);color:var(--dim);font-size:11px;
         display:flex;gap:14px;flex-wrap:wrap}
</style>
</head>
<body>

<header>
  <h1>Devices</h1>
  <span class="meta"><b id="count">0</b> listed</span>
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
  <button id="add" type="button">Add device&hellip;</button>
  <button id="clear" type="button">Forget all&hellip;</button>
  <button id="scan" type="button" class="primary">Scan</button>
</header>

<div class="banner info" id="banner">Loading&hellip;</div>
<div class="banner warn" id="store-banner" hidden></div>

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
        <th>Name</th>
        <th>Product</th>
        <th>Panel</th>
        <th>Address</th>
        <th class="num">Firmware</th>
        <th>Seen</th>
        <th>State</th>
        <th></th>
      </tr>
    </thead>
    <tbody id="rows"></tbody>
  </table>
</div>

<dialog id="edit">
  <form id="edit-form">
    <h2>Name and location</h2>
    <div class="dim" id="edit-which"></div>
    <label>Name <input type="text" id="f-name" maxlength="60" autocomplete="off"></label>
    <label>Building <input type="text" id="f-building" maxlength="60" list="dl-building" autocomplete="off"></label>
    <label>Floor <input type="text" id="f-floor" maxlength="60" list="dl-floor" autocomplete="off"></label>
    <label>Room <input type="text" id="f-room" maxlength="60" list="dl-room" autocomplete="off"></label>
    <datalist id="dl-building"></datalist>
    <datalist id="dl-floor"></datalist>
    <datalist id="dl-room"></datalist>
    <p class="note">Kept in T5000's device list only. Nothing is sent to the device.</p>
    <p class="err" id="edit-error" hidden></p>
    <div class="buttons">
      <button type="button" id="edit-cancel">Cancel</button>
      <button type="submit" id="edit-save" class="primary">Save</button>
    </div>
  </form>
</dialog>

<dialog id="add-dialog">
  <form id="add-form">
    <h2>Add a device by hand</h2>
    <p class="lead">For a device no scan has found: one on a network you are not on, or not
      installed yet. It is listed and saved, and can be named and placed.</p>
    <label>Product <select id="a-product"></select></label>
    <label>Serial number <input type="text" id="a-serial" inputmode="numeric" autocomplete="off"></label>
    <label>Name <input type="text" id="a-name" maxlength="60" autocomplete="off"></label>
    <label>Building <input type="text" id="a-building" maxlength="60" list="dl-building" autocomplete="off"></label>
    <label>Floor <input type="text" id="a-floor" maxlength="60" list="dl-floor" autocomplete="off"></label>
    <label>Room <input type="text" id="a-room" maxlength="60" list="dl-room" autocomplete="off"></label>
    <p class="note">The serial is how a scan recognises the device later, so use the one on its
      label. Nothing is sent to it until a scan finds a device with that serial; that device
      then takes this entry's place, with the name and location given here.</p>
    <p class="err" id="add-error" hidden></p>
    <div class="buttons">
      <button type="button" id="add-cancel">Cancel</button>
      <button type="submit" id="add-save" class="primary">Add</button>
    </div>
  </form>
</dialog>

<footer>
  <span id="readonly-note">Scanning is read-only. No register is written.</span>
  <span id="saved-note"></span>
  <span class="spacer"></span>
  <span><a href="/inputs">Inputs</a></span>
</footer>
)PAGE"
        R"PAGE(
<script>
(function () {
  "use strict";

  var state = null;
  var scanning = false;
  var editing = null;
  var COLUMNS = 9;

  function $(id) { return document.getElementById(id); }
  function text(s) { return s === null || s === undefined ? "" : String(s); }

  // For the few places a banner is built as HTML. Everything else goes in
  // through textContent.
  function esc(s) {
    return text(s).replace(/[&<>"']/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[c];
    });
  }

  function el(tag, cls, content) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (content !== undefined) n.textContent = text(content);
    return n;
  }

  function plural(n, one, many) { return n + " " + (n === 1 ? one : many); }

  // An entry the operator typed in that no scan has found. The server says
  // so through the provenance; once a scan finds the serial it changes.
  var BY_HAND = "added by hand";
  function byHand(d) { return d.provenance === BY_HAND; }

  function setBanner(kind, html) {
    var b = $("banner");
    b.className = "banner " + kind;
    b.innerHTML = html;
    b.hidden = false;
  }

  function findDevice(handle) {
    for (var i = 0; i < state.devices.length; i++)
      if (state.devices[i].handle === handle) return state.devices[i];
    return null;
  }

  // How a device is named in a question about it: the operator's name, then
  // the panel's own, and always the serial, which is the one thing that
  // cannot be two devices at once.
  function describe(d) {
    var name = (d.placement && d.placement.name) || d.panelName;
    var id = d.hasStableIdentity ? "serial " + d.serialNumber : "the device with no serial number";
    return name ? "\"" + name + "\" (" + id + ")" : id;
  }

  function pad(n) { return (n < 10 ? "0" : "") + n; }

  function when(seconds) { return new Date(seconds * 1000).toLocaleString(); }

  function ago(seconds) {
    var d = Date.now() / 1000 - seconds;
    if (d < 60) return "just now";
    if (d < 3600) return Math.floor(d / 60) + " min ago";
    if (d < 86400) return Math.floor(d / 3600) + " h ago";
    if (d < 7 * 86400) return plural(Math.floor(d / 86400), "day", "days") + " ago";
    var t = new Date(seconds * 1000);
    return t.getFullYear() + "-" + pad(t.getMonth() + 1) + "-" + pad(t.getDate());
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

  function repairRow(repair) {
    var tr = el("tr", "rep");
    var td = document.createElement("td");
    td.colSpan = COLUMNS;

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
)PAGE"
        R"PAGE(
  // Devices under their building, floor and room, in that order, with the
  // ones not placed yet last. Within a place, the order they were found.
  // One group with no heading when nothing has been placed at all.
  function groups(devices) {
    function where(d) {
      var p = d.placement || {};
      return [p.building || "", p.floor || "", p.room || ""];
    }
    var any = devices.some(function (d) { return where(d).join("") !== ""; });
    if (!any) return [{ label: null, devices: devices }];

    var byKey = {}, list = [];
    devices.forEach(function (d) {
      var w = where(d), key = JSON.stringify(w);
      if (!byKey[key]) {
        byKey[key] = { where: w, devices: [] };
        list.push(byKey[key]);
      }
      byKey[key].devices.push(d);
    });

    function cmp(a, b) {
      return a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" });
    }
    list.sort(function (a, b) {
      var ua = a.where.join("") === "", ub = b.where.join("") === "";
      if (ua !== ub) return ua ? 1 : -1;
      for (var i = 0; i < 3; i++) {
        var c = cmp(a.where[i], b.where[i]);
        if (c) return c;
      }
      return 0;
    });

    return list.map(function (g) {
      var parts = g.where.filter(function (x) { return x; });
      return { label: parts.length ? parts : null, devices: g.devices };
    });
  }

  function groupRow(label, count) {
    var tr = el("tr", "grp");
    var td = document.createElement("td");
    td.colSpan = COLUMNS;
    if (label) {
      label.forEach(function (part, i) {
        if (i) td.appendChild(document.createTextNode("  ›  "));
        td.appendChild(el("b", null, part));
      });
    } else {
      td.appendChild(el("b", null, "Not placed"));
    }
    td.appendChild(document.createTextNode("  ·  " + plural(count, "device", "devices")));
    tr.appendChild(td);
    return tr;
  }

  function seenCell(d) {
    var td = document.createElement("td");
    if (byHand(d)) {
      td.appendChild(el("span", "pill pill-info", BY_HAND));
      td.title = "Added by hand. No scan has found a device with this serial, so nothing has " +
                 "been read from it and nothing is sent to it.";
    } else if (d.answeredLastScan) {
      td.appendChild(el("span", "pill pill-ok", "answered"));
      td.title = "Answered the last scan (" + d.provenance + ").";
    } else if (d.seenThisSession) {
      td.appendChild(el("span", "pill pill-warn", "not in last scan"));
      td.title = "Answered an earlier scan since T5000 started, but not the last one. " +
                 "Last seen " + when(d.lastSeen) + ".";
    } else if (d.lastSeen > 0) {
      td.className = "dim";
      td.textContent = ago(d.lastSeen);
      td.title = "From the saved list, and not seen since T5000 started. " +
                 "Last answered a scan " + when(d.lastSeen) + ".";
    } else {
      td.className = "dim";
      td.textContent = "never";
    }
    return td;
  }

  function actionsCell(d) {
    var td = el("td", "act");

    var edit = el("button", null, "Edit");
    edit.type = "button";
    edit.setAttribute("data-act", "edit");
    if (!d.hasStableIdentity) {
      edit.disabled = true;
      edit.title = "A device with no serial number cannot be saved, so it cannot be named.";
    } else if (!state.store.saving) {
      edit.disabled = true;
      edit.title = "The list is not being saved, so a name would be lost when T5000 closes.";
    } else {
      edit.title = "Name this device and say where it is. Kept in this list only.";
    }
    td.appendChild(edit);

    var forget = el("button", null, "Forget");
    forget.type = "button";
    forget.setAttribute("data-act", "forget");
    forget.title = "Take it off the list. Nothing is sent to the device.";
    td.appendChild(forget);
    return td;
  }

  function deviceRow(d) {
    var tr = el("tr", "dev" + (d.selected ? " sel" : ""));
    tr.setAttribute("data-handle", d.handle);

    var serial = el("td", "num");
    if (d.hasStableIdentity) {
      serial.textContent = d.serialNumber;
    } else {
      serial.appendChild(el("span", "pill pill-bad", "no serial"));
    }
    tr.appendChild(serial);

    // The operator's name when there is one, otherwise the panel's own,
    // dimmed, so it is clear which of the two is showing.
    var name = document.createElement("td");
    var given = d.placement && d.placement.name;
    if (given) {
      name.textContent = given;
      if (d.panelName) name.title = "The panel calls itself \"" + d.panelName + "\".";
    } else if (d.panelName) {
      name.className = "dim";
      name.textContent = d.panelName;
      name.title = "The name the panel gives itself.";
    } else {
      name.className = "dim";
      name.textContent = "—";
    }
    tr.appendChild(name);

    tr.appendChild(el("td", null, d.productName));

    // The panel type is read from the device, so an entry added by hand has
    // none to show until a scan finds it, whatever the product would allow.
    var panel = el("td", d.panel.resolved && !byHand(d) ? null : "dim");
    panel.textContent = byHand(d) ? "—" : d.panel.resolved ? d.panel.name : "unknown";
    panel.title = byHand(d) ? "Read from the device once a scan finds it." : d.panel.reason;
    tr.appendChild(panel);

    // The address shown is the one the device answered from, which is the
    // one T5000 contacts. When the device describes itself differently,
    // say so, rather than leave the operator to wonder which is in use.
    var addr = el("td", null, d.address);
    if (!d.address) {
      addr.className = "dim";
      addr.textContent = "—";
      if (byHand(d)) addr.title = "Not known until a scan finds it.";
    }
    if (d.addressMismatch) {
      addr.appendChild(document.createTextNode(" "));
      var differs = el("span", "pill pill-warn", "reports " + d.reportedIp);
      differs.title = "This device answered from " + d.answeredFrom + " but says its address is " +
        d.reportedIp + ". T5000 contacts " + d.answeredFrom + ", the address the answer came " +
        "from, as T3000 does. The device may be behind NAT or have a second network interface.";
      addr.appendChild(differs);
    }
    tr.appendChild(addr);
    tr.appendChild(el("td", "num", d.firmware || ""));
    tr.appendChild(seenCell(d));

    var st = document.createElement("td");
    if (d.needsAttention) {
      st.appendChild(el("span", "pill pill-warn", "needs attention"));
    } else if (d.support === "verified") {
      st.appendChild(el("span", "pill pill-ok", "supported"));
    } else {
      st.appendChild(el("span", "pill pill-info", d.support));
    }
    tr.appendChild(st);

    tr.appendChild(actionsCell(d));
    return tr;
  }

  function render() {
    var devices = state.devices;
    $("count").textContent = devices.length;
    $("clear").disabled = devices.length === 0;

    var problems = $("problems");
    if (state.pendingRepairs > 0) {
      problems.hidden = false;
      problems.innerHTML = "<b>" + state.pendingRepairs + "</b> problem" +
        (state.pendingRepairs === 1 ? "" : "s") + " found";
    } else {
      problems.hidden = true;
    }

    renderStore();

    if (!devices.length) {
      showEmpty(state.scan, state.scan.stats);
      renderBanner();
      return;
    }

    $("empty").hidden = true;
    $("grid").hidden = false;

    var rows = $("rows");
    rows.innerHTML = "";

    groups(devices).forEach(function (g, i, all) {
      if (g.label || all.length > 1) rows.appendChild(groupRow(g.label, g.devices.length));
      g.devices.forEach(function (d) {
        rows.appendChild(deviceRow(d));
        d.repairs.forEach(function (r) { rows.appendChild(repairRow(r)); });
      });
    });

    renderBanner();
  }
)PAGE"
        R"PAGE(
  function renderBanner() {
    var s = state.scan, st = s.stats;
    var devices = state.devices;

    if (s.error) {
      setBanner("bad", "<b>Scan problem.</b> " + esc(s.error));
      return;
    }
    if (!s.hasScanned) {
      // Devices added by hand were never there, as far as T5000 knows, so
      // they are counted apart from the ones a scan found last time.
      var typed = devices.filter(byHand).length;
      var found = devices.length - typed;
      var more = typed
        ? "<b>" + typed + "</b> " + (typed === 1 ? "device was" : "devices were") +
          " added by hand, which no scan has found yet."
        : "";
      if (found) {
        setBanner("info",
          "<b>" + found + "</b> " + (found === 1 ? "device" : "devices") +
          " from the saved list. Nothing has been scanned since T5000 started, so " +
          (found === 1 ? "this is the device that was" : "these are the devices that were") +
          " there last time - press Scan to see which are there now. " +
          (more ? more + " " : "") + "Scanning does not write to any device.");
      } else if (typed) {
        setBanner("info", more + " Press Scan to look for them. Scanning does not write to any device.");
      } else {
        setBanner("info",
          "Nothing has been scanned yet. Scanning sends one broadcast and listens - " +
          "it does not write to any device.");
      }
      return;
    }

    var answered = devices.filter(function (d) { return d.answeredLastScan; }).length;
    var parts = [];
    parts.push("<b>" + answered + "</b> answered the last scan");
    if (st.inBootloader > 0) parts.push("<b>" + st.inBootloader + "</b> in bootloader");
    if (st.withoutSerial > 0) parts.push("<b>" + st.withoutSerial + "</b> with no serial");
    if (st.duplicateModbusIds > 0) parts.push("<b>" + st.duplicateModbusIds + "</b> with a duplicate Modbus id");
    if (st.malformed > 0) parts.push("<b>" + st.malformed + "</b> unreadable");

    // The list is cumulative and the scan is not. A scan that finds nothing
    // used to print "0 answered" directly above a list of three devices - a
    // screen contradicting itself. Devices are kept across scans and across
    // runs on purpose, so the gap is named rather than hidden. It is not a
    // warning: with a saved list, devices from another building not answering
    // here is the normal case.
    var quiet = devices.length - answered;
    if (quiet > 0) parts.push("<b>" + quiet + "</b> listed that did not answer it");

    var kind = (st.malformed > 0 || state.pendingRepairs > 0) ? "warn" : "ok";
    setBanner(kind, parts.join(" &middot; ") + " &middot; nothing was written to any device");
  }

  // Whether the list is being saved. A list that has quietly stopped being
  // saved loses every name typed into it when T5000 closes, so this is shown
  // on the page and not only printed in the console.
  function renderStore() {
    var s = state.store, b = $("store-banner");
    var file = text(s.path).split(/[\\/]/).pop();

    if (!s.saving) {
      b.innerHTML = "<b>The device list is not being saved.</b> " + esc(s.path) +
        " could not be used: " + esc(s.error) + ". Devices are listed until T5000 " +
        "closes, and cannot be named.";
      b.hidden = false;
    } else if (s.error) {
      b.innerHTML = "<b>Not saved.</b> " + esc(s.error.charAt(0).toUpperCase() + s.error.slice(1)) + ".";
      b.hidden = false;
    } else {
      b.hidden = true;
    }

    var note = $("saved-note");
    note.textContent = s.saving ? "The list is kept in " + file + "." : "The list is not being saved.";
    note.title = text(s.path);
  }

  function applyState(next) {
    state = next;
    $("readonly-note").textContent = state.readOnly
      ? "Scanning is read-only. No register is written."
      : "WARNING: this build reports that scanning is not read-only.";
    render();
  }

  // Never throws. A request that could not be made at all - T5000 closed,
  // say - comes back as a refusal with the reason, like any other, so no
  // button can be pressed with nothing happening on the page.
  async function post(url, body) {
    var res;
    try {
      res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
      });
    } catch (e) {
      return { ok: false, message: "T5000 could not be reached. Is it still running? (" + text(e && e.message) + ")" };
    }
    // JSON from every route; plain text when the server refused the request
    // before any route ran, and that text is the reason.
    var raw = "";
    try { raw = await res.text(); } catch (e) { /* reported below */ }
    try { return JSON.parse(raw); } catch (e) { /* not JSON */ }
    return { ok: false, message: raw || ("The server answered " + res.status + " with nothing readable.") };
  }

  // What every list action does with its answer: show the list as it now
  // is, then say why the action did not happen, if it did not.
  function settle(data, what) {
    if (data.state) applyState(data.state);
    if (!data.ok) setBanner("bad", "<b>" + esc(what) + "</b> " + esc(data.message || "The request failed."));
  }

  async function loadInterfaces() {
    try {
      var res = await fetch("/api/interfaces", { cache: "no-store" });
      var data = await res.json();
      var sel = $("iface");
      (data.interfaces || []).forEach(function (n) {
        var o = document.createElement("option");
        o.value = n.ip;
        var suffix = n.isLoopback ? " - loopback, only for devices on this computer"
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
      setBanner("bad", "<b>The scan request failed.</b> " + esc(e && e.message));
    } finally {
      scanning = false;
      $("scan").disabled = false;
      $("scan").textContent = "Scan";
    }
  }

  async function select(handle) {
    var data = await post("/api/devices/select", { handle: handle });
    if (data.state) applyState(data.state);
    if (!data.ok) setBanner("warn", esc(data.message || "The device could not be selected."));
  }

  async function forgetOne(handle) {
    var d = findDevice(handle);
    if (!d) return;

    var saved = d.hasStableIdentity && state.store.saving;
    var question = byHand(d)
      ? "Forget " + describe(d) + "?\n\nIt was added by hand and no scan has found it, so it " +
        "is taken off the list and out of the saved file for good, with its name and " +
        "location. Nothing is sent to any device."
      : saved
      ? "Forget " + describe(d) + "?\n\nIt is taken off the list and out of the saved file, " +
        "with any name and location given to it. Nothing is sent to the device. If it " +
        "answers a later scan it is listed again."
      : "Take " + describe(d) + " off the list?\n\nIt is not in the saved file, so only the " +
        "list changes. Nothing is sent to the device. If it answers a later scan it is " +
        "listed again.";
    if (!confirm(question)) return;

    settle(await post("/api/devices/forget", { handle: handle }), "Not forgotten.");
  }

  async function forgetAll() {
    var n = state.devices.length;
    if (!n) return;

    var typed = state.devices.filter(byHand).length;
    var question = state.store.saving
      ? "Forget all " + plural(n, "device", "devices") + "?\n\nThey are taken off the list and " +
        "out of the saved file, with every name and location given to them. Nothing is sent " +
        "to any device. Devices that answer a later scan are listed again" +
        (typed ? "; the " + plural(typed, "device", "devices") + " added by hand are not." : ".")
      : "Clear all " + plural(n, "device", "devices") + " from the list?\n\nThe list is not " +
        "being saved, so nothing on disk changes. Nothing is sent to any device.";
    if (!confirm(question)) return;

    settle(await post("/api/devices/clear", { confirm: true }), "Not forgotten.");
  }
)PAGE"
        R"PAGE(
  // Suggestions for building, floor and room from what is already in the
  // list, so "North" and "north " do not become two buildings by accident.
  function fillSuggestions() {
    ["building", "floor", "room"].forEach(function (field) {
      var seen = {}, list = $("dl-" + field);
      list.innerHTML = "";
      state.devices.forEach(function (d) {
        var v = d.placement && d.placement[field];
        if (v && !seen[v]) {
          seen[v] = true;
          var o = document.createElement("option");
          o.value = v;
          list.appendChild(o);
        }
      });
    });
  }

  function openEdit(handle) {
    var d = findDevice(handle);
    if (!d) return;

    editing = handle;
    var p = d.placement || {};
    $("edit-which").textContent = describe(d) + ", " + d.productName;
    $("f-name").value = p.name || "";
    $("f-name").placeholder = d.panelName || "";
    $("f-building").value = p.building || "";
    $("f-floor").value = p.floor || "";
    $("f-room").value = p.room || "";
    $("edit-error").hidden = true;
    $("edit-save").disabled = false;
    fillSuggestions();
    $("edit").showModal();
    $("f-name").focus();
  }

  $("edit-form").addEventListener("submit", async function (ev) {
    ev.preventDefault();
    $("edit-save").disabled = true;
    try {
      var data = await post("/api/devices/placement", {
        handle: editing,
        name: $("f-name").value,
        building: $("f-building").value,
        floor: $("f-floor").value,
        room: $("f-room").value
      });
      if (data.state) applyState(data.state);
      if (data.ok) {
        $("edit").close();
        return;
      }
      $("edit-error").textContent = data.message || "The request failed.";
      $("edit-error").hidden = false;
    } catch (e) {
      $("edit-error").textContent = "The request failed. " + text(e && e.message);
      $("edit-error").hidden = false;
    } finally {
      $("edit-save").disabled = false;
    }
  });

  $("edit-cancel").addEventListener("click", function () { $("edit").close(); });

  // The products a device can be added as: the ones T5000 has been taught
  // about, from the server, so the page and the check on the server cannot
  // disagree about which those are. Fetched when the dialog first opens.
  var products = null;

  async function loadProducts() {
    if (products) return true;
    try {
      var res = await fetch("/api/products", { cache: "no-store" });
      var data = await res.json();
      // Not a third-party device: a scan never reports one's serial, so an
      // entry for one could never be matched. The server refuses it too.
      products = (data.products || []).filter(function (p) {
        return p.id !== 254;
      }).sort(function (a, b) {
        return a.name.localeCompare(b.name, undefined, { numeric: true });
      });
    } catch (e) {
      return false;
    }
    var sel = $("a-product");
    sel.innerHTML = "";
    var pick = el("option", null, "Choose a product");
    pick.value = "";
    sel.appendChild(pick);
    products.forEach(function (p) {
      var o = el("option", null, p.name + " (product " + p.id + ")");
      o.value = String(p.id);
      sel.appendChild(o);
    });
    return true;
  }

  async function openAdd() {
    if (!state.store.saving) {
      setBanner("bad", "<b>Not added.</b> The list is not being saved, so a device added now " +
        "would be lost when T5000 closes.");
      return;
    }
    ["a-serial", "a-name", "a-building", "a-floor", "a-room"].forEach(function (id) { $(id).value = ""; });
    $("add-error").hidden = true;
    $("add-save").disabled = false;
    if (!(await loadProducts())) {
      setBanner("bad", "<b>Not added.</b> The product list could not be loaded from T5000.");
      return;
    }
    $("a-product").value = "";
    fillSuggestions();
    $("add-dialog").showModal();
    $("a-product").focus();
  }

  $("add-form").addEventListener("submit", async function (ev) {
    ev.preventDefault();
    $("add-save").disabled = true;
    try {
      // The serial goes as the text typed. The server reads it as a whole
      // number and says what is wrong with it, rather than the page
      // guessing what "12,345" meant.
      var data = await post("/api/devices/add", {
        productId: $("a-product").value,
        serialNumber: $("a-serial").value.trim(),
        name: $("a-name").value,
        building: $("a-building").value,
        floor: $("a-floor").value,
        room: $("a-room").value
      });
      if (data.state) applyState(data.state);
      if (data.ok) {
        $("add-dialog").close();
        return;
      }
      $("add-error").textContent = data.message || "The request failed.";
      $("add-error").hidden = false;
    } finally {
      $("add-save").disabled = false;
    }
  });

  $("add-cancel").addEventListener("click", function () { $("add-dialog").close(); });
  $("add").addEventListener("click", openAdd);

  $("scan").addEventListener("click", doScan);
  $("clear").addEventListener("click", forgetAll);

  $("rows").addEventListener("click", function (ev) {
    var tr = ev.target.closest("tr.dev");
    if (!tr) return;
    var handle = tr.getAttribute("data-handle");

    // A button in the row acts on the device without selecting it.
    var button = ev.target.closest("button");
    if (button) {
      var act = button.getAttribute("data-act");
      if (act === "edit") openEdit(handle);
      else if (act === "forget") forgetOne(handle);
      return;
    }
    select(handle);
  });

  loadInterfaces().then(load).catch(function (e) {
    setBanner("bad", "<b>Could not load.</b> " + esc(e && e.message));
  });
})();
</script>
</body>
</html>
)PAGE";
}
