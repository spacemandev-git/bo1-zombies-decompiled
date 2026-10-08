// nav.ts - focus navigation for the whole UI, driven by the keyboard and (through uipad.ts) controllers.
//
// The focus moves spatially between focusable elements of the active layer (the top dialog, else the app).
// A / Enter activates, B / Escape goes back (closes the dialog or leaves the screen), and each screen registers
// context actions for X, Y, LB, RB, Start and Back (with keyboard keys) which the hint bar shows.
// While the engine runs, the layer is suspended and consumes no input.

import { chooseDialog, onDialogsChanged, topDialog } from "../ui/dom";
import { pickNext, type Dir, type Rect } from "./spatial";

export type PadButton = "a" | "b" | "x" | "y" | "lb" | "rb" | "start" | "back";
export type InputMode = "pad" | "keyboard" | "mouse";

export interface NavAction {
  button: PadButton;
  label: string;
  run: () => void;
  key?: string; // KeyboardEvent.key, matched case-insensitively when the focus is not in a text field
  keyLabel?: string; // label in keyboard hints (default: key)
  hidden?: boolean; // keep out of the hint bar
}

const FOCUSABLE =
  "button:not([disabled]), input:not([disabled]):not([type='hidden']), select:not([disabled]), textarea:not([disabled]), a[href], [tabindex]:not([tabindex='-1'])";

const TEXT_TYPES = new Set(["text", "search", "email", "url", "password", "tel", ""]);

export function isTextInput(el: Element | null): el is HTMLInputElement | HTMLTextAreaElement {
  if (el instanceof HTMLTextAreaElement) return true;
  return el instanceof HTMLInputElement && TEXT_TYPES.has(el.type);
}

function visible(el: HTMLElement): boolean {
  if (el.closest("[inert], [hidden], [aria-hidden='true']")) return false;
  const r = el.getBoundingClientRect();
  return r.width > 0 && r.height > 0;
}

function rectOf(el: Element): Rect {
  const r = el.getBoundingClientRect();
  return { left: r.left, top: r.top, right: r.right, bottom: r.bottom };
}

/** Opens a text editor for an input when a controller activates it (set by main.ts: the on-screen keyboard). */
export type TextEditor = (input: HTMLInputElement | HTMLTextAreaElement) => void;

class NavController {
  mode: InputMode = "mouse";
  suspended = false;
  padSeen = false;
  padConnected = false;
  private root: HTMLElement = document.body;
  private screenActions: NavAction[] = [];
  private backHandler: (() => void) | null = null;
  private dialogActions = new WeakMap<HTMLElement, NavAction[]>();
  private listeners = new Set<() => void>();
  private textEditor: TextEditor | null = null;

  init(root: HTMLElement): void {
    this.root = root;
    window.addEventListener("keydown", this.onKey, true);
    window.addEventListener("pointerdown", () => this.setMode("mouse"), true);
    window.addEventListener("mousemove", (e) => {
      if (Math.abs(e.movementX) + Math.abs(e.movementY) > 4) this.setMode("mouse");
    });
    onDialogsChanged(() => this.emit());
    this.applyModeClass();
  }

  setTextEditor(fn: TextEditor): void {
    this.textEditor = fn;
  }

  /** The current screen's context actions and back handler. */
  setScreen(actions: NavAction[], onBack: (() => void) | null): void {
    this.screenActions = actions;
    this.backHandler = onBack;
    this.emit();
  }

  setDialogActions(dialog: HTMLElement, actions: NavAction[]): void {
    this.dialogActions.set(dialog, actions);
    this.emit();
  }

  onChange(fn: () => void): () => void {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  private emit(): void {
    for (const fn of this.listeners) fn();
  }

  setMode(m: InputMode): void {
    if (m === "pad") this.padSeen = true;
    if (this.mode === m) return;
    this.mode = m;
    this.applyModeClass();
    this.emit();
  }

  private applyModeClass(): void {
    const b = document.body.classList;
    b.toggle("input-pad", this.mode === "pad");
    b.toggle("input-keyboard", this.mode === "keyboard");
    b.toggle("input-mouse", this.mode === "mouse");
  }

  setSuspended(s: boolean): void {
    this.suspended = s;
    this.emit();
  }

  setPadConnected(c: boolean): void {
    if (this.padConnected === c) return;
    this.padConnected = c;
    this.emit();
  }

  activeRoot(): HTMLElement {
    return topDialog()?.el ?? this.root;
  }

  inDialog(): boolean {
    return topDialog() !== null;
  }

  activeActions(): NavAction[] {
    const d = topDialog();
    if (d) return this.dialogActions.get(d.el) ?? [];
    return this.screenActions;
  }

  canGoBack(): boolean {
    const d = topDialog();
    return d ? d.closable : this.backHandler !== null;
  }

  focusables(root = this.activeRoot()): HTMLElement[] {
    return [...root.querySelectorAll<HTMLElement>(FOCUSABLE)].filter(visible);
  }

  current(): HTMLElement | null {
    const a = document.activeElement;
    if (a instanceof HTMLElement && a !== document.body && this.activeRoot().contains(a) && visible(a)) return a;
    return null;
  }

  focusFirst(root = this.activeRoot()): HTMLElement | null {
    const el = root.querySelector<HTMLElement>("[data-autofocus]:not([disabled])") ?? this.focusables(root)[0] ?? null;
    if (el) this.focus(el);
    return el;
  }

  focus(el: HTMLElement): void {
    el.focus({ preventScroll: true });
    el.scrollIntoView({ block: "nearest", inline: "nearest" });
  }

  /** A stable key for the focused element (data-nav-id or id) so screens can restore focus after re-rendering. */
  focusKey(): string | null {
    const el = this.current();
    if (!el) return null;
    return el.getAttribute("data-nav-id") ?? (el.id ? `#${el.id}` : null);
  }

  restoreFocus(key: string | null, root = this.root): void {
    if (!key || this.inDialog()) return;
    const el = key.startsWith("#")
      ? document.getElementById(key.slice(1))
      : root.querySelector<HTMLElement>(`[data-nav-id="${CSS.escape(key)}"]`);
    if (el && root.contains(el) && !(el as HTMLButtonElement).disabled) el.focus({ preventScroll: true });
  }

  move(dir: Dir): void {
    const cur = this.current();
    if (!cur) {
      this.focusFirst();
      return;
    }
    const cands = this.focusables()
      .filter((el) => el !== cur)
      .map((el) => ({ rect: rectOf(el), item: el }));
    const next = pickNext(rectOf(cur), cands, dir);
    if (next) this.focus(next);
  }

  activate(): void {
    const el = this.current();
    if (!el) {
      this.focusFirst();
      return;
    }
    if (isTextInput(el)) {
      if (this.mode === "pad" && this.textEditor && !el.readOnly) this.textEditor(el);
      else el.focus();
      return;
    }
    if (el instanceof HTMLSelectElement) {
      void this.chooseSelect(el);
      return;
    }
    el.click();
  }

  async chooseSelect(sel: HTMLSelectElement): Promise<void> {
    const label =
      sel.getAttribute("aria-label") ??
      (sel.id ? document.querySelector(`label[for="${CSS.escape(sel.id)}"]`)?.textContent : null) ??
      "Choose";
    const options = [...sel.options].map((o) => ({ value: o.value, label: o.textContent ?? o.value, disabled: o.disabled }));
    const v = await chooseDialog(label, options, sel.value);
    if (v !== null && v !== sel.value) {
      sel.value = v;
      sel.dispatchEvent(new Event("input", { bubbles: true }));
      sel.dispatchEvent(new Event("change", { bubbles: true }));
    }
    if (sel.isConnected) sel.focus();
  }

  back(): void {
    const d = topDialog();
    if (d) {
      if (d.closable) d.close();
      return;
    }
    this.backHandler?.();
  }

  /** A context button (X, Y, LB, RB, Start, Back). Returns true when an action ran. */
  runButton(b: PadButton): boolean {
    const action = this.activeActions().find((a) => a.button === b);
    if (!action) return false;
    action.run();
    return true;
  }

  private fallback: ((b: PadButton) => void) | null = null;

  /** Handles context buttons no screen action claimed (main.ts: LB/RB switch tabs). */
  setFallback(fn: (b: PadButton) => void): void {
    this.fallback = fn;
  }

  /** Called by uipad.ts for every new button press. */
  padButton(b: PadButton): void {
    if (this.suspended) return;
    this.setMode("pad");
    if (b === "a") this.activate();
    else if (b === "b") this.back();
    else if (!this.runButton(b) && !this.inDialog()) this.fallback?.(b);
  }

  padMove(dir: Dir): void {
    if (this.suspended) return;
    this.setMode("pad");
    this.move(dir);
  }

  private onKey = (e: KeyboardEvent): void => {
    if (this.suspended || e.ctrlKey || e.metaKey || e.altKey) return;
    const target = e.target instanceof Element ? e.target : null;
    const typing = isTextInput(target);
    const dirs: Record<string, Dir> = { ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right" };
    const dir = dirs[e.key];
    if (dir) {
      // in a text field left/right move the caret
      if (typing && (dir === "left" || dir === "right")) return;
      this.setMode("keyboard");
      e.preventDefault();
      this.move(dir);
      return;
    }
    if (e.key === "Escape") {
      this.setMode("keyboard");
      e.preventDefault();
      if (typing && !this.inDialog()) {
        (target as HTMLElement).blur();
        return;
      }
      this.back();
      return;
    }
    if (e.key === "Tab") {
      this.setMode("keyboard");
      return;
    }
    if (e.key === "Enter") {
      this.setMode("keyboard");
      if (target instanceof HTMLSelectElement) {
        e.preventDefault();
        void this.chooseSelect(target);
      } else if (target instanceof HTMLInputElement && (target.type === "checkbox" || target.type === "radio")) {
        e.preventDefault();
        target.click();
      } else if (typing && target instanceof HTMLElement) {
        const submit = target.getAttribute("data-submit");
        if (submit) {
          e.preventDefault();
          document.getElementById(submit)?.click();
        }
      }
      return;
    }
    if (typing || e.repeat) return;
    const key = e.key.toLowerCase();
    const action = this.activeActions().find((a) => a.key?.toLowerCase() === key);
    if (action) {
      this.setMode("keyboard");
      e.preventDefault();
      action.run();
    }
  };
}

export const nav = new NavController();
