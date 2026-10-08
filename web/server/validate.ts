// validate.ts - hand-written validators for every ClientMessage (web/shared/protocol.ts).
//
// Each validator checks types, lengths and ranges and returns a clean copy (unknown keys are dropped), so the lobby
// never forwards or stores anything the client added. Cross-field rules (mods vs map, maxPlayers vs seated players)
// need the room and live in lobby.ts.

import type { ClientMessage, RoomSettings, RtcSignal } from "../shared/protocol";
import { findMap, findMod, MAPS, MAX_PLAYERS_EXTENDED, MODS, ORIGINAL_CHARACTER_COUNT } from "../shared/roster";

export const NAME_MAX = 24;
export const SDP_MAX_BYTES = 16 * 1024;
export const CANDIDATE_MAX = 2048;
export const PEER_ID_MAX = 64;
export const RESUME_TOKEN_MAX = 128;
export const RTT_MAX_MS = 600_000;

/** Room codes: 5 characters without 0/O/1/I/L. */
export const ROOM_CODE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
export const ROOM_CODE_LENGTH = 5;

export type Result<T> = { ok: true; value: T } | { ok: false; message: string };

const ok = <T>(value: T): Result<T> => ({ ok: true, value });
const fail = <T>(message: string): Result<T> => ({ ok: false, message });

type Obj = Record<string, unknown>;
function isObj(v: unknown): v is Obj {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

/** UTF-8 byte length of a string without allocating. */
export function utf8Length(s: string): number {
  let n = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 0x80) n += 1;
    else if (c < 0x800) n += 2;
    else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length) {
      const d = s.charCodeAt(i + 1);
      if (d >= 0xdc00 && d <= 0xdfff) {
        n += 4;
        i++;
      } else n += 3;
    } else n += 3;
  }
  return n;
}

// control, format, surrogate, private-use and unassigned code points, and line / paragraph separators
const NAME_FORBIDDEN = /[\p{C}\p{Zl}\p{Zp}]/u;

/** A player name: trimmed, 1..24 printable characters. */
export function validateName(v: unknown): Result<string> {
  if (typeof v !== "string") return fail("name must be a string");
  if (v.length > NAME_MAX * 4) return fail(`name must be at most ${NAME_MAX} characters`);
  const name = v.normalize("NFC").trim();
  const len = [...name].length;
  if (len < 1 || len > NAME_MAX) return fail(`name must be 1..${NAME_MAX} characters`);
  if (NAME_FORBIDDEN.test(name)) return fail("name may only contain printable characters");
  return ok(name);
}

/** A room code as typed: trimmed and upper-cased. Wrong-looking codes are reported as "not found" by the lobby. */
export function normalizeRoomCode(v: unknown): Result<string> {
  if (typeof v !== "string" || v.length > 32) return fail("code must be a string");
  return ok(v.trim().toUpperCase());
}

export function isRoomCode(code: string): boolean {
  if (code.length !== ROOM_CODE_LENGTH) return false;
  for (const ch of code) if (!ROOM_CODE_ALPHABET.includes(ch)) return false;
  return true;
}

function validatePeerId(v: unknown, field: string): Result<string> {
  if (typeof v !== "string" || v.length < 1 || v.length > PEER_ID_MAX) return fail(`${field} must be a peer id`);
  return ok(v);
}

/** Partial room settings; every present field is checked, absent fields stay absent. */
export function validateSettings(v: unknown): Result<Partial<RoomSettings>> {
  if (!isObj(v)) return fail("settings must be an object");
  const out: Partial<RoomSettings> = {};
  if (v.map !== undefined) {
    if (typeof v.map !== "string" || !findMap(v.map)) return fail(`settings.map must be one of the lobby's maps`);
    out.map = v.map;
  }
  if (v.mods !== undefined) {
    if (!Array.isArray(v.mods) || v.mods.length > MODS.length) return fail("settings.mods must be a list of mod ids");
    const mods: string[] = [];
    for (const id of v.mods) {
      if (typeof id !== "string" || !findMod(id)) return fail(`settings.mods: unknown mod ${JSON.stringify(id)}`);
      if (mods.includes(id)) return fail(`settings.mods: ${id} is listed twice`);
      mods.push(id);
    }
    out.mods = mods;
  }
  if (v.maxPlayers !== undefined) {
    if (typeof v.maxPlayers !== "number" || !Number.isInteger(v.maxPlayers) || v.maxPlayers < 1 || v.maxPlayers > MAX_PLAYERS_EXTENDED) {
      return fail(`settings.maxPlayers must be an integer 1..${MAX_PLAYERS_EXTENDED}`);
    }
    out.maxPlayers = v.maxPlayers;
  }
  if (v.allowDuplicates !== undefined) {
    if (typeof v.allowDuplicates !== "boolean") return fail("settings.allowDuplicates must be true or false");
    out.allowDuplicates = v.allowDuplicates;
  }
  if (v.visibility !== undefined) {
    if (v.visibility !== "private" && v.visibility !== "public") return fail('settings.visibility must be "private" or "public"');
    out.visibility = v.visibility;
  }
  return ok(out);
}

function optionalNullableString(v: unknown, max: number): v is string | null | undefined {
  return v === undefined || v === null || (typeof v === "string" && v.length <= max);
}

export function validateSignal(v: unknown): Result<RtcSignal> {
  if (!isObj(v)) return fail("signal must be an object");
  if (v.type === "offer" || v.type === "answer") {
    if (typeof v.sdp !== "string" || v.sdp.length === 0 || utf8Length(v.sdp) > SDP_MAX_BYTES) {
      return fail(`signal.sdp must be a non-empty string of at most ${SDP_MAX_BYTES} bytes`);
    }
    return ok({ type: v.type, sdp: v.sdp });
  }
  if (v.type === "candidate") {
    if (v.candidate === null) return ok({ type: "candidate", candidate: null });
    const c = v.candidate;
    if (!isObj(c)) return fail("signal.candidate must be an object or null");
    const init: RTCIceCandidateInit = {};
    if (c.candidate !== undefined) {
      if (typeof c.candidate !== "string" || c.candidate.length > CANDIDATE_MAX) return fail("signal.candidate.candidate must be a short string");
      init.candidate = c.candidate;
    }
    if (!optionalNullableString(c.sdpMid, 64)) return fail("signal.candidate.sdpMid must be a short string or null");
    if (c.sdpMid !== undefined) init.sdpMid = c.sdpMid;
    if (c.sdpMLineIndex !== undefined && c.sdpMLineIndex !== null) {
      if (typeof c.sdpMLineIndex !== "number" || !Number.isInteger(c.sdpMLineIndex) || c.sdpMLineIndex < 0 || c.sdpMLineIndex > 65535) {
        return fail("signal.candidate.sdpMLineIndex must be an integer 0..65535 or null");
      }
    }
    if (c.sdpMLineIndex !== undefined) init.sdpMLineIndex = c.sdpMLineIndex as number | null;
    if (!optionalNullableString(c.usernameFragment, 256)) return fail("signal.candidate.usernameFragment must be a short string or null");
    if (c.usernameFragment !== undefined) init.usernameFragment = c.usernameFragment;
    return ok({ type: "candidate", candidate: init });
  }
  return fail('signal.type must be "offer", "answer" or "candidate"');
}

function finite(v: unknown): v is number {
  return typeof v === "number" && Number.isFinite(v);
}

/** Validates a parsed JSON value as a ClientMessage. */
export function validateClientMessage(raw: unknown): Result<ClientMessage> {
  if (!isObj(raw)) return fail("a message must be a JSON object");
  const t = raw.t;
  if (typeof t !== "string") return fail('a message needs a string field "t"');
  switch (t) {
    case "hello": {
      if (typeof raw.version !== "number" || !Number.isInteger(raw.version)) return fail("hello.version must be an integer");
      const name = validateName(raw.name);
      if (!name.ok) return name;
      if (raw.resumeToken !== undefined && (typeof raw.resumeToken !== "string" || raw.resumeToken.length > RESUME_TOKEN_MAX)) {
        return fail("hello.resumeToken must be a string");
      }
      const msg: ClientMessage = { t, version: raw.version, name: name.value };
      if (typeof raw.resumeToken === "string" && raw.resumeToken !== "") msg.resumeToken = raw.resumeToken;
      return ok(msg);
    }
    case "rename": {
      const name = validateName(raw.name);
      return name.ok ? ok({ t, name: name.value }) : name;
    }
    case "room.create":
    case "room.settings": {
      const s = validateSettings(raw.settings ?? {});
      return s.ok ? ok({ t, settings: s.value }) : s;
    }
    case "room.join": {
      const code = normalizeRoomCode(raw.code);
      return code.ok ? ok({ t, code: code.value }) : code;
    }
    case "room.leave":
    case "room.list":
    case "room.start":
    case "room.ended":
      return ok({ t });
    case "room.kick": {
      const id = validatePeerId(raw.peerId, "room.kick.peerId");
      return id.ok ? ok({ t, peerId: id.value }) : id;
    }
    case "slot.character": {
      const c = raw.character;
      if (c !== null && (typeof c !== "number" || !Number.isInteger(c) || c < 0 || c >= ORIGINAL_CHARACTER_COUNT)) {
        return fail(`slot.character.character must be null or an integer 0..${ORIGINAL_CHARACTER_COUNT - 1}`);
      }
      return ok({ t, character: c });
    }
    case "slot.ready":
      if (typeof raw.ready !== "boolean") return fail("slot.ready.ready must be true or false");
      return ok({ t, ready: raw.ready });
    case "slot.files":
      if (typeof raw.hasGameFiles !== "boolean") return fail("slot.files.hasGameFiles must be true or false");
      if (raw.maps === undefined) return ok({ t, hasGameFiles: raw.hasGameFiles });
      if (!Array.isArray(raw.maps) || raw.maps.length > MAPS.length) return fail("slot.files.maps must be a list of map ids");
      if (!raw.maps.every((m: unknown) => typeof m === "string" && findMap(m) !== undefined)) {
        return fail("slot.files.maps has an unknown map id");
      }
      return ok({ t, hasGameFiles: raw.hasGameFiles, maps: [...new Set(raw.maps as string[])] });
    case "rtc.signal": {
      const to = validatePeerId(raw.to, "rtc.signal.to");
      if (!to.ok) return to;
      const signal = validateSignal(raw.signal);
      return signal.ok ? ok({ t, to: to.value, signal: signal.value }) : signal;
    }
    case "net.status": {
      if (raw.mode !== "p2p" && raw.mode !== "relay") return fail('net.status.mode must be "p2p" or "relay"');
      const rtt = raw.rttMs;
      if (rtt !== null && (!finite(rtt) || rtt < 0 || rtt > RTT_MAX_MS)) return fail(`net.status.rttMs must be null or 0..${RTT_MAX_MS}`);
      return ok({ t, mode: raw.mode, rttMs: rtt === null ? null : Math.round(rtt) });
    }
    case "ping":
      if (!finite(raw.time)) return fail("ping.time must be a number");
      return ok({ t, time: raw.time });
    default:
      return fail(`unknown message type ${JSON.stringify(t.slice(0, 32))}`);
  }
}
