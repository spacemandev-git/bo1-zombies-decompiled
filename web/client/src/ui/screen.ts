// screen.ts - the screen contract main.ts mounts and unmounts.

import type { App } from "../app";

export interface ScreenInstance {
  unmount(): void;
}

export type ScreenFactory = (host: HTMLElement, app: App) => ScreenInstance;

/** Collects unsubscribe functions and timers of a screen. */
export class Disposer {
  private fns: (() => void)[] = [];
  add(fn: () => void): void {
    this.fns.push(fn);
  }
  interval(fn: () => void, ms: number): void {
    const id = setInterval(fn, ms);
    this.fns.push(() => clearInterval(id));
  }
  dispose(): void {
    for (const fn of this.fns.splice(0)) {
      try {
        fn();
      } catch (e) {
        console.warn(e);
      }
    }
  }
}
