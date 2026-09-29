#pragma once

// The Firmware page, F1b of docs/t5000-firmware-plan.md: T3000's "Load
// firmware for a single device" (Tools, T3000.rc:11530), checking only. It
// lists the devices with what a check needs of each, reads a panel's
// settings for its bootloader's version when asked, and checks a file picked
// from disk against a device. Nothing is sent to a device but that one read:
// the file goes to T5000, which says what ISP, and T5000, would make of it
// (app/firmware_page.h, app/firmware_read.h).
//
// Text from a device or a file is set with textContent, or through esc(),
// never as markup.

namespace t5000::web
{
    inline constexpr const char* kFirmwarePage = R"PAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Firmware</title>
<style>
  :root {
    --bg:#fff; --surface:#f7f8fa; --border:#e3e6ea; --text:#1a1d21; --dim:#6b7280;
    --accent:#0b6bcb; --row-alt:#fafbfc; --ok:#0f7b3f; --ok-bg:#e6f4ec;
    --warn:#8a5300; --warn-bg:#fdf1dc; --info:#0b4ea2; --info-bg:#e8f0fe;
    --bad:#b42318; --bad-bg:#fdecea;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg:#16181c; --surface:#1d2025; --border:#2c3036; --text:#e6e8ea; --dim:#9aa1ab;
      --accent:#4d9bf0; --row-alt:#191c20; --ok:#5bd18b; --ok-bg:#16301f;
      --warn:#e0b060; --warn-bg:#33270f; --info:#7fb4f0; --info-bg:#16283f;
      --bad:#f97066; --bad-bg:#3a1a17;
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
  button{font:inherit;color:var(--text);background:var(--bg);border:1px solid var(--border);
         border-radius:6px;padding:5px 12px;cursor:pointer}
  button:hover{border-color:var(--accent);color:var(--accent)}
  button:disabled{opacity:.5;cursor:default;border-color:var(--border);color:var(--dim)}
  td button{padding:2px 9px;font-size:12px}

  .banner{flex:none;padding:9px 16px;border-bottom:1px solid var(--border);font-size:12px}
  .banner.info{background:var(--info-bg);color:var(--info)}
  .banner.warn{background:var(--warn-bg);color:var(--warn)}
  .banner.ok{background:var(--ok-bg);color:var(--ok)}
  .banner.bad{background:var(--bad-bg);color:var(--bad)}

  .scroll{flex:1;overflow:auto}
  table{border-collapse:separate;border-spacing:0;width:100%}
  thead th{position:sticky;top:0;z-index:1;background:var(--surface);color:var(--dim);
    font-weight:600;font-size:11px;letter-spacing:.04em;text-transform:uppercase;
    text-align:left;white-space:nowrap;padding:8px 10px;border-bottom:1px solid var(--border)}
  tbody td{padding:6px 10px;border-bottom:1px solid var(--border);vertical-align:top}
  tbody tr:nth-child(even){background:var(--row-alt)}
  .num{text-align:right;font-variant-numeric:tabular-nums}
  .dim{color:var(--dim)}
  .sub{display:block;color:var(--dim);font-size:11.5px}
  .pill{display:inline-block;padding:1px 7px;border-radius:10px;font-size:11px;font-weight:600;cursor:help}
  .pill-ok{background:var(--ok-bg);color:var(--ok)}
  .pill-warn{background:var(--warn-bg);color:var(--warn)}
  .pill-dim{background:var(--surface);color:var(--dim);border:1px solid var(--border)}
  .actions{white-space:nowrap}

  .empty{display:flex;align-items:center;justify-content:center;height:100%;
         color:var(--dim);text-align:center;padding:32px}
  .empty div{max-width:440px}
  .empty h2{font-size:14px;font-weight:600;color:var(--text);margin:0 0 6px}
  .empty p{margin:0 0 8px;font-size:12px}

  footer{flex:none;padding:6px 16px;border-top:1px solid var(--border);
         background:var(--surface);font-size:12px;color:var(--dim);min-height:28px}

  dialog{border:1px solid var(--border);border-radius:10px;background:var(--bg);color:var(--text);
         padding:0;width:min(640px,calc(100vw - 32px));max-height:calc(100vh - 48px);
         box-shadow:0 12px 32px rgba(0,0,0,.25)}
  dialog::backdrop{background:rgba(0,0,0,.35)}
  .dlg{display:flex;flex-direction:column;max-height:calc(100vh - 50px)}
  .dlg h2{font-size:14px;font-weight:600;margin:0;padding:14px 18px 10px;border-bottom:1px solid var(--border)}
  .dlg .body{overflow:auto;padding:12px 18px}
  .dlg .foot{display:flex;justify-content:flex-end;gap:8px;padding:10px 18px;border-top:1px solid var(--border)}
  .verdict{padding:8px 10px;border-radius:6px;margin:0 0 12px;font-weight:600}
  .verdict.ok{background:var(--ok-bg);color:var(--ok)}
  .verdict.bad{background:var(--bad-bg);color:var(--bad)}
  .verdict.warn{background:var(--warn-bg);color:var(--warn)}
  .dlg h3{font-size:12px;font-weight:600;margin:14px 0 6px;color:var(--dim);
          text-transform:uppercase;letter-spacing:.04em}
  dl{display:grid;grid-template-columns:max-content 1fr;gap:3px 14px;margin:0;font-size:12.5px}
  dt{color:var(--dim)}
  dd{margin:0;word-break:break-word}
  ul{margin:0;padding-left:18px;font-size:12.5px}
  ul li{margin:2px 0}
  ul.bad li{color:var(--bad)}
  ul.warn li{color:var(--warn)}

  @media (max-width: 760px) {
    .opt{display:none}
    header{gap:8px}
    thead th,tbody td{padding:5px 7px}
    dl{grid-template-columns:1fr}
    dt{margin-top:4px}
  }
</style>
</head>
<body>

<header>
  <h1>Firmware</h1>
  <span class="meta"><b id="count">0</b> devices</span>
  <span class="spacer"></span>
  <button id="refresh">Refresh</button>
</header>

<div class="banner info" id="intro">Checking only. Pick a device and a firmware file from disk: T5000 reads the file as ISP would, and says whether ISP would take it for that device and whether T5000 would send it. No file is sent to any device, and T5000 does not update bootloaders.</div>
<div class="banner" id="banner" hidden></div>

<div class="scroll">
  <div class="empty" id="empty" hidden>
    <div>
      <h2>No devices</h2>
      <p>Scan, or add a device, on the Devices page. A firmware file is checked against a device in the list.</p>
    </div>
  </div>
  <table id="grid" hidden>
    <thead>
      <tr>
        <th>Device</th><th>Product</th><th class="num opt">Firmware</th><th class="opt">Path</th>
        <th>Bootloader</th><th class="opt">State</th><th></th>
      </tr>
    </thead>
    <tbody id="rows"></tbody>
  </table>
</div>

<footer id="status">Nothing is sent to a device but a panel's settings, one request (sent once more if nothing answers), when Read is clicked.</footer>

<input type="file" id="file" accept=".hex,.bin" hidden>

<dialog id="check-dlg" aria-labelledby="check-title">
  <div class="dlg">
    <h2 id="check-title">Firmware check</h2>
    <div class="body" id="check-body"></div>
    <div class="foot"><button id="check-close">Close</button></div>
  </div>
</dialog>

)PAGE"
        R"PAGE(<script>
  const $ = id => document.getElementById(id);
  const esc = s => String(s == null ? "" : s).replace(/[&<>"]/g,
    c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;" }[c]));

  let devices = [];
  let largestFile = 0;
  let checking = null;   // the device a file is being picked for

  function banner(kind, text) {
    const b = $("banner");
    if (!text) { b.hidden = true; return; }
    b.className = "banner " + kind;
    b.textContent = text;
    b.hidden = false;
  }

  function sizeText(n) {
    if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB";
    if (n >= 1024) return (n / 1024).toFixed(1) + " KB";
    return n + " bytes";
  }

  function stateCell(d) {
    const label = d.state === "bootloader" ? "in its bootloader"
                : d.state === "firmware" ? "running" : "not known";
    const cls = d.state === "bootloader" ? "pill-warn" : d.state === "firmware" ? "pill-ok" : "pill-dim";
    return '<span class="pill ' + cls + '" title="' + esc(d.stateText) + '">' + esc(label) + "</span>";
  }

  function bootloaderCell(d) {
    if (d.bootloader.known)
      return '<b class="num">' + esc(d.bootloader.version) + '</b><span class="sub">' + esc(d.bootloader.from) + "</span>";
    const why = d.virtual ? "A virtual device has none."
              : d.path === "serial" ? "Not read over a serial port yet."
              : d.canRead ? "Not read this session." : d.readWhy;
    return '<span class="dim" title="' + esc(why) + '">not known</span>';
  }

  function render() {
    $("count").textContent = devices.length;
    $("empty").hidden = devices.length !== 0;
    $("grid").hidden = devices.length === 0;
    $("rows").innerHTML = devices.map(d => {
      const name = d.name || d.panelName || "";
      const own = d.own.length
        ? '<span class="pill pill-warn" title="' + esc(d.own.join(" ")) + '">not sent to</span>' : "";
      return "<tr>" +
        "<td>" + (name ? esc(name) + '<span class="sub">' : "<span>") + "Serial " + esc(d.serialNumber) +
          (d.address ? " &middot; " + esc(d.address) : "") + "</span> " + own + "</td>" +
        "<td>" + esc(d.productName) + '<span class="sub">product ' + esc(d.productId) + "</span></td>" +
        '<td class="num opt">' + (d.firmware ? esc(d.firmware) : '<span class="dim">&mdash;</span>') + "</td>" +
        '<td class="opt">' + (d.path ? '<span title="' + esc(d.pathText) + '">' + esc(d.path) + "</span>"
                                     : '<span class="dim" title="' + esc(d.pathText) + '">none</span>') + "</td>" +
        "<td>" + bootloaderCell(d) + "</td>" +
        '<td class="opt">' + stateCell(d) + "</td>" +
        '<td class="actions">' +
          '<button data-read="' + esc(d.handle) + '"' + (d.canRead ? "" : " disabled") +
            ' title="' + esc(d.canRead ? "Read its settings, one request (sent once more if nothing answers), for its bootloader's version" : d.readWhy) +
            '">Read</button> ' +
          '<button data-check="' + esc(d.handle) + '">Check a file&hellip;</button>' +
        "</td></tr>";
    }).join("");
  }

  function take(list) {
    devices = list.devices || [];
    largestFile = list.largestFile || 0;
    render();
  }

  async function load() {
    try {
      const res = await fetch("/api/firmware");
      take(await res.json());
    } catch (e) {
      banner("bad", "T5000 did not answer: " + e.message);
    }
  }

  async function readBootloader(handle, button) {
    button.disabled = true;
    banner("info", "Reading the panel's settings…");
    try {
      const res = await fetch("/api/firmware/read", { method: "POST", body: JSON.stringify({ handle: handle }) });
      const data = await res.json();
      if (data.list) take(data.list);
      banner(data.ok ? "ok" : "warn", data.message || "");
    } catch (e) {
      banner("bad", "T5000 did not answer the read: " + e.message);
    }
    button.disabled = false;
  }

  function list(title, items, cls) {
    if (!items || !items.length) return "";
    return "<h3>" + esc(title) + '</h3><ul class="' + cls + '">' +
      items.map(t => "<li>" + esc(t) + "</li>").join("") + "</ul>";
  }

  function row(label, value) {
    return value === "" || value == null ? "" : "<dt>" + esc(label) + "</dt><dd>" + esc(value) + "</dd>";
  }

  function showCheck(device, data, sent) {
    const f = data.file || {};
    const v = data.verdict;
    // The device as the check was told it, which a read on another page
    // may have changed since the list was loaded; the list's otherwise.
    const told = data.device || device;
    const boot = told.bootloader || device.bootloader;
    const formats = { hex: ".hex", bin: ".bin" };
    const chips = { asix: "ASIX", arm32k: "ARM, header at 0x8200", arm64k: "ARM, header at 0x10200" };
    // Ok, but with notes on what ISP checks only when it flashes: amber.
    const cls = !data.ok ? "bad" : v && v.notes && v.notes.length ? "warn" : "ok";
    let html = '<p class="verdict ' + cls + '">' + esc(data.message) + "</p>";
    if (f.size != null && f.size !== sent) {
      html += '<p class="verdict bad">' + esc("T5000 received " + sizeText(f.size) + " of the file's " + sizeText(sent) +
              ": this is a check of what it received, not of the whole file.") + "</p>";
    }

    html += "<h3>Device</h3><dl>" +
      row("Device", (device.name || device.panelName || "") + " serial " + device.serialNumber) +
      row("Product", told.productName + " (" + told.productId + ")") +
      row("Path", data.pathText || "") +
      row("Bootloader", boot.known ? boot.version + ", " + boot.from : "not known") +
      "</dl>";

    html += "<h3>File</h3><dl>" +
      row("Name", f.name) +
      row("Size", f.size != null ? sizeText(f.size) : "");
    if (f.read) {
      html +=
        row("Kind", (formats[f.kind] || f.kind) + (f.format ? ", " + f.format : "")) +
        row("Chip", chips[f.chip] || "") +
        row("Header at", "0x" + Number(f.headerAt).toString(16).toUpperCase()) +
        row("Company", f.company) +
        row("Product name", f.productName) +
        row("Version", f.version) +
        row("Data", sizeText(f.dataSize));
    }
    html += "</dl>";

    if (v) {
      html += "<h3>The check, on the route ISP would take it</h3><dl>" +
        row("Route", v.route) +
        row("Device, as named", v.deviceName) +
        row("File, as named", v.fileName) +
        row("Needs a newer bootloader", v.needsNewBootloader ? "yes, on some devices" : "no") +
        "</dl>";
      html += list("Refused", v.refusals, "bad");
      html += list("Notes", v.notes, "");
    }
    html += list("T5000's own reasons", data.own, "warn");

    $("check-title").textContent = "Firmware check: " + (f.name || "");
    $("check-body").innerHTML = html;
    $("check-dlg").showModal();
  }

  async function checkFile(device, file) {
    if (largestFile && file.size > largestFile) {
      banner("warn", file.name + " is " + sizeText(file.size) + ". T5000 checks a file of up to " +
             sizeText(largestFile) + ", more than ISP reads into its largest buffer, so it was not sent to T5000.");
      return;
    }
    banner("info", "Checking " + file.name + "…");
    try {
      const url = "/api/firmware/check?handle=" + encodeURIComponent(device.handle) +
                  "&name=" + encodeURIComponent(file.name);
      const res = await fetch(url, { method: "POST", body: file });
      const data = await res.json();
      banner("", "");
      if (res.status !== 200) { banner("bad", data.message || "T5000 refused the check."); return; }
      showCheck(device, data, file.size);
    } catch (e) {
      banner("bad", "T5000 did not answer the check: " + e.message);
    }
  }

  $("rows").addEventListener("click", e => {
    const b = e.target.closest("button");
    if (!b) return;
    if (b.dataset.read) readBootloader(b.dataset.read, b);
    if (b.dataset.check) {
      checking = devices.find(d => d.handle === b.dataset.check) || null;
      $("file").value = "";
      $("file").click();
    }
  });

  $("file").addEventListener("change", () => {
    const f = $("file").files[0];
    if (f && checking) checkFile(checking, f);
  });

  $("check-close").addEventListener("click", () => $("check-dlg").close());
  $("refresh").addEventListener("click", () => { banner("", ""); load(); });

  load();
</script>
</body>
</html>
)PAGE";
}
