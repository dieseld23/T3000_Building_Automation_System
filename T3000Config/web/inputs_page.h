#pragma once

// The Inputs page, embedded in the executable.
//
// Embedded rather than served from disk so the tool is a single file that works
// from wherever it is copied. The existing product already has a class of bug
// where a screen is blank because a ResourceFile folder did not travel with the
// binary; there is no reason to inherit it.

namespace t3000::web
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
  <button id="refresh">Refresh</button>
</header>

<div class="banner info" id="banner">Loading&hellip;</div>

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

<footer id="status">Read-only. Editing arrives once the write path is verified against hardware.</footer>

<script>
  const $ = id => document.getElementById(id);
  const esc = s => String(s == null ? "" : s).replace(/[&<>"]/g,
    c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;" }[c]));

  let allRows = [];

  function showBanner(data) {
    const b = $("banner");
    const rp = data.readPath || {};

    if (data.device && data.device.isFixture) {
      b.className = "banner warn";
      b.innerHTML = "<b>Sample data.</b> No device is connected, so these are "
                  + "fixture points for checking the layout. Nothing here came "
                  + "from hardware.";
      return;
    }

    // Registers means the struct read was refused - usually firmware. Say which,
    // rather than leaving a technician to guess at an empty or partial grid.
    const degraded = rp.path === "modbus-registers";
    b.className = "banner " + (degraded ? "warn" : "ok");
    b.innerHTML = "<b>Read path: " + esc(rp.summary || "unknown") + ".</b> "
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

  $("refresh").onclick = load;
  $("filter").oninput = render;
  load();
</script>
</body>
</html>
)PAGE";
}
