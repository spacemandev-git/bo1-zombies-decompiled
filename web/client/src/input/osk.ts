// osk.ts - on-screen keyboard for text fields when a controller is used (player name, room code).
//
// A dialog with a key grid navigated like the rest of the UI. X = backspace, Y = space, Start = done, B = cancel.
// The physical keyboard also types into it.

import { h, openDialog } from "../ui/dom";
import { nav } from "./nav";

export interface KeyboardOptions {
  title: string;
  value: string;
  maxLength: number;
  kind: "text" | "code";
}

const TEXT_ROWS = ["1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.!"];
const CODE_ROWS = ["1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"];

export function openKeyboard(opts: KeyboardOptions): Promise<string | null> {
  return new Promise((resolve) => {
    let value = opts.value.slice(0, opts.maxLength);
    let lower = opts.kind === "text";
    let result: string | null = null;
    const display = h("div", { class: "osk-display", "data-testid": "osk-display" });
    const keyButtons: HTMLButtonElement[] = [];
    const renderDisplay = () => {
      display.textContent = value;
      display.appendChild(h("span", { class: "osk-caret" }));
      for (const b of keyButtons) {
        const ch = b.dataset.ch ?? "";
        b.textContent = lower ? ch.toLowerCase() : ch;
      }
    };
    const type = (ch: string) => {
      if (value.length >= opts.maxLength) return;
      value += opts.kind === "code" ? ch.toUpperCase() : ch;
      renderDisplay();
    };
    const backspace = () => {
      value = value.slice(0, -1);
      renderDisplay();
    };
    const done = () => {
      result = value;
      dlg.close();
    };
    const rows = (opts.kind === "code" ? CODE_ROWS : TEXT_ROWS).map((row, r) =>
      h(
        "div",
        { class: "osk-row" },
        [...row].map((ch, c) => {
          const b = h(
            "button",
            {
              class: "osk-key",
              "data-ch": ch,
              "data-nav-id": `osk-${r}-${c}`,
              "data-autofocus": r === 1 && c === 0 ? true : undefined,
              onclick: () => type(lower ? ch.toLowerCase() : ch),
            },
            ch,
          );
          keyButtons.push(b);
          return b;
        }),
      ),
    );
    const controls = h(
      "div",
      { class: "osk-row osk-controls" },
      opts.kind === "text"
        ? h(
            "button",
            {
              class: "osk-key wide",
              onclick: () => {
                lower = !lower;
                renderDisplay();
              },
            },
            "Shift",
          )
        : null,
      opts.kind === "text" ? h("button", { class: "osk-key wider", onclick: () => type(" ") }, "Space") : null,
      h("button", { class: "osk-key wide", onclick: backspace, "aria-label": "Backspace" }, "⌫"),
      h("button", { class: "osk-key wide primary", onclick: done, "data-testid": "osk-done" }, "Done"),
    );
    const onKey = (e: KeyboardEvent) => {
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      if (e.key === "Backspace") {
        e.preventDefault();
        backspace();
      } else if (e.key.length === 1 && /[A-Za-z0-9 _.\-!]/.test(e.key)) {
        if (opts.kind === "code" && !/[A-Za-z0-9]/.test(e.key)) return;
        if (e.key === " " && document.activeElement instanceof HTMLButtonElement) return; // space presses the key
        e.preventDefault();
        type(e.key);
      }
    };
    const dlg = openDialog({
      title: opts.title,
      className: "dialog-osk",
      testid: "osk",
      body: [display, h("div", { class: "osk-keys" }, rows, controls)],
      onClose: () => {
        window.removeEventListener("keydown", onKey, true);
        resolve(result);
      },
    });
    window.addEventListener("keydown", onKey, true);
    nav.setDialogActions(dlg.el, [
      { button: "x", label: "Delete", run: backspace, keyLabel: "⌫" },
      ...(opts.kind === "text" ? [{ button: "y" as const, label: "Space", run: () => type(" ") }] : []),
      { button: "start", label: "Done", run: done },
    ]);
    renderDisplay();
  });
}

/** Opens the keyboard for an <input>, writes the result back and fires input/change events. */
export async function editInputWithKeyboard(input: HTMLInputElement | HTMLTextAreaElement): Promise<void> {
  const kind = input.dataset.osk === "code" ? "code" : "text";
  const title =
    input.getAttribute("aria-label") ??
    (input.id ? document.querySelector(`label[for="${CSS.escape(input.id)}"]`)?.textContent : null) ??
    "Enter text";
  const max = input.maxLength > 0 ? input.maxLength : 32;
  const v = await openKeyboard({ title, value: input.value, maxLength: max, kind });
  if (v !== null) {
    input.value = v;
    input.dispatchEvent(new Event("input", { bubbles: true }));
    input.dispatchEvent(new Event("change", { bubbles: true }));
  }
  if (input.isConnected) input.focus();
}
