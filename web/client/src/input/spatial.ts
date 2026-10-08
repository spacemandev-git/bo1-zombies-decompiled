// spatial.ts - picks the element a direction press moves focus to (pure, unit-tested).

export type Dir = "up" | "down" | "left" | "right";

export interface Rect {
  left: number;
  top: number;
  right: number;
  bottom: number;
}

function gap(a0: number, a1: number, b0: number, b1: number): number {
  if (b1 < a0) return a0 - b1;
  if (b0 > a1) return b0 - a1;
  return 0; // overlapping ranges
}

/**
 * The best candidate in direction `dir` from `from`: candidates must lie beyond `from` in that direction; the
 * score favours the nearest one along the axis and strongly penalises distance across it, so focus stays in the
 * same row or column when one exists.
 */
export function pickNext<T>(from: Rect, candidates: ReadonlyArray<{ rect: Rect; item: T }>, dir: Dir): T | null {
  const fcx = (from.left + from.right) / 2;
  const fcy = (from.top + from.bottom) / 2;
  let best: T | null = null;
  let bestScore = Infinity;
  for (const c of candidates) {
    const r = c.rect;
    const cx = (r.left + r.right) / 2;
    const cy = (r.top + r.bottom) / 2;
    let primary: number;
    let cross: number;
    let centerCross: number;
    switch (dir) {
      case "right":
        if (!(cx > fcx + 1 && r.right > from.right)) continue;
        primary = Math.max(0, r.left - from.right);
        cross = gap(from.top, from.bottom, r.top, r.bottom);
        centerCross = Math.abs(cy - fcy);
        break;
      case "left":
        if (!(cx < fcx - 1 && r.left < from.left)) continue;
        primary = Math.max(0, from.left - r.right);
        cross = gap(from.top, from.bottom, r.top, r.bottom);
        centerCross = Math.abs(cy - fcy);
        break;
      case "down":
        if (!(cy > fcy + 1 && r.bottom > from.bottom)) continue;
        primary = Math.max(0, r.top - from.bottom);
        cross = gap(from.left, from.right, r.left, r.right);
        centerCross = Math.abs(cx - fcx);
        break;
      case "up":
        if (!(cy < fcy - 1 && r.top < from.top)) continue;
        primary = Math.max(0, from.top - r.bottom);
        cross = gap(from.left, from.right, r.left, r.right);
        centerCross = Math.abs(cx - fcx);
        break;
    }
    const score = primary + cross * 3 + centerCross * 0.05;
    if (score < bestScore) {
      bestScore = score;
      best = c.item;
    }
  }
  return best;
}
