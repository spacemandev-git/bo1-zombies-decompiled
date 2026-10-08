// dom.ts - a tiny element builder, toasts and modal dialogs.

export type Child = Node | string | number | null | undefined | false | Child[];

type Handler = (ev: Event) => void;

export interface Props {
  [key: string]: unknown;
  class?: string | (string | false | null | undefined)[];
  style?: string;
}

/** h("button", { class: "btn", onclick: fn, "data-testid": "x", disabled: true }, "Text") */
export function h<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  props?: Props | null,
  ...children: Child[]
): HTMLElementTagNameMap[K] {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === "class") {
        el.className = Array.isArray(v) ? v.filter(Boolean).join(" ") : String(v);
      } else if (k === "style") {
        el.setAttribute("style", String(v));
      } else if (k.startsWith("on") && typeof v === "function") {
        el.addEventListener(k.slice(2), v as Handler);
      } else if (k === "value" || k === "checked" || k === "selected" || k === "indeterminate") {
        (el as unknown as Record<string, unknown>)[k] = v;
      } else if (v === true) {
        el.setAttribute(k, "");
      } else {
        el.setAttribute(k, String(v));
      }
    }
  }
  append(el, children);
  return el;
}

export function append(el: Node, children: Child[]): void {
  for (const c of children) {
    if (c === null || c === undefined || c === false) continue;
    if (Array.isArray(c)) append(el, c);
    else if (c instanceof Node) el.appendChild(c);
    else el.appendChild(document.createTextNode(String(c)));
  }
}

/** el.append() that accepts null / false / nested arrays like h(). */
export function put(el: Element, ...children: Child[]): void {
  append(el, children);
}

export function clear(el: Element): void {
  while (el.firstChild) el.removeChild(el.firstChild);
}

export function $(sel: string, root: ParentNode = document): HTMLElement | null {
  return root.querySelector<HTMLElement>(sel);
}

// ----- toasts -----

export type ToastKind = "info" | "success" | "error" | "warn";

export function toast(message: string, kind: ToastKind = "info", ms = 4500): void {
  let host = document.getElementById("toasts");
  if (!host) {
    host = h("div", { id: "toasts", "aria-live": "polite" });
    document.body.appendChild(host);
  }
  const el = h("div", { class: ["toast", `toast-${kind}`], role: kind === "error" ? "alert" : "status" }, message);
  host.appendChild(el);
  const remove = () => {
    el.classList.add("toast-out");
    setTimeout(() => el.remove(), 250);
  };
  el.addEventListener("click", remove);
  setTimeout(remove, ms);
}

// ----- dialogs -----

export interface DialogOptions {
  title: string;
  body: Child;
  className?: string;
  /** called when the dialog closes for any reason */
  onClose?: () => void;
  /** false: Escape / B do not close it */
  closable?: boolean;
  testid?: string;
}

export interface DialogHandle {
  el: HTMLElement;
  backdrop: HTMLElement;
  close(): void;
  closed: boolean;
  closable: boolean;
}

const dialogStack: DialogHandle[] = [];
const dialogListeners = new Set<() => void>();

export function onDialogsChanged(fn: () => void): () => void {
  dialogListeners.add(fn);
  return () => dialogListeners.delete(fn);
}

export function topDialog(): DialogHandle | null {
  return dialogStack[dialogStack.length - 1] ?? null;
}

export function openDialog(opts: DialogOptions): DialogHandle {
  const previous = document.activeElement instanceof HTMLElement ? document.activeElement : null;
  const titleId = `dlg-${Math.random().toString(36).slice(2, 8)}`;
  const el = h(
    "div",
    {
      class: ["dialog", opts.className],
      role: "dialog",
      "aria-modal": "true",
      "aria-labelledby": titleId,
      "data-testid": opts.testid,
    },
    h("h2", { class: "dialog-title", id: titleId }, opts.title),
    h("div", { class: "dialog-body" }, opts.body),
  );
  const backdrop = h("div", { class: "dialog-backdrop" }, el);
  const handle: DialogHandle = {
    el,
    backdrop,
    closed: false,
    closable: opts.closable !== false,
    close() {
      if (handle.closed) return;
      handle.closed = true;
      const i = dialogStack.indexOf(handle);
      if (i >= 0) dialogStack.splice(i, 1);
      backdrop.remove();
      opts.onClose?.();
      if (previous && previous.isConnected) previous.focus({ preventScroll: false });
      for (const fn of dialogListeners) fn();
    },
  };
  backdrop.addEventListener("mousedown", (ev) => {
    if (ev.target === backdrop && handle.closable) handle.close();
  });
  let host = document.getElementById("dialogs");
  if (!host) {
    host = h("div", { id: "dialogs" });
    document.body.appendChild(host);
  }
  host.appendChild(backdrop);
  dialogStack.push(handle);
  for (const fn of dialogListeners) fn();
  // focus the first focusable control (or the one marked data-autofocus)
  queueMicrotask(() => {
    const target =
      el.querySelector<HTMLElement>("[data-autofocus]") ??
      el.querySelector<HTMLElement>("button:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex='0']");
    target?.focus();
  });
  return handle;
}

export function confirmDialog(title: string, message: string, okLabel = "OK", cancelLabel = "Cancel"): Promise<boolean> {
  return new Promise((resolve) => {
    let result = false;
    const dlg = openDialog({
      title,
      testid: "confirm-dialog",
      body: [
        h("p", null, message),
        h(
          "div",
          { class: "row end" },
          h("button", { class: "btn", onclick: () => dlg.close(), "data-testid": "confirm-cancel" }, cancelLabel),
          h(
            "button",
            {
              class: "btn primary",
              "data-autofocus": true,
              "data-testid": "confirm-ok",
              onclick: () => {
                result = true;
                dlg.close();
              },
            },
            okLabel,
          ),
        ),
      ],
      onClose: () => resolve(result),
    });
  });
}

/** Lets the user pick one option (used for <select> with a controller, and option lists). */
export function chooseDialog<T>(
  title: string,
  options: { value: T; label: string; detail?: string; disabled?: boolean }[],
  current?: T,
): Promise<T | null> {
  return new Promise((resolve) => {
    let result: T | null = null;
    const list = h(
      "div",
      { class: "choose-list" },
      options.map((o) =>
        h(
          "button",
          {
            class: ["btn", "choose-item", o.value === current && "selected"],
            disabled: o.disabled,
            "data-autofocus": o.value === current ? true : undefined,
            onclick: () => {
              result = o.value;
              dlg.close();
            },
          },
          h("span", { class: "choose-label" }, o.label),
          o.detail ? h("span", { class: "choose-detail" }, o.detail) : null,
        ),
      ),
    );
    const dlg = openDialog({ title, body: list, className: "dialog-choose", onClose: () => resolve(result) });
  });
}

export async function copyText(text: string): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    const ta = h("textarea", { style: "position:fixed;opacity:0" });
    ta.value = text;
    document.body.appendChild(ta);
    ta.select();
    let ok = false;
    try {
      ok = document.execCommand("copy");
    } catch {
      ok = false;
    }
    ta.remove();
    return ok;
  }
}

export function fmtMs(ms: number | null | undefined): string {
  return ms === null || ms === undefined ? "–" : `${Math.round(ms)} ms`;
}
