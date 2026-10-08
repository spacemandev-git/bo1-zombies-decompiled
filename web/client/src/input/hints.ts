// hints.ts - the bottom hint bar: controller glyphs when a pad was used last, keyboard keys otherwise.

import { clear, h, put } from "../ui/dom";
import { icons } from "../ui/icons";
import { nav, type NavAction, type PadButton } from "./nav";

const PAD_GLYPH: Record<PadButton, string> = {
  a: "A",
  b: "B",
  x: "X",
  y: "Y",
  lb: "LB",
  rb: "RB",
  start: "☰",
  back: "⧉",
};

export function glyph(b: PadButton): HTMLElement {
  return h("span", { class: ["glyph", `glyph-${b}`], "aria-label": b.toUpperCase() }, PAD_GLYPH[b]);
}

function key(label: string): HTMLElement {
  return h("kbd", { class: "key" }, label);
}

function hint(pad: boolean, a: Pick<NavAction, "button" | "label" | "key" | "keyLabel">): HTMLElement | null {
  if (pad) return h("span", { class: "hint" }, glyph(a.button), a.label);
  const k = a.keyLabel ?? (a.key ? (a.key.length === 1 ? a.key.toUpperCase() : a.key) : null);
  if (!k) return null;
  return h("span", { class: "hint" }, key(k), a.label);
}

export function mountHints(host: HTMLElement): void {
  const render = () => {
    clear(host);
    if (nav.suspended) {
      host.hidden = true;
      return;
    }
    host.hidden = false;
    const pad = nav.mode === "pad";
    const items: (HTMLElement | null)[] = [];
    if (pad) items.push(h("span", { class: "hint" }, glyph("a"), "Select"));
    else items.push(h("span", { class: "hint" }, key("↑↓←→"), "Move"), h("span", { class: "hint" }, key("Enter"), "Select"));
    if (nav.canGoBack()) items.push(hint(pad, { button: "b", label: nav.inDialog() ? "Close" : "Back", keyLabel: "Esc" }));
    for (const a of nav.activeActions()) if (!a.hidden) items.push(hint(pad, a));
    put(host, ...items.filter((x): x is HTMLElement => x !== null));
    if (!nav.padConnected && !nav.padSeen && typeof navigator.getGamepads === "function") {
      put(host, h("span", { class: "hint hint-pad", "data-testid": "pad-hint" }, icons.pad(), "Using a controller? Press any button on it."));
    } else if (nav.padConnected && !pad) {
      put(host, h("span", { class: "hint hint-pad" }, icons.pad(), "Controller ready"));
    }
  };
  nav.onChange(render);
  render();
}
