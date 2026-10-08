// probe.ts - the page's own measurement packets on the game transport (never given to the engine).
//
// They travel on the same path as engine packets (WebRTC "game" channel or relay frames), so RTT and loss reflect
// what the engine gets. Layout (24 bytes):
//   0..3   FE FF FF FF   (as a little-endian int32: -2; the engine's netchan never sends it: -1 is
//                        connectionless and sequenced packets start with a sequence number below 0x7FFFFFFF
//                        or a fragment flag on a sequence that never gets that high)
//   4..7   "B1NT"
//   8      type: 1 ping, 2 pong, 3 burst, 4 burst echo
//   9..11  zero
//   12..15 id (u32 LE)
//   16..23 sender time (f64 LE, performance.now() ms)

export const PROBE_SIZE = 24;
const MAGIC = [0xfe, 0xff, 0xff, 0xff, 0x42, 0x31, 0x4e, 0x54];

export const ProbeType = { Ping: 1, Pong: 2, Burst: 3, BurstEcho: 4 } as const;
export type ProbeType = (typeof ProbeType)[keyof typeof ProbeType];

export interface Probe {
  type: ProbeType;
  id: number;
  time: number;
}

export function isProbe(data: Uint8Array): boolean {
  if (data.length !== PROBE_SIZE) return false;
  for (let i = 0; i < MAGIC.length; i++) if (data[i] !== MAGIC[i]) return false;
  return true;
}

export function encodeProbe(p: Probe): Uint8Array<ArrayBuffer> {
  const out = new Uint8Array(PROBE_SIZE);
  out.set(MAGIC, 0);
  const dv = new DataView(out.buffer);
  dv.setUint8(8, p.type);
  dv.setUint32(12, p.id >>> 0, true);
  dv.setFloat64(16, p.time, true);
  return out;
}

export function decodeProbe(data: Uint8Array): Probe | null {
  if (!isProbe(data)) return null;
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const type = dv.getUint8(8);
  if (type < 1 || type > 4) return null;
  return { type: type as ProbeType, id: dv.getUint32(12, true), time: dv.getFloat64(16, true) };
}

/** The reply a receiver sends for a probe (null when the probe is itself a reply). */
export function probeReply(p: Probe): Probe | null {
  if (p.type === ProbeType.Ping) return { type: ProbeType.Pong, id: p.id, time: p.time };
  if (p.type === ProbeType.Burst) return { type: ProbeType.BurstEcho, id: p.id, time: p.time };
  return null;
}

export interface BurstResult {
  slot: number;
  sent: number;
  received: number;
  lossPct: number;
  rttAvg: number | null;
  rttMin: number | null;
  rttMax: number | null;
}

export function summarizeBurst(slot: number, sent: number, rtts: number[]): BurstResult {
  const received = rtts.length;
  return {
    slot,
    sent,
    received,
    lossPct: sent > 0 ? Math.round(((sent - received) / sent) * 1000) / 10 : 0,
    rttAvg: received ? rtts.reduce((a, b) => a + b, 0) / received : null,
    rttMin: received ? Math.min(...rtts) : null,
    rttMax: received ? Math.max(...rtts) : null,
  };
}
