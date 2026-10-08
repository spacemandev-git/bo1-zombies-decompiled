// static.ts - file responses: safe path resolution, MIME types, cache policy, precompressed siblings, ranges, ETags.

import { stat } from "node:fs/promises";
import path from "node:path";

export const CACHE_IMMUTABLE = "public, max-age=31536000, immutable";
export const CACHE_SHORT = "public, max-age=300";
export const CACHE_REVALIDATE = "no-cache";
export const CACHE_NONE = "no-store";

const TEXT = "text/plain; charset=utf-8";

const MIME: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".map": "application/json; charset=utf-8",
  ".webmanifest": "application/manifest+json",
  ".wasm": "application/wasm",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".webp": "image/webp",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".ico": "image/x-icon",
  ".avif": "image/avif",
  ".woff2": "font/woff2",
  ".woff": "font/woff",
  ".ttf": "font/ttf",
  ".mp3": "audio/mpeg",
  ".ogg": "audio/ogg",
  ".wav": "audio/wav",
  ".mp4": "video/mp4",
  ".webm": "video/webm",
  ".txt": TEXT,
  ".md": TEXT,
  // game scripts and data (mods/, data/main/)
  ".gsc": TEXT,
  ".csc": TEXT,
  ".info": TEXT,
  ".ents": TEXT,
  ".cfg": TEXT,
  ".str": TEXT,
  ".csv": TEXT,
};

export function contentType(file: string): string {
  return MIME[path.extname(file).toLowerCase()] ?? "application/octet-stream";
}

/**
 * True when a file name carries a content hash, as bundlers name chunks: "index-3fk2a9xq.js", "app.5d41402a.css".
 * The hash part is 8..64 letters/digits containing at least one digit (so "-settings.js" is not a hash).
 */
export function isHashedAsset(file: string): boolean {
  const m = /[.-]([A-Za-z0-9]{8,64})(\.[A-Za-z0-9]+)+$/.exec(path.basename(file));
  return m !== null && /\d/.test(m[1]!);
}

/**
 * Decodes and checks a URL path below a served directory ("/a/b.js" -> ["a", "b.js"]). Returns null for anything
 * that could leave the directory or name a hidden file: bad escapes, "..", ".", dot-names, empty segments, encoded
 * slashes or backslashes, NUL and other control characters, drive letters.
 */
export function safeSegments(urlPath: string): string[] | null {
  const raw = urlPath.replace(/^\/+/, "");
  if (raw === "") return [];
  const out: string[] = [];
  for (const part of raw.split("/")) {
    let seg: string;
    try {
      seg = decodeURIComponent(part);
    } catch {
      return null;
    }
    if (seg === "" || seg.startsWith(".") || /[\\/:\x00-\x1f\x7f]/.test(seg)) return null;
    out.push(seg);
  }
  return out;
}

/** Joins checked segments under root and verifies the result is still inside root. */
export function resolveInside(root: string, segments: string[]): string | null {
  const base = path.resolve(root);
  const full = path.resolve(base, ...segments);
  return full.startsWith(base + path.sep) ? full : null;
}

export interface FileInfo {
  size: number;
  mtimeMs: number;
}

export async function fileInfo(file: string): Promise<FileInfo | null> {
  try {
    const s = await stat(file);
    return s.isFile() ? { size: s.size, mtimeMs: s.mtimeMs } : null;
  } catch {
    return null;
  }
}

/** Parses Accept-Encoding and returns the encodings the client accepts (q > 0). */
export function acceptedEncodings(header: string | null): Set<string> {
  const out = new Set<string>();
  if (!header) return out;
  for (const part of header.split(",")) {
    const [name, ...params] = part.trim().toLowerCase().split(";");
    if (!name) continue;
    const q = params.map((p) => p.trim()).find((p) => p.startsWith("q="));
    if (q !== undefined && !(Number(q.slice(2)) > 0)) continue;
    out.add(name.trim());
  }
  return out;
}

export function etagMatches(header: string | null, etag: string): boolean {
  if (!header) return false;
  if (header.trim() === "*") return true;
  const bare = etag.replace(/^W\//, "");
  return header.split(",").some((t) => t.trim().replace(/^W\//, "") === bare);
}

/** Parses a single "bytes=a-b" range against a file size. null = no usable Range header, "invalid" = 416. */
export function parseRange(header: string | null, size: number): { start: number; end: number } | "invalid" | null {
  if (!header) return null;
  const m = /^bytes=(\d*)-(\d*)$/.exec(header.trim());
  if (!m) return null; // multiple ranges or another unit: answer with the whole file
  const [, a, b] = m;
  if (a === "" && b === "") return "invalid";
  let start: number;
  let end: number;
  if (a === "") {
    const suffix = Number(b);
    if (suffix === 0) return "invalid";
    start = Math.max(0, size - suffix);
    end = size - 1;
  } else {
    start = Number(a);
    end = b === "" ? size - 1 : Math.min(Number(b), size - 1);
  }
  if (start >= size || start > end) return "invalid";
  return { start, end };
}

export interface ServeOptions {
  cacheControl: string;
  /** Overrides the MIME type picked from the file name. */
  type?: string;
}

/**
 * Answers a GET/HEAD for one file: 304 on a matching ETag, a 206/416 for a Range request (identity encoding), else
 * the .br or .gz sibling when the client accepts it, else the file. Returns null when the file does not exist.
 */
export async function serveFile(req: Request, file: string, opts: ServeOptions): Promise<Response | null> {
  const info = await fileInfo(file);
  if (!info) return null;
  const type = opts.type ?? contentType(file);
  const tag = `"${info.size.toString(16)}-${Math.floor(info.mtimeMs).toString(16)}`;
  const headers = new Headers({ "Content-Type": type, "Cache-Control": opts.cacheControl, Vary: "Accept-Encoding" });

  const range = req.headers.get("range");
  const ifRange = req.headers.get("if-range");
  const useRange = range !== null && (ifRange === null || ifRange === `${tag}"`);
  if (useRange) {
    const r = parseRange(range, info.size);
    if (r === "invalid") {
      headers.set("Content-Range", `bytes */${info.size}`);
      return new Response(null, { status: 416, headers });
    }
    if (r) {
      headers.set("ETag", `${tag}"`);
      headers.set("Accept-Ranges", "bytes");
      headers.set("Content-Range", `bytes ${r.start}-${r.end}/${info.size}`);
      return new Response(Bun.file(file).slice(r.start, r.end + 1, type), { status: 206, headers });
    }
  }

  const accepted = acceptedEncodings(req.headers.get("accept-encoding"));
  for (const [enc, ext] of [
    ["br", ".br"],
    ["gzip", ".gz"],
  ] as const) {
    if (!accepted.has(enc)) continue;
    const sibling = file + ext;
    const sInfo = await fileInfo(sibling);
    if (!sInfo || sInfo.mtimeMs < info.mtimeMs) continue; // missing, or stale after a rebuild of the file
    const etag = `${tag}-${enc}"`;
    headers.set("ETag", etag);
    if (etagMatches(req.headers.get("if-none-match"), etag)) return new Response(null, { status: 304, headers });
    headers.set("Content-Encoding", enc);
    return new Response(Bun.file(sibling, { type }), { headers });
  }

  const etag = `${tag}"`;
  headers.set("ETag", etag);
  if (etagMatches(req.headers.get("if-none-match"), etag)) return new Response(null, { status: 304, headers });
  headers.set("Accept-Ranges", "bytes");
  return new Response(Bun.file(file, { type }), { headers });
}

/** A response for in-memory content with ETag/304 and optional gzip/brotli variants. */
export interface MemoryAsset {
  type: string;
  etag: string;
  identity: Uint8Array<ArrayBuffer>;
  gzip?: Uint8Array<ArrayBuffer>;
  br?: Uint8Array<ArrayBuffer>;
}

export function serveMemory(req: Request, asset: MemoryAsset, cacheControl: string): Response {
  const headers = new Headers({ "Content-Type": asset.type, "Cache-Control": cacheControl, Vary: "Accept-Encoding" });
  const accepted = acceptedEncodings(req.headers.get("accept-encoding"));
  let body = asset.identity;
  let etag = asset.etag;
  if (asset.br && accepted.has("br")) {
    body = asset.br;
    etag = asset.etag.replace(/"$/, '-br"');
    headers.set("Content-Encoding", "br");
  } else if (asset.gzip && accepted.has("gzip")) {
    body = asset.gzip;
    etag = asset.etag.replace(/"$/, '-gzip"');
    headers.set("Content-Encoding", "gzip");
  }
  headers.set("ETag", etag);
  if (etagMatches(req.headers.get("if-none-match"), etag)) return new Response(null, { status: 304, headers });
  return new Response(body, { headers });
}
