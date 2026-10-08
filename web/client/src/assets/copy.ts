// copy.ts - streams Blobs/Files into OPFS under bo1/ with progress and cancel.
//
// Preferred path: the copy worker (copyworker.ts, synchronous access handles: fast, and the only writer Safari
// had for a long time). Fallback: FileSystemFileHandle.createWritable() + Blob.stream().pipeTo() on the main
// thread. Writes are serialized (one file at a time).

import { getDir, splitPath } from "./opfs";

declare const __BO1_COPY_WORKER__: string | undefined;

export class CopyCancelled extends Error {
  constructor() {
    super("cancelled");
  }
}

interface WorkerReply {
  type: "progress" | "done" | "error";
  id: number;
  written?: number;
  message?: string;
  cancelled?: boolean;
}

let worker: Worker | null = null;
let workerBroken = false;
let nextId = 1;
let chain: Promise<unknown> = Promise.resolve();

function workerUrl(): string | null {
  try {
    if (typeof __BO1_COPY_WORKER__ === "string" && __BO1_COPY_WORKER__) {
      return new URL(__BO1_COPY_WORKER__, document.baseURI).toString();
    }
  } catch {
    /* not defined in this build */
  }
  return null;
}

function getWorker(): Worker | null {
  if (workerBroken) return null;
  if (worker) return worker;
  const url = workerUrl();
  if (!url || typeof Worker === "undefined") return null;
  try {
    worker = new Worker(url, { type: "module", name: "bo1-copy" });
    return worker;
  } catch {
    workerBroken = true;
    return null;
  }
}

function viaWorker(w: Worker, dirs: string[], name: string, blob: Blob, onProgress?: (n: number) => void, signal?: AbortSignal): Promise<number> {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const onAbort = () => w.postMessage({ type: "cancel" });
    const cleanup = () => {
      w.removeEventListener("message", onMsg);
      w.removeEventListener("error", onErr);
      signal?.removeEventListener("abort", onAbort);
    };
    const onMsg = (ev: MessageEvent<WorkerReply>) => {
      const m = ev.data;
      if (m.id !== id) return;
      if (m.type === "progress") onProgress?.(m.written ?? 0);
      else if (m.type === "done") {
        cleanup();
        resolve(m.written ?? blob.size);
      } else {
        cleanup();
        if (m.cancelled || signal?.aborted) reject(new CopyCancelled());
        else reject(new Error(m.message ?? "write failed"));
      }
    };
    const onErr = () => {
      cleanup();
      workerBroken = true;
      worker = null;
      reject(new Error("worker failed to load"));
    };
    w.addEventListener("message", onMsg);
    w.addEventListener("error", onErr);
    signal?.addEventListener("abort", onAbort, { once: true });
    if (signal?.aborted) onAbort();
    w.postMessage({ type: "write", id, dirs, name, blob });
  });
}

async function viaWritable(dirs: string[], name: string, blob: Blob, onProgress?: (n: number) => void, signal?: AbortSignal): Promise<number> {
  const dir = await getDir(dirs, true);
  const fh = await dir.getFileHandle(name, { create: true });
  if (typeof fh.createWritable !== "function") {
    throw new Error("This browser cannot write files into its private storage (no createWritable or worker).");
  }
  const writable = await fh.createWritable({ keepExistingData: false });
  let written = 0;
  let last = 0;
  const counter = new TransformStream<Uint8Array, Uint8Array>({
    transform(chunk, ctl) {
      written += chunk.byteLength;
      const now = performance.now();
      if (now - last > 100) {
        last = now;
        onProgress?.(written);
      }
      ctl.enqueue(chunk);
    },
  });
  try {
    await blob.stream().pipeThrough(counter).pipeTo(writable, { signal });
  } catch (e) {
    if (signal?.aborted) throw new CopyCancelled();
    throw e;
  }
  return written;
}

/**
 * Writes `blob` to bo1/<path>. Resolves with the bytes written; rejects with CopyCancelled when `signal` aborts.
 */
export function writeBlob(path: string, blob: Blob, onProgress?: (written: number) => void, signal?: AbortSignal): Promise<number> {
  const segs = splitPath(path);
  const name = segs.pop();
  if (!name) return Promise.reject(new Error(`bad path ${path}`));
  const run = async (): Promise<number> => {
    if (signal?.aborted) throw new CopyCancelled();
    const w = getWorker();
    if (w) {
      try {
        return await viaWorker(w, segs, name, blob, onProgress, signal);
      } catch (e) {
        if (e instanceof CopyCancelled) throw e;
        const msg = e instanceof Error ? e.message : String(e);
        if (!/createSyncAccessHandle|worker failed/i.test(msg)) throw e;
        workerBroken = true; // fall through to the main-thread writer
      }
    }
    return viaWritable(segs, name, blob, onProgress, signal);
  };
  const p = chain.then(run, run);
  chain = p.catch(() => undefined);
  return p;
}

export function writeText(path: string, text: string): Promise<number> {
  return writeBlob(path, new Blob([text], { type: "text/plain" }));
}
