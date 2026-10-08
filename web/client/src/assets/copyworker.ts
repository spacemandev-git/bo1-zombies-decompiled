// copyworker.ts - writes Blobs/Files into OPFS with synchronous access handles (dedicated worker; works in every
// browser with OPFS, including Safari, which has no createWritable on older versions). One job at a time:
//   in:  { type: "write", id, dirs: string[], name, blob }   |   { type: "cancel" }
//   out: { type: "progress", id, written } | { type: "done", id, written } | { type: "error", id, message, cancelled }

interface SyncAccessHandle {
  write(buffer: ArrayBufferView, options?: { at?: number }): number;
  truncate(size: number): void;
  flush(): void;
  close(): void;
}

interface WriteMsg {
  type: "write";
  id: number;
  dirs: string[];
  name: string;
  blob: Blob;
}

type InMsg = WriteMsg | { type: "cancel" };

interface WorkerScope {
  postMessage(msg: unknown): void;
  addEventListener(type: "message", fn: (ev: MessageEvent<InMsg>) => void): void;
}

const scope = self as unknown as WorkerScope;
let cancelled = false;

async function write(m: WriteMsg): Promise<number> {
  let dir = await navigator.storage.getDirectory();
  for (const s of m.dirs) dir = await dir.getDirectoryHandle(s, { create: true });
  const fh = await dir.getFileHandle(m.name, { create: true });
  const create = (fh as unknown as { createSyncAccessHandle?: () => Promise<SyncAccessHandle> }).createSyncAccessHandle;
  if (typeof create !== "function") throw new Error("createSyncAccessHandle is not supported");
  const handle = await create.call(fh);
  let at = 0;
  try {
    handle.truncate(0);
    const reader = m.blob.stream().getReader();
    let lastPost = 0;
    for (;;) {
      if (cancelled) {
        await reader.cancel().catch(() => undefined);
        throw new Error("cancelled");
      }
      const { done, value } = await reader.read();
      if (done) break;
      let off = 0;
      while (off < value.byteLength) {
        const n = handle.write(value.subarray(off), { at: at + off });
        if (n <= 0) throw new Error("write failed (storage full?)");
        off += n;
      }
      at += value.byteLength;
      const now = performance.now();
      if (now - lastPost > 100) {
        lastPost = now;
        scope.postMessage({ type: "progress", id: m.id, written: at });
      }
    }
    handle.flush();
  } finally {
    handle.close();
  }
  return at;
}

scope.addEventListener("message", (ev) => {
  const m = ev.data;
  if (m.type === "cancel") {
    cancelled = true;
    return;
  }
  if (m.type === "write") {
    cancelled = false;
    write(m).then(
      (written) => scope.postMessage({ type: "done", id: m.id, written }),
      (err: unknown) =>
        scope.postMessage({
          type: "error",
          id: m.id,
          cancelled,
          message: err instanceof Error ? err.message : String(err),
        }),
    );
  }
});
