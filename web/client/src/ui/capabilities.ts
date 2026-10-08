// capabilities.ts - what this browser can do, and what each missing feature means for the player.

import { clear, h, put } from "./dom";
import { icons } from "./icons";

export type Need = "required" | "recommended" | "optional";

export interface Capability {
  id: string;
  label: string;
  ok: boolean;
  need: Need;
  detail: string; // what it is for
  missing: string; // what it means when it is missing
}

// wasm module with a shared memory and an atomic load (the wasm-feature-detect "threads" probe)
const THREADS_PROBE = new Uint8Array([
  0, 97, 115, 109, 1, 0, 0, 0, 1, 4, 1, 96, 0, 0, 3, 2, 1, 0, 5, 4, 1, 3, 1, 1, 10, 11, 1, 9, 0, 65, 0, 254, 16, 2, 0,
  26, 11,
]);

function wasmThreads(): boolean {
  try {
    if (typeof WebAssembly !== "object" || typeof SharedArrayBuffer !== "function") return false;
    if (!WebAssembly.validate(THREADS_PROBE)) return false;
    const mem = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
    return mem.buffer instanceof SharedArrayBuffer;
  } catch {
    return false;
  }
}

function webgl(): { gl2: boolean; float: boolean; s3tc: boolean; renderer: string | null } {
  const out = { gl2: false, float: false, s3tc: false, renderer: null as string | null };
  try {
    const canvas = document.createElement("canvas");
    const gl = canvas.getContext("webgl2");
    if (!gl) return out;
    out.gl2 = true;
    out.float = !!gl.getExtension("EXT_color_buffer_float");
    out.s3tc = !!gl.getExtension("WEBGL_compressed_texture_s3tc");
    const dbg = gl.getExtension("WEBGL_debug_renderer_info");
    out.renderer = dbg ? String(gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL)) : null;
    gl.getExtension("WEBGL_lose_context")?.loseContext();
  } catch {
    /* no WebGL */
  }
  return out;
}

async function opfsWorks(): Promise<boolean> {
  try {
    if (typeof navigator.storage?.getDirectory !== "function") return false;
    await navigator.storage.getDirectory();
    return true;
  } catch {
    return false;
  }
}

export async function detectCapabilities(): Promise<Capability[]> {
  const gl = webgl();
  const coi = typeof crossOriginIsolated === "boolean" && crossOriginIsolated;
  const sab = typeof SharedArrayBuffer === "function";
  const atomicsWaitAsync = typeof (Atomics as unknown as { waitAsync?: unknown }).waitAsync === "function";
  return [
    {
      id: "isolation",
      label: "Cross-origin isolation + SharedArrayBuffer",
      ok: coi && sab,
      need: "required",
      detail: "The engine runs on several threads that share memory.",
      missing: coi
        ? "SharedArrayBuffer is disabled in this browser: the engine cannot start."
        : "The page is not cross-origin isolated (COOP/COEP headers missing, or an extension/iframe breaks it): the engine cannot start.",
    },
    {
      id: "threads",
      label: "WebAssembly threads",
      ok: wasmThreads(),
      need: "required",
      detail: "Shared wasm memory and atomics for the engine's threads.",
      missing: "The engine cannot run in this browser.",
    },
    {
      id: "webgl2",
      label: "WebGL 2",
      ok: gl.gl2,
      need: "required",
      detail: gl.renderer ? `Renderer: ${gl.renderer}` : "3D rendering.",
      missing: "Nothing can be drawn. Enable hardware acceleration or try another browser.",
    },
    {
      id: "float",
      label: "EXT_color_buffer_float",
      ok: gl.float,
      need: "required",
      detail: "Floating-point render targets (lighting and post effects).",
      missing: "The renderer cannot create its render targets on this GPU / browser.",
    },
    {
      id: "s3tc",
      label: "WEBGL_compressed_texture_s3tc",
      ok: gl.s3tc,
      need: "required",
      detail: "DXT/BC compressed textures, the format of the game's textures.",
      missing: "The game's textures cannot be shown. Most phones and tablets lack it; use a desktop or laptop.",
    },
    {
      id: "opfs",
      label: "Origin Private File System",
      ok: await opfsWorks(),
      need: "required",
      detail: "Private browser storage where your game files are copied (never uploaded).",
      missing: "Your game files cannot be stored. Private / incognito windows often disable it.",
    },
    {
      id: "dirpicker",
      label: "Folder picker (showDirectoryPicker)",
      ok: typeof (window as unknown as { showDirectoryPicker?: unknown }).showDirectoryPicker === "function",
      need: "optional",
      detail: "Pick your game folder directly.",
      missing: "The importer uses the browser's folder upload dialog instead (files still stay on this computer).",
    },
    {
      id: "webrtc",
      label: "WebRTC data channels",
      ok: typeof RTCPeerConnection === "function",
      need: "recommended",
      detail: "Direct low-latency connections between players.",
      missing: "Multiplayer traffic goes through the server relay (higher latency).",
    },
    {
      id: "gamepad",
      label: "Gamepad API",
      ok: typeof navigator.getGamepads === "function",
      need: "optional",
      detail: "Controllers, in the menus and in game.",
      missing: "Controllers cannot be used; keyboard and mouse still work.",
    },
    {
      id: "pointerlock",
      label: "Pointer Lock",
      ok: typeof Element !== "undefined" && "requestPointerLock" in Element.prototype,
      need: "recommended",
      detail: "Mouse look in game.",
      missing: "Mouse aiming will not work; use a controller.",
    },
    {
      id: "waitasync",
      label: "Atomics.waitAsync",
      ok: atomicsWaitAsync,
      need: "optional",
      detail: "Sends game packets the moment the engine writes them.",
      missing: "The page polls for outgoing packets instead (slightly more latency).",
    },
  ];
}

export function capabilitySummary(caps: Capability[]): { ok: boolean; problems: Capability[] } {
  const problems = caps.filter((c) => !c.ok && c.need === "required");
  return { ok: problems.length === 0, problems };
}

export function renderCapabilities(host: HTMLElement, caps: Capability[]): void {
  clear(host);
  const sum = capabilitySummary(caps);
  put(host,
    h(
      "div",
      { class: ["banner", sum.ok ? "banner-ok" : "banner-error"], "data-testid": "caps-summary" },
      sum.ok
        ? "This browser has everything the game needs."
        : `This browser is missing ${sum.problems.length} required feature${sum.problems.length > 1 ? "s" : ""}. The lobby still works; the game will not start here.`,
    ),
    h(
      "ul",
      { class: "caps-list" },
      caps.map((c) =>
        h(
          "li",
          { class: ["cap", c.ok ? "cap-ok" : `cap-missing cap-${c.need}`], "data-cap": c.id },
          h("span", { class: "cap-icon" }, c.ok ? icons.check() : icons.cross()),
          h(
            "div",
            { class: "cap-text" },
            h("div", { class: "cap-label" }, c.label, h("span", { class: "cap-need" }, c.need)),
            h("div", { class: "cap-detail" }, c.ok ? c.detail : c.missing),
          ),
        ),
      ),
    ),
  );
}
