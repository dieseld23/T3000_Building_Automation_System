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
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg:#16181c; --surface:#1d2025; --border:#2c3036; --text:#e6e8ea; --dim:#9aa1ab;
      --accent:#4d9bf0; --row-alt:#191c20; --ok:#5bd18b; --ok-bg:#16301f;
      --warn:#e0b060; --warn-bg:#33270f; --info:#7fb4f0; --info-bg:#16283f;
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
  <button id="settings">Connection</button>
  <button id="refresh">Refresh</button>
</header>

<div class="banner info" id="banner">Loading&hellip;</div>
<div class="banner" id="path-banner" hidden></div>

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
        <th>Input</th><th>Full Label</th><th class="num">Value</th>
        <th>Auto/Man</th><th>Status</th><th class="opt">Signal</th>
        <th class="num opt">Range</th><th class="num opt">Calibration</th>
        <th class="num opt">Filter</th><th class="num opt">Panel</th><th class="opt">Label</th>
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

<footer id="status">Read-only. Editing arrives once the write path is verified against hardware.</footer>

<script>
  const $ = id => document.getElementById(id);
  const esc = s => String(s == null ? "" : s).replace(/[&<>"]/g,
    c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;" }[c]));

  let allRows = [];

  function showBanner(data) {
    const rp = data.readPath || {};
    const fixture = !!(data.device && data.device.isFixture);

    // Registers means the struct read was refused - usually firmware. Say which,
    // rather than leaving a technician to guess at an empty or partial grid.
    const degraded = rp.path === "modbus-registers";

    // Both bands, not one. An earlier version returned early on fixture data,
    // which meant the read-path explanation - the thing this page exists to
    // surface - was the one banner nobody ever saw while developing against the
    // fixture. Provenance and read path answer different questions.
    $("banner").className = "banner " + (fixture ? "warn" : "info");
    $("banner").innerHTML = fixture
      ? "<b>Sample data.</b> No device is connected, so these are fixture points "
        + "for checking the layout. Nothing here came from hardware."
      : "<b>Live device.</b> Points below were read from serial "
        + esc((data.device && data.device.serialNumber) || "unknown") + ".";

    const p = $("path-banner");
    p.hidden = false;
    p.className = "banner " + (degraded ? "warn" : "ok");
    p.innerHTML = "<b>Read path: " + esc(rp.summary || "unknown") + ".</b> "
                + esc(rp.detail || "");
  }

  function render() {
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
      <tr>
        <td class="num">${esc(r.input)}</td>
        <td>${esc(r.fullLabel) || '<span class="dim">(unnamed)</span>'}</td>
        <td class="num">${esc(r.value)}</td>
        <td>${r.autoManual === "Manual"
              ? '<span class="pill pill-man">Manual</span>'
              : '<span class="dim">Auto</span>'}</td>
        <td>${r.status === "OK"
              ? '<span class="pill pill-ok">OK</span>'
              : '<span class="pill pill-warn">Decom</span>'}</td>
        <td class="dim opt">${esc(r.signal)}</td>
        <td class="num dim opt">${esc(r.range)}</td>
        <td class="num opt">${esc(r.calibration)}</td>
        <td class="num dim opt">${esc(r.filter)}</td>
        <td class="num dim opt">${esc(r.panel)}</td>
        <td class="dim opt">${esc(r.label)}</td>
      </tr>`).join("");
  }

  async function load() {
    $("status").textContent = "Reading…";
    try {
      const res = await fetch("/api/inputs", { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await res.json();

      $("serial").textContent = data.device && data.device.serialNumber
        ? data.device.serialNumber : "—";

      allRows = data.inputs || [];
      showBanner(data);

      if (allRows.length === 0) {
        $("empty-title").textContent = "No points returned";
        $("empty-detail").textContent = (data.readPath && data.readPath.detail) || "";
      }

      render();
      $("status").textContent =
        "Read-only. Editing arrives once the write path is verified against hardware.";
    } catch (err) {
      $("banner").className = "banner warn";
      $("banner").innerHTML = "<b>Could not reach the backend.</b> " + esc(err.message);
      $("status").textContent = "Not connected.";
    }
  }


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
