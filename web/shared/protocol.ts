// protocol.ts - the lobby / signaling protocol between the browser and the web server (server/index.ts).
//
// One WebSocket per browser tab at <BASE_PATH>/ws. Text frames carry the JSON messages below; binary frames carry
// relayed game packets (the fallback when a WebRTC data channel to the host cannot be opened):
//
//   byte 0      RELAY_FRAME (0x01)
//   byte 1      slot: on send the destination slot, on receive the source slot (same room)
//   byte 2..    the game packet, unchanged
//
// Game traffic is a star: every client talks only to the host, who runs the listen server in its own tab.
// The engine addresses each slot as a fixed fake IPv4 address (slotAddress) so its own netcode needs no changes.
//
// WebSocket close codes the server uses: 1008 rate limit or protocol abuse, 1012 server restarting (redeploy),
// 4000 the same player reconnected from another socket (resumeToken).

import type { ModInfo } from "./roster";

export const PROTOCOL_VERSION = 1;
export const RELAY_FRAME = 0x01;
export const GAME_PORT = 28960;

/** The address the engine sees for the player in `slot` (host = slot 0 unless the host picked another slot). */
export function slotAddress(slot: number): string {
  return `10.66.0.${slot + 1}`;
}

export type RoomPhase = "lobby" | "starting" | "in-game";

export interface SlotState {
  index: number; // client slot = the client number the engine gives the player
  peerId: string | null;
  name: string;
  character: number | null; // 0..3, null = not picked yet
  ready: boolean;
  isHost: boolean;
  connected: boolean; // false while a dropped peer may still resume
  hasGameFiles: boolean; // the player imported the files this room's map needs
  maps: string[]; // every map the player has the files for (empty until the player reports it)
}

export interface RoomSettings {
  map: string;
  mods: ModInfo["id"][]; // optional mods from shared/roster.ts MODS, in load order
  maxPlayers: number; // 1..8; above 4 duplicate characters are forced on
  allowDuplicates: boolean;
  visibility: "private" | "public";
}

export interface RoomState {
  code: string;
  hostPeerId: string;
  phase: RoomPhase;
  settings: RoomSettings;
  slots: SlotState[];
}

export interface RoomSummary {
  code: string;
  hostName: string;
  map: string;
  mods: string[];
  players: number;
  maxPlayers: number;
}

export interface LaunchPlayer {
  slot: number; // lobby slot (its fake address is slotAddress(slot))
  clientNum: number; // the engine client number the host's server gives this player (shared/launch.ts assignClientNums)
  name: string;
  character: number; // 0..3
}

/** What every tab needs to start its engine once the host starts the game (built by shared/launch.ts buildLaunch). */
export interface LaunchConfig {
  roomCode: string;
  map: string;
  mods: string[];
  hostSlot: number;
  yourSlot: number;
  maxPlayers: number;
  players: LaunchPlayer[];
}

export interface IceServer {
  urls: string | string[];
  username?: string;
  credential?: string;
}

export type RtcSignal =
  | { type: "offer"; sdp: string }
  | { type: "answer"; sdp: string }
  | { type: "candidate"; candidate: RTCIceCandidateInit | null };

// ----- browser -> server -----

export type ClientMessage =
  | { t: "hello"; version: number; name: string; resumeToken?: string }
  | { t: "rename"; name: string }
  | { t: "room.create"; settings: Partial<RoomSettings> }
  | { t: "room.join"; code: string }
  | { t: "room.leave" }
  | { t: "room.list" }
  | { t: "room.settings"; settings: Partial<RoomSettings> } // host only
  | { t: "room.kick"; peerId: string } // host only
  | { t: "room.start" } // host only
  | { t: "room.ended" } // host only: the game is over, back to the lobby
  | { t: "slot.character"; character: number | null }
  | { t: "slot.ready"; ready: boolean }
  | { t: "slot.files"; hasGameFiles: boolean; maps?: string[] } // with maps, the server derives hasGameFiles itself
  | { t: "rtc.signal"; to: string; signal: RtcSignal }
  | { t: "net.status"; mode: "p2p" | "relay"; rttMs: number | null }
  | { t: "ping"; time: number };

// ----- server -> browser -----

export type ErrorCode =
  | "bad-message"
  | "version"
  | "not-in-room"
  | "room-not-found"
  | "room-full"
  | "server-full" // the server holds MAX_ROOMS rooms
  | "room-started"
  | "not-host"
  | "character-taken"
  | "not-ready"
  | "rate-limited";

export type ServerMessage =
  | { t: "welcome"; peerId: string; resumeToken: string; iceServers: IceServer[] }
  | { t: "room.state"; room: RoomState; yourPeerId: string }
  | { t: "room.list"; rooms: RoomSummary[] }
  | { t: "room.left"; reason: "left" | "kicked" | "closed" }
  | { t: "room.started"; launch: LaunchConfig }
  | { t: "rtc.signal"; from: string; signal: RtcSignal }
  | { t: "net.status"; peerId: string; mode: "p2p" | "relay"; rttMs: number | null }
  | { t: "pong"; time: number }
  | { t: "error"; code: ErrorCode; message: string };
