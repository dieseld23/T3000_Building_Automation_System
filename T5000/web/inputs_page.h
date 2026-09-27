#pragma once

// The Inputs page, embedded in the executable.
//
// Embedded rather than served from disk so the tool is a single file that works
// from wherever it is copied. The existing product already has a class of bug
// where a screen is blank because a ResourceFile folder did not travel with the
// binary; there is no reason to inherit it.

namespace t5000::web
{
    inline constexpr const char* kInputsPage = R"PAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Inputs</title>
<style>
  :root {
    --bg:#fff; --surface:#f7f8fa; --border:#e3e6ea; --text:#1a1d21; --dim:#6b7280;
    --accent:#0b6bcb; --row-alt:#fafbfc; --ok:#0f7b3f; --ok-bg:#e6f4ec;
    --warn:#8a5300; --warn-bg:#fdf1dc; --info:#0b4ea2; --info-bg:#e8f0fe;
    --changed-bg:#fff2c2;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg:#16181c; --surface:#1d2025; --border:#2c3036; --text:#e6e8ea; --dim:#9aa1ab;
      --accent:#4d9bf0; --row-alt:#191c20; --ok:#5bd18b; --ok-bg:#16301f;
      --warn:#e0b060; --warn-bg:#33270f; --info:#7fb4f0; --info-bg:#16283f;
      --changed-bg:#3a3012;
    }
  }
  *{box-sizing:border-box}
  [hidden]{display:none !important}
  html,body{height:100%}
  body{margin:0;background:var(--bg);color:var(--text);display:flex;flex-direction:column;
       font:13px/1.45 "Segoe UI Variable Text","Segoe UI",system-ui,sans-serif}

  header{display:flex;align-items:center;gap:14px;flex-wrap:wrap;padding:10px 16px;
         border-bottom:1px solid var(--border);background:var(--surface);flex:none}
  h1{font-size:14px;font-weight:600;margin:0}
  .meta{color:var(--dim);font-size:12px}
  .meta b{color:var(--text);font-weight:600;font-variant-numeric:tabular-nums}
  .spacer{flex:1}
  input[type=search]{font:inherit;color:inherit;background:var(--bg);
    border:1px solid var(--border);border-radius:6px;padding:5px 10px;min-width:180px}
  input[type=search]:focus{border-color:var(--accent);outline:none}
  button{font:inherit;color:var(--text);background:var(--bg);border:1px solid var(--border);
         border-radius:6px;padding:5px 12px;cursor:pointer}
  button:hover{border-color:var(--accent);color:var(--accent)}

  /* The read-path banner is not decoration. A grid that is empty because the
     firmware predates the PTP tunnel has to say so, in the place the reader is
     already looking. */
  .banner{flex:none;padding:9px 16px;border-bottom:1px solid var(--border);font-size:12px}
  .banner b{font-weight:600}
  .banner.info{background:var(--info-bg);color:var(--info)}
  .banner.warn{background:var(--warn-bg);color:var(--warn)}
  .banner.ok{background:var(--ok-bg);color:var(--ok)}

  .scroll{flex:1;overflow:auto}
  table{border-collapse:separate;border-spacing:0;width:100%}
  thead th{position:sticky;top:0;z-index:1;background:var(--surface);color:var(--dim);
    font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase;
    text-align:left;white-space:nowrap;padding:8px 10px;border-bottom:1px solid var(--border)}
  tbody td{padding:5px 10px;border-bottom:1px solid var(--border);white-space:nowrap}
  tbody tr:nth-child(even){background:var(--row-alt)}
  tbody tr:hover{background:var(--info-bg)}
  .num{text-align:right;font-variant-numeric:tabular-nums}
  .dim{color:var(--dim)}
  .pill{display:inline-block;padding:1px 7px;border-radius:10px;font-size:11px;font-weight:600}
  .pill-ok{background:var(--ok-bg);color:var(--ok)}
  .pill-warn{background:var(--warn-bg);color:var(--warn)}
  .pill-man{background:var(--info-bg);color:var(--info)}

  /* An input configured offline: the cells that can be changed, and the ones
     that have been, tinted as T3000 tints a changed cell
     (LIST_ITEM_CHANGED_BKCOLOR). */
  td.edit{cursor:pointer}
  td.edit:hover{outline:1px dashed var(--accent);outline-offset:-3px}
  tbody td.changed{background:var(--changed-bg)}
  td.edit input{font:inherit;color:var(--text);background:var(--bg);width:100%;min-width:64px;
    border:1px solid var(--accent);border-radius:4px;padding:1px 5px}
  button.undo{padding:1px 8px;font-size:11px}
  /* A calibration's sign, switched on its own as T3000's Sign column is. */
  td button.sign{padding:0 6px;margin-right:3px;font:inherit;line-height:1.3}

  /* The Range dialog: T3000's ranges for one input, numbered as T3000's own
     Range dialog numbers them. */
  dialog.ranges{padding:0;border:1px solid var(--border);border-radius:10px;background:var(--bg);
    color:var(--text);width:min(680px,calc(100vw - 32px));max-height:min(86vh,720px);
    box-shadow:0 12px 40px rgba(0,0,0,.35)}
  dialog.ranges::backdrop{background:rgba(0,0,0,.45)}
  dialog.ranges h2{margin:0;padding:14px 18px;font-size:14px;border-bottom:1px solid var(--border)}
  dialog.ranges .body{padding:12px 18px;display:grid;gap:12px}
  dialog.ranges .groups{display:grid;grid-template-columns:1fr 1.4fr;gap:12px}
  dialog.ranges .groups:has(> [hidden]){grid-template-columns:1fr}
  @media (max-width: 560px) { dialog.ranges .groups{grid-template-columns:1fr} }
  dialog.ranges h3{margin:0 0 4px;font-size:11px;font-weight:600;color:var(--dim);
    letter-spacing:.04em;text-transform:uppercase}
  dialog.ranges .choice{display:block;width:100%;text-align:left;border-color:transparent;
    padding:3px 8px;font-variant-numeric:tabular-nums}
  dialog.ranges .choice.current{border-color:var(--accent);color:var(--accent);font-weight:600}
  dialog.ranges .note{margin:0;font-size:11px;color:var(--dim)}
  dialog.ranges footer{display:flex;justify-content:flex-end;gap:8px;padding:10px 18px;
    border-top:1px solid var(--border);background:var(--surface)}
  dialog.ask{width:min(440px,calc(100vw - 32px))}
  dialog.ask p{margin:0}

  .empty{display:flex;align-items:center;justify-content:center;height:100%;
         color:var(--dim);text-align:center;padding:32px}
  .empty div{max-width:420px}
  .empty h2{font-size:14px;font-weight:600;color:var(--text);margin:0 0 6px}
  .empty p{margin:0 0 8px;font-size:12px}

  footer{flex:none;padding:6px 16px;border-top:1px solid var(--border);
         background:var(--surface);font-size:12px;color:var(--dim);min-height:28px}

  /* Connection settings, as an overlay rather than a second page - a
     technician checking a setting has not finished looking at the grid. */
  .scrim{position:fixed;inset:0;background:rgba(0,0,0,.45);display:flex;
         align-items:flex-start;justify-content:center;padding:40px 16px;z-index:10}
  .sheet{background:var(--bg);border:1px solid var(--border);border-radius:10px;
         width:100%;max-width:520px;max-height:100%;overflow:auto;
         box-shadow:0 12px 40px rgba(0,0,0,.35)}
  .sheet h2{margin:0;padding:14px 18px;font-size:14px;border-bottom:1px solid var(--border)}
  .sheet .rows{padding:14px 18px;display:grid;gap:12px}
  .sheet label{display:grid;gap:4px;font-size:12px;color:var(--dim)}
  .sheet input,.sheet select{font:inherit;color:var(--text);background:var(--bg);
    border:1px solid var(--border);border-radius:6px;padding:6px 9px;width:100%}
  .sheet input:focus,.sheet select:focus{border-color:var(--accent);outline:none}
  .sheet .err{color:var(--warn);font-size:11px}
  .sheet input.bad,.sheet select.bad{border-color:var(--warn)}
  .sheet footer{position:static;display:flex;gap:8px;justify-content:flex-end;
    padding:12px 18px;border-top:1px solid var(--border);background:var(--surface)}
  .sheet .path{font-size:11px;color:var(--dim);word-break:break-all;padding:0 18px 10px}
  .primary{border-color:var(--accent);color:var(--accent)}

  /* Tablet: the columns a technician needs in front of a unit stay; the rest
     fold away rather than shrinking the important ones into unreadability. */
  @media (max-width: 760px) {
    .opt{display:none}
    header{gap:8px}
    input[type=search]{min-width:120px;flex:1}

    /* Hiding columns is not enough on its own - the remaining five still
       overflowed at 375px and pushed the Status pill off the right edge, where
       "Decom" read as "Decom" with the m missing. Tighten the padding and let
       the label ellipsize instead of forcing the table wider than the screen. */
    thead th,tbody td{padding:5px 7px}
    tbody td:nth-child(2){max-width:38vw;overflow:hidden;text-overflow:ellipsis}
    table{table-layout:auto;width:100%}
  }
</style>
</head>
<body>

<header>
  <h1>Inputs</h1>
  <span class="meta">Device <b id="serial">&mdash;</b></span>
  <span class="meta"><b id="count">0</b> points</span>
  <span class="spacer"></span>
  <input type="search" id="filter" placeholder="Filter points" autocomplete="off">
  <button id="import" hidden title="Take this device's inputs from a .prog file T3000 saved from it">Import .prog</button>
  <input type="file" id="import-file" accept=".prog" hidden>
  <button id="settings">Connection</button>
  <button id="refresh">Refresh</button>
</header>

<div class="banner info" id="banner">Loading&hellip;</div>
<div class="banner" id="path-banner" hidden></div>
<div class="banner warn" id="edit-msg" role="alert" hidden></div>

<div class="scroll">
  <div class="empty" id="empty" hidden>
    <div>
      <h2 id="empty-title">No points to show</h2>
      <p id="empty-detail"></p>
    </div>
  </div>
  <table id="grid" hidden>
    <thead>
      <tr>
        <th>Input</th><th>Full Label</th><th class="num">Value</th><th>Units</th>
        <th>Auto/Man</th><th>Status</th><th class="opt">Range</th>
        <th class="num opt">Calibration</th><th class="num opt">Filter</th>
        <th class="opt">Signal Type</th><th class="opt">Label</th><th id="undo-head" class="opt" hidden></th>
      </tr>
    </thead>
    <tbody id="rows"></tbody>
  </table>
</div>

<div class="scrim" id="scrim" hidden>
  <div class="sheet">
    <h2>Connection</h2>
    <div class="rows">
      <label>Transport
        <select id="f-transport">
          <option value="bacnet-ip">BACnet/IP</option>
          <option value="bacnet-mstp">BACnet MSTP (serial)</option>
          <option value="modbus-tcp">Modbus TCP</option>
          <option value="modbus-rtu">Modbus RTU (serial)</option>
        </select>
      </label>
      <label data-for="net">Host or IP<input id="f-host" autocomplete="off" placeholder="192.168.1.50"></label>
      <label data-for="bacnet-ip">UDP port<input id="f-udpPort" type="number"></label>
      <label data-for="modbus-tcp">TCP port<input id="f-tcpPort" type="number"></label>
      <label data-for="serial">COM port<input id="f-comPort" type="number" min="1" max="255"></label>
      <label data-for="serial">Baud rate<select id="f-baud"></select></label>
      <label data-for="bacnet">BACnet device instance<input id="f-deviceInstance" type="number"></label>
      <label data-for="bacnet-mstp">MSTP max master<input id="f-mstpMaxMaster" type="number" min="1" max="127"></label>
      <label data-for="modbus">Modbus slave id<input id="f-modbusSlaveId" type="number" min="1" max="247"></label>
    </div>
    <div class="path" id="config-path"></div>
    <footer>
      <button id="cancel">Cancel</button>
      <button id="save" class="primary">Save</button>
    </footer>
  </div>
</div>

<dialog class="ranges" id="range-dlg" aria-labelledby="range-title">
  <h2 id="range-title">Range</h2>
  <div class="body">
    <div id="range-unused"></div>
    <div class="groups">
      <section id="range-digital-group"><h3>Digital</h3><div id="range-digital"></div></section>
      <section id="range-analog-group"><h3>Analog</h3><div id="range-analog"></div></section>
    </div>
    <p class="note" id="range-note"></p>
  </div>
  <footer><button type="button" id="range-cancel">Cancel</button></footer>
</dialog>

<dialog class="ranges ask" id="signal-dlg" aria-labelledby="signal-title">
  <h2 id="signal-title">Signal type</h2>
  <div class="body"><div id="signal-list"></div></div>
  <footer><button type="button" id="signal-cancel">Cancel</button></footer>
</dialog>

<dialog class="ranges ask" id="sign-dlg" aria-labelledby="sign-title">
  <h2 id="sign-title">Calibration sign</h2>
  <div class="body"><p id="sign-text"></p></div>
  <footer>
    <button type="button" id="sign-cancel">Cancel</button>
    <button type="button" class="primary" id="sign-ok">Change it</button>
  </footer>
</dialog>

<dialog class="ranges ask" id="import-dlg" aria-labelledby="import-title">
  <h2 id="import-title">Import</h2>
  <div class="body"><p id="import-text"></p></div>
  <footer>
    <button type="button" id="import-cancel">Cancel</button>
    <button type="button" class="primary" id="import-ok">Import</button>
  </footer>
</dialog>

<footer id="status">Read-only. Editing arrives once the write path is verified against hardware.</footer>

)PAGE"
        R"PAGE(<script>
  const $ = id => document.getElementById(id);
  const esc = s => String(s == null ? "" : s).replace(/[&<>"]/g,
    c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;" }[c]));

  let allRows = [];

  // The "offline" object of a configuration T5000 keeps for a device added
  // by hand, or null for points read from a device or the fixture.
  let offline = null;

  // True while a cell is being typed in. The grid is not rebuilt then: the
  // answer to a change made in another cell can arrive meanwhile, and
  // rebuilding would take away the cell and what is typed in it. It is
  // rebuilt, from the latest payload, when the cell closes.
  let editing = false;

  function showBanner(data) {
    const rp = data.readPath || {};

    // A device added by hand that has not been found. Nothing was read, and
    // the banners say so before anything else.
    if (data.offline) {
      const o = data.offline;
      $("banner").className = "banner " + (o.saving ? "info" : "warn");
      $("banner").innerHTML = "<b>Configured offline.</b> " + esc(o.note);

      const pb = $("path-banner");
      pb.hidden = false;
      pb.className = "banner ok";
      pb.innerHTML = "<b>Model " + esc(o.model) + ".</b> " + esc(rp.detail || "") + " "
        + esc(o.edited === 0 ? "No input has been changed."
                             : o.edited + (o.edited === 1 ? " input has" : " inputs have") + " been changed.")
        + esc(panelText(data.panel));
      return;
    }
    const fixture = !!(data.device && data.device.isFixture);

    // Set only for a device no scan has found since T5000 started: when it
    // was last seen, and that its address is the saved one, or that Find
    // found it at the address given. Said in both banners, as the reason a
    // read was refused or the proof it was not.
    const sighting = (data.device && data.device.sighting) ? " " + esc(data.device.sighting) : "";

    // A device that was FOUND but cannot be read is its own state, and the
    // most important one to get right. Without this branch the page fell
    // through to "Live device - points below were read from serial NNN" over
    // an empty grid, because the payload had no isFixture and a missing flag
    // reads as false. It claimed a reading that never happened.
    if (data.unavailable) {
      $("banner").className = "banner warn";
      $("banner").innerHTML = "<b>Nothing was read from this device.</b> "
        + esc(data.message || "T5000 cannot read it yet.") + sighting;

      const pb = $("path-banner");
      pb.hidden = false;
      pb.className = "banner warn";
      pb.innerHTML = "<b>Read path: " + esc(rp.summary || "none") + ".</b> "
                   + esc((data.device && data.device.address) || "");
      return;
    }

    // Registers means the struct read was refused - usually firmware. Say which,
    // rather than leaving a technician to guess at an empty or partial grid.
    const degraded = rp.path === "modbus-registers";

    // "Read from the device" is claimed on readFromWire === true and nothing
    // else. It used to be the fall-through for anything not marked fixture, so
    // a payload that left a flag out claimed a reading by default - which is
    // exactly how the unreadable-device payload came to say it had been read.
    // A positive claim now needs a positive statement from the server.
    const fromWire = !!(data.device && data.device.readFromWire === true);

    // Both bands, not one. An earlier version returned early on fixture data,
    // which meant the read-path explanation - the thing this page exists to
    // surface - was the one banner nobody ever saw while developing against the
    // fixture. Provenance and read path answer different questions.
    $("banner").className = "banner " + (fromWire ? "info" : "warn");
    if (fixture) {
      $("banner").innerHTML = "<b>Sample data.</b> No device is selected, so these are "
        + "fixture points for checking the layout. Nothing here came from hardware.";
    } else if (fromWire) {
      $("banner").innerHTML = "<b>Read from the device.</b> Serial "
        + esc(data.device.serialNumber) + " at " + esc(data.device.address || "unknown address")
        + ", " + esc(new Date().toLocaleTimeString()) + ". Values do not refresh on their own "
        + "- reload to read again." + sighting;
    } else {
      $("banner").innerHTML = "<b>Unconfirmed.</b> The server did not say these points came "
        + "from a device, so this page will not say so either.";
    }

    const p = $("path-banner");
    p.hidden = false;
    p.className = "banner " + (degraded ? "warn" : "ok");
    p.innerHTML = "<b>Read path: " + esc(rp.summary || "unknown") + ".</b> "
                + esc(rp.detail || "")
                + esc(panelText(data.panel));
  }

  // The panel as its settings describe it, then whatever the server says
  // about how the rows differ from T3000's because of it.
  function panelText(panel) {
    if (!panel) return "";
    let t = "";
    if (panel.known) {
      t += " Panel " + (panel.name ? '"' + panel.name + '", ' : "")
         + "number " + panel.number + ", model code " + panel.miniType
         + ", firmware " + panel.firmware + ".";
    }
    if (panel.note) t += " " + panel.note;
    return t;
  }

  // The cell's attributes: its classes, and in an offline configuration
  // whether it can be changed, as the input stands, and whether it has been.
  function cell(r, field, cls) {
    let c = cls || "";
    if (offline && offline.saving && r.editable && r.editable.includes(field)) c += " edit";
    if (r.changed && r.changed.includes(field)) c += " changed";
    return (c.trim() ? ' class="' + c.trim() + '"' : "") + ' data-f="' + field + '"';
  }

  // A calibration and its sign share a cell. Where the sign can be changed
  // it is a button of its own, as T3000's Sign column is a column of its own.
  function calibrationCell(r) {
    if (!r.calibration) return "";
    const sign = offline && offline.saving && r.editable && r.editable.includes("sign")
      ? '<button class="sign" data-sign title="Change the sign">' + esc(r.sign) + "</button>"
      : esc(r.sign);
    return sign + esc(r.calibration);
  }

  function undoCell(r) {
    if (!offline) return "";
    return '<td class="opt">' + (offline.saving && r.changed && r.changed.length
      ? '<button class="undo" data-undo="' + esc(r.index) + '" title="Put this input back as it started">Undo</button>'
      : "") + "</td>";
  }

  // ------------------------------------------------------- .prog import

  // The file goes to the server as it is, in base64; the server checks it
  // and says what an import would do, and nothing is saved until the
  // operator agrees.
  let importBody = null;

  function base64(buffer) {
    const bytes = new Uint8Array(buffer);
    let s = "";
    for (let i = 0; i < bytes.length; i += 0x8000)
      s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(s);
  }

  async function postImport(body) {
    const res = await fetch("/api/inputs/import", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });
    return res.json();
  }

  $("import").onclick = () => { $("import-file").value = ""; $("import-file").click(); };

  $("import-file").onchange = async () => {
    const f = $("import-file").files[0];
    if (!f || !offline) return;
    $("edit-msg").hidden = true;
    // Well past the largest (67184 bytes); the server says why a file
    // this side of it is refused.
    if (f.size > 180000) { refuse(f.name + " is too large to be a .prog file."); return; }
    try {
      const body = { handle: offline.handle, file: base64(await f.arrayBuffer()) };
      const data = await postImport(Object.assign({ check: true }, body));
      if (!data.ok) { refuse(data.message || "The file was refused."); return; }
      importBody = body;
      $("import-title").textContent = "Import " + f.name;
      $("import-text").textContent = data.message;
      $("import-dlg").showModal();
      $("import-cancel").focus();
    } catch (err) {
      refuse("The file could not be sent: " + err.message);
    }
  };

  $("import-ok").onclick = async () => {
    const body = importBody;
    $("import-dlg").close();
    if (!body) return;
    try {
      const data = await postImport(body);
      if (data.inputs) show(data.inputs);
      if (!data.ok) { refuse(data.message || "The file was refused."); return; }
      $("edit-msg").hidden = false;
      $("edit-msg").className = "banner ok";
      $("edit-msg").textContent = data.message;
    } catch (err) {
      refuse("The file could not be sent: " + err.message);
    }
  };
  $("import-cancel").onclick = () => $("import-dlg").close();
  $("import-dlg").addEventListener("close", () => { importBody = null; });

  function render() {
    if (editing) return;
    const needle = $("filter").value.trim().toLowerCase();
    const rows = needle
      ? allRows.filter(r => (r.fullLabel + " " + r.label + " " + r.input).toLowerCase().includes(needle))
      : allRows;

    $("count").textContent = rows.length;
    $("grid").hidden = rows.length === 0;
    $("empty").hidden = rows.length !== 0;

    if (rows.length === 0 && allRows.length > 0) {
      $("empty-title").textContent = "No points match that filter";
      $("empty-detail").textContent = "";
    }

    $("rows").innerHTML = rows.map(r => `
      <tr data-i="${esc(r.index)}">
        <td class="num">${esc(r.input)}</td>
        <td${cell(r, "fullLabel")}>${esc(r.fullLabel) || '<span class="dim">(unnamed)</span>'}</td>
        <td${cell(r, "value", "num")}>${esc(r.value)}${r.note
              ? ' <span class="pill pill-man" title="' + esc(r.note) + '">?</span>' : ''}</td>
        <td class="dim">${esc(r.units)}</td>
        <td${cell(r, "autoManual")}>${r.autoManual === "Manual"
              ? '<span class="pill pill-man">Manual</span>'
              : '<span class="dim">Auto</span>'}</td>
        <td${cell(r, "status")}>${r.alarm
              ? '<span class="pill pill-warn">' + esc(r.status) + '</span>'
              : '<span class="dim">' + esc(r.status) + '</span>'}</td>
)PAGE"
        R"PAGE(        <td${cell(r, "range", "dim opt")}>${esc(r.range)}</td>
        <td${cell(r, "calibration", "num opt")}>${calibrationCell(r)}</td>
        <td${cell(r, "filter", "num dim opt")}>${esc(r.filter)}</td>
        <td${cell(r, "signalType", "dim opt")}>${esc(r.signalType)}</td>
        <td${cell(r, "label", "dim opt")}>${esc(r.label)}</td>
        ${undoCell(r)}
      </tr>`).join("");
  }

  const READ_ONLY = "Read-only. Editing arrives once the write path is verified against hardware.";
  const KEPT = "Changes are kept in T5000's device list. Nothing is sent to any device.";
  const NOT_KEPT = "The device list is not being saved, so nothing here can be changed. Nothing is sent to any device.";

  // A payload, from a read or from a change: the grid and its banners.
  function show(data) {
    offline = data.offline || null;
    $("serial").textContent = data.device && data.device.serialNumber
      ? data.device.serialNumber : "—";

    allRows = data.inputs || [];
    showBanner(data);
    $("undo-head").hidden = !offline;
    $("import").hidden = !(offline && offline.saving);

    if (allRows.length === 0) {
      $("empty-title").textContent = data.unavailable
        ? "This device has not been read"
        : "No points returned";
      $("empty-detail").textContent =
        data.message || (data.readPath && data.readPath.detail) || "";
    }

    render();
    $("status").textContent = !offline ? READ_ONLY : offline.saving ? KEPT : NOT_KEPT;
  }

  async function load() {
    $("status").textContent = "Reading…";
    $("edit-msg").hidden = true;
    try {
      const res = await fetch("/api/inputs", { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);
      show(await res.json());
    } catch (err) {
      $("banner").className = "banner warn";
      $("banner").innerHTML = "<b>Could not reach the backend.</b> " + esc(err.message);
      $("status").textContent = "Not connected.";
    }
  }

  // ------------------------------------------------------ offline changes

  function refuse(message) {
    $("edit-msg").hidden = false;
    $("edit-msg").className = "banner warn";
    $("edit-msg").innerHTML = "<b>Not changed.</b> " + esc(message);
  }

  // A change or an undo. The answer carries the payload as it now is, so
  // the grid shows what was saved, not what was typed.
  async function send(url, body) {
    $("edit-msg").hidden = true;
    try {
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
      });
      const data = await res.json();
      if (data.inputs) show(data.inputs); else render();
      if (!data.ok) refuse(data.message || "The change was refused.");
    } catch (err) {
      render();
      refuse("The request failed: " + err.message);
    }
  }

  // Text is typed in place, as in T3000's grid: Enter or leaving the cell
  // saves it, Escape does not. The server holds it to T3000's rules.
  function openEditor(td, row, field) {
    // A calibration is typed with its sign, which follows what is typed.
    const was = field === "calibration" ? String(row.sign || "") + row.calibration
      : String(row[field] == null ? "" : row[field]);
    const input = document.createElement("input");
    input.value = was;
    input.maxLength = { fullLabel: 20, label: 8, filter: 3 }[field] || 16;
    input.setAttribute("aria-label", "Input " + row.input + " " + field);
    td.textContent = "";
    td.appendChild(input);
    editing = true;
    input.focus();
    input.select();

    let done = false;
    const finish = save => {
      if (done) return;
      done = true;
      editing = false;
      if (save && input.value !== was) {
        send("/api/inputs/edit", { handle: offline.handle, index: String(row.index), field: field, value: input.value });
      } else {
        render();
      }
    };
    input.addEventListener("keydown", e => {
      if (e.key === "Enter") { e.preventDefault(); finish(true); }
      else if (e.key === "Escape") { e.preventDefault(); finish(false); }
    });
    input.addEventListener("blur", () => finish(true));
  }

  $("rows").addEventListener("click", ev => {
    if (!offline || !offline.saving) return;

    const undo = ev.target.closest("button[data-undo]");
    if (undo) {
      send("/api/inputs/revert", { handle: offline.handle, index: undo.dataset.undo });
      return;
    }

    const sign = ev.target.closest("button[data-sign]");
    if (sign) {
      const signed = allRows.find(r => String(r.index) === sign.closest("tr").dataset.i);
      if (signed) askSign(signed);
      return;
    }

    const td = ev.target.closest("td.edit");
    if (!td || td.querySelector("input")) return;
    const index = td.parentElement.dataset.i;
    const row = allRows.find(r => String(r.index) === index);
    if (!row) return;

    // A digital input's state changes on a click, as in T3000.
    if (td.dataset.f === "value" && row.valueToggle) {
      send("/api/inputs/edit", { handle: offline.handle, index: index, field: "value", value: row.valueToggle });
      return;
    }
    if (td.dataset.f === "signalType") {
      openSignal(row);
      return;
    }

    // Auto/Manual changes on a click, as in T3000.
    if (td.dataset.f === "autoManual") {
      send("/api/inputs/edit", { handle: offline.handle, index: index, field: "autoManual",
                                 value: row.autoManual === "Manual" ? "Auto" : "Manual" });
      return;
    }
    if (td.dataset.f === "range") {
      openRange(row);
      return;
    }
    openEditor(td, row, td.dataset.f);
  });

  // The Range dialog lists the ranges T3000 offers this input, numbered as
  // T3000's Range dialog numbers them: 0 Unused, 1-22 digital, above 30
  // analog. Choosing one saves it.
  let rangeRow = null;

  function openRange(row) {
    const offered = new Set(row.ranges || []);
    const choices = (offline.rangeChoices || []).filter(c => offered.has(c.n));
    const button = c => '<button type="button" class="choice' + (c.n === row.rangeNumber ? ' current"'
        + ' aria-current="true' : '') + '" data-n="' + c.n + '">' + c.n + ". " + esc(c.name) + "</button>";
    const list = test => choices.filter(c => test(c.n)).map(button).join("");

    $("range-title").textContent = "Range of input " + row.input
      + (row.fullLabel ? " (" + row.fullLabel + ")" : "");
    $("range-unused").innerHTML = list(n => n === 0);
    $("range-digital").innerHTML = list(n => n > 0 && n <= 30);
    $("range-analog").innerHTML = list(n => n > 30);
    $("range-digital-group").hidden = !$("range-digital").innerHTML;
    $("range-analog-group").hidden = !$("range-analog").innerHTML;
    $("range-note").textContent = offline.rangeNote || "";

    rangeRow = row;
    $("range-dlg").showModal();
    const focus = $("range-dlg").querySelector(".choice.current") || $("range-dlg").querySelector(".choice");
    if (focus) focus.focus();
  }

  $("range-dlg").addEventListener("click", ev => {
    // A click on the backdrop lands on the dialog itself.
    if (ev.target === $("range-dlg")) { $("range-dlg").close(); return; }
    const b = ev.target.closest("button[data-n]");
    if (!b || !rangeRow) return;
    const n = Number(b.dataset.n);
    const row = rangeRow;
    $("range-dlg").close();
    if (n !== row.rangeNumber) {
      send("/api/inputs/edit", { handle: offline.handle, index: String(row.index), field: "range", value: String(n) });
    }
  });
  $("range-cancel").onclick = () => $("range-dlg").close();
  $("range-dlg").addEventListener("close", () => { rangeRow = null; });

  // Signal Type is chosen from T3000's list for it. Choosing the one shown
  // sends nothing: T3000 would store Thermistor Dry Contact as 4 where it
  // was 0, which looks the same.
  let signalRow = null;

  function openSignal(row) {
    $("signal-title").textContent = "Signal type of input " + row.input
      + (row.fullLabel ? " (" + row.fullLabel + ")" : "");
    $("signal-list").innerHTML = (offline.signalTypes || []).map((n, k) =>
      '<button type="button" class="choice' + (n === row.signalType ? ' current" aria-current="true' : '')
      + '" data-k="' + k + '">' + esc(n) + "</button>").join("");
    signalRow = row;
    $("signal-dlg").showModal();
    const focus = $("signal-dlg").querySelector(".choice.current") || $("signal-dlg").querySelector(".choice");
    if (focus) focus.focus();
  }

  $("signal-dlg").addEventListener("click", ev => {
    if (ev.target === $("signal-dlg")) { $("signal-dlg").close(); return; }
    const b = ev.target.closest("button[data-k]");
    if (!b || !signalRow) return;
    const name = (offline.signalTypes || [])[Number(b.dataset.k)];
    const row = signalRow;
    $("signal-dlg").close();
    if (name && name !== row.signalType) {
      send("/api/inputs/edit", { handle: offline.handle, index: String(row.index), field: "signalType", value: name });
    }
  });
  $("signal-cancel").onclick = () => $("signal-dlg").close();
  $("signal-dlg").addEventListener("close", () => { signalRow = null; });

  // The sign changes only once confirmed, as T3000 asks before it changes it.
  let signRow = null;

  function askSign(row) {
    const to = row.sign === "-" ? "+" : "-";
    $("sign-text").textContent = "This will change the calibration of input " + row.input + " from "
      + row.sign + row.calibration + " to " + to + row.calibration + ".";
    signRow = row;
    $("sign-dlg").showModal();
    $("sign-cancel").focus();
  }

  $("sign-ok").onclick = () => {
    const row = signRow;
    $("sign-dlg").close();
    if (row) {
      send("/api/inputs/edit", { handle: offline.handle, index: String(row.index), field: "sign",
                                 value: row.sign === "-" ? "+" : "-" });
    }
  };
  $("sign-cancel").onclick = () => $("sign-dlg").close();
  $("sign-dlg").addEventListener("click", ev => { if (ev.target === $("sign-dlg")) $("sign-dlg").close(); });
  $("sign-dlg").addEventListener("close", () => { signRow = null; });


  // ------------------------------------------------------------- connection

  const FIELDS = ["transport","host","udpPort","tcpPort","comPort","baud",
                  "deviceInstance","mstpMaxMaster","modbusSlaveId"];

  // Which fields each transport actually uses. Showing a COM port for BACnet/IP
  // invites someone to set it and wonder why nothing changed.
  function applyVisibility() {
    const t = $("f-transport").value;
    const serial = t === "bacnet-mstp" || t === "modbus-rtu";
    const groups = {
      "net": !serial,
      "serial": serial,
      "bacnet": t.startsWith("bacnet"),
      "bacnet-ip": t === "bacnet-ip",
      "bacnet-mstp": t === "bacnet-mstp",
      "modbus": t.startsWith("modbus"),
      "modbus-tcp": t === "modbus-tcp"
    };
    document.querySelectorAll("[data-for]").forEach(el => {
      el.hidden = !groups[el.dataset.for];
    });
  }

  function showErrors(errors) {
    document.querySelectorAll(".sheet .err").forEach(e => e.remove());
    document.querySelectorAll(".sheet .bad").forEach(e => e.classList.remove("bad"));

    let unattached = [];
    (errors || []).forEach(e => {
      const input = $("f-" + e.field);
      if (!input) { unattached.push(e.message); return; }
      input.classList.add("bad");
      const note = document.createElement("span");
      note.className = "err";
      note.textContent = e.message;
      input.parentElement.appendChild(note);
    });

    if (unattached.length) {
      const note = document.createElement("div");
      note.className = "err";
      note.style.padding = "0 18px 8px";
      note.textContent = unattached.join(" ");
      $("config-path").parentElement.insertBefore(note, $("config-path"));
    }
  }

  async function openSettings() {
    const res = await fetch("/api/connection", { cache: "no-store" });
    const data = await res.json();

    const baud = $("f-baud");
    baud.innerHTML = (data.baudRates || []).map(r =>
      `<option value="${r}">${r}</option>`).join("");

    FIELDS.forEach(f => { const el = $("f-" + f); if (el) el.value = data.connection[f]; });
    $("config-path").textContent = "Saved to " + data.configPath;

    applyVisibility();
    showErrors(data.errors);   // surface existing problems before a submit
    $("scrim").hidden = false;
  }

  async function saveSettings() {
    const payload = {};
    FIELDS.forEach(f => {
      const el = $("f-" + f);
      if (!el) return;
      payload[f] = (el.type === "number") ? Number(el.value) : el.value;
    });

    const res = await fetch("/api/connection", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const data = await res.json();

    if (!data.ok) { showErrors(data.errors); return; }

    showErrors([]);
    $("scrim").hidden = true;
    $("status").textContent = "Connection settings saved to " + data.savedTo;
  }

  $("settings").onclick = openSettings;
  $("cancel").onclick = () => { $("scrim").hidden = true; };
  $("save").onclick = saveSettings;
  $("f-transport").onchange = applyVisibility;
  $("scrim").onclick = e => { if (e.target === $("scrim")) $("scrim").hidden = true; };
  document.addEventListener("keydown", e => {
    if (e.key === "Escape" && !$("scrim").hidden) $("scrim").hidden = true;
  });

  $("refresh").onclick = load;
  $("filter").oninput = render;
  load();
</script>
</body>
</html>
)PAGE";
}
