import { describe, expect, test } from "bun:test";
import { slotAddress } from "../shared/protocol";
import {
  NET_MAX_PACKET,
  NET_SLOT_SIZE,
  packIp,
  readNetShared,
  ringByteSize,
  ringDrain,
  ringHeader,
  ringInit,
  ringRead,
  ringUsed,
  ringWrite,
  slotIp,
  slotOfIp,
  unpackIp,
  writeIndexAddress,
  writeLocalIp,
} from "../client/src/engine/ring";

function bytes(n: number, seed: number): Uint8Array<ArrayBuffer> {
  return Uint8Array.from({ length: n }, (_, i) => (i * 31 + seed) & 0xff);
}

function makeRing(capacity: number, base = 64, shared = false, start = 0) {
  const size = base + ringByteSize(capacity);
  const buf = shared ? new SharedArrayBuffer(size) : new ArrayBuffer(size);
  ringInit(buf, base, capacity, NET_SLOT_SIZE, start);
  return { buf, base };
}

describe("addresses", () => {
  test("packing matches the contract", () => {
    expect(packIp("10.66.0.1")).toBe(0x0100420a);
    expect(unpackIp(0x0100420a)).toBe("10.66.0.1");
    expect(slotIp(0)).toBe(packIp(slotAddress(0)));
    expect(slotIp(7)).toBe(packIp("10.66.0.8"));
    expect(() => packIp("10.66.0")).toThrow();
    expect(() => packIp("10.66.0.256")).toThrow();
  });
  test("slot of an address", () => {
    expect(slotOfIp(packIp("10.66.0.1"))).toBe(0);
    expect(slotOfIp(packIp("10.66.0.4"))).toBe(3);
    expect(slotOfIp(packIp("10.66.0.255"))).toBe("broadcast");
    expect(slotOfIp(packIp("255.255.255.255"))).toBe("broadcast");
    expect(slotOfIp(packIp("10.66.0.0"))).toBeNull();
    expect(slotOfIp(packIp("192.168.1.4"))).toBeNull();
  });
});

describe("ring layout", () => {
  test("header and slot fields are where the C struct puts them", () => {
    const { buf, base } = makeRing(4, 128);
    const dv = new DataView(buf);
    expect(dv.getUint32(base + 0, true)).toBe(4); // capacity
    expect(dv.getUint32(base + 4, true)).toBe(NET_SLOT_SIZE); // slot_size
    expect(dv.getUint32(base + 8, true)).toBe(0); // write_index
    expect(dv.getUint32(base + 12, true)).toBe(0); // read_index
    expect(writeIndexAddress(base)).toBe(base + 8);

    const data = bytes(100, 7);
    expect(ringWrite(buf, base, packIp("10.66.0.2"), 28960, data)).toBe(true);
    expect(dv.getUint32(base + 8, true)).toBe(1);
    const slot0 = base + 16;
    expect(dv.getUint32(slot0, true)).toBe(0x0200420a);
    expect(dv.getUint16(slot0 + 4, true)).toBe(28960);
    expect(dv.getUint16(slot0 + 6, true)).toBe(100);
    expect(new Uint8Array(buf, slot0 + 8, 100)).toEqual(data);

    // second packet goes into slot 1 at +16 + 1408
    ringWrite(buf, base, 1, 2, bytes(3, 1));
    expect(dv.getUint16(base + 16 + NET_SLOT_SIZE + 6, true)).toBe(3);
  });

  test("an engine-written slot is read back by the page", () => {
    const { buf, base } = makeRing(8, 0);
    const dv = new DataView(buf);
    // emulate the engine producer
    dv.setUint32(base + 16, packIp("10.66.0.3"), true);
    dv.setUint16(base + 16 + 4, 28960, true);
    dv.setUint16(base + 16 + 6, 5, true);
    new Uint8Array(buf, base + 16 + 8, 5).set([1, 2, 3, 4, 5]);
    dv.setUint32(base + 8, 1, true);
    const p = ringRead(buf, base);
    expect(p).not.toBeNull();
    expect(p!.ip).toBe(packIp("10.66.0.3"));
    expect(p!.port).toBe(28960);
    expect([...p!.data]).toEqual([1, 2, 3, 4, 5]);
    expect(dv.getUint32(base + 12, true)).toBe(1);
    expect(ringRead(buf, base)).toBeNull();
  });
});

describe("ring behaviour", () => {
  test("round trip, FIFO, copies survive slot reuse", () => {
    const { buf, base } = makeRing(4);
    const a = bytes(10, 1);
    const b = bytes(1264, 2);
    ringWrite(buf, base, 11, 1, a);
    ringWrite(buf, base, 22, 2, b);
    expect(ringUsed(buf, base)).toBe(2);
    const pa = ringRead(buf, base)!;
    // overwrite the freed slot; the returned copy must not change
    for (let i = 0; i < 4; i++) ringWrite(buf, base, 99, 9, bytes(10, 50 + i));
    expect(pa.ip).toBe(11);
    expect(pa.data).toEqual(a);
    const pb = ringRead(buf, base)!;
    expect(pb.ip).toBe(22);
    expect(pb.data).toEqual(b);
  });

  test("a full ring drops new packets until the consumer reads", () => {
    const { buf, base } = makeRing(4);
    for (let i = 0; i < 4; i++) expect(ringWrite(buf, base, i, 0, bytes(8, i))).toBe(true);
    expect(ringWrite(buf, base, 4, 0, bytes(8, 4))).toBe(false);
    expect(ringUsed(buf, base)).toBe(4);
    expect(ringRead(buf, base)!.ip).toBe(0);
    expect(ringWrite(buf, base, 5, 0, bytes(8, 5))).toBe(true);
    const ips: number[] = [];
    expect(ringDrain(buf, base, (p) => ips.push(p.ip))).toBe(4);
    expect(ips).toEqual([1, 2, 3, 5]);
    expect(ringUsed(buf, base)).toBe(0);
  });

  test("slot wraparound over many packets", () => {
    const { buf, base } = makeRing(4);
    for (let i = 0; i < 37; i++) {
      const d = bytes((i * 97) % NET_MAX_PACKET, i);
      expect(ringWrite(buf, base, i, i, d)).toBe(true);
      if (i % 3 === 0) expect(ringWrite(buf, base, 1000 + i, i, bytes(5, i))).toBe(true);
      let p = ringRead(buf, base)!;
      expect(p.ip).toBe(i);
      expect(p.data).toEqual(d);
      if (i % 3 === 0) {
        p = ringRead(buf, base)!;
        expect(p.ip).toBe(1000 + i);
      }
    }
    const h = ringHeader(buf, base);
    expect(h.writeIndex).toBe(h.readIndex);
    expect(h.writeIndex).toBe(37 + 13);
  });

  test("u32 index counters wrap around 2^32", () => {
    const { buf, base } = makeRing(4, 0, false, 0xfffffffe);
    for (let i = 0; i < 4; i++) expect(ringWrite(buf, base, i, 0, bytes(4, i))).toBe(true);
    expect(ringWrite(buf, base, 9, 0, bytes(4, 9))).toBe(false); // full across the wrap
    expect(ringHeader(buf, base).writeIndex).toBe(2);
    expect(ringUsed(buf, base)).toBe(4);
    const ips: number[] = [];
    ringDrain(buf, base, (p) => ips.push(p.ip));
    expect(ips).toEqual([0, 1, 2, 3]);
    expect(ringHeader(buf, base).readIndex).toBe(2);
  });

  test("packet size limits", () => {
    const { buf, base } = makeRing(2);
    expect(ringWrite(buf, base, 1, 1, bytes(NET_MAX_PACKET + 1, 0))).toBe(false);
    expect(ringWrite(buf, base, 1, 1, bytes(NET_MAX_PACKET, 0))).toBe(true);
    expect(ringRead(buf, base)!.data.length).toBe(NET_MAX_PACKET);
    expect(ringWrite(buf, base, 1, 1, new Uint8Array(0))).toBe(true);
    expect(ringRead(buf, base)!.data.length).toBe(0);
  });

  test("works on SharedArrayBuffer (wasm memory)", () => {
    const { buf, base } = makeRing(256, 1024, true);
    expect(buf).toBeInstanceOf(SharedArrayBuffer);
    ringWrite(buf, base, 7, 28960, bytes(64, 3));
    const p = ringRead(buf, base)!;
    expect(p.data.buffer).toBeInstanceOf(ArrayBuffer); // a plain copy, sendable on a data channel
    expect(p.data).toEqual(bytes(64, 3));
  });

  test("unaligned addresses are rejected", () => {
    const buf = new ArrayBuffer(4096);
    expect(() => ringInit(buf, 2, 4)).toThrow();
    expect(() => ringInit(buf, 0, 3)).toThrow();
  });
});

describe("bo1_net_shared", () => {
  test("pointers, local_ip and ready", () => {
    const buf = new ArrayBuffer(256);
    const ptr = 32;
    const dv = new DataView(buf);
    dv.setUint32(ptr + 0, 1000, true);
    dv.setUint32(ptr + 4, 2000, true);
    dv.setUint32(ptr + 12, 1, true);
    writeLocalIp(buf, ptr, slotIp(2));
    expect(dv.getUint32(ptr + 8, true)).toBe(packIp("10.66.0.3"));
    expect(readNetShared(buf, ptr)).toEqual({ toPage: 1000, fromPage: 2000, localIp: packIp("10.66.0.3"), ready: 1 });
  });
});
