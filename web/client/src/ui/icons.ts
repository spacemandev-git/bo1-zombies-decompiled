// icons.ts - small inline SVG icons.

const NS = "http://www.w3.org/2000/svg";

function svg(paths: string[], cls: string, viewBox = "0 0 24 24"): SVGSVGElement {
  const el = document.createElementNS(NS, "svg");
  el.setAttribute("viewBox", viewBox);
  el.setAttribute("class", `icon ${cls}`);
  el.setAttribute("aria-hidden", "true");
  for (const d of paths) {
    const p = document.createElementNS(NS, "path");
    p.setAttribute("d", d);
    el.appendChild(p);
  }
  return el;
}

export const icons = {
  pad: () =>
    svg(
      [
        "M7 7h10a5 5 0 0 1 4.9 6l-.8 4a2.5 2.5 0 0 1-4.3 1.2L14.5 16h-5l-2.3 2.2A2.5 2.5 0 0 1 2.9 17l-.8-4A5 5 0 0 1 7 7z",
        "M7 10v4M5 12h4",
        "M16 11h.01M18 13h.01",
      ],
      "icon-pad",
    ),
  crown: () => svg(["M3 18h18l-2-10-5 4-2-6-2 6-5-4z"], "icon-crown"),
  check: () => svg(["M4 12l5 5L20 6"], "icon-check"),
  cross: () => svg(["M6 6l12 12M18 6L6 18"], "icon-cross"),
  lock: () => svg(["M6 11h12v9H6z", "M8 11V8a4 4 0 0 1 8 0v3"], "icon-lock"),
  globe: () => svg(["M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z", "M3 12h18", "M12 3c3 3 3 15 0 18c-3-3-3-15 0-18z"], "icon-globe"),
  folder: () => svg(["M3 6h6l2 2h10v11H3z"], "icon-folder"),
  shield: () => svg(["M12 3l8 3v6c0 5-3.5 8-8 9c-4.5-1-8-4-8-9V6z", "M8.5 12l2.5 2.5L16 9.5"], "icon-shield"),
};
