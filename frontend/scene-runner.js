// Screenshot scenes (`kronos_editor --scene <file>`, see docs/screenshots/
// README.md and src/main.cpp's SceneRun). Native code hands this page one
// scene per page load (getPendingScene()); runScene() puts the UI into the
// state the scene describes, waits for it to settle, then asks native code
// to capture the window (captureScene()) and reloads for the next scene.
// Native code closes every dataset in between (numbering restarts at #1), so
// each scene starts like a fresh launch and opens exactly its own files.
//
// Scene fields go through the same functions the UI's own controls call
// (pane.loadDataset(), switchCategory(), selectSetlist(), jumpToInstrument(),
// setPaneVisibility(), swapPanes(), ...). Three pane states have no such
// function -- the bank filter, the Combis tab's Set List filter, expanding a
// Duplicates group -- so for those the runner operates the pane's own
// control (click/change/input on the real element). Those element lookups
// live only in this file and fail loudly when the markup changes. Unknown
// fields are an error, not ignored -- a typo must not silently produce a
// screenshot of the wrong state.
//
// Screenshot mode (body.scene-mode, style.css): toasts hidden, no text
// cursor, the status bar reset to "Ready.", and every dataset's full path
// shown as just its file name (the PCGs live in a private directory whose
// path doesn't belong in published docs).

// id/out/rectangle/window: validated and used by the native side.
const SCENE_KEYS = new Set([
  "id", "out", "rectangle", "window",
  "openDatasets", "left", "right", "visibility", "swapped", "tooltip", "dropdown",
]);
const SCENE_PANE_KEYS = new Set(["dataset", "category", "setlist", "banks", "setlistFilter", "filter", "open"]);
const SCENE_CATEGORIES = new Set(["setlist", "combis", "programs", "duplicates", "internals"]);
const SCENE_SECTIONS = new Set(["general", "comment"]);
const SCENE_SETTLE_TIMEOUT_MS = 5000;

function sceneError(where, message) {
  return new Error(`${where}: ${message}`);
}

function checkSceneKeys(object, allowed, where) {
  if (object === null || typeof object !== "object" || Array.isArray(object)) throw sceneError(where, "must be an object");
  for (const key of Object.keys(object)) {
    if (!allowed.has(key)) throw sceneError(where, `unknown field "${key}"`);
  }
}

function sceneElement(root, selector, where, what) {
  const el = root.querySelector(selector);
  if (!el) throw sceneError(where, `${what} not found (${selector}) -- has the markup changed? see scene-runner.js`);
  return el;
}

// "U-A 019" -> {bank, number} against PROGRAM_BANK_NAMES/COMBI_BANK_NAMES (pane.js).
function parseSlotName(text, isProgram, where) {
  const match = /^(\S+)\s+(\d{1,3})$/.exec(String(text).trim());
  const names = isProgram ? PROGRAM_BANK_NAMES : COMBI_BANK_NAMES;
  const bank = match ? names.indexOf(match[1]) : -1;
  const number = match ? Number(match[2]) : NaN;
  if (bank < 0 || !(number >= 0 && number <= 127)) {
    throw sceneError(where, `"open": "${text}" is not a ${isProgram ? "Program" : "Combi"} slot like "U-A 019"`);
  }
  return { bank, number };
}

function setControlValue(el, value, eventName) {
  el.value = value;
  el.dispatchEvent(new Event(eventName, { bubbles: true }));
}

// Only these banks selected in the visible bank-filter row: "None", then one
// click per bank (labels read "I-A" or "I-A (EXi)").
function applySceneBanks(paneEl, banks, where) {
  if (!Array.isArray(banks) || banks.length === 0) throw sceneError(where, `"banks" must be a non-empty array of bank names`);
  const noneBtn = [...paneEl.querySelectorAll(".select-control-row:not([hidden]) .select-control-button")].find(
    (b) => b.textContent === "None"
  );
  if (!noneBtn) throw sceneError(where, `"banks": the None button isn't visible (only the programs/combis categories have a bank filter)`);
  noneBtn.click();
  const buttons = [...paneEl.querySelectorAll(".bank-filter-row:not([hidden]) .bank-filter-button")];
  for (const bank of banks) {
    const btn = buttons.find((b) => b.textContent === bank || b.textContent.startsWith(bank + " "));
    if (!btn) throw sceneError(where, `"banks": no bank button "${bank}"`);
    if (btn.disabled) throw sceneError(where, `"banks": bank "${bank}" is empty in this dataset`);
    btn.click();
  }
}

async function applySceneOpen(pane, paneEl, spec, where) {
  const { category, open } = spec;
  if (category === "setlist") {
    checkSceneKeys(open, new Set(["song", "sections"]), `${where} "open"`);
    const sections = open.sections || ["comment"];
    if (!Number.isInteger(open.song)) throw sceneError(where, `"open": "song" must be a slot number`);
    if (!Array.isArray(sections) || sections.length === 0 || sections.some((s) => !SCENE_SECTIONS.has(s))) {
      throw sceneError(where, `"open": "sections" must be a non-empty subset of ["general", "comment"]`);
    }
    if (!(await pane.openSetlistSlot(open.song, sections))) throw sceneError(where, `"open": slot ${open.song} couldn't be opened`);
  } else if (category === "programs" || category === "combis") {
    const isProgram = category === "programs";
    const { bank, number } = parseSlotName(open, isProgram, where);
    pane.jumpToInstrument({ isProgram, bank, number });
    if (!paneEl.querySelector(`[data-panel="${category}"] tr.is-selected`)) {
      throw sceneError(where, `"open": ${open} isn't in this dataset`);
    }
  } else if (category === "duplicates") {
    const row = [...paneEl.querySelectorAll('[data-panel="duplicates"] tbody tr')].find(
      (tr) => tr.firstElementChild && tr.firstElementChild.textContent === open
    );
    if (!row) throw sceneError(where, `"open": no duplicate group named "${open}"`);
    row.click();
  } else {
    throw sceneError(where, `"open" needs a "category" of setlist, programs, combis or duplicates`);
  }
}

// Opens one dataset by alias (or finds it already open -- openFile() dedupes
// by path) and returns the bridge's {datasetId, displayName}.
// Tells native code how far the scene got -- its timeout message names the
// last step reported.
function sceneStep(text) {
  window.sceneProgress(text);
}

async function openSceneDataset(alias, datasets, where, refreshDatasets) {
  sceneStep(`${where}: opening dataset "${alias}"`);
  const path = datasets[alias];
  if (!path) throw sceneError(where, `unknown dataset "${alias}" (not in the file's "datasets")`);
  const result = await window.openFile(path);
  if (!result.ok) throw sceneError(where, result.error);
  sceneStep(`${where}: refreshing dataset list`);
  await refreshDatasets();
  return result;
}

async function applyScenePane(side, paneId, spec, datasets, { panes, refreshDatasets }) {
  const where = `"${side}"`;
  checkSceneKeys(spec, SCENE_PANE_KEYS, where);
  if (spec.dataset == null) throw sceneError(where, `needs a "dataset"`);
  const pane = panes[paneId];
  const paneEl = sceneElement(document, `.pane[data-pane="${paneId}"]`, where, "pane");

  const result = await openSceneDataset(spec.dataset, datasets, where, refreshDatasets);
  sceneStep(`${where}: loading dataset into the pane`);
  await pane.loadDataset(result.datasetId, result.displayName);
  sceneStep(`${where}: applying pane fields`);

  if (spec.category != null) {
    if (!SCENE_CATEGORIES.has(spec.category)) throw sceneError(where, `unknown category "${spec.category}"`);
    const tab = sceneElement(paneEl, `.pane-category-tabs li[data-category="${spec.category}"]`, where, "category tab");
    if (tab.classList.contains("is-tab-disabled")) {
      throw sceneError(where, `category "${spec.category}" is empty in dataset "${spec.dataset}"`);
    }
    pane.switchCategory(spec.category);
  }
  if (spec.setlist != null) {
    const select = sceneElement(paneEl, ".setlist-select", where, "Set List selector");
    if (![...select.options].some((o) => Number(o.value) === spec.setlist)) {
      throw sceneError(where, `"setlist": no Set List ${spec.setlist}`);
    }
    await pane.selectSetlist(spec.setlist);
  }
  if (spec.banks != null) applySceneBanks(paneEl, spec.banks, where);
  // Combis' jumpToEntry() resets the Set List filter (on purpose: a jump
  // must not land on a filtered-out row), so "open" goes first there.
  if (spec.open != null && spec.category === "combis") await applySceneOpen(pane, paneEl, spec, where);
  if (spec.setlistFilter != null) {
    if (spec.category !== "combis") throw sceneError(where, `"setlistFilter" only exists on the combis category`);
    const select = sceneElement(paneEl, '[data-select-control="combis"] .setlist-filter-select', where, "Set List filter");
    if (![...select.options].some((o) => Number(o.value) === spec.setlistFilter)) {
      throw sceneError(where, `"setlistFilter": no Set List ${spec.setlistFilter}`);
    }
    setControlValue(select, String(spec.setlistFilter), "change");
  }
  if (spec.filter != null) {
    const selector = spec.category === "setlist" || spec.category == null ? '[data-category-panel="setlist"] .filter-input' : ".library-filter";
    setControlValue(sceneElement(paneEl, selector, where, "filter box"), String(spec.filter), "input");
  }
  if (spec.open != null && spec.category !== "combis") await applySceneOpen(pane, paneEl, spec, where);
  return spec.open != null ? () => scrollToOpenedRow(pane, paneEl, spec, where) : null;
}

// Run once the page has settled: scrolling the opened row into place while
// the panel is still laying itself out lands it in the wrong spot (seen:
// under the sticky header, or a Duplicates list jumping to its end). Same
// helpers a jump uses -- the Setlist panel's own (via openSetlistSlot(),
// which only scrolls when the sections are already open), pane.js's for
// Programs/Combis; Duplicates scrolls in its own box, which neither knows.
async function scrollToOpenedRow(pane, paneEl, spec, where) {
  if (spec.category === "setlist") {
    await pane.openSetlistSlot(spec.open.song, spec.open.sections || ["comment"]);
    return;
  }
  const row = paneEl.querySelector(`[data-panel="${spec.category}"] tr.is-selected`);
  if (!row) throw sceneError(where, `"open": the opened row isn't shown (filtered out?)`);
  if (spec.category === "duplicates") row.scrollIntoView({ block: "center" });
  else scrollRowBelowHeader(row);
}

// Every open dataset's full path -> its file name, in text, <option> labels
// and tooltips -- the private data directory stays out of the docs.
function hideScenePaths(datasets) {
  const paths = Object.values(datasets).sort((a, b) => b.length - a.length);
  const shorten = (text) => paths.reduce((t, p) => t.split(p).join(p.split(/[\\/]/).pop()), text);
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    const shortened = shorten(node.nodeValue);
    if (shortened !== node.nodeValue) node.nodeValue = shortened;
  }
  for (const el of document.querySelectorAll("[title]")) el.title = shorten(el.title);
}

// An overlay for an element outside the window would silently draw nothing
// useful -- refuse instead.
function onScreenRect(el, where, selector) {
  const rect = el.getBoundingClientRect();
  if (rect.width === 0 || rect.bottom <= 0 || rect.top >= window.innerHeight || rect.right <= 0 || rect.left >= window.innerWidth) {
    throw sceneError(where, `${selector} is hidden or outside the window`);
  }
  return rect;
}

// A drawn stand-in for a native tooltip (hovering can't be scripted, and the
// real one is drawn by the OS outside the page) -- the element's own title
// text, just below it.
function showSceneTooltip(selector) {
  const el = sceneElement(document, selector, '"tooltip"', "element");
  if (!el.title) throw sceneError('"tooltip"', `${selector} has no title text`);
  const rect = onScreenRect(el, '"tooltip"', selector);
  const tip = document.createElement("div");
  tip.className = "scene-tooltip";
  tip.textContent = el.title;
  tip.style.left = `${Math.round(rect.left + rect.width / 2)}px`;
  tip.style.top = `${Math.round(rect.bottom + 6)}px`;
  document.body.appendChild(tip);
}

// Same for an open <select>: its options in a menu drawn over the control,
// a check mark on the current one.
function showSceneDropdown(selector) {
  const select = sceneElement(document, selector, '"dropdown"', "element");
  if (select.tagName !== "SELECT") throw sceneError('"dropdown"', `${selector} is not a <select>`);
  const rect = onScreenRect(select, '"dropdown"', selector);
  const menu = document.createElement("div");
  menu.className = "scene-dropdown";
  for (const option of select.options) {
    const item = document.createElement("div");
    item.className = "scene-dropdown-item";
    item.textContent = option.textContent;
    if (option.selected) item.classList.add("is-checked");
    menu.appendChild(item);
  }
  menu.style.left = `${Math.round(rect.left)}px`;
  menu.style.top = `${Math.round(rect.top)}px`;
  menu.style.minWidth = `${Math.round(rect.width)}px`;
  document.body.appendChild(menu);
}

// A timer tick, not requestAnimationFrame(): WebKit pauses rAF while the
// window is fully covered, which would hang a run instead of failing it.
const nextFrame = () => new Promise((resolve) => setTimeout(resolve, 16));

// Done when fonts are loaded and nothing has moved for 10 frames in a row --
// scrollRowBelowHeader() scrolls smoothly, and Tabulator redraws after the
// last await, so a fixed delay would catch either half-way.
async function settleScene() {
  sceneStep("settling: fonts");
  await document.fonts.ready;
  sceneStep("settling: waiting for layout/scrolling to stop");
  const scrollers = [...document.querySelectorAll("*")].filter((el) => el.scrollHeight > el.clientHeight);
  const describe = (el) => `${el.tagName.toLowerCase()}.${[...el.classList].join(".")}`;
  const snapshot = () => [
    ...scrollers.map((el) => `${describe(el)} scroll ${el.scrollTop},${el.scrollLeft} of ${el.scrollHeight}`),
    `page height ${document.body.scrollHeight}`,
  ];
  const deadline = performance.now() + SCENE_SETTLE_TIMEOUT_MS;
  let last = snapshot();
  for (let still = 0; still < 10; ) {
    await nextFrame();
    const now = snapshot();
    const moved = now.filter((value, i) => value !== last[i]);
    still = moved.length === 0 ? still + 1 : 0;
    if (performance.now() > deadline) throw new Error(`the page kept moving -- never settled (${moved.join("; ")}, was ${last.filter((v, i) => v !== now[i]).join("; ")})`);
    last = now;
  }
}

// Native code resizes the window for a scene with its own "window" just
// before handing it over; the page's layout follows asynchronously.
async function waitForWindowSize(width, height) {
  const deadline = performance.now() + SCENE_SETTLE_TIMEOUT_MS;
  while (window.innerWidth !== width || window.innerHeight !== height) {
    if (performance.now() > deadline) {
      throw new Error(`window is ${window.innerWidth}x${window.innerHeight}, expected ${width}x${height}`);
    }
    await nextFrame();
  }
}

// `pending` = getPendingScene()'s result: {scene, datasets: {alias: path},
// windowWidth, windowHeight, index, total}.
// `deps` = app.js's own panes/refreshDatasets/setPaneVisibility/swapPanes,
// passed in rather than reached for.
async function runScene(pending, deps) {
  const { scene, datasets } = pending;
  document.body.classList.add("scene-mode");
  try {
    sceneStep("waiting for the window size");
    await waitForWindowSize(pending.windowWidth, pending.windowHeight);
    checkSceneKeys(scene, SCENE_KEYS, `scene "${scene.id}"`);
    // A fresh page starts unswapped (left = pane A); swapping first means
    // "left"/"right" below always name what's visually there.
    // Opened first, in this order, so their #numbers are predictable -- also
    // the way to have a dataset open that no pane shows.
    if (scene.openDatasets != null) {
      if (!Array.isArray(scene.openDatasets)) throw sceneError('"openDatasets"', "must be an array of dataset aliases");
      for (const alias of scene.openDatasets) await openSceneDataset(alias, datasets, '"openDatasets"', deps.refreshDatasets);
    }
    if (scene.swapped != null && typeof scene.swapped !== "boolean") throw sceneError('"swapped"', "must be true or false");
    if (scene.swapped) deps.swapPanes();
    const leftId = scene.swapped ? "B" : "A";
    const rightId = scene.swapped ? "A" : "B";
    const rescrolls = [];
    if (scene.left != null) rescrolls.push(await applyScenePane("left", leftId, scene.left, datasets, deps));
    if (scene.right != null) rescrolls.push(await applyScenePane("right", rightId, scene.right, datasets, deps));
    if (scene.visibility != null) {
      if (!["both", "left", "right"].includes(scene.visibility)) throw sceneError('"visibility"', "must be both, left or right");
      deps.setPaneVisibility(scene.visibility);
    }
    await settleScene();
    sceneStep("scrolling opened rows into place");
    for (const rescroll of rescrolls) if (rescroll) await rescroll();
    await settleScene();
    document.getElementById("statusBar").textContent = "Ready.";
    hideScenePaths(datasets);
    if (scene.dropdown != null) showSceneDropdown(scene.dropdown);
    if (scene.tooltip != null) showSceneTooltip(scene.tooltip);
    await settleScene();
  } catch (err) {
    await window.sceneFailed(scene.id, err && err.message ? err.message : String(err));
    return;
  }
  sceneStep("capturing");
  const result = await window.captureScene(scene.id);
  if (result.ok && !result.done) location.reload();
}
