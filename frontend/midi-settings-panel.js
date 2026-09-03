// MIDI Settings sidebar (2026-08-28) -- private-module only. Lists
// connected MIDI devices and can request a single Object Dump from a
// Kronos, saving the raw reply to a file. See this app's own private repo
// STATE.md for the SysEx RFC this is built on (macOS/CoreMIDI only so far
// -- see that repo's KronosMidi.h).
//
// Owns none of the sliding-panel SHELL itself -- createSidebarPanel()
// (sidebar-panel.js) handles the backdrop/open-close-state/two-step-reveal
// that every one of this app's sidebars needs; this file only ever builds
// its OWN content (the device table, the dump-request form, the captured-
// dumps list) and calls sidebar.update() when that content's own state
// changes. This used to hand-roll the entire shell itself (and, before
// that, opened a whole separate native window) -- both reconsidered per
// direct feedback, see sidebar-panel.js's own doc comment for the full
// history.
//
// Wrapped in an IIFE, same reason combi-cross-dataset-panel.js/
// confirm-dialog.js already are (STATE.md entry 60) -- classic <script>
// tags on one page share ONE global lexical scope for let/const.
(function () {

const sidebar = window.createSidebarPanel(document.getElementById("midiSettingsPanelRoot"), { edge: "right" });

let devices = [];
// Per-source-endpoint dump counter -- what makes "Kronos-MIDI-Dump xxxx"
// actually distinguish different devices/requests, per direct request.
// Keyed by endpointRef (the device a dump was REQUESTED FROM), not name --
// two devices could plausibly share a name; endpointRef never collides
// within one CoreMIDI session.
const dumpCountByEndpoint = new Map();
let dumpSeq = 0;
// Session-only, not persisted -- {label, byteCount, bytes, seq} per
// captured dump, newest first.
let capturedDumps = [];
let selectedDestination = null;
let selectedSource = null;
let selectedChannel = 0;
let selectedObj = 0;
let statusMessage = "";
let statusIsError = false;
let isRequesting = false;

function setStatus(message, isError) {
  statusMessage = message;
  statusIsError = Boolean(isError);
  sidebar.update();
}

// Prefers a device whose name looks like a Kronos (KronosMidi.cpp's own
// looksLikeKronos flag) for the initial destination/source selection, if
// the direction-appropriate list has one -- still just a convenience
// default, never a hard requirement (the dropdowns list every endpoint
// regardless), matching KronosMidi.h's own "don't assume a pairing"
// stance.
function pickDefault(list) {
  if (list.length === 0) return null;
  const kronosMatch = list.find((d) => d.looksLikeKronos);
  return (kronosMatch || list[0]).endpointRef;
}

async function refreshDevices() {
  devices = await window.listMidiDevices();
  const destinations = devices.filter((d) => d.isDestination);
  const sources = devices.filter((d) => d.isSource);
  if (!destinations.some((d) => d.endpointRef === selectedDestination)) selectedDestination = pickDefault(destinations);
  if (!sources.some((d) => d.endpointRef === selectedSource)) selectedSource = pickDefault(sources);
  sidebar.update();
}

async function requestDump() {
  if (selectedDestination == null || selectedSource == null || isRequesting) return;
  isRequesting = true;
  setStatus("Requesting dump...", false);

  // bank/index are hardcoded to 0/0 for this first pass (the very first
  // slot of whichever obj is chosen -- e.g. Program INT-A000) -- see
  // KronosMidi.h's own requestObjectDump() doc comment for why a single
  // fixed object, not a whole-bank loop, is deliberately as far as this
  // goes so far.
  const result = await window.requestKronosDump(selectedDestination, selectedSource, selectedChannel, selectedObj, 0, 0);
  isRequesting = false;

  if (!result.ok) {
    setStatus(`Failed: ${result.error}`, true);
    return;
  }

  const sourceDevice = devices.find((d) => d.endpointRef === selectedSource);
  const deviceName = sourceDevice ? sourceDevice.name : "device";
  const countForThisDevice = (dumpCountByEndpoint.get(selectedSource) || 0) + 1;
  dumpCountByEndpoint.set(selectedSource, countForThisDevice);
  dumpSeq += 1;

  capturedDumps = [
    {
      label: `Kronos-MIDI-Dump ${deviceName} #${countForThisDevice}`,
      byteCount: result.byteCount,
      bytes: result.bytes,
      seq: dumpSeq,
    },
    ...capturedDumps,
  ];
  setStatus(`Got ${result.byteCount} bytes.`, false);
}

async function saveDump(dump) {
  const result = await window.saveDumpToFile(dump.bytes, `kronos-midi-dump-${dump.seq}.syx`);
  if (!result.ok) {
    if (result.error) setStatus(`Save failed: ${result.error}`, true);
    return;
  }
  setStatus(`Saved to ${result.path}`, false);
}

function optionEl(value, label, selected) {
  const opt = document.createElement("option");
  opt.value = String(value);
  opt.textContent = label;
  opt.selected = selected;
  return opt;
}

function buildDeviceSelect(directionKey, selectedRef, onChange) {
  const select = document.createElement("select");
  const matching = devices.filter((d) => d[directionKey]);
  if (matching.length === 0) {
    select.appendChild(optionEl("", "(none found)", true));
    select.disabled = true;
  } else {
    for (const d of matching) {
      const label = d.looksLikeKronos ? `${d.name} (looks like a Kronos)` : d.name;
      select.appendChild(optionEl(d.endpointRef, label, d.endpointRef === selectedRef));
    }
  }
  select.addEventListener("change", () => {
    onChange(select.value ? Number(select.value) : null);
    sidebar.update();
  });
  return select;
}

function buildBody(bodyEl) {
  const devicesHeading = document.createElement("h3");
  devicesHeading.className = "sidebar-section-heading";
  devicesHeading.textContent = "Connected devices";
  const refreshBtn = document.createElement("button");
  refreshBtn.type = "button";
  refreshBtn.className = "button is-small";
  refreshBtn.textContent = "Refresh";
  refreshBtn.addEventListener("click", refreshDevices);
  bodyEl.append(devicesHeading, refreshBtn);

  const table = document.createElement("table");
  table.className = "table is-fullwidth is-narrow";
  table.innerHTML = "<thead><tr><th>Name</th><th>Direction</th></tr></thead><tbody></tbody>";
  const tbody = table.querySelector("tbody");
  if (devices.length === 0) {
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.colSpan = 2;
    td.className = "usage-empty";
    td.textContent = "No MIDI devices found.";
    tr.appendChild(td);
    tbody.appendChild(tr);
  } else {
    for (const d of devices) {
      const tr = document.createElement("tr");
      const nameTd = document.createElement("td");
      nameTd.textContent = d.name || "(unnamed)";
      if (d.looksLikeKronos) {
        const flag = document.createElement("span");
        flag.className = "midi-kronos-flag";
        flag.textContent = " ● looks like a Kronos";
        nameTd.appendChild(flag);
      }
      const dirTd = document.createElement("td");
      const dirs = [];
      if (d.isSource) dirs.push("source");
      if (d.isDestination) dirs.push("destination");
      dirTd.textContent = dirs.join(", ");
      tr.append(nameTd, dirTd);
      tbody.appendChild(tr);
    }
  }
  bodyEl.appendChild(table);

  const dumpHeading = document.createElement("h3");
  dumpHeading.className = "sidebar-section-heading";
  dumpHeading.textContent = "Request a dump";
  bodyEl.appendChild(dumpHeading);

  const destLabel = document.createElement("label");
  destLabel.textContent = "Send to (destination)";
  const destSelectWrap = document.createElement("div");
  destSelectWrap.className = "select is-small is-fullwidth";
  destSelectWrap.appendChild(buildDeviceSelect("isDestination", selectedDestination, (v) => (selectedDestination = v)));
  bodyEl.append(destLabel, destSelectWrap);

  const sourceLabel = document.createElement("label");
  sourceLabel.textContent = "Receive from (source)";
  const sourceSelectWrap = document.createElement("div");
  sourceSelectWrap.className = "select is-small is-fullwidth";
  sourceSelectWrap.appendChild(buildDeviceSelect("isSource", selectedSource, (v) => (selectedSource = v)));
  bodyEl.append(sourceLabel, sourceSelectWrap);

  const channelLabel = document.createElement("label");
  channelLabel.textContent = "Global MIDI channel (0-15)";
  const channelSelect = document.createElement("select");
  for (let i = 0; i < 16; i++) channelSelect.appendChild(optionEl(i, i === 0 ? "0 (factory default)" : String(i), i === selectedChannel));
  channelSelect.addEventListener("change", () => {
    selectedChannel = Number(channelSelect.value);
  });
  const channelSelectWrap = document.createElement("div");
  channelSelectWrap.className = "select is-small is-fullwidth";
  channelSelectWrap.appendChild(channelSelect);
  bodyEl.append(channelLabel, channelSelectWrap);

  const objLabel = document.createElement("label");
  objLabel.textContent = "Object (first slot only, e.g. Program INT-A000)";
  const objSelect = document.createElement("select");
  objSelect.appendChild(optionEl(0, "0x00 Program", selectedObj === 0));
  objSelect.appendChild(optionEl(1, "0x01 Combination", selectedObj === 1));
  objSelect.appendChild(optionEl(3, "0x03 Global", selectedObj === 3));
  objSelect.addEventListener("change", () => {
    selectedObj = Number(objSelect.value);
  });
  const objSelectWrap = document.createElement("div");
  objSelectWrap.className = "select is-small is-fullwidth";
  objSelectWrap.appendChild(objSelect);
  bodyEl.append(objLabel, objSelectWrap);

  const dumpBtn = document.createElement("button");
  dumpBtn.type = "button";
  dumpBtn.className = "button is-small accent-button";
  dumpBtn.textContent = "Dump Request";
  dumpBtn.disabled = selectedDestination == null || selectedSource == null || isRequesting;
  dumpBtn.addEventListener("click", requestDump);
  bodyEl.appendChild(dumpBtn);

  if (statusMessage) {
    const status = document.createElement("div");
    status.className = statusIsError ? "usage-empty midi-status-error" : "usage-note";
    status.textContent = statusMessage;
    bodyEl.appendChild(status);
  }

  if (capturedDumps.length > 0) {
    const dumpsHeading = document.createElement("h3");
    dumpsHeading.className = "sidebar-section-heading";
    dumpsHeading.textContent = "Captured dumps (this session only, not saved automatically)";
    bodyEl.appendChild(dumpsHeading);

    for (const dump of capturedDumps) {
      const row = document.createElement("div");
      row.className = "midi-dump-row";
      const label = document.createElement("span");
      label.textContent = `${dump.label} — ${dump.byteCount} bytes`;
      const saveBtn = document.createElement("button");
      saveBtn.type = "button";
      saveBtn.className = "button is-small";
      saveBtn.textContent = "Save to file...";
      saveBtn.addEventListener("click", () => saveDump(dump));
      row.append(label, saveBtn);
      bodyEl.appendChild(row);
    }
  }
}

window.toggleMidiSettingsPanel = () => {
  if (sidebar.isOpen()) {
    sidebar.close();
    return;
  }
  sidebar.open({ title: "MIDI Settings (experimental)", build: buildBody });
  refreshDevices();
};

})();
