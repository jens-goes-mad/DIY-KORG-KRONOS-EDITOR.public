// KronosEnvelope: a generic connected multi-point curve editor -- draggable
// vertically, point positions fixed evenly along X by array index. This
// file deliberately knows nothing about ADSR, EG, KRONOS, or any specific
// synth engine -- it is driven entirely by the `points` config array passed
// in, so the SAME component instantiates for a 4-stage ADSR, an 8-stage
// vector envelope, or anything else shaped as "N named scalar values with
// their own min/max, shown as a connected curve." See private repo's
// STATE.md ("kronos-ui COMPONENT SET DECIDED...") for how this fits the
// wider kronos-ui family (kronos-select/kronos-knob/etc.) and why this one
// lives in the PUBLIC repo specifically -- it has no engine-specific
// knowledge, unlike anything that decodes real Program bytes.
//
// Uses lit-html for rendering (frontend/vendor/lit-html.js), NOT
// LitElement/Custom Elements -- matches this app's own established pilot
// pattern (see combi-cross-dataset-panel.js's and confirm-dialog.js's own
// comments): a plain factory function owns a container element and calls
// lit.render() on state changes, same shape as
// components/kronos/setlist-editor-comment-and-font.js's
// createSetlistCommentEditor(). "<kronos-envelope>" is a naming convention
// here, not a literal custom-element tag.
//
// Open kronos-envelope.test.html (via a static file server, same as any
// other component here) to develop/test this file in isolation.

let lit = null;
let litHtmlPromise = null;
function loadLitHtml() {
  if (!litHtmlPromise) {
    litHtmlPromise = import("../../vendor/lit-html.js").then((mod) => {
      lit = mod;
      return mod;
    });
  }
  return litHtmlPromise;
}

const VIEW_WIDTH = 480;
const VIEW_HEIGHT = 160;
const PADDING = 16;

function valueToY(point) {
  const t = (point.value - point.min) / (point.max - point.min || 1);
  return VIEW_HEIGHT - PADDING - t * (VIEW_HEIGHT - PADDING * 2);
}

function yToValue(point, y) {
  const t = 1 - (y - PADDING) / (VIEW_HEIGHT - PADDING * 2);
  const clamped = Math.min(1, Math.max(0, t));
  return point.min + clamped * (point.max - point.min);
}

function pointX(index, count) {
  if (count <= 1) return VIEW_WIDTH / 2;
  return PADDING + (index * (VIEW_WIDTH - PADDING * 2)) / (count - 1);
}

// container: an empty element this owns entirely (matches
// createSetlistCommentEditor's own contract).
// initialPoints: [{ name, min, max, value, unit? }, ...] -- NOT copied, but
// never mutated in place either (every change produces a new array), so
// the caller's own reference is safe to keep around and compare against.
// onChange(points): called with the NEW points array after every drag step.
export async function createKronosEnvelope(container, initialPoints, { onChange } = {}) {
  await loadLitHtml();
  container.classList.add("kronos-envelope");

  let points = initialPoints;
  let dragIndex = null;

  function svgYFromEvent(ev) {
    const svg = container.querySelector("svg");
    const rect = svg.getBoundingClientRect();
    const scaleY = VIEW_HEIGHT / rect.height;
    return (ev.clientY - rect.top) * scaleY;
  }

  function onPointerDown(index, ev) {
    ev.preventDefault();
    dragIndex = index;
    ev.target.setPointerCapture(ev.pointerId);
  }

  function onPointerMove(ev) {
    if (dragIndex === null) return;
    const y = svgYFromEvent(ev);
    const newValue = yToValue(points[dragIndex], y);
    points = points.map((p, i) => (i === dragIndex ? { ...p, value: newValue } : p));
    render();
    onChange && onChange(points);
  }

  function onPointerUp() {
    dragIndex = null;
  }

  function render() {
    const coords = points.map((p, i) => ({ x: pointX(i, points.length), y: valueToY(p) }));
    const polylinePoints = coords.map((c) => `${c.x},${c.y}`).join(" ");
    lit.render(
      lit.html`
        <svg
          class="kronos-envelope-svg"
          viewBox="0 0 ${VIEW_WIDTH} ${VIEW_HEIGHT}"
          @pointermove=${onPointerMove}
          @pointerup=${onPointerUp}
          @pointerleave=${onPointerUp}
        >
          <polyline class="kronos-envelope-curve" points=${polylinePoints}></polyline>
          ${coords.map(
            // lit.svg, NOT lit.html -- this is a NESTED template (each circle
            // is its own separate TemplateResult, mapped in), so it has no
            // enclosing <svg> tag of its own to inherit namespace from. Using
            // lit.html here silently parses <circle> as an unknown HTML
            // element (namespaceURI xhtml, not svg) -- it still shows up in
            // the DOM with the right attributes, but renders as a zero-size
            // invisible element. Found by diffing a live circle's
            // getBoundingClientRect()/namespaceURI against the sibling
            // <polyline> (which IS a direct child of the outer <svg> template
            // and gets the SVG namespace correctly) -- not guessed at.
            (c, i) => lit.svg`
              <circle
                class="kronos-envelope-handle"
                cx=${c.x}
                cy=${c.y}
                r="8"
                @pointerdown=${(ev) => onPointerDown(i, ev)}
              ></circle>
            `
          )}
        </svg>
        <div class="kronos-envelope-labels">
          ${points.map(
            (p) => lit.html`
              <div class="kronos-envelope-label">
                <span class="kronos-envelope-label-name">${p.name}</span>
                <span class="kronos-envelope-label-value">${Math.round(p.value)}${p.unit || ""}</span>
              </div>
            `
          )}
        </div>
      `,
      container
    );
  }

  render();

  return {
    getPoints: () => points,
    setPoints: (newPoints) => {
      points = newPoints;
      render();
    },
  };
}
