// uipad.ts - reads controllers for the UI (not for the engine: engine/gamepadbridge.ts does that while a game runs).
//
// Standard mapping: D-pad (12-15) or the left stick moves the focus with key repeat (first repeat after 400 ms,
// then every 120 ms); 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 8 Back, 9 Start go to nav.padButton on press.
// Gamepads only show up in navigator.getGamepads() after the user presses a button while the page has focus.

import type { Dir } from "./spatial";
import { nav, type PadButton } from "./nav";

const BUTTONS: [number, PadButton][] = [
  [0, "a"],
  [1, "b"],
  [2, "x"],
  [3, "y"],
  [4, "lb"],
  [5, "rb"],
  [8, "back"],
  [9, "start"],
];

export const REPEAT_DELAY_MS = 400;
export const REPEAT_INTERVAL_MS = 120;
const STICK_THRESHOLD = 0.5;

function padDirection(p: Gamepad): Dir | null {
  const pressed = (i: number) => p.buttons[i]?.pressed === true;
  if (pressed(12)) return "up";
  if (pressed(13)) return "down";
  if (pressed(14)) return "left";
  if (pressed(15)) return "right";
  const x = p.axes[0] ?? 0;
  const y = p.axes[1] ?? 0;
  if (Math.max(Math.abs(x), Math.abs(y)) < STICK_THRESHOLD) return null;
  if (Math.abs(x) > Math.abs(y)) return x > 0 ? "right" : "left";
  return y > 0 ? "down" : "up";
}

export class UiPad {
  private prev = new Map<number, boolean[]>();
  private held: Dir | null = null;
  private nextRepeat = 0;
  private raf = 0;

  start(): void {
    if (typeof navigator.getGamepads !== "function") return;
    window.addEventListener("gamepadconnected", () => nav.setPadConnected(true));
    window.addEventListener("gamepaddisconnected", () => nav.setPadConnected(this.anyConnected()));
    const loop = (t: number) => {
      this.poll(t);
      this.raf = requestAnimationFrame(loop);
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    cancelAnimationFrame(this.raf);
  }

  private pads(): Gamepad[] {
    try {
      return (navigator.getGamepads() ?? []).filter((p): p is Gamepad => !!p && p.connected);
    } catch {
      return [];
    }
  }

  private anyConnected(): boolean {
    return this.pads().length > 0;
  }

  poll(now: number): void {
    const pads = this.pads();
    nav.setPadConnected(pads.length > 0);
    const presses: PadButton[] = [];
    let dir: Dir | null = null;
    for (const p of pads) {
      const before = this.prev.get(p.index) ?? [];
      const nowPressed = p.buttons.map((b) => b.pressed);
      for (const [i, name] of BUTTONS) if (nowPressed[i] && !before[i]) presses.push(name);
      this.prev.set(p.index, nowPressed);
      dir ??= padDirection(p);
    }
    // while the engine runs (or the tab is hidden) the state is tracked but not acted on, so no stale press
    // fires when the UI takes over again
    if (nav.suspended || document.hidden) {
      this.held = dir;
      this.nextRepeat = now + REPEAT_DELAY_MS;
      return;
    }
    if (dir !== this.held) {
      this.held = dir;
      if (dir) {
        nav.padMove(dir);
        this.nextRepeat = now + REPEAT_DELAY_MS;
      }
    } else if (dir && now >= this.nextRepeat) {
      nav.padMove(dir);
      this.nextRepeat = now + REPEAT_INTERVAL_MS;
    }
    for (const b of presses) nav.padButton(b);
  }
}
