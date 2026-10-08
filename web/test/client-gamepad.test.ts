import { describe, expect, test } from "bun:test";
import {
  encodePad,
  GAMEPAD_STRIDE,
  MAX_GAMEPADS,
  PadSlots,
  readPad,
  readRumble,
  STANDARD_TO_XINPUT,
  writePad,
  XINPUT,
  type PadLike,
} from "../client/src/engine/padstruct";

function pad(pressed: number[] = [], axes: number[] = [0, 0, 0, 0], values: Record<number, number> = {}, mapping = "standard"): PadLike {
  return {
    connected: true,
    mapping,
    axes,
    buttons: Array.from({ length: 17 }, (_, i) => ({ pressed: pressed.includes(i), value: values[i] ?? (pressed.includes(i) ? 1 : 0) })),
  };
}

describe("encodePad", () => {
  test("standard buttons map to XInput bits", () => {
    const expected: [number, number][] = [
      [0, XINPUT.A],
      [1, XINPUT.B],
      [2, XINPUT.X],
      [3, XINPUT.Y],
      [4, XINPUT.LEFT_SHOULDER],
      [5, XINPUT.RIGHT_SHOULDER],
      [8, XINPUT.BACK],
      [9, XINPUT.START],
      [10, XINPUT.LEFT_THUMB],
      [11, XINPUT.RIGHT_THUMB],
      [12, XINPUT.DPAD_UP],
      [13, XINPUT.DPAD_DOWN],
      [14, XINPUT.DPAD_LEFT],
      [15, XINPUT.DPAD_RIGHT],
    ];
    for (const [i, bit] of expected) expect(encodePad(pad([i])).buttons).toBe(bit);
    expect(encodePad(pad([0, 3, 12])).buttons).toBe(XINPUT.A | XINPUT.Y | XINPUT.DPAD_UP);
    // triggers are analog only, never bits; button 16 (guide) is ignored
    expect(encodePad(pad([6, 7, 16])).buttons).toBe(0);
    expect(STANDARD_TO_XINPUT).toHaveLength(16);
  });

  test("contract bit values", () => {
    expect(XINPUT).toEqual({
      DPAD_UP: 0x0001,
      DPAD_DOWN: 0x0002,
      DPAD_LEFT: 0x0004,
      DPAD_RIGHT: 0x0008,
      START: 0x0010,
      BACK: 0x0020,
      LEFT_THUMB: 0x0040,
      RIGHT_THUMB: 0x0080,
      LEFT_SHOULDER: 0x0100,
      RIGHT_SHOULDER: 0x0200,
      A: 0x1000,
      B: 0x2000,
      X: 0x4000,
      Y: 0x8000,
    });
  });

  test("Y axes are flipped (up is positive), X axes kept, values clamped", () => {
    const s = encodePad(pad([], [0.25, -0.75, -0.5, 0.5]));
    expect(s.lx).toBe(0.25);
    expect(s.ly).toBe(0.75);
    expect(s.rx).toBe(-0.5);
    expect(s.ry).toBe(-0.5);
    const c = encodePad(pad([], [2, -3, Number.NaN, 1.5]));
    expect(c.lx).toBe(1);
    expect(c.ly).toBe(1);
    expect(c.rx).toBe(0);
    expect(c.ry).toBe(-1);
    expect(Object.is(encodePad(pad([], [0, 0, 0, 0])).ly, -0)).toBe(false);
  });

  test("analog triggers", () => {
    const s = encodePad(pad([], undefined, { 6: 0.4, 7: 1 }));
    expect(s.lt).toBeCloseTo(0.4);
    expect(s.rt).toBe(1);
  });

  test("non-standard or missing pads are disconnected", () => {
    expect(encodePad(null).connected).toBe(false);
    expect(encodePad(pad([0], undefined, {}, "")).connected).toBe(false);
    expect(encodePad({ ...pad([0]), connected: false })).toEqual({ connected: false, buttons: 0, lt: 0, rt: 0, lx: 0, ly: 0, rx: 0, ry: 0 });
  });
});

describe("bo1_gamepad memory", () => {
  test("field offsets, stride and seq", () => {
    const base = 256;
    const buf = new ArrayBuffer(base + GAMEPAD_STRIDE * MAX_GAMEPADS);
    const dv = new DataView(buf);
    expect(GAMEPAD_STRIDE).toBe(48);
    const s = encodePad(pad([0, 13], [0.5, -1, 0, 0.25], { 6: 0.5 }));
    expect(writePad(buf, base, 2, s)).toBe(1);
    const e = base + 2 * 48;
    expect(dv.getUint32(e + 0, true)).toBe(1); // connected
    expect(dv.getUint32(e + 4, true)).toBe(XINPUT.A | XINPUT.DPAD_DOWN); // buttons
    expect(dv.getFloat32(e + 8, true)).toBeCloseTo(0.5); // lt
    expect(dv.getFloat32(e + 12, true)).toBe(0); // rt
    expect(dv.getFloat32(e + 16, true)).toBeCloseTo(0.5); // lx
    expect(dv.getFloat32(e + 20, true)).toBe(1); // ly (flipped)
    expect(dv.getFloat32(e + 24, true)).toBe(0); // rx
    expect(dv.getFloat32(e + 28, true)).toBeCloseTo(-0.25); // ry (flipped)
    expect(dv.getUint32(e + 40, true)).toBe(1); // seq
    expect(writePad(buf, base, 2, s)).toBe(2);
    expect(dv.getUint32(e + 40, true)).toBe(2);
    // other entries untouched
    expect(dv.getUint32(base + 48 + 40, true)).toBe(0);
    expect(dv.getUint32(base + 3 * 48 + 0, true)).toBe(0);
  });

  test("the page never writes the engine's rumble fields; rumble reads clamp", () => {
    const buf = new SharedArrayBuffer(GAMEPAD_STRIDE * MAX_GAMEPADS);
    const dv = new DataView(buf);
    dv.setFloat32(32, 0.75, true); // rumble_low (engine)
    dv.setFloat32(36, 1.5, true); // rumble_high (engine)
    writePad(buf, 0, 0, encodePad(pad([1])));
    expect(dv.getFloat32(32, true)).toBe(0.75);
    expect(readRumble(buf, 0, 0)).toEqual({ low: 0.75, high: 1 });
    const r = readPad(buf, 0, 0);
    expect(r.buttons).toBe(XINPUT.B);
    expect(r.seq).toBe(1);
    expect(() => writePad(buf, 0, 4, encodePad(null))).toThrow();
  });

  test("seq wraps as u32", () => {
    const buf = new ArrayBuffer(GAMEPAD_STRIDE);
    new DataView(buf).setUint32(40, 0xffffffff, true);
    expect(writePad(buf, 0, 0, encodePad(null))).toBe(0);
  });
});

describe("PadSlots", () => {
  const p = (index: number, mapping = "standard") => ({ ...pad(), index, mapping });
  test("stable slots, lowest free slot for new pads, non-standard ignored", () => {
    const slots = new PadSlots();
    expect(slots.assign([p(0), p(1)])).toEqual([0, 1, null, null]);
    // pad 0 unplugged: pad 1 keeps slot 1
    expect(slots.assign([null, p(1)])).toEqual([null, 1, null, null]);
    // a new pad takes slot 0
    expect(slots.assign([null, p(1), p(2)])).toEqual([2, 1, null, null]);
    expect(slots.assign([null, p(1), p(2), p(3, "")])).toEqual([2, 1, null, null]);
  });
});
