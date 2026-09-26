#pragma once

// The Outputs page, embedded in the executable for the same reason the
// Inputs page is (inputs_page.h). Its banners follow that page's rules: a
// reading is claimed only on readFromWire, and a device that was not read
// says so, never as an empty grid under a "read from" line.

namespace t5000::web
{
    inline constexpr const char* kOutputsPage = R"PAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Outputs</title>
<style>
  :root {
    --bg:#fff; --surface:#f7f8fa; --border:#e3e6ea; --text:#1a1d21; --dim:#6b7280;
    --accent:#0b6bcb; --row-alt:#fafbfc; --ok:#0f7b3f; --ok-bg:#e6f4ec;
    --warn:#8a5300; --warn-bg:#fdf1dc; --info:#0b4ea2; --info-bg:#e8f0fe;
    --hand:#b42318; --hand-bg:#fdecea;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg:#16181c; --surface:#1d2025; --border:#2c3036; --text:#e6e8ea; --dim:#9aa1ab;
      --accent:#4d9bf0; --row-alt:#191c20; --ok:#5bd18b; --ok-bg:#16301f;
      --warn:#e0b060; --warn-bg:#33270f; --info:#7fb4f0; --info-bg:#16283f;
      --hand:#f97066; --hand-bg:#3a1a17;
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
  nav{display:flex;gap:10px;font-size:12px}
  nav a{color:var(--accent);text-decoration:none}
  nav a:hover{text-decoration:underline}
  .meta{color:var(--dim);font-size:12px}
  .meta b{color:var(--text);font-weight:600;font-variant-numeric:tabular-nums}
  .spacer{flex:1}
  input[type=search]{font:inherit;color:inherit;background:var(--bg);
    border:1px solid var(--border);border-radius:6px;padding:5px 10px;min-width:180px}
  input[type=search]:focus{border-color:var(--accent);outline:none}
  button{font:inherit;color:var(--text);background:var(--bg);border:1px solid var(--border);
         border-radius:6px;padding:5px 12px;cursor:pointer}
  button:hover{border-color:var(--accent);color:var(--accent)}

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
  /* pre, not nowrap: T3000's text is shown as it is, and some of it has runs
     of spaces - "4   -> 20" - that HTML would otherwise fold into one. */
  tbody td{padding:5px 10px;border-bottom:1px solid var(--border);white-space:pre}
  tbody tr:nth-child(even){background:var(--row-alt)}
  tbody tr:hover{background:var(--info-bg)}
  /* T3000 draws a row red while its hand-off-auto switch is at off or hand:
     the output is under a person's control, not the program's. */
  tbody tr.hand td{color:var(--hand)}
  tbody tr.hand{background:var(--hand-bg)}
  .num{text-align:right;font-variant-numeric:tabular-nums}
  .dim{color:var(--dim)}
  .pill{display:inline-block;padding:1px 7px;border-radius:10px;font-size:11px;font-weight:600}
  .pill-man{background:var(--info-bg);color:var(--info)}
  .pill-hand{background:var(--hand);color:var(--bg)}
  .pill-note{background:var(--info-bg);color:var(--info);cursor:help}

  .empty{display:flex;align-items:center;justify-content:center;height:100%;
         color:var(--dim);text-align:center;padding:32px}
  .empty div{max-width:420px}
  .empty h2{font-size:14px;font-weight:600;color:var(--text);margin:0 0 6px}
  .empty p{margin:0 0 8px;font-size:12px}

  footer{flex:none;padding:6px 16px;border-top:1px solid var(--border);
         background:var(--surface);font-size:12px;color:var(--dim);min-height:28px}

  @media (max-width: 760px) {
    .opt{display:none}
    header{gap:8px}
    input[type=search]{min-width:120px;flex:1}
    thead th,tbody td{padding:5px 7px}
    tbody td:nth-child(2){max-width:30vw;overflow:hidden;text-overflow:ellipsis}
    table{table-layout:auto;width:100%}
  }
</style>
</head>
<body>

<header>
  <h1>Outputs</h1>
  <nav><a href="/">Devices</a><a href="/inputs">Inputs</a></nav>
  <span class="meta">Device <b id="serial">&mdash;</b></span>
  <span class="meta"><b id="count">0</b> points</span>
  <span class="spacer"></span>
  <input type="search" id="filter" placeholder="Filter points" autocomplete="off">
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
        <th>Output</th><th>Full Label</th><th class="opt">Auto/Man</th><th>HOA Switch</th>
        <th class="num">Value</th><th class="opt">Units</th><th class="opt">Range</th>
        <th class="num opt">Low V</th><th class="num opt">High V</th><th class="num opt">PWM Period</th>
        <th>Status</th><th class="opt">Label</th>
      </tr>
    </thead>
    <tbody id="rows"></tbody>
  </table>
</div>

<footer id="status">Read-only. T3000's Panel, Type and Product Name columns are not shown yet.</footer>

<script>
  const $ = id => document.getElementById(id);
  const esc = s => String(s == null ? "" : s).replace(/[&<>"]/g,
    c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;" }[c]));
  const FOOTER = "Read-only. T3000's Panel, Type and Product Name columns are not shown yet.";

  let allRows = [];

  function showBanner(data) {
    const rp = data.readPath || {};
    const fixture = !!(data.device && data.device.isFixture);
    const sighting = (data.device && data.device.sighting) ? " " + esc(data.device.sighting) : "";

    // Found but not read: its own state, never an empty grid under a claim
    // that something was read.
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

    const degraded = rp.path === "modbus-registers";

    // A reading is claimed on readFromWire === true and nothing else.
    const fromWire = !!(data.device && data.device.readFromWire === true);

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

  function hoaCell(r) {
    if (r.hand) return '<span class="pill pill-hand">' + esc(r.hoa) + '</span>';
    return '<span class="dim">' + esc(r.hoa) + '</span>';
  }

  function autoManualCell(r) {
    if (r.autoManual === "Manual") return '<span class="pill pill-man">Manual</span>';
    return '<span class="dim">' + esc(r.autoManual) + '</span>';
  }

  function render() {
    const needle = $("filter").value.trim().toLowerCase();
    const rows = needle
      ? allRows.filter(r => (r.fullLabel + " " + r.label + " OUT" + r.output).toLowerCase().includes(needle))
      : allRows;

    $("count").textContent = rows.length;
    $("grid").hidden = rows.length === 0;
    $("empty").hidden = rows.length !== 0;

    if (rows.length === 0 && allRows.length > 0) {
      $("empty-title").textContent = "No points match that filter";
      $("empty-detail").textContent = "";
    }

    $("rows").innerHTML = rows.map(r => `
      <tr${r.hand ? ' class="hand"' : ''}>
        <td>OUT${esc(r.output)}${r.note
              ? ' <span class="pill pill-note" title="' + esc(r.note) + '">?</span>' : ''}</td>
        <td>${esc(r.fullLabel) || '<span class="dim">(unnamed)</span>'}</td>
        <td class="opt">${autoManualCell(r)}</td>
        <td>${hoaCell(r)}</td>
        <td class="num">${esc(r.value)}</td>
        <td class="dim opt">${esc(r.units)}</td>
        <td class="dim opt">${esc(r.range)}</td>
)PAGE"
        R"PAGE(        <td class="num dim opt">${esc(r.lowVoltage)}</td>
        <td class="num dim opt">${esc(r.highVoltage)}</td>
        <td class="num dim opt">${esc(r.pwmPeriod)}</td>
        <td class="dim">${esc(r.status)}</td>
        <td class="dim opt">${esc(r.label)}</td>
      </tr>`).join("");
  }

  async function load() {
    $("status").textContent = "Reading…";
    try {
      const res = await fetch("/api/outputs", { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);
      const data = await res.json();

      $("serial").textContent = data.device && data.device.serialNumber
        ? data.device.serialNumber : "—";

      allRows = data.outputs || [];
      showBanner(data);

      if (allRows.length === 0) {
        // Three different empties. A device that was not read; a panel
        // that was read and has no outputs T3000 shows; and nothing at all.
        const panel = data.panel || {};
        if (data.unavailable) {
          $("empty-title").textContent = "This device has not been read";
          $("empty-detail").textContent = data.message || "";
        } else if (panel.known && panel.outputsRead > 0) {
          $("empty-title").textContent = "This panel has no outputs to show";
          $("empty-detail").textContent = panel.note || "";
        } else {
          $("empty-title").textContent = "No points returned";
          $("empty-detail").textContent = (data.readPath && data.readPath.detail) || "";
        }
      }

      render();
      $("status").textContent = FOOTER;
    } catch (err) {
      $("banner").className = "banner warn";
      $("banner").innerHTML = "<b>Could not reach the backend.</b> " + esc(err.message);
      $("status").textContent = "Not connected.";
    }
  }

  $("refresh").onclick = load;
  $("filter").oninput = render;
  load();
</script>
</body>
</html>
)PAGE";
}
